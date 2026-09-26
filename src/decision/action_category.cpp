#include "decision/action_category.hpp"

namespace pastit {

ActionCategory action_category(ActionKind kind) {
    switch (kind) {
        case ActionKind::PasteText:
        case ActionKind::PasteImage:
        case ActionKind::PasteUrl:
        case ActionKind::PasteEmail:
        case ActionKind::CopyPath:
        case ActionKind::CopyTemporaryImagePath:
            return ActionCategory::Paste;
        case ActionKind::OpenUrl:
        case ActionKind::OpenPath:
        case ActionKind::RevealPath:
        case ActionKind::OpenTerminalAtPath:
        case ActionKind::ComposeEmail:
        case ActionKind::SendEmail:
            return ActionCategory::Open;
        case ActionKind::SaveTextFile:
        case ActionKind::SaveImageFile:
        case ActionKind::DownloadUrl:
        case ActionKind::SaveUrlFile:
        case ActionKind::SaveEmailFile:
        case ActionKind::MovePath:
        case ActionKind::CopyPathToDirectory:
        case ActionKind::SaveJsonFile:
        case ActionKind::SaveJsonPrettyFile:
        case ActionKind::SaveResumeFile:
        case ActionKind::SaveContactAsJson:
        case ActionKind::SaveContactVCard:
        case ActionKind::SaveCodeFile:
            return ActionCategory::Save;
        case ActionKind::CopyResumeField:
        case ActionKind::CopyResumeAsJson:
        case ActionKind::ExtractContactInfo:
        case ActionKind::CopyContactAsJson:
        case ActionKind::CopyContactField:
        case ActionKind::CopyContactVCard:
        case ActionKind::CopyFileName:
        case ActionKind::CopyParentPath:
        case ActionKind::CopyJsonPaths:
        case ActionKind::TextStatistics:
        case ActionKind::NumberStatistics:
        case ActionKind::DecodeJwt:
        case ActionKind::HashSha256:
        case ActionKind::HashSha512:
            return ActionCategory::Extract;
        case ActionKind::PingIp:
        case ActionKind::TraceRouteIp:
        case ActionKind::ReverseDnsIp:
        case ActionKind::DigIp:
        case ActionKind::NetworkDiagnosticReport:
            return ActionCategory::Network;
        case ActionKind::CloneGithubHttps:
        case ActionKind::CloneGithubSsh:
        case ActionKind::CopyGithubHttpsUrl:
        case ActionKind::CopyGithubSshUrl:
            return ActionCategory::Code;
        case ActionKind::TransformText:
        case ActionKind::ExplainText:
        case ActionKind::ExplainCode:
        case ActionKind::CustomPrompt:
        case ActionKind::SummarizePage:
            return ActionCategory::Ai;
        case ActionKind::PreviewMarkdown:
        case ActionKind::ViewTable:
            return ActionCategory::Media;
        case ActionKind::RunPipeline:
            return ActionCategory::Code;
        case ActionKind::AnonymizeText:
        case ActionKind::RestorePlaceholders:
            return ActionCategory::Extract;
        case ActionKind::DrawMermaidDiagram:
        case ActionKind::GenerateQr:
        case ActionKind::AnnotateImage:
        case ActionKind::Graph:
            return ActionCategory::Media;
        default:
            return ActionCategory::Convert;
    }
}

double action_specificity_prior(ActionKind kind) {
    switch (kind) {
        // Shape-specific: offered only when the content matched.
        case ActionKind::ExtractContactInfo:
        case ActionKind::CopyContactVCard:
        case ActionKind::CopyResumeAsJson:
        case ActionKind::NetworkDiagnosticReport:
        case ActionKind::PingIp:
        case ActionKind::CopyGithubHttpsUrl:
        case ActionKind::CloneGithubHttps:
        case ActionKind::ConvertTimezone:
        case ActionKind::ToUnixTimestamp:
        case ActionKind::CopyColorHex:
        case ActionKind::CopyColorRgb:
        case ActionKind::CopyColorHsl:
        case ActionKind::DecodeJwt:
        case ActionKind::Base64Decode:
        case ActionKind::UrlDecode:
        case ActionKind::CleanUrl:
        case ActionKind::ToMarkdownTable:
        case ActionKind::NumberStatistics:
        case ActionKind::Graph:
        case ActionKind::PrettyJson:
        case ActionKind::JsonToYaml:
        case ActionKind::DrawMermaidDiagram:
        case ActionKind::SaveCodeFile:
        case ActionKind::PreviewMarkdown:
        case ActionKind::ViewTable:
        case ActionKind::RestorePlaceholders:
            return 0.06;
        // Only offered when personal data was found in the clipboard.
        case ActionKind::AnonymizeText:
            return 0.05;
        // The obvious primary use of the content.
        case ActionKind::PasteText:
        case ActionKind::PasteUrl:
        case ActionKind::OpenUrl:
        case ActionKind::PasteImage:
        case ActionKind::PasteEmail:
        case ActionKind::ComposeEmail:
        case ActionKind::OpenPath:
        case ActionKind::AnnotateImage:
            return 0.04;
        case ActionKind::SaveTextFile:
        case ActionKind::SaveImageFile:
        case ActionKind::CopyPathToDirectory:
        case ActionKind::SaveJsonPrettyFile:
        case ActionKind::CopyMarkdownLink:
        case ActionKind::RevealPath:
        case ActionKind::MinifyJson:
        case ActionKind::JsonToCsv:
            return 0.02;
        default:
            return 0.0;
    }
}

}  // namespace pastit
