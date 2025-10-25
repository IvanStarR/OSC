#include "gitproc/gitrepo.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;
namespace gitproc {

static int sysrun(const std::string& cmd) {
    return std::system(cmd.c_str());
}

GitRepo::GitRepo(std::string workdir) : workdir_(std::move(workdir)) {}

bool GitRepo::open_or_clone(const std::string& url_or_path, const std::string& branch) {
    branch_ = branch.empty() ? "main" : branch;
    if (fs::exists(workdir_ / fs::path(".git"))) {
        // already a repo
        return true;
    }
    if (fs::exists(url_or_path) && fs::exists(fs::path(url_or_path) / ".git")) {
        // local clone (or just use worktree)
        std::string cmd = "git clone -q -b " + branch_ + " " + url_or_path + " " + workdir_;
        return sysrun(cmd)==0;
    } else {
        std::string cmd = "git clone -q -b " + branch_ + " " + url_or_path + " " + workdir_;
        return sysrun(cmd)==0;
    }
}

bool GitRepo::pull_reset() {
    // fetch + reset --hard origin/<branch_>
    std::string fetch = "git -C " + workdir_ + " fetch -q --all";
    std::string reset = "git -C " + workdir_ + " reset -q --hard origin/" + branch_;
    int rc1 = sysrun(fetch);
    int rc2 = sysrun(reset);
    return rc1==0 && rc2==0;
}

std::vector<ServiceEntry> GitRepo::scan_services(const std::string& rel_dir) const {
    std::vector<ServiceEntry> out;
    fs::path p = fs::path(workdir_) / rel_dir;
    if (!fs::exists(p)) return out;

    for (auto& e : fs::recursive_directory_iterator(p)) {
        if (!e.is_regular_file()) continue;
        auto path = e.path();
        if (path.extension()==".service") {
            ServiceEntry se;
            se.path = path.string();
            auto stem = path.stem().string();
            se.name = stem; // "web"
            out.push_back(std::move(se));
        }
    }
    std::sort(out.begin(), out.end(), [](const ServiceEntry& a, const ServiceEntry& b){
        return a.name < b.name;
    });
    return out;
}

std::optional<std::string> GitRepo::read_file(const std::string& rel_path) const {
    fs::path p = fs::path(workdir_) / rel_path;
    std::ifstream f(p);
    if (!f.is_open()) return std::nullopt;
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

bool GitRepo::checkout_file_at(const std::string& commit, const std::string& rel_file) {
    // сохраним текущий файл на диске состоянием из <commit>:<rel_file>
    // git -C <workdir> show <commit>:<rel_file> > <abs_path>
    fs::path abs = fs::path(workdir_) / rel_file;
    fs::create_directories(abs.parent_path());
    std::string cmd = "git -C " + workdir_ + " show " + commit + ":" + rel_file + " > " + abs.string();
    return sysrun(cmd)==0;
}

ServicesDiff diff_sets(const std::vector<ServiceEntry>& old_list,
                       const std::vector<ServiceEntry>& new_list) {
    ServicesDiff d;
    std::unordered_set<std::string> oldn, newn;
    for (auto& s: old_list) oldn.insert(s.name);
    for (auto& s: new_list) newn.insert(s.name);

    for (auto& n: newn) if (!oldn.count(n)) d.added.insert(n);
    for (auto& n: oldn) if (!newn.count(n)) d.removed.insert(n);

    // simple "changed" эвристика: имена есть в обеих, но пути различаются по времени изменения
    for (auto& n: newn) {
        if (oldn.count(n)) {
            auto o = std::find_if(old_list.begin(), old_list.end(), [&](auto& s){return s.name==n;});
            auto nn = std::find_if(new_list.begin(), new_list.end(), [&](auto& s){return s.name==n;});
            if (o!=old_list.end() && nn!=new_list.end()) {
                try {
                    auto to = fs::last_write_time(o->path);
                    auto tn = fs::last_write_time(nn->path);
                    if (tn != to) d.changed.insert(n);
                } catch (...) {}
            }
        }
    }
    return d;
}

} // namespace gitproc
