#include "actions/action_catalog.hpp"
#include "detect/fast_content_detector.hpp"
#include "detect/resume_detector.hpp"
#include "decision/fallback_ranker.hpp"
#include "executor/action_executor.hpp"
#include "ui/theme.hpp"
#include "transform/text_transforms.hpp"
#include "util/json.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace pastit;

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    for (const unsigned char ch : text) out.push_back(static_cast<std::byte>(ch));
    return out;
}

bool has_kind(const ActionCatalog& catalog, ActionKind kind) {
    return std::any_of(catalog.actions.begin(), catalog.actions.end(),
                       [&](const ActionInstance& action) { return action.kind == kind; });
}

ActionCatalog catalog_for(ContentKind kind, std::string text) {
    DecisionSnapshot snapshot;
    ClipboardItem item;
    item.ref = "clip";
    item.kind = kind;
    item.preview = std::move(text);
    item.size_bytes = item.preview.size();
    item.mime_types = {"text/plain"};
    snapshot.clipboard_items = {item};
    snapshot.recent_paths = {{.ref = "tmp", .path = std::filesystem::temp_directory_path(),
                              .kind = PathKind::Directory, .last_seen_ms = 1, .source = "test", .exists = true}};
    return build_catalog(snapshot);
}

void transforms() {
    assert(to_upper_ascii("abc é") == "ABC é");
    assert(to_title_case("hello wORLD 3rd") == "Hello World 3rd");
    assert(tidy_whitespace("a  \n\n\n\nb\t\n\n") == "a\n\nb");
    assert(sort_lines("b\nA\nc") == "A\nb\nc");
    assert(dedupe_lines("a\nb\na\nc\nb") == "a\nb\nc");
    assert(text_statistics("one two\nthree") == "Lines: 2\nWords: 3\nCharacters: 13\nBytes: 13");

    assert(base64_encode("hello") == "aGVsbG8=");
    assert(base64_decode("aGVsbG8=") == "hello");
    assert(base64_decode("aGVsbG8") == "hello");
    assert(!base64_decode("not base64!"));
    assert(looks_like_base64_text("SGVsbG8sIFBhc3RlSXQgd29ybGQh"));
    assert(!looks_like_base64_text("configuration"));
    assert(!looks_like_base64_text("deadbeefdeadbeefdeadbeef"));

    assert(url_encode("a b&c/é") == "a%20b%26c%2F%C3%A9");
    assert(url_decode("a%20b%26c") == "a b&c");
    assert(!url_decode("plain text"));

    assert(minify_json("{ \"a\" : [1, 2], \"b\": \"x y\" }") == "{\"a\":[1,2],\"b\":\"x y\"}");
    // Key order and number text are kept exactly as written.
    assert(json_to_yaml(R"({"name":"PasteIt","tags":["a","b"],"nested":{"on":true,"n":2.50}})") ==
           "name: PasteIt\ntags:\n  - a\n  - b\nnested:\n  \"on\": true\n  n: 2.50");
    assert(json_to_csv(R"([{"b":"x,y","a":1},{"a":2,"c":null}])") == "b,a,c\n\"x,y\",1,\n,2,");
    assert(json_to_yaml(R"({"s":"caf\u00e9 \ud83d\ude00"})") == "s: caf\xC3\xA9 \xF0\x9F\x98\x80");
    assert(!json_to_csv(R"({"a":1})"));
    assert(json_paths(R"({"odd key":1,"items":[{"name":"x"}]})") == "[\"odd key\"]\n.items[0].name");

    assert(clean_tracking_url("https://ex.com/p?id=3&utm_source=x&fbclid=y#top") == "https://ex.com/p?id=3#top");
    assert(clean_tracking_url("https://ex.com/p?utm_medium=a") == "https://ex.com/p");
    assert(!clean_tracking_url("https://ex.com/p?id=3"));
    assert(markdown_link("https://www.example.com/docs/") == "[example.com/docs](https://www.example.com/docs/)");

    const auto red = parse_color("#f00");
    assert(red && red->r == 255 && red->g == 0 && red->b == 0);
    assert(color_rgb(*red) == "rgb(255, 0, 0)");
    assert(color_hsl(*red) == "hsl(0, 100%, 50%)");
    const auto teal = parse_color("rgb(0, 128, 128)");
    assert(teal && color_hex(*teal) == "#008080");
    const auto from_hsl = parse_color("hsl(120, 100%, 25%)");
    assert(from_hsl && color_hex(*from_hsl) == "#008000");
    assert(!parse_color("#12345") && !parse_color("rgb(300,0,0)") && !parse_color("red car"));

    // {"alg":"HS256","typ":"JWT"}.{"sub":"42","exp":1}.sig
    const auto jwt = decode_jwt("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiI0MiIsImV4cCI6MX0.c2ln");
    assert(jwt && jwt->find("\"sub\": \"42\"") != std::string::npos);
    assert(jwt->find("(expired)") != std::string::npos);
    assert(!decode_jwt("a.b.c"));

    assert(is_uuid("123e4567-e89b-12d3-a456-426614174000"));
    const auto fresh = generate_uuid_v4();
    assert(is_uuid(fresh) && fresh[14] == '4');

    assert(to_markdown_table("name,qty\napple,3\npear,4") == "| name | qty |\n| --- | --- |\n| apple | 3 |\n| pear | 4 |");
    assert(to_markdown_table("a\tb\n1\t2") == "| a | b |\n| --- | --- |\n| 1 | 2 |");
    assert(!to_markdown_table("just one line"));
    assert(number_statistics("1, 2, 3, 10") == "Count: 4\nSum: 16\nMean: 4\nMedian: 2.5\nMin: 1\nMax: 10");

    assert(guess_code_extension("#include <vector>\nstd::vector<int> v;") == "cpp");
    assert(guess_code_extension("def main():\n    pass\n") == "py");
    assert(guess_code_extension("#!/bin/bash\necho hi") == "sh");

    const auto vcard = contact_vcard({{"name", "Jane Doe"}, {"email", "jane@example.com"}, {"phone", "+65 9123"}});
    assert(vcard.find("FN:Jane Doe\r\n") != std::string::npos);
    assert(vcard.find("N:Doe;Jane;;;\r\n") != std::string::npos);
    assert(vcard.find("EMAIL;TYPE=INTERNET:jane@example.com") != std::string::npos);
}

