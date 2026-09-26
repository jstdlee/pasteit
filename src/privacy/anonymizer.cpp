#include "privacy/anonymizer.hpp"

#include "privacy/name_lists.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace pastit {
namespace {

constexpr std::size_t kMaxScanBytes = 512 * 1024;

std::string lower(std::string_view text) {
    std::string out{text};
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::string digits_of(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        if (std::isdigit(static_cast<unsigned char>(ch))) out += ch;
    }
    return out;
}

// Priority when findings overlap: specific, validated kinds win.
int priority(PiiCategory category) {
    switch (category) {
        case PiiCategory::Secret: return 10;
        case PiiCategory::Email: return 9;
        case PiiCategory::CreditCard: return 8;
        case PiiCategory::Iban: return 8;
        case PiiCategory::NationalId: return 8;
        case PiiCategory::Ipv6: return 7;
        case PiiCategory::Ipv4: return 7;
        case PiiCategory::Mac: return 7;
        case PiiCategory::HomePath: return 6;
        case PiiCategory::Custom: return 6;
        case PiiCategory::BirthDate: return 5;
        case PiiCategory::Phone: return 4;
        case PiiCategory::Address: return 3;
        case PiiCategory::Person: return 2;
    }
    return 0;
}

struct Collector {
    std::string_view text;
    const AnonymizeOptions& options;
    std::vector<PiiFinding> findings;

    bool enabled(PiiCategory category) const { return !options.disabled.contains(category); }

    void add(std::size_t begin, std::size_t end, PiiCategory category) {
        if (!enabled(category) || end <= begin || end > text.size()) return;
        std::string value{text.substr(begin, end - begin)};
        for (const auto& keep : options.never_hide) {
            if (!keep.empty() && lower(keep) == lower(value)) return;
        }
        findings.push_back({begin, end, category, std::move(value)});
    }

    // Adds every match of a regex; group selects the sensitive part.
    template <typename Validate>
    void scan(const std::regex& pattern, PiiCategory category, int group, Validate validate) {
        if (!enabled(category)) return;
        const std::string haystack{text};
        for (std::sregex_iterator it(haystack.begin(), haystack.end(), pattern), end; it != end; ++it) {
            const auto& match = *it;
            if (!match[group].matched) continue;
            const auto value = match[group].str();
            if (!validate(value)) continue;
            const auto begin = static_cast<std::size_t>(match.position(group));
            add(begin, begin + static_cast<std::size_t>(match.length(group)), category);
        }
    }

    void scan(const std::regex& pattern, PiiCategory category, int group = 0) {
        scan(pattern, category, group, [](const std::string&) { return true; });
    }
};

bool valid_ipv4(const std::string& value) {
    std::istringstream parts(value);
    std::string part;
    int count = 0;
    while (std::getline(parts, part, '.')) {
        if (part.empty() || part.size() > 3 || (part.size() > 1 && part[0] == '0')) return false;
        if (std::stoi(part) > 255) return false;
        ++count;
    }
    // Version numbers like 1.2.3.4 are rare as IPs in prose but common in
    // text; skip the obvious ones.
    return count == 4 && value != "0.0.0.0";
}

bool valid_ipv6(const std::string& value) {
    const auto colons = std::count(value.begin(), value.end(), ':');
    if (colons < 2 || colons > 7) return false;
    if (value.find(":::") != std::string::npos) return false;
    const auto double_colon = value.find("::");
    if (double_colon != std::string::npos && value.find("::", double_colon + 1) != std::string::npos) return false;
    std::size_t groups = 0;
    std::istringstream parts(value);
    for (std::string part; std::getline(parts, part, ':');) {
        if (part.empty()) continue;
        if (part.size() > 4 || !std::all_of(part.begin(), part.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); })) {
            return false;
        }
        ++groups;
    }
    // Times like 12:30:45 are not addresses.
    const bool all_decimal = std::all_of(value.begin(), value.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) || ch == ':'; });
    return !all_decimal && (groups == 8 || (double_colon != std::string::npos && groups < 8));
}

