#include "detect/content_profile.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace pasteit;

struct Case {
    const char* name;
    std::string text;
    DataShape expected;
};

std::string repeat_rows(const std::string& row, int count) {
    std::string out;
    for (int index = 0; index < count; ++index) out += row + "\n";
    return out;
}

}  // namespace

int main() {
    std::string big_csv = "id,name,score\n";
    for (int index = 0; index < 3000; ++index) big_csv += std::to_string(index) + ",user" + std::to_string(index) + "," + std::to_string(index % 97) + "\n";
    std::string preamble_csv = "Report generated 2026-09-01\nSource: sales export\n\n" + big_csv;

    const std::vector<Case> cases{
        {"color token", "#3366ff", DataShape::Token},
        {"uuid token", "123e4567-e89b-12d3-a456-426614174000", DataShape::Token},
        {"sentence", "Please send the updated invoice to the finance team before Friday. Thanks!", DataShape::Prose},
        {"prose with commas", "Well, I think, honestly, that we should go.\nAnyway, the plan, as agreed, stays.\nSo, yes, we go tomorrow, early, together.", DataShape::Prose},
        {"cjk prose", "今天我们讨论了项目的进度，下周继续跟进。\n请大家准备好材料。", DataShape::Prose},
        {"json object", R"({"name":"PasteIt","tags":["a","b"],"n":2})", DataShape::Json},
        {"json pretty", "{\n  \"a\": 1,\n  \"b\": [true, null]\n}", DataShape::Json},
        {"ndjson", "{\"a\":1}\n{\"a\":2}\n{\"a\":3}\n", DataShape::Ndjson},
        {"csv header", "name,qty,price\napple,3,1.20\npear,4,0.80\nfig,10,2.50\n", DataShape::Csv},
        {"csv semicolon", "name;qty\napple;3\npear;4\nkiwi;7\n", DataShape::Csv},
        {"tsv", "name\tqty\napple\t3\npear\t4\n", DataShape::Tsv},
        {"markdown", "# Title\n\nSome **bold** text and a [link](https://x.y).\n\n- item one\n- item two\n", DataShape::Markdown},
        {"markdown fence", "## Usage\n\n```bash\nmake\n```\n\nRun it.\n", DataShape::Markdown},
        {"python", "import os\n\ndef main():\n    print(os.getcwd())\n", DataShape::Code},
        {"cpp", "#include <vector>\nint main() {\n    std::vector<int> v;\n    return 0;\n}\n", DataShape::Code},
        {"log", "2026-09-26 10:00:01 INFO started\n2026-09-26 10:00:02 WARN slow disk\n2026-09-26 10:00:03 ERROR failed\n", DataShape::Log},
        {"level log", "error: disk full\nwarning: slow\nerror: timeout\ninfo: started\n", DataShape::Log},
        {"key value", "host = example.com\nport = 8080\nuser = admin\n", DataShape::KeyValue},
        {"yaml", "server:\n  host: example.com\n  port: 8080\nusers:\n  - alice\n  - bob\n", DataShape::Yaml},
        {"html", "<!DOCTYPE html>\n<html><body><div>Hello</div></body></html>", DataShape::Html},
        {"xml", "<?xml version=\"1.0\"?>\n<config><item id=\"1\">x</item></config>", DataShape::Xml},
        {"sql", "SELECT id, name\nFROM users\nWHERE active = 1;", DataShape::Sql},
        {"url list", "https://a.com/x\nhttps://b.org/y?z=1\nhttps://c.net\n", DataShape::UrlList},
        {"path list", "/home/me/a.txt\n/home/me/b.txt\n/var/log/syslog\n", DataShape::PathList},
        {"numbers", "3 5 8 13 21 34", DataShape::NumberSeries},
        {"number column", "12\n15\n9\n22\n", DataShape::NumberSeries},
        {"big csv sampled", big_csv, DataShape::Csv},
        {"csv with preamble", preamble_csv, DataShape::Csv},
        {"log big", repeat_rows("2026-09-26 10:00:01 INFO request ok id=42", 2000), DataShape::Log},
    };

    int failures = 0;
    for (const auto& test : cases) {
        const auto profile = profile_content(ContentKind::Text, test.text);
        if (profile.shape != test.expected) {
            ++failures;
            std::cerr << "MISMATCH " << test.name << ": got " << data_shape_name(profile.shape) << " ("
                      << profile.confidence << "), expected " << data_shape_name(test.expected) << "\n";
        }
    }
    std::cout << (cases.size() - failures) << "/" << cases.size() << " profiles correct\n";
    assert(failures == 0);

    const auto csv = profile_content(ContentKind::Text, "name,qty,price\napple,3,1.20\npear,4,0.80\nfig,10,2.50\n");
    assert(csv.delimiter == ',' && csv.columns == 3 && csv.header && csv.numeric_columns == 2 && csv.validated);

    const auto sampled = profile_content(ContentKind::Text, big_csv);
    assert(sampled.has_tag("sampled") && !sampled.validated && sampled.estimated_lines > 2000);

    const auto color = profile_content(ContentKind::Text, "#3366ff");
    assert(color.has_tag("color"));

    const auto code = profile_content(ContentKind::Text, "import os\n\ndef main():\n    print(os.getcwd())\n");
    assert(code.language == "py");

    const auto truncated = profile_content(ContentKind::Text, "{\"a\": [1, 2");
    assert(truncated.shape != DataShape::Json);

    assert(profile_content(ContentKind::Image, "").shape == DataShape::Binary);
    assert(profile_content(ContentKind::Text, "   ").shape == DataShape::Empty);
}
