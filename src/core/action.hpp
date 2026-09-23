#pragma once

#include <map>
#include <string>

namespace pastit {

enum class ActionKind {
    PasteText,
    SaveTextFile,
    PasteImage,
    SaveImageFile,
    CopyTemporaryImagePath,
    PasteUrl,
    OpenUrl,
    DownloadUrl,
    SaveUrlFile,
    PasteEmail,
    SaveEmailFile,
    ComposeEmail,
    SendEmail,
    CopyPath,
    MovePath,
    CopyPathToDirectory,
    PrettyJson,
    SaveJsonFile,
    SaveJsonPrettyFile,
    CopyResumeField,
    CopyResumeAsJson,
    SaveResumeFile,
    TransformText,
    ExtractContactInfo,
    CopyContactAsJson,
    SaveContactAsJson,
    CopyContactField,
    OpenTerminalAtPath,
    PingIp,
    TraceRouteIp,
    ReverseDnsIp,
    DigIp,
    NetworkDiagnosticReport,
    CloneGithubHttps,
    CloneGithubSsh,
    CopyGithubHttpsUrl,
    CopyGithubSshUrl,
    ConvertTimezone,
    ToUnixTimestamp,
    CopyNormalizedDateTime,
    DrawMermaidDiagram,
    GenerateQr,
    HashSha256,
    HashSha512,
    AnnotateImage,
    ExplainText,
    ExplainCode,
};

struct ActionInstance {
    std::string id;
    ActionKind kind = ActionKind::PasteText;
    std::string source_ref;
    std::string target_ref;
    std::string filename;
    std::string representation;
    std::string label;
    std::string description;
    std::map<std::string, std::string> parameters;
    bool enabled = true;
};

}  // namespace pastit