bool plausible_phone(const std::string& value) {
    const auto digits = digits_of(value);
    if (digits.size() < 8 || digits.size() > 15) return false;
    // A bare digit run (no +, spaces or dashes) is usually an ID, date or
    // amount; accept only Singapore numbers and Chinese mobiles.
    if (digits.size() == value.size()) {
        static const std::regex local(R"(^[3689]\d{7}$|^1[3-9]\d{9}$)");
        if (!std::regex_match(digits, local)) return false;
    }
    // Dates and times written with separators are not phone numbers.
    static const std::regex date_like(R"(^\d{4}[-/.]\d{1,2}[-/.]\d{1,2}$|^\d{1,2}[-/.]\d{1,2}[-/.]\d{2,4}$)");
    if (std::regex_match(value, date_like)) return false;
    return std::set<char>(digits.begin(), digits.end()).size() > 2;  // not 00000000
}

}  // namespace

bool luhn_valid(std::string_view text) {
    const auto digits = digits_of(text);
    if (digits.size() < 13 || digits.size() > 19) return false;
    int sum = 0;
    bool twice = false;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        int digit = *it - '0';
        if (twice) {
            digit *= 2;
            if (digit > 9) digit -= 9;
        }
        sum += digit;
        twice = !twice;
    }
    return sum % 10 == 0;
}

bool iban_valid(std::string_view text) {
    std::string iban;
    for (const char ch : text) {
        if (ch != ' ') iban += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (iban.size() < 15 || iban.size() > 34) return false;
    const auto rotated = iban.substr(4) + iban.substr(0, 4);
    int remainder = 0;
    for (const char ch : rotated) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            remainder = (remainder * 10 + (ch - '0')) % 97;
        } else if (std::isupper(static_cast<unsigned char>(ch))) {
            const int value = ch - 'A' + 10;
            remainder = (remainder * 100 + value) % 97;
        } else {
            return false;
        }
    }
    return remainder == 1;
}

bool singapore_nric_valid(std::string_view id) {
    if (id.size() != 9) return false;
    const char prefix = static_cast<char>(std::toupper(static_cast<unsigned char>(id[0])));
    if (std::string_view{"STFGM"}.find(prefix) == std::string_view::npos) return false;
    static constexpr int weights[] = {2, 7, 6, 5, 4, 3, 2};
    int sum = 0;
    for (int index = 0; index < 7; ++index) {
        if (!std::isdigit(static_cast<unsigned char>(id[index + 1]))) return false;
        sum += (id[index + 1] - '0') * weights[index];
    }
    if (prefix == 'T' || prefix == 'G') sum += 4;
    if (prefix == 'M') sum += 3;
    const int remainder = sum % 11;
    const char check = static_cast<char>(std::toupper(static_cast<unsigned char>(id[8])));
    static constexpr std::string_view citizen = "JZIHGFEDCBA";
    static constexpr std::string_view foreigner = "XWUTRQPNMLK";
    static constexpr std::string_view m_series = "KLJNPQRTUWX";
    const auto table = prefix == 'S' || prefix == 'T' ? citizen : prefix == 'M' ? m_series : foreigner;
    return table[static_cast<std::size_t>(prefix == 'M' ? 10 - remainder : remainder)] == check;
}

bool china_resident_id_valid(std::string_view id) {
    if (id.size() != 18) return false;
    static constexpr int weights[] = {7, 9, 10, 5, 8, 4, 2, 1, 6, 3, 7, 9, 10, 5, 8, 4, 2};
    static constexpr std::string_view checks = "10X98765432";
    int sum = 0;
    for (int index = 0; index < 17; ++index) {
        if (!std::isdigit(static_cast<unsigned char>(id[index]))) return false;
        sum += (id[index] - '0') * weights[index];
    }
    return checks[static_cast<std::size_t>(sum % 11)] == std::toupper(static_cast<unsigned char>(id[17]));
}

std::string pii_category_name(PiiCategory category) {
    switch (category) {
        case PiiCategory::Email: return "email";
        case PiiCategory::Phone: return "phone";
        case PiiCategory::Ipv4: return "ipv4";
        case PiiCategory::Ipv6: return "ipv6";
        case PiiCategory::Mac: return "mac";
        case PiiCategory::CreditCard: return "card";
        case PiiCategory::Iban: return "iban";
        case PiiCategory::NationalId: return "id";
        case PiiCategory::Secret: return "secret";
        case PiiCategory::Person: return "name";
        case PiiCategory::Address: return "address";
        case PiiCategory::BirthDate: return "birthdate";
        case PiiCategory::HomePath: return "path";
        case PiiCategory::Custom: return "custom";
    }
    return "custom";
}

