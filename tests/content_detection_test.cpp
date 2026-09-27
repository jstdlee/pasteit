#include "test_env.hpp"
#include "detect/content_detector.hpp"
#include "detect/resume_detector.hpp"
#include "storage/clipboard_store.hpp"
#include "storage/path_history.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

bool has_tag(const pasteit::DetectionResult& result, pasteit::SemanticTag tag) {
    for (const auto value : result.tags) {
        if (value == tag) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using namespace pasteit;

    auto temp_root = std::filesystem::temp_directory_path() / "pasteit_content_detection_test";
    pasteit_test::remove_tree(temp_root);
    std::filesystem::create_directories(temp_root);

    ClipboardStore store(temp_root / "store");
    const auto text_item = store.put(ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes("hello clipboard"),
        .kind = ContentKind::Text,
        .source_app = "unit-test",
        .captured_at_ms = 100,
    });
    assert(text_item.kind == ContentKind::Text);
    assert(text_item.preview == "hello clipboard");
    assert(store.read_text(text_item.ref) == "hello clipboard");

    const auto plain = detect_content({"text/plain"}, "just some notes");
    assert(plain.kind == ContentKind::Text);
    assert(has_tag(plain, SemanticTag::PlainText));
    assert(!plain.valid_json);

    const auto url = detect_content({"text/plain"}, "https://example.com/report.pdf");
    assert(url.kind == ContentKind::Url);
    assert(has_tag(url, SemanticTag::Url));

    const auto email = detect_content({"text/plain"}, "jane@example.com");
    assert(email.kind == ContentKind::Email);
    assert(has_tag(email, SemanticTag::Email));

    const auto image = detect_content({"image/png"}, "");
    assert(image.kind == ContentKind::Image);
    assert(has_tag(image, SemanticTag::Image));

    const auto path_file = temp_root / "source.txt";
    {
        std::ofstream out(path_file);
        out << "file body";
    }
    const auto path_result = detect_content({"text/plain"}, path_file.string());
    assert(path_result.kind == ContentKind::Path);
    assert(path_result.path == path_file);
    assert(path_result.path_kind == PathKind::File);

    PathHistory history;
    const auto file_uri = std::string{"file://"} + path_file.string();
    const auto file_location = history.observe(file_uri, "clipboard", 200);
    assert(file_location.has_value());
    assert(file_location->path == path_file);
    assert(file_location->kind == PathKind::File);
    assert(file_location->exists);
    const auto recent_paths = history.recent(2);
    assert(recent_paths.size() == 2);
    assert(std::any_of(recent_paths.begin(), recent_paths.end(), [&](const auto& item) {
        return item.path == temp_root && item.kind == PathKind::Directory;
    }));

    const auto valid_json = detect_content({"application/json"}, "{\"a\":1,\"b\":[true,null]}");
    assert(valid_json.kind == ContentKind::Json);
    assert(valid_json.valid_json);
    assert(has_tag(valid_json, SemanticTag::Json));

    const auto invalid_json = detect_content({"application/json"}, "{\"a\":");
    assert(invalid_json.kind == ContentKind::Text);
    assert(!invalid_json.valid_json);

    const std::string resume_text =
        "Jane Doe\n"
        "jane@example.com\n"
        "+65 9123 4567\n"
        "Skills\n"
        "C++, Linux, SQLite\n";
    const auto resume = detect_resume_fields(resume_text);
    assert(resume.is_resume);
    assert(resume.fields.size() >= 4);
    assert(resume.find("name")->value == "Jane Doe");
    assert(resume.find("email")->value == "jane@example.com");
    assert(resume.find("phone")->value == "+65 9123 4567");
    assert(resume.find("skills")->value == "C++, Linux, SQLite");
    assert(resume.find("email")->source_range.first == resume_text.find("jane@example.com"));

    pasteit_test::remove_tree(temp_root);
}
