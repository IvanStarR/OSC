#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

static std::filesystem::path make_tmpdir(const std::string& prefix) {
    std::filesystem::path base = std::filesystem::temp_directory_path() / (prefix + "XXXXXX");
    std::string s = base.string();
    std::vector<char> buf(s.begin(), s.end());
    buf.push_back('\0');
    char* p = mkdtemp(buf.data());
    REQUIRE(p != nullptr);
    return std::filesystem::path(p);
}

static std::string run_cmd_capture(const std::vector<std::string>& args, const std::filesystem::path& cwd) {
    std::string cmd;
    cmd += "cd ";
    cmd += "'" + cwd.string() + "'";
    cmd += " && ";
    for (size_t i=0;i<args.size();++i) {
        if (i) cmd += " ";
        cmd += "'";
        cmd += args[i];
        cmd += "'";
    }
    cmd += " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::string out;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), pipe);
        if (n == 0) break;
        out.append(buf, buf + n);
    }
    pclose(pipe);
    return out;
}

static std::filesystem::path find_sysaudit_binary() {
    const char* env = std::getenv("SYSAUDIT_BIN");
    if (env && std::filesystem::exists(env)) return std::filesystem::path(env);
    std::filesystem::path a = std::filesystem::current_path() / "../bin/sysaudit";
    if (std::filesystem::exists(a)) return a;
    std::filesystem::path b = std::filesystem::current_path() / "bin/sysaudit";
    if (std::filesystem::exists(b)) return b;
    return {};
}

TEST_CASE("Integration: batch commit and debounce lead to single commit") {
    auto bin = find_sysaudit_binary();
    REQUIRE(std::filesystem::exists(bin));

    auto dir = make_tmpdir("integration_");
    std::string file = (dir / "burst.txt").string();

    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        execl(bin.string().c_str(),
              "sysaudit",
              "--watch", dir.string().c_str(),
              "--debounce-ms", "200",
              "--batch-window-ms", "800",
              "--stats", "1",
              "--verbose",
              (char*)nullptr);
        _exit(127);
    }

    usleep(300 * 1000);
    for (int i=0;i<25;i++) {
        std::ofstream(file) << "iter " << i;
        usleep(20 * 1000);
    }

    usleep(1500 * 1000);
    kill(pid, SIGINT);
    int status=0;
    waitpid(pid, &status, 0);

    std::string log = run_cmd_capture({"git","log","--pretty=%s"}, dir);
    size_t cnt = 0;
    size_t pos = 0;
    while (true) {
        auto p = log.find("sysaudit:", pos);
        if (p == std::string::npos) break;
        cnt++;
        pos = p + 1;
    }
    REQUIRE(cnt >= 1);
    REQUIRE(cnt <= 2);
}