std::string pii_category_label(PiiCategory category) {
    switch (category) {
        case PiiCategory::Email: return "Email";
        case PiiCategory::Phone: return "Phone";
        case PiiCategory::Ipv4: return "IPv4";
        case PiiCategory::Ipv6: return "IPv6";
        case PiiCategory::Mac: return "MAC address";
        case PiiCategory::CreditCard: return "Card number";
        case PiiCategory::Iban: return "IBAN";
        case PiiCategory::NationalId: return "ID number";
        case PiiCategory::Secret: return "Secret / key";
        case PiiCategory::Person: return "Person name";
        case PiiCategory::Address: return "Address";
        case PiiCategory::BirthDate: return "Birth date";
        case PiiCategory::HomePath: return "User folder";
        case PiiCategory::Custom: return "Custom word";
    }
    return "Custom word";
}

const std::vector<PiiCategory>& all_pii_categories() {
    static const std::vector<PiiCategory> categories{
        PiiCategory::Person, PiiCategory::Email, PiiCategory::Phone, PiiCategory::Address, PiiCategory::BirthDate,
        PiiCategory::NationalId, PiiCategory::CreditCard, PiiCategory::Iban, PiiCategory::Ipv4, PiiCategory::Ipv6,
        PiiCategory::Mac, PiiCategory::Secret, PiiCategory::HomePath, PiiCategory::Custom,
    };
    return categories;
}

ReplacementStyle replacement_style_from_name(std::string_view name) {
    if (name == "mask") return ReplacementStyle::Mask;
    if (name == "fake") return ReplacementStyle::Fake;
    if (name == "redact") return ReplacementStyle::Redact;
    return ReplacementStyle::Placeholder;
}

