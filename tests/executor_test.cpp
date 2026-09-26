#include "executor/action_executor.hpp"
#include "app/download_job.hpp"
#include "util/json.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
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

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out;
    out.reserve(chars.size());
    for (const char ch : chars) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

pasteit::ClipboardItem put_text(pasteit::ClipboardStore& store, std::string text, pasteit::ContentKind kind) {
    return store.put(pasteit::ClipboardData{
        .mime_types = {"text/plain"},
        .bytes = bytes(text),
        .kind = kind,
        .source_app = "executor-test",
        .captured_at_ms = 500,
    });
}

pasteit::ActionInstance action(pasteit::ActionKind kind, const std::string& source_ref, const std::string& target_ref = {}) {
    pasteit::ActionInstance out;
    out.id = "test_action";
    out.kind = kind;
    out.source_ref = source_ref;
    out.target_ref = target_ref;
    out.filename = "out.txt";
    out.representation = "raw";
    out.label = "test";
    out.enabled = true;
    return out;
}

class BlockingDownloadTransport final : public pasteit::DownloadTransport {
public:
    pasteit::DownloadResponse fetch(const pasteit::DownloadRequest& request,
                                   const std::atomic_bool& cancelled) override {
        {
            std::lock_guard lock(mutex_);
            requests.push_back(request);
        }
        changed.notify_all();
        std::unique_lock lock(mutex_);
        while (!released && !cancelled.load()) {
            changed.wait_for(lock, std::chrono::milliseconds(10));
        }
        if (cancelled.load()) {
            return {.status_code = 0, .error = "cancelled"};
        }
        return {.status_code = 200, .total_size = 12, .range_supported = false, .body = "download body"};
    }

    void wait_for_request() {
        std::unique_lock lock(mutex_);
        const bool ready = changed.wait_for(lock, std::chrono::seconds(2), [&] { return !requests.empty(); });
        assert(ready);
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released = true;
        }
        changed.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed;
    std::vector<pasteit::DownloadRequest> requests;
    bool released = false;
};

}  // namespace

