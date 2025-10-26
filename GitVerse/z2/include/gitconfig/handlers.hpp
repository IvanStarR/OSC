#pragma once
#include <string>
#include "gitconfig/http.hpp"
#include "gitconfig/kv.hpp"

namespace gitconfig {

HttpResponse handle_get_key(KVStore& kv, const std::string& key);
HttpResponse handle_put_key(KVStore& kv, const std::string& key, const std::string& body);
HttpResponse handle_delete_key(KVStore& kv, const std::string& key);
HttpResponse handle_list(KVStore& kv, const std::string& prefix, bool recursive);

}