std::vector<PiiFinding> find_pii(std::string_view full_text, const AnonymizeOptions& options) {
    const auto text = full_text.substr(0, std::min(full_text.size(), kMaxScanBytes));
    Collector collector{text, options, {}};

    // Secrets first: keys and tokens have unmistakable shapes.
    static const std::regex private_key(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)");
    static const std::regex key_shapes(
        R"(\b(AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{40,}|xox[abprs]-[A-Za-z0-9-]{10,}|sk-[A-Za-z0-9_-]{20,}|AIza[0-9A-Za-z_-]{35}|eyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]*))");
    static const std::regex bearer(R"(\b[Bb]earer\s+([A-Za-z0-9._~+/-]{16,}=*))");
    static const std::regex assigned_secret(
        R"re(\b(?:password|passwd|pwd|secret|token|api[_-]?key|access[_-]?token|client[_-]?secret)\b["']?\s*[:=]\s*["']?([^\s"',;]{4,}))re",
        std::regex::icase);
    static const std::regex url_password(R"(\b[a-z][a-z0-9+.-]*://[^\s:/@]+:([^\s@/]+)@)", std::regex::icase);
    collector.scan(private_key, PiiCategory::Secret);
    collector.scan(key_shapes, PiiCategory::Secret, 1);
    collector.scan(bearer, PiiCategory::Secret, 1);
    collector.scan(assigned_secret, PiiCategory::Secret, 1);
    collector.scan(url_password, PiiCategory::Secret, 1);

    static const std::regex email(R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b)");
    collector.scan(email, PiiCategory::Email);

    static const std::regex card(R"(\b(?:\d[ -]?){12,18}\d\b)");
    collector.scan(card, PiiCategory::CreditCard, 0, [](const std::string& value) { return luhn_valid(value); });
    static const std::regex iban(R"(\b[A-Z]{2}\d{2}(?: ?[A-Z0-9]{4}){2,7}(?: ?[A-Z0-9]{1,4})?\b)");
    collector.scan(iban, PiiCategory::Iban, 0, [](const std::string& value) { return iban_valid(value); });
    static const std::regex nric(R"(\b[STFGMstfgm]\d{7}[A-Za-z]\b)");
    collector.scan(nric, PiiCategory::NationalId, 0, [](const std::string& value) { return singapore_nric_valid(value); });
    static const std::regex china_id(R"(\b\d{17}[\dXx]\b)");
    collector.scan(china_id, PiiCategory::NationalId, 0, [](const std::string& value) { return china_resident_id_valid(value); });

    static const std::regex ipv4(R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)");
    collector.scan(ipv4, PiiCategory::Ipv4, 0, valid_ipv4);
    static const std::regex ipv6(R"((?:^|[^\w:])((?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4})(?![\w:]))");
    collector.scan(ipv6, PiiCategory::Ipv6, 1, valid_ipv6);
    static const std::regex mac(R"(\b(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b)");
    collector.scan(mac, PiiCategory::Mac);

    static const std::regex phone(R"((?:\+\d{1,3}[\s.-]?)?(?:\(\d{1,4}\)[\s.-]?)?\d[\d\s.-]{6,}\d)");
    collector.scan(phone, PiiCategory::Phone, 0, plausible_phone);

    // Multi-byte characters are grouped, never put in [...] or before ?:
    // std::regex matches bytes.
    static const std::regex birth(R"((?:DOB|D\.O\.B\.|date of birth|birth ?date|born(?: on)?|出生日期|生日)\s*(?::|：)?\s*(\d{1,4}(?:[-/.]|年)\d{1,2}(?:[-/.]|月)\d{1,4}(?:日)?|\d{1,2} [A-Z][a-z]+ \d{4}|[A-Z][a-z]+ \d{1,2},? \d{4}))",
                                  std::regex::icase);
    collector.scan(birth, PiiCategory::BirthDate, 1);

    static const std::regex street(
        R"(\b\d{1,5}[A-Za-z]?,?\s+(?:[A-Z][A-Za-z'.-]*\s){0,4}(?:Street|St\.?|Road|Rd\.?|Avenue|Ave\.?|Lane|Ln\.?|Drive|Dr\.?|Boulevard|Blvd\.?|Way|Place|Pl\.?|Crescent|Close|Court|Ct\.?)\b(?:[^\n]{0,40}?\b\d{5,6}\b)?)");
    collector.scan(street, PiiCategory::Address);
    static const std::regex postal_block(R"((?:Blk|Block)\s+\d+[A-Z]?\b[^\n]{0,60}?(?:Singapore\s+)?\d{6}\b)", std::regex::icase);
    collector.scan(postal_block, PiiCategory::Address);

    static const std::regex home(R"((?:/home/|/Users/|[A-Za-z]:\\Users\\)([^/\\\s]+))");
    collector.scan(home, PiiCategory::HomePath, 1, [](const std::string& user) {
        return user != "Shared" && user != "Public" && user != "Default";
    });

    // Names: labels and titles, then dictionary pairs, then Chinese forms.
    static const std::regex labeled_name(
        R"((?:\b(?:Name|Full name|Contact|From|To|Attn|Dear|Hi|Hello|Signed|Regards|Sincerely|Customer|Patient|Employee)\b[:,]?\s+)((?:[A-Z][a-z'-]+)(?:\s+[A-Z][a-z'-]+){0,2}))");
    collector.scan(labeled_name, PiiCategory::Person, 1, [](const std::string& value) {
        static const std::set<std::string> not_names{"Team", "All", "There", "Everyone", "Sir", "Madam", "Customer", "Support"};
        return !not_names.contains(value);
    });
    static const std::regex titled_name(R"(\b(?:Mr|Mrs|Ms|Miss|Dr|Prof)\.?\s+((?:[A-Z][a-z'-]+)(?:\s+[A-Z][a-z'-]+)?))");
    collector.scan(titled_name, PiiCategory::Person, 1);
    static const std::regex capitalized_pair(R"(\b([A-Z][a-z'-]+)\s+([A-Z][a-z'-]+)(?:\s+([A-Z][a-z'-]+))?\b)");
    if (collector.enabled(PiiCategory::Person)) {
        const std::string haystack{text};
        for (std::sregex_iterator it(haystack.begin(), haystack.end(), capitalized_pair), end; it != end; ++it) {
            const auto& match = *it;
            const bool first_known = is_common_given_name(match[1].str());
            const bool last_known = is_common_surname(match[2].str()) ||
                                    (match[3].matched && is_common_surname(match[3].str()));
            if (first_known || (last_known && is_common_given_name(match[1].str()))) {
                const auto begin = static_cast<std::size_t>(match.position(0));
                collector.add(begin, begin + static_cast<std::size_t>(match.length(0)), PiiCategory::Person);
            }
        }
        static const std::regex chinese_labeled(R"((?:姓名|联系人|负责人|收件人|经办人)\s*(?::|：)?\s*([\xE4-\xE9][\x80-\xBF]{2}(?:[\xE4-\xE9][\x80-\xBF]{2}){1,2}))");
        collector.scan(chinese_labeled, PiiCategory::Person, 1);
        static const std::regex chinese_titled(R"(([\xE4-\xE9][\x80-\xBF]{2}(?:[\xE4-\xE9][\x80-\xBF]{2}){0,2})(?:先生|女士|小姐|老师|经理|总监|医生))");
        collector.scan(chinese_titled, PiiCategory::Person, 1, [](const std::string& value) {
            return is_chinese_surname(value.substr(0, 3));
        });
    }

    // Custom words, whole-word and case-insensitive.
    if (collector.enabled(PiiCategory::Custom)) {
        const auto haystack = lower(text);
        for (const auto& word : options.always_hide) {
            const auto needle = lower(word);
            if (needle.empty()) continue;
            for (auto at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + needle.size())) {
                const bool left_ok = at == 0 || !std::isalnum(static_cast<unsigned char>(haystack[at - 1]));
                const auto after = at + needle.size();
                const bool right_ok = after >= haystack.size() || !std::isalnum(static_cast<unsigned char>(haystack[after]));
                if (left_ok && right_ok) collector.add(at, after, PiiCategory::Custom);
            }
        }
    }

    // Resolve overlaps: higher priority, then longer, wins.
    auto& findings = collector.findings;
    std::sort(findings.begin(), findings.end(), [](const PiiFinding& left, const PiiFinding& right) {
        if (priority(left.category) != priority(right.category)) return priority(left.category) > priority(right.category);
        return left.end - left.begin > right.end - right.begin;
    });
    std::vector<PiiFinding> kept;
    for (auto& finding : findings) {
        const bool overlaps = std::any_of(kept.begin(), kept.end(), [&](const PiiFinding& other) {
            return finding.begin < other.end && other.begin < finding.end;
        });
        if (!overlaps) kept.push_back(std::move(finding));
    }
    std::sort(kept.begin(), kept.end(), [](const PiiFinding& left, const PiiFinding& right) { return left.begin < right.begin; });
    return kept;
}

