#pragma once
#include <string>
#include <vector>

namespace gitproc {

// Упрощённый дроп прав: setgid/setuid + PR_SET_NO_NEW_PRIVS.
// Если доступна libcap, можно расширить (но этот модуль не требует её).
bool drop_privileges(const std::string& run_as_user, const std::vector<std::string>& drop_caps);
// drop_caps: имена capabilities для справки; фактический дроп — через no_new_privs (без libcap это best-effort).

} // namespace gitproc
