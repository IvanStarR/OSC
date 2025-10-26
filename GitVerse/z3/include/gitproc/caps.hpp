#pragma once
#include <string>
#include <vector>

namespace gitproc {

bool drop_privileges(const std::string &run_as_user,
                     const std::vector<std::string> &drop_caps);

} // namespace gitproc
