#include "net/page_text.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace pastit;
    const std::string html = R"(<!DOCTYPE html><html><head><title>Release &amp; Notes</title>
<style>body{color:red}</style><script>alert("x")</script></head>
<body><nav>Home | About</nav><article><h1>PasteIt 0.4</h1><p>Adds <b>tables</b> &mdash; and charts.</p>
<ul><li>One</li><li>Two &#x263A;</li></ul><p>)" + std::string(420, 'x') + R"(</p></article><footer>(c) 2026</footer></body></html>)";
    const auto page = extract_page_text(html, "text/html; charset=utf-8");
    assert(page.title == "Release & Notes");
    assert(page.text.starts_with("PasteIt 0.4\nAdds tables \xE2\x80\x94 and charts.\n- One\n- Two \xE2\x98\xBA"));
    assert(page.text.find("alert") == std::string::npos && page.text.find("Home") == std::string::npos &&
           page.text.find("2026") == std::string::npos && page.text.find("color") == std::string::npos);

    const auto plain = extract_page_text("  hello \n\n  world  ", "text/plain");
    assert(plain.text == "hello\nworld");

    const auto capped = extract_page_text(std::string(20000, 'a'), "text/plain", 100);
    assert(capped.truncated && capped.text.size() == 100);
}
