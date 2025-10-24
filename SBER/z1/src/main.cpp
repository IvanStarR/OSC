#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>

// --- Утилита: безопасный запуск фиксированных команд и чтение stdout ---
static std::string run_cmd(const std::string& cmd) {
    // ВНИМАНИЕ: мы не принимаем внешние аргументы; команды фиксированы ниже.
    // popen читает stdout; stderr пусть молчит/уходит в dnf.
    std::string data;
    FILE* pipe = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!pipe) return data;
    char buf[8192];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), pipe);
        if (n == 0) break;
        data.append(buf, n);
    }
    pclose(pipe);
    return data;
}

// Трим пробелы
static inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Разбить строки
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        out.push_back(trim(line));
    }
    return out;
}

// JSON-escape (минимально достаточно)
static std::string json_escape(const std::string& s) {
    std::string out; out.reserve(s.size()+8);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

// --- Глобальные параметры (можно править под себя) ---
struct Config {
    // Пусто = все включённые репозитории
    std::string repoids;           // напр. "openscaler-24.03-base,openscaler-24.03-updates"
    std::string archs = "x86_64,noarch";
    std::string outdir = "web/static";
} CFG;

// Сервис: собрать список имен пакетов (rpm) без версий
static std::vector<std::string> list_all_binary_packages() {
    std::ostringstream cmd;
    cmd << "dnf repoquery --available --qf '%{name}'";
    if (!CFG.repoids.empty()) cmd << " --repoid '" << CFG.repoids << "'";
    if (!CFG.archs.empty())   cmd << " --arch '" << CFG.archs << "'";
    std::string out = run_cmd(cmd.str());
    auto lines = split_lines(out);
    // Уникализируем и сортируем
    std::set<std::string> uniq(lines.begin(), lines.end());
    return std::vector<std::string>(uniq.begin(), uniq.end());
}

// Сервис: список SRPM-имён (без версий)
static std::vector<std::string> list_all_srpm_names() {
    std::ostringstream cmd;
    cmd << "dnf repoquery --available --qf '%{sourcerpm}'";
    if (!CFG.repoids.empty()) cmd << " --repoid '" << CFG.repoids << "'";
    if (!CFG.archs.empty())   cmd << " --arch '" << CFG.archs << "'";
    // sourcerpm выглядит как "bash-5.1.8-2.oe2303.src.rpm" — обрежем до имени
    std::string out = run_cmd(cmd.str());
    auto lines = split_lines(out);
    std::set<std::string> srpm_names;
    for (auto &x : lines) {
        if (x.empty()) continue;
        // удалить хвост "-<ver>.<dist>.src.rpm"
        // найдём последний '-' и оставим всё до него, если дальше цифры
        size_t p = x.rfind('-');
        if (p != std::string::npos) {
            std::string left = x.substr(0, p);
            // минимальная эвристика: после '-' начинается версия с цифры?
            if (p+1 < x.size() && isdigit((unsigned char)x[p+1])) {
                srpm_names.insert(left);
                continue;
            }
        }
        // fallback: убрать .src.rpm суффикс
        const std::string suf = ".src.rpm";
        if (x.size() > suf.size() && x.rfind(suf) == x.size()-suf.size()) {
            srpm_names.insert(x.substr(0, x.size()-suf.size()));
        } else {
            srpm_names.insert(x);
        }
    }
    return std::vector<std::string>(srpm_names.begin(), srpm_names.end());
}

// Резолв зависимостей для бинарного пакета: rpm -> list(provided-packages)
static std::vector<std::string> resolve_runtime_requires(const std::string& pkg) {
    std::ostringstream cmd;
    cmd << "dnf repoquery --requires --resolve '" << pkg << "'";
    if (!CFG.repoids.empty()) cmd << " --repoid '" << CFG.repoids << "'";
    if (!CFG.archs.empty())   cmd << " --arch '" << CFG.archs << "'";
    std::string out = run_cmd(cmd.str());
    auto lines = split_lines(out);
    // repoquery обычно отдаёт NEVRA; нам нужно только имя пакета
    std::vector<std::string> pkgnames;
    pkgnames.reserve(lines.size());
    for (auto &l : lines) {
        if (l.empty()) continue;
        // Имя до первой '-<ver>'… эвристика: отрезать по первой '-' перед цифрой
        size_t cut = std::string::npos;
        for (size_t i=0; i<l.size(); ++i) {
            if (l[i]=='-' && i+1<l.size() && isdigit((unsigned char)l[i+1])) {
                cut = i;
                break;
            }
        }
        if (cut!=std::string::npos) pkgnames.push_back(l.substr(0,cut));
        else pkgnames.push_back(l); // fallback
    }
    // Уникализируем
    std::sort(pkgnames.begin(), pkgnames.end());
    pkgnames.erase(std::unique(pkgnames.begin(), pkgnames.end()), pkgnames.end());
    return pkgnames;
}

// Резолв BuildRequires для SRPM: srpm -> list(binary packages satisfying)
static std::vector<std::string> resolve_build_requires(const std::string& srpm) {
    std::ostringstream cmd;
    cmd << "dnf repoquery --requires --resolve --srpm '" << srpm << "'";
    if (!CFG.repoids.empty()) cmd << " --repoid '" << CFG.repoids << "'";
    std::string out = run_cmd(cmd.str());
    auto lines = split_lines(out);
    std::vector<std::string> pkgnames;
    pkgnames.reserve(lines.size());
    for (auto &l : lines) {
        if (l.empty()) continue;
        size_t cut = std::string::npos;
        for (size_t i=0; i<l.size(); ++i) {
            if (l[i]=='-' && i+1<l.size() && isdigit((unsigned char)l[i+1])) {
                cut = i;
                break;
            }
        }
        if (cut!=std::string::npos) pkgnames.push_back(l.substr(0,cut));
        else pkgnames.push_back(l);
    }
    std::sort(pkgnames.begin(), pkgnames.end());
    pkgnames.erase(std::unique(pkgnames.begin(), pkgnames.end()), pkgnames.end());
    return pkgnames;
}

struct Graph {
    // id -> kind ("rpm" | "srpm")
    std::unordered_map<std::string,std::string> nodes;
    // edges (source -> target)
    std::vector<std::pair<std::string,std::string>> links;
};

static void add_node(Graph& g, const std::string& id, const std::string& kind) {
    auto it = g.nodes.find(id);
    if (it==g.nodes.end()) g.nodes[id]=kind;
}

static void add_edge(Graph& g, const std::string& a, const std::string& b) {
    g.links.emplace_back(a,b);
}

static void write_graph_json(const Graph& g, const std::string& title, const std::string& outpath) {
    std::ofstream f(outpath);
    if (!f) {
        std::cerr << "[ERR] cannot write " << outpath << "\n";
        return;
    }
    f << "{\n";
    f << "  \"title\": \"" << json_escape(title) << "\",\n";
    f << "  \"meta\": {\"node_count\": " << g.nodes.size()
      << ", \"edge_count\": " << g.links.size() << "},\n";
    f << "  \"nodes\": [\n";
    bool first=true;
    for (auto &kv : g.nodes) {
        if (!first) f << ",\n";
        first=false;
        const std::string& id = kv.first;
        const std::string& kind = kv.second;
        f << "    {\"id\":\"" << json_escape(id)
          << "\",\"kind\":\"" << json_escape(kind)
          << "\",\"group\":\"" << json_escape(kind) << "\"}";
    }
    f << "\n  ],\n";
    f << "  \"links\": [\n";
    for (size_t i=0;i<g.links.size();++i) {
        if (i) f << ",\n";
        f << "    {\"source\":\"" << json_escape(g.links[i].first)
          << "\",\"target\":\"" << json_escape(g.links[i].second) << "\"}";
    }
    f << "\n  ]\n";
    f << "}\n";
    std::cerr << "[OK] wrote " << outpath << " (nodes="<<g.nodes.size()<<", edges="<<g.links.size()<<")\n";
}

int main(int argc, char** argv) {
    // Можно задать REPOIDS и OUTDIR через переменные окружения (не аргументы!)
    if (const char* e = std::getenv("REPOIDS")) CFG.repoids = e;
    if (const char* e = std::getenv("ARCHS"))   CFG.archs   = e;
    if (const char* e = std::getenv("OUTDIR"))  CFG.outdir  = e;

    std::cout << "[*] repoids=" << (CFG.repoids.empty() ? "<ALL>" : CFG.repoids)
              << " archs=" << CFG.archs
              << " outdir=" << CFG.outdir << "\n";

    // 1) Runtime граф
    std::cout << "[*] Listing all binary packages...\n";
    auto all_pkgs = list_all_binary_packages();
    std::cout << "    packages: " << all_pkgs.size() << "\n";

    Graph runtime;
    for (auto &p : all_pkgs) add_node(runtime, p, "rpm");

    std::cout << "[*] Resolving runtime requires...\n";
    size_t processed = 0;
    for (auto &p : all_pkgs) {
        auto deps = resolve_runtime_requires(p);
        for (auto &d : deps) {
            add_node(runtime, d, "rpm");          // на всякий случай добавим
            add_edge(runtime, p, d);
        }
        if ((++processed % 200)==0)
            std::cout << "    processed " << processed << "/" << all_pkgs.size() << "\n";
    }

    std::string runtime_out = CFG.outdir + "/runtime_graph.json";
    write_graph_json(runtime, "Runtime dependencies", runtime_out);

    // 2) Build граф
    std::cout << "[*] Listing all SRPM names...\n";
    auto all_srpms = list_all_srpm_names();
    std::cout << "    srpms: " << all_srpms.size() << "\n";

    Graph build;
    for (auto &s : all_srpms) add_node(build, s, "srpm");

    std::cout << "[*] Resolving build requires (BuildRequires)...\n";
    processed = 0;
    for (auto &s : all_srpms) {
        auto deps = resolve_build_requires(s);
        for (auto &d : deps) {
            add_node(build, d, "rpm");
            add_edge(build, s, d);
        }
        if ((++processed % 100)==0)
            std::cout << "    processed " << processed << "/" << all_srpms.size() << "\n";
    }

    std::string build_out = CFG.outdir + "/build_graph.json";
    write_graph_json(build, "Build dependencies", build_out);

    std::cout << "[DONE]\n";
    return 0;
}