int main() {
    using namespace pasteit;

    const auto root = std::filesystem::temp_directory_path() / "pasteit_executor_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "target");
    std::filesystem::create_directories(root / "other_target");

    ClipboardStore store(root / "store");
    PathHistory history;
    const auto target = history.observe_path(root / "target", PathKind::Directory, "test", 600);
    const auto other_target = history.observe_path(root / "other_target", PathKind::Directory, "test", 601);
    ExecutionContext context{store, history, "req_exec"};

    const auto text_item = put_text(store, "hello file", ContentKind::Text);
    auto save_text = action(ActionKind::SaveTextFile, text_item.ref, target.ref);
    save_text.filename = "note.txt";
    const auto save_text_result = execute_action(save_text, context);
    assert(save_text_result.status == ExecutionStatus::Completed);
    assert(read_text(*save_text_result.output_path) == "hello file");
    assert(save_text_result.output_clipboard_ref.has_value());
    assert(store.read_text(*save_text_result.output_clipboard_ref) == save_text_result.output_path->string());
    const auto save_text_collision = execute_action(save_text, context);
    assert(save_text_collision.output_path->filename() == "note (2).txt");
    assert(read_text(*save_text_result.output_path) == "hello file");

    const std::vector<std::byte> png_bytes = {
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}, std::byte{0x0d}, std::byte{0x0a},
    };
    const auto image_item = store.put(ClipboardData{
        .mime_types = {"image/jpeg"},
        .bytes = png_bytes,
        .kind = ContentKind::Image,
        .source_app = "executor-test",
        .captured_at_ms = 700,
    });
    auto save_image = action(ActionKind::SaveImageFile, image_item.ref, target.ref);
    save_image.filename = "image.png";
    const auto save_image_result = execute_action(save_image, context);
    assert(save_image_result.status == ExecutionStatus::Completed);
    assert(read_bytes(*save_image_result.output_path) == png_bytes);

    const auto paste_image_result = execute_action(action(ActionKind::PasteImage, image_item.ref), context);
    assert(paste_image_result.status == ExecutionStatus::Completed);
    assert(paste_image_result.output_clipboard_ref.has_value());
    const auto pasted_image = store.item(*paste_image_result.output_clipboard_ref);
    assert(pasted_image.has_value());
    assert(pasted_image->mime_types == std::vector<std::string>{"image/jpeg"});
    assert(store.read(pasted_image->ref) == png_bytes);
    const auto temp_path_result = execute_action(action(ActionKind::CopyTemporaryImagePath, image_item.ref, "temp"), context);
    assert(temp_path_result.status == ExecutionStatus::Completed);
    assert(temp_path_result.output_path.has_value());
    assert(read_bytes(*temp_path_result.output_path) == png_bytes);
    assert(store.read_text(*temp_path_result.output_clipboard_ref) == temp_path_result.output_path->string());

    const auto json_item = put_text(store, "{\"a\":1,\"b\":[true,null]}", ContentKind::Json);
    auto pretty_json = action(ActionKind::SaveJsonPrettyFile, json_item.ref, target.ref);
    pretty_json.filename = "pretty.json";
    const auto pretty_result = execute_action(pretty_json, context);
    assert(pretty_result.status == ExecutionStatus::Completed);
    assert(read_text(*pretty_result.output_path) == "{\n  \"a\": 1,\n  \"b\": [\n    true,\n    null\n  ]\n}\n");

    const auto invalid_json_item = put_text(store, "{\"a\":", ContentKind::Text);
    auto invalid_pretty = action(ActionKind::SaveJsonPrettyFile, invalid_json_item.ref, target.ref);
    invalid_pretty.filename = "invalid.json";
    const auto invalid_result = execute_action(invalid_pretty, context);
    assert(invalid_result.status == ExecutionStatus::Failed);
    assert(!invalid_result.output_path.has_value());

    const auto source_path = root / "source.txt";
    {
        std::ofstream out(source_path);
        out << "copy me";
    }
    const auto path_item = put_text(store, source_path.string(), ContentKind::Path);
    const auto copy_result = execute_action(action(ActionKind::CopyPathToDirectory, path_item.ref, other_target.ref), context);
    assert(copy_result.status == ExecutionStatus::Completed);
    assert(std::filesystem::exists(root / "other_target" / "source.txt"));
    assert(std::filesystem::exists(source_path));

    auto confirmed_copy = action(ActionKind::CopyPathToDirectory, path_item.ref);
    confirmed_copy.filename = "renamed-copy.txt";
    confirmed_copy.parameters["confirmed_destination"] = (root / "other_target").string();
    const auto confirmed_copy_result = execute_action(confirmed_copy, context);
    assert(confirmed_copy_result.status == ExecutionStatus::Completed);
    assert(confirmed_copy_result.output_path->filename() == "renamed-copy.txt");
    const auto confirmed_copy_collision = execute_action(confirmed_copy, context);
    assert(confirmed_copy_collision.status == ExecutionStatus::Completed);
    assert(confirmed_copy_collision.output_path->filename() == "renamed-copy (2).txt");

    const auto uri_source = root / "URI source.txt";
    const auto uri_source_two = root / "uri-source-two.txt";
    {
        std::ofstream out(uri_source);
        out << "copy URI one";
    }
    {
        std::ofstream out(uri_source_two);
        out << "copy URI two";
    }
    const auto uri_list_item = put_text(
        store,
        "file://" + root.string() + "/URI%20source.txt\r\nfile://" + uri_source_two.string() + "\r\n",
        ContentKind::Path);
    const auto uri_copy_result =
        execute_action(action(ActionKind::CopyPathToDirectory, uri_list_item.ref, target.ref), context);
    assert(uri_copy_result.status == ExecutionStatus::Completed);
    assert(std::filesystem::exists(root / "target" / "URI source.txt"));
    assert(std::filesystem::exists(root / "target" / "uri-source-two.txt"));

    const auto move_source = root / "move.txt";
    {
        std::ofstream out(move_source);
        out << "move me";
    }
    const auto move_item = put_text(store, move_source.string(), ContentKind::Path);
    const auto move_result = execute_action(action(ActionKind::MovePath, move_item.ref, target.ref), context);
    assert(move_result.status == ExecutionStatus::Completed);
    assert(std::filesystem::exists(root / "target" / "move.txt"));
    assert(!std::filesystem::exists(move_source));

    const auto renamed_move_source = root / "move-again.txt";
    { std::ofstream out(renamed_move_source); out << "move renamed"; }
    const auto renamed_move_item = put_text(store, renamed_move_source.string(), ContentKind::Path);
    auto confirmed_move = action(ActionKind::MovePath, renamed_move_item.ref);
    confirmed_move.filename = "renamed-move.txt";
    confirmed_move.parameters["confirmed_destination"] = (root / "target").string();
    const auto confirmed_move_result = execute_action(confirmed_move, context);
    assert(confirmed_move_result.status == ExecutionStatus::Completed);
    assert(std::filesystem::exists(root / "target" / "renamed-move.txt"));
    assert(!std::filesystem::exists(renamed_move_source));

    const auto same_path_item = put_text(store, (root / "target" / "renamed-move.txt").string(), ContentKind::Path);
    auto same_path_move = action(ActionKind::MovePath, same_path_item.ref);
    same_path_move.filename = "renamed-move.txt";
    same_path_move.parameters["confirmed_destination"] = (root / "target").string();
    assert(execute_action(same_path_move, context).status == ExecutionStatus::Failed);

    const auto email_item = put_text(store, "jane@example.com", ContentKind::Email);
    auto save_email = action(ActionKind::SaveEmailFile, email_item.ref, target.ref);
    save_email.filename = "message.eml";
    const auto email_result = execute_action(save_email, context);
    assert(email_result.status == ExecutionStatus::Completed);
    assert(email_result.output_paths == std::vector<std::filesystem::path>{*email_result.output_path});
    assert(read_text(*email_result.output_path).find("To: jane@example.com\n") != std::string::npos);

    auto confirmed_email = action(ActionKind::SaveEmailFile, email_item.ref);
    confirmed_email.filename = "confirmed.eml";
    confirmed_email.parameters["confirmed_destination"] = (root / "other_target").string();
    assert(execute_action(confirmed_email, context).output_path == root / "other_target" / "confirmed.eml");

    const auto mailto_item = put_text(
        store, "mailto:jane@example.com?subject=Hello%20PasteIt&body=Line%201%0ALine%202", ContentKind::Email);
    auto save_mailto = action(ActionKind::SaveEmailFile, mailto_item.ref, target.ref);
    save_mailto.filename = "mailto-message.eml";
    const auto save_mailto_result = execute_action(save_mailto, context);
    assert(save_mailto_result.status == ExecutionStatus::Completed);
    const auto saved_mailto = read_text(*save_mailto_result.output_path);
    assert(saved_mailto.find("To: jane@example.com\n") != std::string::npos);
    assert(saved_mailto.find("Subject: Hello PasteIt\n") != std::string::npos);
    assert(saved_mailto.find("Line 1\nLine 2\n") != std::string::npos);

    const auto send_result = execute_action(action(ActionKind::SendEmail, email_item.ref), context);
    assert(send_result.status == ExecutionStatus::Unsupported);

    std::string opened_uri;
    context.open_uri = [&opened_uri](std::string_view uri) {
        opened_uri = uri;
        return true;
    };
    const auto url_item = put_text(store, "https://example.com/file", ContentKind::Url);
    const auto open_result = execute_action(action(ActionKind::OpenUrl, url_item.ref), context);
    assert(open_result.status == ExecutionStatus::Completed);
    assert(opened_uri == "https://example.com/file");

    const auto download_source = root / "download-source.txt";
    {
        std::ofstream out(download_source);
        out << "download through deterministic adapter";
    }
    const auto local_url_item = put_text(store, "file://" + download_source.string(), ContentKind::Url);
    auto download_action = action(ActionKind::DownloadUrl, local_url_item.ref, target.ref);
    download_action.filename = "downloaded.txt";
    const auto download_result = execute_action(download_action, context);
    assert(download_result.status == ExecutionStatus::Completed);
    assert(download_result.output_path.has_value());
    assert(download_result.output_paths == std::vector<std::filesystem::path>{*download_result.output_path});
    assert(read_text(*download_result.output_path) == "download through deterministic adapter");

    const auto saved_url_source = root / "saved-url-source.bin";
    {
        std::ofstream out(saved_url_source, std::ios::binary);
        out << "bytes returned by URL";
    }
    const auto saved_url_item = put_text(store, "file://" + saved_url_source.string(), ContentKind::Url);
    auto confirmed_url = action(ActionKind::SaveUrlFile, saved_url_item.ref);
    confirmed_url.filename = "confirmed.bin";
    confirmed_url.parameters["confirmed_destination"] = (root / "other_target").string();
    const auto saved_url_result = execute_action(confirmed_url, context);
    assert(saved_url_result.status == ExecutionStatus::Completed);
    assert(saved_url_result.output_path == root / "other_target" / "confirmed.bin");
    assert(read_text(*saved_url_result.output_path) == "bytes returned by URL");

    auto blocking_transport = std::make_shared<BlockingDownloadTransport>();
    DownloadManager download_manager(blocking_transport);
    ExecutionContext async_download_context{store, history, "req_download_async"};
    async_download_context.download_manager = &download_manager;
    const auto async_url_item = put_text(store, "https://example.test/slow.bin", ContentKind::Url);
    auto async_download = action(ActionKind::DownloadUrl, async_url_item.ref, target.ref);
    async_download.filename = "slow.bin";
    const auto async_download_result = execute_action(async_download, async_download_context);
    assert(async_download_result.status == ExecutionStatus::Executing);
    assert(async_download_result.download_job_id.has_value());
    assert(async_download_result.output_path == root / "target" / "slow.bin");
    blocking_transport->wait_for_request();
    assert(blocking_transport->requests.size() == 1);
    assert(!std::filesystem::exists(root / "target" / "slow.bin"));
    blocking_transport->release();
    download_manager.wait(*async_download_result.download_job_id);
    assert(download_manager.get(*async_download_result.download_job_id)->status == DownloadStatus::Completed);
    assert(read_text(root / "target" / "slow.bin") == "download body");

    opened_uri.clear();
    const auto compose_result = execute_action(action(ActionKind::ComposeEmail, email_item.ref), context);
    assert(compose_result.status == ExecutionStatus::Completed);
    assert(opened_uri == "mailto:jane@example.com");

    opened_uri.clear();
    const auto compose_mailto_result = execute_action(action(ActionKind::ComposeEmail, mailto_item.ref), context);
    assert(compose_mailto_result.status == ExecutionStatus::Completed);
    assert(opened_uri == "mailto:jane@example.com?subject=Hello%20PasteIt&body=Line%201%0ALine%202");

    std::string sent_email;
    context.direct_send_configured = true;
    context.send_email = [&sent_email](std::string_view email) {
        sent_email = email;
        return true;
    };
    const auto configured_send = execute_action(action(ActionKind::SendEmail, email_item.ref), context);
    assert(configured_send.status == ExecutionStatus::Completed);
    assert(sent_email == "jane@example.com");

    sent_email.clear();
    const auto configured_mailto_send = execute_action(action(ActionKind::SendEmail, mailto_item.ref), context);
    assert(configured_mailto_send.status == ExecutionStatus::Completed);
    assert(sent_email == "jane@example.com");

    auto resume_field = action(ActionKind::CopyResumeField, text_item.ref);
    resume_field.parameters["field_kind"] = "email";
    resume_field.parameters["value"] = "jane@example.com";
    const auto resume_copy = execute_action(resume_field, context);
    assert(resume_copy.status == ExecutionStatus::Completed);
    assert(resume_copy.output_clipboard_ref.has_value());
    assert(store.read_text(*resume_copy.output_clipboard_ref) == "jane@example.com");

    const auto quoted_resume_item = put_text(
        store, "Jane \"JJ\" Doe\njane@example.com\n+65 9123 4567\nSkills\nC++, \"Linux\", JSON\\API\n",
        ContentKind::Text);
    const auto resume_json_result = execute_action(action(ActionKind::CopyResumeAsJson, quoted_resume_item.ref), context);
    assert(resume_json_result.status == ExecutionStatus::Completed);
    assert(resume_json_result.output_clipboard_ref.has_value());
    assert(is_valid_json(store.read_text(*resume_json_result.output_clipboard_ref)));

    std::filesystem::remove_all(root);
}
