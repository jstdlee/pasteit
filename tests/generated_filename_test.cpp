#include "test_env.hpp"
#include "app/generated_filename.hpp"

#include "core/action.hpp"
#include "core/types.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

class TempDirectory {
public:
    explicit TempDirectory(std::string_view label) {
        std::random_device random;
        const auto base = std::filesystem::temp_directory_path();
        for (std::size_t attempt = 0; attempt < 100; ++attempt) {
            path_ = base / (std::string{label} + '-' + std::to_string(random()) + '-' + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("failed to create unique test directory");
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

pasteit::ClipboardItem clipboard_item(
    pasteit::ContentKind kind,
    std::vector<std::string> mime_types,
    std::string preview = {}) {
    pasteit::ClipboardItem item;
    item.ref = "clip_test";
    item.kind = kind;
    item.mime_types = std::move(mime_types);
    item.preview = std::move(preview);
    return item;
}

std::chrono::system_clock::time_point fixed_time() {
#if defined(_MSC_VER)
    _putenv_s("TZ", "UTC");
    _tzset();
#else
    pasteit_test::set_env("TZ", "UTC");
    tzset();
#endif
    using namespace std::chrono;
    return sys_days{year{2026} / 9 / 20} + hours{13} + minutes{42};
}

void touch(const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "occupied";
}

}  // namespace

int main() {
    using namespace pasteit;

    const TempDirectory temporary_directory{"pasteit-generated-filename-test"};
    const auto& root = temporary_directory.path();

    const auto now = fixed_time();

    const auto png = clipboard_item(ContentKind::Image, {"image/png"});
    assert(generated_filename(ActionKind::SaveImageFile, png, now, root) == "2026092013-01.png");
    touch(root / "2026092013-01.png");
    assert(generated_filename(ActionKind::SaveImageFile, png, now, root) == "2026092013-02.png");

    const auto jpeg = clipboard_item(ContentKind::Image, {"image/jpeg"});
    assert(generated_filename(ActionKind::SaveImageFile, jpeg, now, root) == "2026092013-01.jpg");

    const auto text = clipboard_item(ContentKind::Text, {"text/plain"});
    assert(generated_filename(ActionKind::SaveTextFile, text, now, root) == "2026092013-01.txt");

    const auto json = clipboard_item(ContentKind::Json, {"application/json"});
    assert(generated_filename(ActionKind::SaveJsonPrettyFile, json, now, root) == "2026092013-01.json");

    const auto email = clipboard_item(ContentKind::Email, {"message/rfc822"});
    assert(generated_filename(ActionKind::SaveEmailFile, email, now, root) == "2026092013-01.eml");

    const auto url = clipboard_item(ContentKind::Url, {"text/uri-list"}, "https://example.com/report.pdf");
    assert(generated_filename(ActionKind::SaveUrlFile, url, now, root) == "2026092013-01.pdf");
    assert(generated_filename(ActionKind::DownloadUrl, url, now, root) == "2026092013-01.pdf");

    const auto opaque_download = clipboard_item(ContentKind::Url, {"text/uri-list"}, "https://example.com/download");
    assert(generated_filename(ActionKind::DownloadUrl, opaque_download, now, root) == "2026092013-01.bin");
}