std::string PlaceholderVault::placeholder_for(PiiCategory category, const std::string& original) {
    const auto key = pii_category_name(category) + "|" + original;
    if (const auto found = placeholders_.find(key); found != placeholders_.end()) return found->second;
    auto name = pii_category_name(category);
    for (auto& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const auto placeholder = "[" + name + "_" + std::to_string(++counters_[category]) + "]";
    placeholders_[key] = placeholder;
    originals_[placeholder] = original;
    return placeholder;
}

std::string PlaceholderVault::restore(std::string_view text) const {
    static const std::regex placeholder(R"(\[[A-Z0-9]+_\d+\])");
    std::string out;
    const std::string input{text};
    std::size_t last = 0;
    for (std::sregex_iterator it(input.begin(), input.end(), placeholder), end; it != end; ++it) {
        const auto position = static_cast<std::size_t>(it->position());
        out.append(input, last, position - last);
        const auto found = originals_.find(it->str());
        out += found == originals_.end() ? it->str() : found->second;
        last = position + static_cast<std::size_t>(it->length());
    }
    out.append(input, last, std::string::npos);
    return out;
}

std::size_t PlaceholderVault::restorable_count(std::string_view text) const {
    std::size_t count = 0;
    for (const auto& [placeholder, original] : originals_) {
        if (text.find(placeholder) != std::string_view::npos) ++count;
    }
    return count;
}

void PlaceholderVault::clear() {
    originals_.clear();
    placeholders_.clear();
    counters_.clear();
}

bool contains_placeholders(std::string_view text) {
    static const std::regex placeholder(
        R"(\[(EMAIL|PHONE|IPV4|IPV6|MAC|CARD|IBAN|ID|SECRET|NAME|ADDRESS|BIRTHDATE|PATH|CUSTOM)_\d+\])");
    return std::regex_search(text.begin(), text.end(), placeholder);
}

namespace {

std::string mask_value(PiiCategory category, const std::string& value) {
    if (category == PiiCategory::Email) {
        const auto at = value.find('@');
        const auto dot = value.rfind('.');
        std::string out = value.substr(0, 1) + "***@";
        if (at != std::string::npos && at + 1 < value.size()) out += value.substr(at + 1, 1) + "***";
        if (dot != std::string::npos && dot > at) out += value.substr(dot);
        return out;
    }
    if (category == PiiCategory::Phone || category == PiiCategory::CreditCard || category == PiiCategory::Iban ||
        category == PiiCategory::NationalId) {
        // Keep the last digits, like a receipt.
        std::string out = value;
        std::size_t keep = 0;
        for (auto it = out.rbegin(); it != out.rend(); ++it) {
            if (!std::isalnum(static_cast<unsigned char>(*it))) continue;
            if (keep++ >= (category == PiiCategory::Phone ? 2U : 4U)) *it = '*';
        }
        return out;
    }
    if (value.size() <= 2) return std::string(value.size(), '*');
    return value.substr(0, 1) + std::string(std::min<std::size_t>(value.size() - 1, 8), '*');
}

std::string fake_value(PiiCategory category, std::size_t index) {
    const auto n = std::to_string(index);
    switch (category) {
        case PiiCategory::Email: return "user" + n + "@example.com";
        case PiiCategory::Phone: return "+1 555-01" + std::string(index < 10 ? "0" : "") + n;
        case PiiCategory::Ipv4: return "192.0.2." + std::to_string(index % 254 + 1);
        case PiiCategory::Ipv6: return "2001:db8::" + n;
        case PiiCategory::Mac: return "00:00:5e:00:53:" + std::string(index < 16 ? "0" : "") + std::to_string(index % 100);
        case PiiCategory::CreditCard: return "0000 0000 0000 " + std::string(4 - std::min<std::size_t>(4, n.size()), '0') + n;
        case PiiCategory::Iban: return "XX00 TEST 0000 0000 " + n;
        case PiiCategory::NationalId: return "ID-" + n;
        case PiiCategory::Secret: return "REDACTED_SECRET_" + n;
        case PiiCategory::Person: return "Person " + std::string(1, static_cast<char>('A' + (index - 1) % 26));
        case PiiCategory::Address: return n + " Example Street";
        case PiiCategory::BirthDate: return "1970-01-01";
        case PiiCategory::HomePath: return "user";
        case PiiCategory::Custom: return "Redacted " + n;
    }
    return "redacted";
}

}  // namespace

AnonymizeResult apply_anonymization(std::string_view text, const std::vector<PiiFinding>& findings,
                                    ReplacementStyle style, PlaceholderVault* vault) {
    AnonymizeResult result;
    PlaceholderVault local;
    auto& placeholders = vault != nullptr ? *vault : local;
    std::map<std::string, std::size_t> fake_numbers;
    std::map<PiiCategory, std::size_t> fake_counters;
    std::size_t last = 0;
    for (const auto& finding : findings) {
        if (finding.begin < last || finding.end > text.size()) continue;
        result.text.append(text.substr(last, finding.begin - last));
        switch (style) {
            case ReplacementStyle::Placeholder:
                result.text += placeholders.placeholder_for(finding.category, finding.text);
                break;
            case ReplacementStyle::Mask:
                result.text += mask_value(finding.category, finding.text);
                break;
            case ReplacementStyle::Fake: {
                const auto key = pii_category_name(finding.category) + "|" + finding.text;
                auto [it, inserted] = fake_numbers.try_emplace(key, 0);
                if (inserted) it->second = ++fake_counters[finding.category];
                result.text += fake_value(finding.category, it->second);
                break;
            }
            case ReplacementStyle::Redact: {
                const auto characters = std::count_if(finding.text.begin(), finding.text.end(), [](char ch) {
                    return (static_cast<unsigned char>(ch) & 0xC0U) != 0x80U;
                });
                for (std::ptrdiff_t index = 0; index < std::min<std::ptrdiff_t>(characters, 24); ++index) result.text += "\xE2\x96\x88";
                break;
            }
        }
        last = finding.end;
        result.findings.push_back(finding);
    }
    result.text.append(text.substr(last));
    return result;
}

AnonymizeResult anonymize_text(std::string_view text, const AnonymizeOptions& options, PlaceholderVault* vault) {
    return apply_anonymization(text, find_pii(text, options), options.style, vault);
}

}  // namespace pastit
