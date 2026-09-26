#include "core/action.hpp"
#include "core/protocol.hpp"
#include "core/types.hpp"

namespace pasteit {
namespace {
constexpr int kKeepArchiveMemberNonEmpty = 1;
}

int core_archive_member_anchor() {
    return kKeepArchiveMemberNonEmpty;
}
}  // namespace pasteit
