#include <sysaudit/util.hpp>

#include <chrono>
#include <ctime>
#include <array>
#include <vector>
#include <cstring>

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace sysaudit {

static int safe_pipe(int fds[2]){
    return pipe2(fds, O_CLOEXEC);
}

CmdResult run_command(const std::vector<std::string>& args,
                      const std::filesystem::path& cwd)
{
    CmdResult res{};
    if (args.empty()) {
        res.exit_code = -1;
        res.err = "empty argv";
        return res;
    }

    int out_pipe[2], err_pipe[2];
    if (safe_pipe(out_pipe) != 0 || safe_pipe(err_pipe) != 0) {
        res.exit_code = -1;
        res.err = "pipe failed";
        return res;
    }

    pid_t pid = fork();
    if (pid == -1) {
        res.exit_code = -1;
        res.err = "fork failed";
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return res;
    }

    if (pid == 0) {
        if (!cwd.empty()) {
            ::chdir(cwd.c_str());
        }
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        std::vector<char*> argv_c;
        argv_c.reserve(args.size()+1);
        for (auto& s : args) argv_c.push_back(const_cast<char*>(s.c_str()));
        argv_c.push_back(nullptr);

        execvp(argv_c[0], argv_c.data());
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    auto read_all = [](int fd){
        std::string s;
        std::array<char, 4096> buf{};
        for (;;) {
            ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n > 0) { s.append(buf.data(), static_cast<size_t>(n)); }
            else break;
        }
        return s;
    };

    res.out = read_all(out_pipe[0]);
    res.err = read_all(err_pipe[0]);
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        res.exit_code = -1;
        return res;
    }
    if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) res.exit_code = 128 + WTERMSIG(status);
    else res.exit_code = -1;

    return res;
}

std::string iso8601_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    char buf[64];
    strftime(buf, sizeof(buf), "%FT%TZ", &tm);
    return std::string(buf);
}

} // namespace sysaudit
