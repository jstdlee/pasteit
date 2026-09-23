#pragma once

#include "executor/action_executor.hpp"

namespace pastit {

ExecutionResult execute_url_action(const ActionInstance& action, ExecutionContext& context);

}  // namespace pastit