void detectors() {
    // Prose with a trailing semicolon is no longer code; real code still is.
    assert(!detect_fast_content(ContentKind::Text, "Meet at noon; bring snacks;").code);
    assert(detect_fast_content(ContentKind::Text, "int main() { return 0; }").code);
    assert(detect_fast_content(ContentKind::Text, "const total = items.reduce((a, b) => a + b);").code);
    // A contact card is not a resume.
    assert(!detect_resume_fields("Jane Doe\njane@example.com\n").is_resume);
    assert(detect_resume_fields("Jane Doe\njane@example.com\nExperience\nACME\nEducation\nNUS\n").is_resume);
    // QR is offered for short payloads only.
    assert(detect_fast_content(ContentKind::Text, "wifi:secret").qr);
    assert(!detect_fast_content(ContentKind::Text, std::string(600, 'x')).qr);
}

void catalog() {
    const auto color = catalog_for(ContentKind::Text, "#3366ff");
    assert(has_kind(color, ActionKind::CopyColorRgb) && has_kind(color, ActionKind::CopyColorHsl));
    const auto rgb_label = std::find_if(color.actions.begin(), color.actions.end(),
                                        [](const ActionInstance& a) { return a.kind == ActionKind::CopyColorRgb; });
    assert(rgb_label->label == "Copy as RGB  rgb(51, 102, 255)");
    assert(!has_kind(color, ActionKind::CopyColorHex));  // Already hex.
    assert(has_kind(catalog_for(ContentKind::Text, "rgb(51, 102, 255)"), ActionKind::CopyColorHex));

    const auto table = catalog_for(ContentKind::Text, "name,qty\napple,3\npear,4");
    assert(has_kind(table, ActionKind::ToMarkdownTable) && has_kind(table, ActionKind::SortLines));

    const auto numbers = catalog_for(ContentKind::Text, "3 5 8 13");
    assert(has_kind(numbers, ActionKind::NumberStatistics) && has_kind(numbers, ActionKind::Graph));

    const auto code = catalog_for(ContentKind::Text, "def main():\n    print('hi')\n");
    assert(has_kind(code, ActionKind::SaveCodeFile));
    assert(!has_kind(code, ActionKind::ToUpperCase) && !has_kind(code, ActionKind::SortLines));

    const auto encoded = catalog_for(ContentKind::Text, "SGVsbG8sIFBhc3RlSXQgd29ybGQh");
    assert(has_kind(encoded, ActionKind::Base64Decode) && !has_kind(encoded, ActionKind::Base64Encode));

    const auto uuid = catalog_for(ContentKind::Text, "123e4567-e89b-12d3-a456-426614174000");
    assert(has_kind(uuid, ActionKind::GenerateUuid));

    const auto json = catalog_for(ContentKind::Json, R"([{"a":1},{"a":2}])");
    assert(has_kind(json, ActionKind::JsonToYaml) && has_kind(json, ActionKind::JsonToCsv) &&
           has_kind(json, ActionKind::CopyJsonPaths) && has_kind(json, ActionKind::CustomPrompt));
    assert(!has_kind(json, ActionKind::MinifyJson));  // Already minified.

    const auto url = catalog_for(ContentKind::Url, "https://example.com/a?utm_source=x");
    assert(has_kind(url, ActionKind::CleanUrl) && has_kind(url, ActionKind::CopyMarkdownLink));
    assert(!has_kind(catalog_for(ContentKind::Url, "https://example.com/a"), ActionKind::CleanUrl));

    const auto contact = catalog_for(ContentKind::Text, "Name: Jane Doe\nEmail: jane@example.com\nPhone: +65 9123 4567");
    assert(has_kind(contact, ActionKind::CopyContactVCard) && has_kind(contact, ActionKind::SaveContactVCard));

    const auto path = catalog_for(ContentKind::Path, std::filesystem::temp_directory_path().string());
    assert(has_kind(path, ActionKind::OpenPath) && has_kind(path, ActionKind::RevealPath) &&
           has_kind(path, ActionKind::CopyFileName) && has_kind(path, ActionKind::CopyParentPath));
}

