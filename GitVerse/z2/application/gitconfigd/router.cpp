#include "router.hpp"
#include <fmt/format.h>
#include <algorithm>

namespace router_ns {

Router::Router(Cfg cfg)
  : kv_({cfg.repo, cfg.data_root}), repo_(cfg.repo), cfg_(std::move(cfg)) {}

static std::string ensure_key(const std::string& k) {
  if (k.empty()) return "/";
  if (k[0] == '/') return k;
  return "/" + k;
}

http_ns::Response Router::handle(const http_ns::Request& r) {
  if (r.method=="GET" && r.path.rfind("/keys/",0)==0) {
    std::string k = util_ns::url_decode(r.path.substr(6));
    return handle_keys_get(ensure_key(k));
  }
  if (r.method=="POST" && r.path.rfind("/keys/",0)==0) {
    std::string k = util_ns::url_decode(r.path.substr(6));
    return handle_keys_post(ensure_key(k), r.body);
  }
  if (r.method=="DELETE" && r.path.rfind("/keys/",0)==0) {
    std::string k = util_ns::url_decode(r.path.substr(6));
    return handle_keys_delete(ensure_key(k));
  }
  if (r.method=="GET" && r.path=="/list") {
    return handle_list(r.query);
  }
  if (r.method=="POST" && r.path=="/sync") {
    return handle_sync();
  }
  if (r.method=="GET" && r.path=="/nodes") {
    return handle_nodes_get();
  }
  if (r.method=="POST" && r.path=="/nodes") {
    return handle_nodes_post(r.body);
  }
  http_ns::Response resp;
  resp.status = 404;
  resp.status_text = "Not Found";
  resp.body = "not found";
  return resp;
}

http_ns::Response Router::handle_keys_get(const std::string& key) {
  http_ns::Response resp;
  std::string err;
  auto v = kv_.get(key, &err);
  if (!v.has_value()) {
    resp.status = 404;
    resp.body = "not found";
    return resp;
  }
  resp.body = *v;
  resp.headers["Content-Type"] = "text/plain";
  return resp;
}

http_ns::Response Router::handle_keys_post(const std::string& key, const std::string& body) {
  http_ns::Response resp;
  std::string err;
  if (!kv_.set(key, body, &err)) {
    resp.status = 500;
    resp.body = err;
    return resp;
  }
  resp.body = "OK";
  return resp;
}

http_ns::Response Router::handle_keys_delete(const std::string& key) {
  http_ns::Response resp;
  std::string err;
  if (!kv_.erase(key, &err)) {
    resp.status = 500;
    resp.body = err;
    return resp;
  }
  resp.body = "OK";
  return resp;
}

http_ns::Response Router::handle_list(const std::string& query) {
  bool recursive=false;
  std::string prefix="/";
  for (auto& kv : util_ns::parse_query(query)) {
    if (kv.first=="prefix") prefix = kv.second.empty()?"/":kv.second;
    if (kv.first=="recursive") recursive = (kv.second=="1"||kv.second=="true");
  }
  std::string err;
  auto items = kv_.list(prefix, recursive, &err);
  std::sort(items.begin(), items.end(), [](const gitconfig::ListEntry& x, const gitconfig::ListEntry& y){
    if (x.key==y.key) return x.is_dir && !y.is_dir;
    return x.key < y.key;
  });
  http_ns::Response resp;
  if (!err.empty()) { resp.status=500; resp.body=err; return resp; }
  std::string json="[";
  for (size_t i=0;i<items.size();++i) {
    auto& e = items[i];
    json += fmt::format("{{\"key\":\"{}\",\"dir\":{}}}", util_ns::json_escape(e.key), e.is_dir?"true":"false");
    if (i+1<items.size()) json += ",";
  }
  json += "]";
  resp.headers["Content-Type"] = "application/json";
  resp.body = std::move(json);
  return resp;
}

http_ns::Response Router::handle_sync() {
  http_ns::Response resp;
  std::string err;
  if (!repo_.ensure_initialized("gitconfig", "gitconfig@localhost", &err)) {
    resp.status=500; resp.body=err; return resp;
  }
  auto peers = gitconfig::Nodes::list_peers(cfg_.repo, cfg_.data_root);
  std::vector<std::string> remotes;
  if (!cfg_.remote.empty()) remotes.push_back(cfg_.remote);
  for (auto& u : peers) {
    if (!gitconfig::Nodes::ensure_remote_for_peer(repo_, u, &err)) {}
    remotes.push_back(gitconfig::Nodes::remote_name_for(u));
  }
  std::vector<std::string> synced;
  std::vector<std::string> skipped;
  std::vector<std::pair<std::string,std::string>> errors;
  for (auto& rn : remotes) {
    std::string e2;
    auto out = gitconfig::ConflictResolver::sync_lww(kv_, repo_, rn, cfg_.branch, &e2);
    if (out.ok) {
      synced.push_back(rn);
    } else {
      if (!e2.empty()) errors.push_back({rn, e2});
      else skipped.push_back(rn);
    }
  }
  std::string json="{\"synced\":[";
  for (size_t i=0;i<synced.size();++i){ json += fmt::format("\"{}\"", synced[i]); if (i+1<synced.size()) json += ","; }
  json += "],\"skipped\":[";
  for (size_t i=0;i<skipped.size();++i){ json += fmt::format("\"{}\"", skipped[i]); if (i+1<skipped.size()) json += ","; }
  json += "],\"errors\":[";
  for (size_t i=0;i<errors.size();++i){ json += fmt::format("{{\"remote\":\"{}\",\"error\":\"{}\"}}", errors[i].first, util_ns::json_escape(errors[i].second)); if (i+1<errors.size()) json += ","; }
  json += "]}";
  resp.headers["Content-Type"] = "application/json";
  resp.body = json;
  return resp;
}

http_ns::Response Router::handle_nodes_get() {
  auto peers = gitconfig::Nodes::list_peers(cfg_.repo, cfg_.data_root);
  std::string json="[";
  for (size_t i=0;i<peers.size();++i) {
    json += fmt::format("\"{}\"", util_ns::json_escape(peers[i]));
    if (i+1<peers.size()) json += ",";
  }
  json += "]";
  http_ns::Response resp;
  resp.headers["Content-Type"] = "application/json";
  resp.body = json;
  return resp;
}

http_ns::Response Router::handle_nodes_post(const std::string& body) {
  http_ns::Response resp;
  std::string err;
  if (body.empty()) { resp.status=400; resp.body="empty"; return resp; }
  if (!gitconfig::Nodes::add_peer(cfg_.repo, cfg_.data_root, body, &err)) { resp.status=500; resp.body=err; return resp; }
  if (!repo_.ensure_initialized("gitconfig", "gitconfig@localhost", &err)) { resp.status=500; resp.body=err; return resp; }
  if (!gitconfig::Nodes::ensure_remote_for_peer(repo_, body, &err)) { resp.status=500; resp.body=err; return resp; }
  resp.body = "OK";
  return resp;
}

}
