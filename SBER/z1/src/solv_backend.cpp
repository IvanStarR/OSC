#include "solv_backend.hpp"

#include <spdlog/spdlog.h>
#include <tinyxml2.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "thread_pool.hpp"

extern "C" {
#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/repo.h>
#include <solv/repo_rpmmd.h>
#include <solv/selection.h>
#include <solv/solv_xfopen.h>
#include <solv/util.h>
}

using std::string;
using std::vector;

static string join_path(const string& a, const string& b) {
    if (a.empty())
        return b;
    if (a.back() == '/')
        return a + b;
    return a + "/" + b;
}

static void split_csv(const string& s, vector<string>& out) {
    out.clear();
    string cur;
    for (char c : s) {
        if (c == ',' || isspace((unsigned char)c)) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else
            cur.push_back(c);
    }
    if (!cur.empty())
        out.push_back(cur);
}

static vector<string> list_all_repoids_from_cache() {
    vector<string> out;
    const string base = "/var/cache/dnf";
    if (!std::filesystem::exists(base))
        return out;
    std::regex re("^(.*)-[0-9a-f]{10,}$");// "repoid-<hash>"
    for (auto& it : std::filesystem::directory_iterator(base)) {
        if (!it.is_directory())
            continue;
        std::smatch m;
        auto name = it.path().filename().string();
        if (std::regex_match(name, m, re) && m.size() >= 2)
            out.push_back(m[1].str());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static vector<string> find_repodata_dirs(const string& repoid) {
    vector<string> out;
    const string base = "/var/cache/dnf";
    if (!std::filesystem::exists(base))
        return out;
    for (auto& it : std::filesystem::directory_iterator(base)) {
        if (!it.is_directory())
            continue;
        auto name = it.path().filename().string();
        if (name.rfind(repoid + "-", 0) == 0) {
            auto repodata = join_path(it.path().string(), "repodata");
            if (std::filesystem::exists(repodata))
                out.push_back(repodata);
        }
    }
    return out;
}

struct RpmmdPaths {
    string primary, filelists, other;
};

static string normalize_href(string href) {
    while (href.rfind("./", 0) == 0)
        href.erase(0, 2);
    if (href.rfind("repodata/", 0) == 0)
        href.erase(0, 9);
    return href;
}

static bool parse_repomd(const string& repomd, RpmmdPaths& out) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(repomd.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    auto* root = doc.RootElement();
    if (!root)
        return false;

    for (auto* data = root->FirstChildElement("data"); data; data = data->NextSiblingElement("data")) {
        const char* type = data->Attribute("type");
        if (!type)
            continue;
        auto* loc = data->FirstChildElement("location");
        if (!loc)
            continue;
        const char* href = loc->Attribute("href");
        if (!href)
            continue;
        string t(type), h = normalize_href(href);
        if (t == "primary")
            out.primary = h;
        else if (t == "filelists")
            out.filelists = h;
        else if (t == "other")
            out.other = h;
    }
    return !out.primary.empty();
}

struct XFile {
    FILE* fp {nullptr};
    bool is_pipe {false};
};

static XFile xfopen_any(const string& path) {
    XFile x;
    x.fp = solv_xfopen(path.c_str(), "r");
    if (x.fp)
        return x;

    if (path.size() >= 4 && path.rfind(".zst") == path.size() - 4) {
        std::string cmd = "zstd -dc -- '" + path + "'";
        x.fp = popen(cmd.c_str(), "r");
        if (x.fp)
            x.is_pipe = true;
    }
    return x;
}

static void xfclose_any(XFile& x) {
    if (!x.fp)
        return;
    if (x.is_pipe)
        pclose(x.fp);
    else
        fclose(x.fp);
    x.fp = nullptr;
}

static bool repo_add_from_repodata_dir(Repo* repo, const string& dir) {
    string repomd = join_path(dir, "repomd.xml");
    if (!std::filesystem::exists(repomd) && std::filesystem::exists(repomd + ".gz"))
        repomd += ".gz";

    RpmmdPaths p;
    if (!parse_repomd(repomd, p)) {
        for (auto& it : std::filesystem::directory_iterator(dir)) {
            auto bn = it.path().filename().string();
            if (bn.find("primary.xml") != string::npos)
                p.primary = bn;
            else if (bn.find("filelists.xml") != string::npos)
                p.filelists = bn;
            else if (bn.find("other.xml") != string::npos)
                p.other = bn;
            else if (bn.find("primary.xml.zst") != string::npos)
                p.primary = bn;
            else if (bn.find("filelists.xml.zst") != string::npos)
                p.filelists = bn;
            else if (bn.find("other.xml.zst") != string::npos)
                p.other = bn;
        }
        if (p.primary.empty())
            return false;
    }

    const string primary = join_path(dir, p.primary);
    const string filelists = p.filelists.empty() ? "" : join_path(dir, p.filelists);
    const string other = p.other.empty() ? "" : join_path(dir, p.other);

    int flags = 0;

    {
        XFile xf = xfopen_any(primary);
        if (!xf.fp) {
            spdlog::warn("cannot open {}", primary);
            return false;
        }
        if (repo_add_rpmmd(repo, xf.fp, nullptr, flags) != 0) {
            xfclose_any(xf);
            return false;
        }
        xfclose_any(xf);
    }
    if (!filelists.empty()) {
        XFile xf = xfopen_any(filelists);
        if (xf.fp) {
            repo_add_rpmmd(repo, xf.fp, nullptr, flags);
            xfclose_any(xf);
        }
    }
    if (!other.empty()) {
        XFile xf = xfopen_any(other);
        if (xf.fp) {
            repo_add_rpmmd(repo, xf.fp, nullptr, flags);
            xfclose_any(xf);
        }
    }

    return true;
}

struct PoolGuard {
    Pool* pool {nullptr};
    PoolGuard() {
        pool = pool_create();
    }
    ~PoolGuard() {
        if (pool)
            pool_free(pool);
    }
};

struct EdgeHasher {
    size_t operator()(const std::pair<string, string>& p) const noexcept {
        return std::hash<string> {}(p.first) ^ (std::hash<string> {}(p.second) << 1);
    }
};

bool build_graphs_with_libsolv(const string& repoids_csv, const string& archs_csv, Graph& gr_runtime, Graph& gr_build, SolvStats& stats, int threads) {
    vector<string> repoids;
    if (repoids_csv.empty())
        repoids = list_all_repoids_from_cache();
    else
        split_csv(repoids_csv, repoids);
    if (repoids.empty()) {
        spdlog::warn("No repos found in /var/cache/dnf. Run `sudo dnf makecache`.");
        return false;
    }

    std::unordered_set<string> archs;
    {
        vector<string> tmp;
        split_csv(archs_csv, tmp);
        for (auto& a : tmp)
            archs.insert(a);
        archs.insert("noarch");
    }

    PoolGuard pg;
    Pool* pool = pg.pool;
    size_t repos_loaded = 0;
    for (auto& rid : repoids) {
        auto dirs = find_repodata_dirs(rid);
        for (auto& d : dirs) {
            Repo* r = repo_create(pool, (rid + ":" + d).c_str());
            if (repo_add_from_repodata_dir(r, d)) {
                repo_internalize(r);
                repos_loaded++;
            } else {
                spdlog::warn("skip repo (bad repodata): {}", d);
                repo_free(r, 1);
            }
        }
    }
    if (repos_loaded == 0) {
        spdlog::error("No repodata loaded.");
        return false;
    }
    stats.repos_loaded = repos_loaded;
    spdlog::info("libsolv: repos loaded: {}", repos_loaded);

    pool_createwhatprovides(pool);

    vector<Id> runtime_ids, build_ids;
    int nsolv = pool->nsolvables;
    for (Id id = 1; id < nsolv; ++id) {
        Solvable* s = pool->solvables + id;
        if (!s || !s->repo)
            continue;
        const char* arch = pool_id2str(pool, s->arch);
        const char* name = pool_id2str(pool, s->name);
        bool is_src = (strcmp(arch, "src") == 0) || (strcmp(arch, "nosrc") == 0);
        if (is_src) {
            build_ids.push_back(id);
            gr_build.add_node(name, "srpm");
        } else {
            if (archs.count(arch) == 0)
                continue;
            runtime_ids.push_back(id);
            gr_runtime.add_node(name, "rpm");
        }
    }
    stats.solvables_seen = runtime_ids.size() + build_ids.size();
    spdlog::info("solvables: runtime={} build={}", runtime_ids.size(), build_ids.size());

    std::mutex pool_mx;
    std::unordered_set<std::pair<string, string>, EdgeHasher> seen_rt, seen_bd;
    std::mutex rt_mx, bd_mx;

    auto worker_runtime = [&](Id id) {
        Solvable* s;
        {
            std::lock_guard<std::mutex> lk(pool_mx);
            s = pool->solvables + id;
        }
        const char* srcname = pool_id2str(pool, s->name);
        Queue req;
        queue_init(&req);
        {
            std::lock_guard<std::mutex> lk(pool_mx);
            solvable_lookup_deparray(s, SOLVABLE_REQUIRES, &req, -1);
        }
        for (int i = 0; i < req.count; ++i) {
            Id dep = req.elements[i];
            Id* wp;
            {
                std::lock_guard<std::mutex> lk(pool_mx);
                wp = pool_whatprovides_ptr(pool, dep);
            }
            if (!wp)
                continue;
            for (Id* p = wp; *p; ++p) {
                Solvable* ps;
                {
                    std::lock_guard<std::mutex> lk(pool_mx);
                    ps = pool->solvables + *p;
                }
                if (!ps || !ps->repo)
                    continue;
                const char* parch = pool_id2str(pool, ps->arch);
                if (archs.count(parch) == 0)
                    continue;
                const char* dstname = pool_id2str(pool, ps->name);
                {
                    std::lock_guard<std::mutex> lk(rt_mx);
                    auto ins = seen_rt.emplace(srcname, dstname);
                    if (ins.second) {
                        gr_runtime.add_node(dstname, "rpm");
                        gr_runtime.add_edge(srcname, dstname);
                    }
                }
            }
        }
        queue_free(&req);
    };

    auto worker_build = [&](Id id) {
        Solvable* s;
        {
            std::lock_guard<std::mutex> lk(pool_mx);
            s = pool->solvables + id;
        }
        const char* srcname = pool_id2str(pool, s->name);
        Queue req;
        queue_init(&req);
        {
            std::lock_guard<std::mutex> lk(pool_mx);
            solvable_lookup_deparray(s, SOLVABLE_REQUIRES, &req, -1);
        }
        for (int i = 0; i < req.count; ++i) {
            Id dep = req.elements[i];
            Id* wp;
            {
                std::lock_guard<std::mutex> lk(pool_mx);
                wp = pool_whatprovides_ptr(pool, dep);
            }
            if (!wp)
                continue;
            for (Id* p = wp; *p; ++p) {
                Solvable* ps;
                {
                    std::lock_guard<std::mutex> lk(pool_mx);
                    ps = pool->solvables + *p;
                }
                if (!ps || !ps->repo)
                    continue;
                const char* dstname = pool_id2str(pool, ps->name);
                {
                    std::lock_guard<std::mutex> lk(bd_mx);
                    auto ins = seen_bd.emplace(srcname, dstname);
                    if (ins.second) {
                        gr_build.add_node(dstname, "rpm");
                        gr_build.add_edge(srcname, dstname);
                    }
                }
            }
        }
        queue_free(&req);
    };

    unsigned nthreads = threads > 0 ? (unsigned)threads : std::max(1u, std::thread::hardware_concurrency());

    ThreadPool pool_rt(nthreads);
    std::atomic<size_t> done_rt {0};
    for (auto id : runtime_ids) {
        pool_rt.submit([&, id] {
            worker_runtime(id);
            size_t d = ++done_rt;
            if ((d % 2000) == 0)
                spdlog::info("libsolv runtime progress: {}/{}", d, runtime_ids.size());
        });
    }
    pool_rt.wait_empty();

    ThreadPool pool_bd(nthreads);
    std::atomic<size_t> done_bd {0};
    for (auto id : build_ids) {
        pool_bd.submit([&, id] {
            worker_build(id);
            size_t d = ++done_bd;
            if ((d % 1000) == 0)
                spdlog::info("libsolv build progress: {}/{}", d, build_ids.size());
        });
    }
    pool_bd.wait_empty();

    stats.runtime_edges = seen_rt.size();
    stats.build_edges = seen_bd.size();
    spdlog::info("libsolv: runtime edges={} build edges={}", stats.runtime_edges, stats.build_edges);
    return true;
}
