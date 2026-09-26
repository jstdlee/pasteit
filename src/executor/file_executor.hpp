#pragma once

#include "executor/action_executor.hpp"

namespace pasteit {

ExecutionResult execute_file_action(const ActionInstance& action, ExecutionContext& context);

}  // namespace pasteit
