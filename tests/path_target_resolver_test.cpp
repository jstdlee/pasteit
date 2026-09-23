#include "history/path_target_resolver.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

pastit::ClipboardItem path_item() {
    pastit::ClipboardItem item;
    item.ref = "clip_path";
    item.kind = pastit::ContentKind::Path;
    item.tags = {pastit::SemanticTag::Path};
    return item;
}

class ScopedTestDirectory {
public:
    ScopedTestDirectory() {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned int attempt = 0; attempt < 100; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("pastit-path-target-resolver-test-" + suffix + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error{"failed to create unique path resolver test directory"};
    }

    ~ScopedTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void assert_candidate(const pastit::DestinationCandidate& candidate,
                      const std::filesystem::path& path,
                      pastit::DestinationRole role,
                      bool exists) {
    assert(candidate.path == path);
    assert(candidate.role == role);
    assert(candidate.exists == exists);
}

}  // namespace

int main() {
    using pastit::DestinationRole;
    namespace fs = std::filesystem;

    const ScopedTestDirectory test_directory;
    const auto root = test_directory.path();
    fs::create_directories(root / "manual");
    fs::create_directories(root / "source");
    fs::create_directories(root / "focused");
    fs::create_directories(root / "configured");
    fs::create_directories(root / "recent");
    fs::create_directories(root / "multi-one");
    fs::create_directories(root / "multi-two");
    std::ofstream(root / "source" / "note.txt") << "x";
    std::ofstream(root / "root-file.txt") << "root";
    std::ofstream(root / "multi-one" / "a.txt") << "a";
    std::ofstream(root / "multi-two" / "b.txt") << "b";

    const auto missing_manual = root / "missing-manual";
    const std::vector<pastit::PathLocation> recent{
        {.ref = "r1",
         .path = root / "focused",
         .kind = pastit::PathKind::Directory,
         .last_seen_ms = 40,
         .source = "duplicate",
         .exists = true},
        {.ref = "r2",
         .path = root / "recent",
         .kind = pastit::PathKind::Directory,
         .last_seen_ms = 30,
         .source = "recent",
         .exists = true},
        {.ref = "r3",
         .path = root / "missing-recent",
         .kind = pastit::PathKind::Directory,
         .last_seen_ms = 20,
         .source = "recent",
         .exists = false},
    };

    const auto file_targets = pastit::resolve_file_targets(path_item(),
                                                           "file://" + (root / "source" / "note.txt").string(),
                                                           missing_manual,
                                                           root / "focused",
                                                           root / "configured",
                                                           recent);

    assert(file_targets.size() == 6);
    assert_candidate(file_targets[0], missing_manual.lexically_normal(), DestinationRole::Manual, false);
    assert_candidate(file_targets[1], root / "source", DestinationRole::SourceParent, true);
    assert_candidate(file_targets[2], root / "focused", DestinationRole::FocusedDirectory, true);
    assert_candidate(file_targets[3], root / "configured", DestinationRole::ConfiguredDefault, true);
    assert_candidate(file_targets[4], root / "recent", DestinationRole::Recent, true);
    assert_candidate(file_targets[5], fs::temp_directory_path().lexically_normal(), DestinationRole::Temporary, true);

    const auto directory_source = pastit::resolve_file_targets(path_item(),
                                                               (root / "source").string(),
                                                               std::nullopt,
                                                               std::nullopt,
                                                               std::nullopt,
                                                               {});
    assert(directory_source.size() >= 2);
    assert_candidate(directory_source.front(), root, DestinationRole::SourceParent, true);

    const auto multi_source = std::string{"file://"} + (root / "multi-one" / "a.txt").string() + "\n" +
                              "file://" + (root / "multi-two" / "b.txt").string() + "\n";
    const auto multi_targets =
        pastit::resolve_file_targets(path_item(), multi_source, std::nullopt, std::nullopt, std::nullopt, {});
    assert(multi_targets.size() >= 3);
    assert_candidate(multi_targets[0], root / "multi-one", DestinationRole::SourceParent, true);
    assert_candidate(multi_targets[1], root / "multi-two", DestinationRole::SourceParent, true);

    const auto root_file_targets = pastit::resolve_file_targets(path_item(),
                                                                "file:///pastit-root-file.txt",
                                                                std::nullopt,
                                                                std::nullopt,
                                                                std::nullopt,
                                                                {});
    assert(root_file_targets.size() == 2);
    assert_candidate(root_file_targets[0], "/", DestinationRole::SourceParent, true);
    assert_candidate(root_file_targets[1], fs::temp_directory_path().lexically_normal(), DestinationRole::Temporary, true);

    const auto absent_source = pastit::resolve_file_targets(pastit::ClipboardItem{},
                                                            "not a path",
                                                            std::nullopt,
                                                            std::nullopt,
                                                            std::nullopt,
                                                            {});
    assert(absent_source.size() == 1);
    assert_candidate(absent_source[0], fs::temp_directory_path().lexically_normal(), DestinationRole::Temporary, true);

    const auto missing_parent = pastit::resolve_file_targets(path_item(),
                                                             (root / "gone" / "file.txt").string(),
                                                             std::nullopt,
                                                             std::nullopt,
                                                             std::nullopt,
                                                             {});
    assert(missing_parent.size() == 1);
    assert_candidate(missing_parent[0], fs::temp_directory_path().lexically_normal(), DestinationRole::Temporary, true);

}
