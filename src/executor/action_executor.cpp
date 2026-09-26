#include "executor/action_executor.hpp"

#include "executor/email_executor.hpp"
#include "executor/fast_action_executor.hpp"
#include "executor/file_executor.hpp"
#include "executor/url_executor.hpp"
#include "executor/utility_executor.hpp"
#include "platform/fast_action_services.hpp"

namespace pasteit {
namespace {

bool is_fast_action(ActionKind kind) {
    switch (kind) {
        case ActionKind::ExtractContactInfo:
        case ActionKind::CopyContactAsJson:
        case ActionKind::SaveContactAsJson:
        case ActionKind::CopyContactField:
        case ActionKind::OpenTerminalAtPath:
        case ActionKind::PingIp:
        case ActionKind::TraceRouteIp:
        case ActionKind::ReverseDnsIp:
        case ActionKind::DigIp:
        case ActionKind::NetworkDiagnosticReport:
        case ActionKind::CloneGithubHttps:
        case ActionKind::CloneGithubSsh:
        case ActionKind::CopyGithubHttpsUrl:
        case ActionKind::CopyGithubSshUrl:
        case ActionKind::ConvertTimezone:
        case ActionKind::ToUnixTimestamp:
        case ActionKind::CopyNormalizedDateTime:
        case ActionKind::DrawMermaidDiagram:
        case ActionKind::GenerateQr:
        case ActionKind::HashSha256:
        case ActionKind::HashSha512:
        case ActionKind::AnnotateImage:
        case ActionKind::ExplainText:
        case ActionKind::ExplainCode:
        case ActionKind::TransformText:
            return true;
        default:
            return false;
    }
}

bool is_async_fast_action(ActionKind kind) {
    switch (kind) {
        case ActionKind::PingIp:
        case ActionKind::TraceRouteIp:
        case ActionKind::ReverseDnsIp:
        case ActionKind::DigIp:
        case ActionKind::NetworkDiagnosticReport:
        case ActionKind::CloneGithubHttps:
        case ActionKind::CloneGithubSsh:
        case ActionKind::HashSha256:
        case ActionKind::HashSha512:
        case ActionKind::DrawMermaidDiagram:
        case ActionKind::GenerateQr:
            return true;
        default:
            return false;
    }
}

}  // namespace

ExecutionResult execute_action(const ActionInstance& action, ExecutionContext& context) {
    if (is_utility_action(action.kind)) {
        return execute_utility_action(action, context);
    }
    if (is_fast_action(action.kind)) {
        if (context.fast_action_executor != nullptr) {
            return context.fast_action_executor->execute(action, context);
        }
        if (is_async_fast_action(action.kind)) {
            ExecutionResult result;
            result.request_id = context.request_id;
            result.action_id = action.id;
            result.status = ExecutionStatus::Unsupported;
            result.message = "async action requires a persistent fast-action executor";
            return result;
        }
        auto& services = context.fast_action_services == nullptr ? empty_fast_action_services()
                                                                 : *context.fast_action_services;
        FastActionExecutor executor(services);
        return executor.execute(action, context);
    }

    switch (action.kind) {
        case ActionKind::PasteUrl:
        case ActionKind::OpenUrl:
        case ActionKind::DownloadUrl:
        case ActionKind::SaveUrlFile:
            return execute_url_action(action, context);
        case ActionKind::PasteEmail:
        case ActionKind::SaveEmailFile:
        case ActionKind::ComposeEmail:
        case ActionKind::SendEmail:
            return execute_email_action(action, context);
        default:
            return execute_file_action(action, context);
    }
}

}  // namespace pasteit
