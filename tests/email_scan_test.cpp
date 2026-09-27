#include "detect/email_scan.hpp"
#include "detect/fast_content_detector.hpp"

#include <cassert>
#include <chrono>
#include <string>

int main() {
    using namespace pasteit;
    auto found = find_emails("Contact alice.tan@example.com or bob+x@mail.co.uk, not a@b or @x.com.");
    assert(found.size() == 2);
    assert(found[0].value == "alice.tan@example.com" && found[0].position == 8);
    assert(found[1].value == "bob+x@mail.co.uk");
    assert(find_emails("user@host.c1").empty());        // TLD needs two letters
    assert(find_emails("x@y.io.z9")[0].value == "x@y.io");  // longest valid domain prefix
    assert(contains_email("mail me: a_b@c-d.org") && !contains_email("no email here"));

    // Long runs of letters used to backtrack quadratically in std::regex
    // (MSVC throws error_complexity); the scanner stays linear.
    const std::string long_text = std::string(200000, 'a') + " " + std::string(50000, '1') + " end@of.text";
    const auto start = std::chrono::steady_clock::now();
    found = find_emails(long_text);
    assert(found.size() == 1 && found[0].value == "end@of.text");
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    const auto signals = detect_fast_content(ContentKind::Text, std::string(5000, 'a') + " -> " + std::string(5000, 'b'));
    (void)signals;  // must not throw on long arrow-like text
}
