#include "detect/email_scan.hpp"

namespace pasteit {
namespace {

bool is_alpha(char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); }
bool is_alnum(char ch) { return is_alpha(ch) || (ch >= '0' && ch <= '9'); }
bool is_local(char ch) { return is_alnum(ch) || ch == '.' || ch == '_' || ch == '%' || ch == '+' || ch == '-'; }
bool is_domain(char ch) { return is_alnum(ch) || ch == '.' || ch == '-'; }

}  // namespace

std::vector<EmailMatch> find_emails(std::string_view text, std::size_t limit) {
    std::vector<EmailMatch> matches;
    std::size_t floor = 0;  // matches do not overlap: never extend left of the previous end
    for (std::size_t at = text.find('@'); at != std::string_view::npos && matches.size() < limit; at = text.find('@', at + 1)) {
        if (at < floor) continue;
        std::size_t start = at;
        while (start > floor && is_local(text[start - 1])) --start;
        if (start == at) continue;
        std::size_t run_end = at + 1;
        while (run_end < text.size() && is_domain(text[run_end])) ++run_end;
        // Longest domain prefix ending in "." + two or more letters, with at
        // least one character before that dot (what the greedy regex keeps).
        std::size_t end = run_end;
        bool found = false;
        for (; end >= at + 4; --end) {
            std::size_t letters = end;
            while (letters > at + 1 && is_alpha(text[letters - 1])) --letters;
            if (end - letters >= 2 && letters >= at + 3 && text[letters - 1] == '.') {
                found = true;
                break;
            }
            if (letters == end) continue;  // not a letter here: try a shorter end
            end = letters;                  // skip the whole letter run at once (loop decrements)
            if (end < at + 4) break;
        }
        if (!found) continue;
        matches.push_back({start, std::string{text.substr(start, end - start)}});
        floor = end;
        at = end - 1;
    }
    return matches;
}

bool contains_email(std::string_view text) {
    return !find_emails(text, 1).empty();
}

}  // namespace pasteit
