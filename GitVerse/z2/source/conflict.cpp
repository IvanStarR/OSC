#include "gitconfig/conflict.hpp"
#include "gitconfig/kv.hpp"
#include "gitconfig/repo.hpp"
#include <string>
#include <vector>

namespace gitconfig {

static bool git_ok(Repo& r, const std::string& args) {
  std::string out; int code=0;
  r.run_git(args, &out, &code);
  return code==0;
}

SyncOutcome ConflictResolver::sync_lww(KVStore& kv, Repo& repo, const std::string& remote, const std::string& branch, std::string* err) {
  (void)kv;

  SyncOutcome res; res.remote = remote;

  {
    std::string out; int code=0;
    repo.run_git("rev-parse -q --verify MERGE_HEAD", &out, &code);
    if (code==0) {
      if (!git_ok(repo, "merge --abort")) {
        res.error = "merge abort failed";
        if (err) *err = res.error;
        return res;
      }
    }
  }

  {
    std::string out; int code=0;
    repo.run_git("rev-parse -q --verify HEAD", &out, &code);
    if (code!=0 && !remote.empty()) {
      std::string fo = "fetch " + remote + " " + branch;
      if (!git_ok(repo, fo)) {
        res.error = "fetch failed";
        return res;
      }
      std::string co = "checkout -B " + branch + " " + remote + "/" + branch;
      if (!git_ok(repo, co)) {
        res.error = "checkout from remote failed";
        return res;
      }
    }
  }

  if (!remote.empty()) {
    if (!repo.fetch(remote, branch, err)) {
      res.error = err ? *err : "fetch error";
      return res;
    }
  }

  std::string merge_rev = remote.empty() ? branch : (remote + "/" + branch);
  int mcode = repo.merge_no_commit(merge_rev, err);
  if (mcode<0) {
    res.error = err ? *err : "merge failed";
    return res;
  }

  if (mcode==0) {
    if (repo.has_uncommitted(err)) {
      if (!repo.commit_all("sync merge", err)) {
        res.error = err ? *err : "commit failed";
        return res;
      }
      res.merged = true;
    }
    res.ok = true;
    return res;
  }

  auto conflicted = repo.conflicted_files(err);
  if (err && !err->empty()) {
    res.error = *err;
    return res;
  }

  bool any = false;
  for (auto& rel : conflicted) {
    long long to = repo.last_change_ts("ours", rel, err);
    if (err && !err->empty()) { res.error = *err; return res; }
    long long tt = repo.last_change_ts("theirs", rel, err);
    if (err && !err->empty()) { res.error = *err; return res; }
    std::string side = (tt>=to) ? "theirs" : "ours";
    if (!repo.checkout_side(rel, side, err)) {
      res.error = err ? *err : "checkout side failed";
      return res;
    }
    any = true;
  }

  if (any) {
    if (!repo.commit_all("sync lww", err)) {
      res.error = err ? *err : "commit failed";
      return res;
    }
    res.merged = true;
    res.conflicts_resolved = true;
    res.ok = true;
    return res;
  }

  res.ok = true;
  return res;
}

}
