// Temporary MSVC probe: which regex throws on Windows? (removed after diagnosis)
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

int main() {
    const std::vector<std::string> samples = {
        "hello world", "https://github.com/jstdlee/pasteit?utm_source=x", "Alice Tan\nalice.tan@example.com\n+65 9123 4567\n12 Main Street",
        "2026-09-27T08:30:00Z and 1790482515 Mon, 27 Sep 2026 08:30:00 GMT 09/27/2026 8:30 PM Sep 27, 2026 8:30 AM",
        "#include <stdio.h>\nint main() { return 0; }\nimport os\nconst x = 1;", "A --> B --> C\nflowchart LR\nUser extends Base",
        "region,quarter,revenue\nNorth,Q1,120\nSouth,Q1,95", "10.20.0.0/22 192.168.1.1 255.255.255.0 0xC0A80101", "SGVsbG8gd29ybGQ= 48656c6c6f 01001000",
        "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.sig #ff8800 rgb(1,2,3) hsl(1,2%,3%)", std::string(3000, 'a') + " " + std::string(2000, '1')};
    int failures = 0;
    try {
        const std::regex re(R"PROBE((^|[^0-9.])((?:[0-9]{1,3}\.){3}[0-9]{1,3})(?=$|[^0-9.]))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "action_catalog:ipv4_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "action_catalog:ipv4_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(https://github\.com/([A-Za-z0-9][A-Za-z0-9-]{0,38})/([A-Za-z0-9._-]+)(?:[?#][^\s]*)?(?=$|[\s)\]>},.;]))PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "action_catalog:pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "action_catalog:pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_detector:pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_detector:pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^#{1,6}\s+\S)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:heading", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:heading", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*([-*+]|\d+[.)])\s+\S)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:list", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:list", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^>\s?)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:quote", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:quote", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:table_rule", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:table_rule", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((\[[^\]]+\]\([^)\s]+\)|\*\*[^*]+\*\*|`[^`]+`|!\[[^\]]*\]\())PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:inline_marks", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:inline_marks", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(<(!doctype html|html|head|body|div|span|p|a|table|script)\b)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:html", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:html", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*(select|insert\s+into|update|delete\s+from|create\s+(table|index|view)|alter\s+table|with)\b)PROBE", std::regex::icase | std::regex_constants::multiline);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:sql", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:sql", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*(\[?\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}|\[?\d{2}:\d{2}:\d{2}|[A-Z][a-z]{2}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}|\[?(INFO|WARN|WARNING|ERROR|DEBUG|TRACE|FATAL)\b))PROBE", std::regex_constants::ECMAScript | std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:log_line", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:log_line", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*[A-Za-z_][\w .-]{0,40}\s*[:=]\s*\S)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:key_value", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:key_value", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^(\s*-\s+\S|\s*[\w-]+:\s*$|\s{2,}[\w-]+:\s))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:yaml_line", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:yaml_line", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^#!\S|#include\s*[<"]|^\s*(import|from)\s+[\w.]+(\s+import\b|\s*;?\s*$)|^\s*(def|class|function|fn|func)\s+\w+|\b(int|void|auto|let|const|var)\s+\w+\s*[(=]|=>|^\s*(public|private|protected|static)\s+\w+))PROBE", std::regex_constants::ECMAScript | std::regex_constants::multiline);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "content_profile:strong_code", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "content_profile:strong_code", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^[A-Z][A-Za-z'.-]+(?:\s+[A-Z][A-Za-z'.-]+){1,3}$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(street|st\.?|road|rd\.?|avenue|ave\.?|lane|ln\.?|drive|dr\.?|boulevard|blvd\.?|way|place|pl\.?)\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:street_word", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:street_word", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:email_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:email_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((\+?\d[\d\s().-]{6,}\d))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:phone_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:phone_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^|[^0-9.])((?:[0-9]{1,3}\.){3}[0-9]{1,3})($|[^0-9.]))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:ipv4_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:ipv4_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^|[\s(<\[])(https://github\.com/[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]+(?:\.git)?(?:[?#][^\s]*)?)(?=$|[\s)\]>},.;]))PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^|[\r\n])\s*(flowchart|graph|sequenceDiagram|classDiagram|erDiagram|timeline)\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:diagram_header", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:diagram_header", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^|[\r\n])\s*@startuml\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:uml_header", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:uml_header", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b[A-Z][A-Za-z0-9_]*\s+(extends|inherits)\s+[A-Z][A-Za-z0-9_]*\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:class_relation", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:class_relation", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(entity\s+relationship|one-to-many|many-to-many)\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:er_signal", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:er_signal", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*[A-Za-z0-9_]+\s*(?:->|<-|-->|<--|--)\s*[A-Za-z0-9_]+\s*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:compact_arrow", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:compact_arrow", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE([A-Za-z0-9_]+\s*(?:->|<-|-->|<--|--)\s*[A-Za-z0-9_]+(?:\s*(?:->|<-|-->|<--|--)\s*[A-Za-z0-9_]+)+)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:arrow_chain", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:arrow_chain", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*([0-9]{4})-([0-9]{2})-([0-9]{2})\s*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:date_only_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:date_only_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b([0-9]{4})-([0-9]{2})-([0-9]{2})[T ]([0-9]{2}):([0-9]{2})(?::([0-9]{2}))?(Z|[+-][0-9]{2}:[0-9]{2})\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:iso_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:iso_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^|[^0-9])([0-9]{10}(?:[0-9]{3})?)(?=$|[^0-9]))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:unix_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:unix_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun),\s+([0-9]{1,2})\s+((?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*)\s+([0-9]{4})\s+([0-9]{2}):([0-9]{2})(?::([0-9]{2}))?\s+(GMT|UTC|[+-][0-9]{4})\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:rfc_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:rfc_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b([0-9]{1,2})[/-]([0-9]{1,2})[/-]([0-9]{2,4})\s+([0-9]{1,2}):([0-9]{2})(?:\s*(AM|PM))?\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:numeric_common_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:numeric_common_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b((?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*)\s+([0-9]{1,2}),?\s+([0-9]{4})\s+([0-9]{1,2}):([0-9]{2})(?:\s*(AM|PM))?\b)PROBE", std::regex_constants::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:month_common_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:month_common_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((^#!\S|#include\s*[<"]|^\s*(import|from)\s+[\w.]+(\s+import\b|\s*;?\s*$)|^\s*(def|class|function|fn|func)\s+\w+|\b(int|void|auto|let|const|var)\s+\w+\s*[(=]|=>|^\s*(public|private|protected|static)\s+\w+))PROBE", std::regex_constants::multiline);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:strong_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:strong_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(([{};]\s*$|^\s*[$>]\s+\w+))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "fast_content_detector:weak_line", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "fast_content_detector:weak_line", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "resume_detector:email_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "resume_detector:email_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((\+?\d[\d ()\-]{6,}\d))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "resume_detector:phone_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "resume_detector:phone_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*(experience|work experience|work history|employment|education|skills|technical skills|projects|summary|certifications)\s*:?\s*$)PROBE", std::regex_constants::icase | std::regex_constants::multiline);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "resume_detector:heading_pattern", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "resume_detector:heading_pattern", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(<title[^>]*>([\s\S]*?)</title>)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:title", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:title", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(<!--[\s\S]*?-->)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:comments", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:comments", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(<li[^>]*>)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:list_item", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:list_item", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(<h([1-6])[^>]*>)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:heading", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:heading", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(</?(p|div|br|tr|table|section|article|blockquote|pre|h[1-6]|ul|ol|dl|dt|dd|hr)\b[^>]*>)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:block", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:block", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(</t[dh]>)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:cell", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:cell", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(<[^>]*>)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "page_text:tag", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "page_text:tag", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^[3689]\d{7}$|^1[3-9]\d{9}$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:local", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:local", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\d{4}[-/.]\d{1,2}[-/.]\d{1,2}$|^\d{1,2}[-/.]\d{1,2}[-/.]\d{2,4}$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:date_like", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:date_like", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:private_key", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:private_key", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{40,}|xox[abprs]-[A-Za-z0-9-]{10,}|sk-[A-Za-z0-9_-]{20,}|AIza[0-9A-Za-z_-]{35}|eyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]*))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:key_shapes", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:key_shapes", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b[Bb]earer\s+([A-Za-z0-9._~+/-]{16,}=*))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:bearer", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:bearer", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(?:password|passwd|pwd|secret|token|api[_-]?key|access[_-]?token|client[_-]?secret)\b["']?\s*[:=]\s*["']?([^\s"',;]{4,}))PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:assigned_secret", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:assigned_secret", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b[a-z][a-z0-9+.-]*://[^\s:/@]+:([^\s@/]+)@)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:url_password", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:url_password", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:email", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:email", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(?:\d[ -]?){12,18}\d\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:card", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:card", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b[A-Z]{2}\d{2}(?: ?[A-Z0-9]{4}){2,7}(?: ?[A-Z0-9]{1,4})?\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:iban", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:iban", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b[STFGMstfgm]\d{7}[A-Za-z]\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:nric", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:nric", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b\d{17}[\dXx]\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:china_id", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:china_id", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(?:\d{1,3}\.){3}\d{1,3}\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:ipv4", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:ipv4", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:^|[^\w:])((?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4})(?![\w:]))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:ipv6", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:ipv6", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:mac", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:mac", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:\+\d{1,3}[\s.-]?)?(?:\(\d{1,4}\)[\s.-]?)?\d[\d\s.-]{6,}\d)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:phone", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:phone", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:DOB|D\.O\.B\.|date of birth|birth ?date|born(?: on)?|出生日期|生日)\s*(?::|：)?\s*(\d{1,4}(?:[-/.]|年)\d{1,2}(?:[-/.]|月)\d{1,4}(?:日)?|\d{1,2} [A-Z][a-z]+ \d{4}|[A-Z][a-z]+ \d{1,2},? \d{4}))PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:birth", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:birth", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b\d{1,5}[A-Za-z]?,?\s+(?:[A-Z][A-Za-z'.-]*\s){0,4}(?:Street|St\.?|Road|Rd\.?|Avenue|Ave\.?|Lane|Ln\.?|Drive|Dr\.?|Boulevard|Blvd\.?|Way|Place|Pl\.?|Crescent|Close|Court|Ct\.?)\b(?:[^\n]{0,40}?\b\d{5,6}\b)?)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:street", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:street", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:Blk|Block)\s+\d+[A-Z]?\b[^\n]{0,60}?(?:Singapore\s+)?\d{6}\b)PROBE", std::regex::icase);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:postal_block", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:postal_block", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:/home/|/Users/|[A-Za-z]:\\Users\\)([^/\\\s]+))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:home", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:home", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:\b(?:Name|Full name|Contact|From|To|Attn|Dear|Hi|Hello|Signed|Regards|Sincerely|Customer|Patient|Employee)\b[:,]?\s+)((?:[A-Z][a-z'-]+)(?:\s+[A-Z][a-z'-]+){0,2}))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:labeled_name", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:labeled_name", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b(?:Mr|Mrs|Ms|Miss|Dr|Prof)\.?\s+((?:[A-Z][a-z'-]+)(?:\s+[A-Z][a-z'-]+)?))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:titled_name", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:titled_name", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\b([A-Z][a-z'-]+)\s+([A-Z][a-z'-]+)(?:\s+([A-Z][a-z'-]+))?\b)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:capitalized_pair", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:capitalized_pair", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE((?:姓名|联系人|负责人|收件人|经办人)\s*(?::|：)?\s*([\xE4-\xE9][\x80-\xBF]{2}(?:[\xE4-\xE9][\x80-\xBF]{2}){1,2}))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:chinese_labeled", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:chinese_labeled", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(([\xE4-\xE9][\x80-\xBF]{2}(?:[\xE4-\xE9][\x80-\xBF]{2}){0,2})(?:先生|女士|小姐|老师|经理|总监|医生))PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:chinese_titled", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:chinese_titled", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\[[A-Z0-9]+_\d+\])PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:placeholder", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:placeholder", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(\[(EMAIL|PHONE|IPV4|IPV6|MAC|CARD|IBAN|ID|SECRET|NAME|ADDRESS|BIRTHDATE|PATH|CUSTOM)_\d+\])PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "anonymizer:placeholder", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "anonymizer:placeholder", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "markdown:separator", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "markdown:separator", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "markdown:heading", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "markdown:heading", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\s{0,3}([-*_])(\s*\1){2,}\s*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "markdown:rule", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "markdown:rule", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^(\s*)([-*+]|(\d{1,9})[.)])\s+(.*)$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "markdown:list_item", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "markdown:list_item", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\[( |x|X)\]\s+(.*)$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "markdown:task", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "markdown:task", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^\d{4}-\d{2}-\d{2}([ T]\d{2}:\d{2}(:\d{2})?)?|^\d{1,2}/\d{1,2}/\d{2,4}$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "table_data:date", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "table_data:date", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^[A-Za-z_][A-Za-z0-9_]*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "text_transforms:identifier", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "text_transforms:identifier", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^#([0-9a-f]{3}|[0-9a-f]{4}|[0-9a-f]{6}|[0-9a-f]{8})$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "text_transforms:hex", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "text_transforms:hex", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^rgba?\(\s*(\d{1,3})\s*[, ]\s*(\d{1,3})\s*[, ]\s*(\d{1,3})\s*(?:[,/]\s*([0-9.]+%?)\s*)?\)$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "text_transforms:rgb", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "text_transforms:rgb", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^hsla?\(\s*([0-9.]+)(?:deg)?\s*[, ]\s*([0-9.]+)%\s*[, ]\s*([0-9.]+)%\s*(?:[,/]\s*([0-9.]+%?)\s*)?\)$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "text_transforms:hsl", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "text_transforms:hsl", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]*$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "text_transforms:jwt", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "text_transforms:jwt", static_cast<int>(e.code()), e.what()); }
    try {
        const std::regex re(R"PROBE(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)PROBE", std::regex_constants::ECMAScript);
        for (const auto& sample : samples) {
            try { std::smatch m; (void)std::regex_search(sample, m, re); }
            catch (const std::regex_error& e) { ++failures; std::printf("SEARCH %s code=%d sample=%.40s : %s\n", "text_transforms:uuid", static_cast<int>(e.code()), sample.c_str(), e.what()); }
        }
    } catch (const std::regex_error& e) { ++failures; std::printf("COMPILE %s code=%d : %s\n", "text_transforms:uuid", static_cast<int>(e.code()), e.what()); }
    std::printf("probed 83 patterns, %d failures\n", failures);
    return 0;
}