void ranking_and_display() {
    // Without Djev, the fallback leads with what the content is.
    const auto first_kind = [](ContentKind kind, std::string text) {
        const auto catalog = catalog_for(kind, std::move(text));
        DecisionSnapshot snapshot;
        snapshot.clipboard_items = {{.ref = "clip", .kind = kind}};
        return rank_fallback(catalog, snapshot, 1).front().action.kind;
    };
    const auto color_first = first_kind(ContentKind::Text, "rgb(1, 2, 3)");
    assert(color_first == ActionKind::CopyColorHex || color_first == ActionKind::CopyColorHsl ||
           color_first == ActionKind::CopyColorRgb);
    const auto contact_first = first_kind(ContentKind::Text, "Name: Jane Doe\nEmail: jane@example.com");
    assert(contact_first == ActionKind::ExtractContactInfo || contact_first == ActionKind::CopyContactVCard);
    assert(first_kind(ContentKind::Text, "just a sentence") == ActionKind::PasteText);

    const auto home = std::string{std::getenv("HOME") == nullptr ? "/home/user" : std::getenv("HOME")};
    if (std::getenv("HOME") != nullptr) assert(display_path(home + "/notes") == "~/notes");
    const auto elided = display_path("/very/long/path/with/many/segments/and/a/final/file-name.txt", 24);
    assert(elided.size() <= 26 && elided.ends_with("file-name.txt"));
}

void execution() {
    const auto root = std::filesystem::temp_directory_path() / "pastit_utility_actions_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "out");
    ClipboardStore store(root / "store");
    PathHistory history;
    const auto target = history.observe_path(root / "out", PathKind::Directory, "test", 1);
    std::string opened;
    ExecutionContext context{store, history, "req"};
    context.open_uri = [&](std::string_view uri) {
        opened = uri;
        return true;
    };
    const auto put = [&](std::string text, ContentKind kind) {
        return store.put(ClipboardData{.mime_types = {"text/plain"}, .bytes = bytes(text), .kind = kind,
                                       .source_app = "test", .captured_at_ms = 1});
    };
    const auto run = [&](ActionKind kind, const ClipboardItem& item, std::map<std::string, std::string> parameters = {},
                         std::string target_ref = {}) {
        ActionInstance action;
        action.id = "a";
        action.kind = kind;
        action.source_ref = item.ref;
        action.target_ref = std::move(target_ref);
        action.parameters = std::move(parameters);
        action.enabled = true;
        return execute_action(action, context);
    };

    const auto url = put("https://ex.com/?utm_source=a&q=1", ContentKind::Url);
    const auto cleaned = run(ActionKind::CleanUrl, url);
    assert(cleaned.status == ExecutionStatus::Completed);
    assert(store.read_text(*cleaned.output_clipboard_ref) == "https://ex.com/?q=1");

    const auto color = put("hsl(0, 100%, 50%)", ContentKind::Text);
    assert(store.read_text(*run(ActionKind::CopyColorHex, color).output_clipboard_ref) == "#ff0000");

    const auto json = put(R"({"b":1,"a":[true]})", ContentKind::Json);
    assert(store.read_text(*run(ActionKind::JsonToYaml, json).output_clipboard_ref) == "b: 1\na:\n  - true");

    const auto file = root / "out" / "report.txt";
    std::ofstream(file) << "x";
    const auto path_item = put(file.string(), ContentKind::Path);
    assert(store.read_text(*run(ActionKind::CopyFileName, path_item, {{"path", file.string()}}).output_clipboard_ref) ==
           "report.txt");
    assert(run(ActionKind::RevealPath, path_item, {{"path", file.string()}}).status == ExecutionStatus::Completed);
    assert(opened == (root / "out").string());

    const auto code = put("def main():\n    pass\n", ContentKind::Text);
    ActionInstance save_code;
    save_code.id = "save";
    save_code.kind = ActionKind::SaveCodeFile;
    save_code.source_ref = code.ref;
    save_code.target_ref = target.ref;
    save_code.filename = "clipboard.py";
    save_code.parameters["extension"] = "py";
    const auto saved = execute_action(save_code, context);
    assert(saved.status == ExecutionStatus::Completed && saved.output_path->extension() == ".py");

    // Content that no longer matches fails instead of copying garbage.
    const auto prose = put("not a color", ContentKind::Text);
    assert(run(ActionKind::CopyColorHex, prose).status == ExecutionStatus::Failed);
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    transforms();
    detectors();
    catalog();
    ranking_and_display();
    execution();
}
