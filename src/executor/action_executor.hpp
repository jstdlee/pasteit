#pragma once

#include "core/action.hpp"
#include "core/protocol.hpp"
#include "storage/clipboard_store.hpp"
#include "storage/path_history.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace pasteit {

class DownloadManager;
class FastActionExecutor;
class FastActionServices;

struct ExecutionContext {
    ClipboardStore& clipboard_store;
    PathHistory& path_history;
    std::string request_id;
    bool direct_send_configured = false;
    std::int64_t now_ms = 0;
    std::function<bool(std::string_view)> open_uri;
    std::function<bool(std::string_view)> send_email;
    FastActionServices* fast_action_services = nullptr;
    FastActionExecutor* fast_action_executor = nullptr;
    DownloadManager* download_manager = nullptr;
};

ExecutionResult execute_action(const ActionInstance& action, ExecutionContext& context);

}  // namespace pasteit
