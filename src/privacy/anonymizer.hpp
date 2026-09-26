#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

enum class PiiCategory {
    Email,
    Phone,
    Ipv4,
    Ipv6,
    Mac,
    CreditCard,
    Iban,
    NationalId,
    Secret,
    Person,
    Address,
    BirthDate,
    HomePath,
    Custom,
};

enum class ReplacementStyle { Placeholder, Mask, Fake, Redact };

struct PiiFinding {
    std::size_t begin = 0;
    std::size_t end = 0;
    PiiCategory category = PiiCategory::Custom;
    std::string text;
};

struct AnonymizeOptions {
    ReplacementStyle style = ReplacementStyle::Placeholder;
    std::set<PiiCategory> disabled;
    std::vector<std::string> always_hide;  // custom words, matched case-insensitively
    std::vector<std::string> never_hide;   // exact values to leave untouched
};

// Session memory of placeholder <-> original, so repeated values get the same
// placeholder and LLM output can be mapped back. Never persisted.
class PlaceholderVault {
public:
    std::string placeholder_for(PiiCategory category, const std::string& original);
    // Replaces known placeholders (e.g. [EMAIL_1]) with their originals.
    std::string restore(std::string_view text) const;
    std::size_t restorable_count(std::string_view text) const;
    bool empty() const { return originals_.empty(); }
    void clear();

private:
    std::map<std::string, std::string> originals_;   // placeholder -> original
    std::map<std::string, std::string> placeholders_;  // category|original -> placeholder
    std::map<PiiCategory, std::size_t> counters_;
};

struct AnonymizeResult {
    std::string text;
    std::vector<PiiFinding> findings;
};

std::vector<PiiFinding> find_pii(std::string_view text, const AnonymizeOptions& options = {});
// Replaces the given findings (as returned by find_pii, possibly filtered).
AnonymizeResult apply_anonymization(std::string_view text, const std::vector<PiiFinding>& findings,
                                    ReplacementStyle style, PlaceholderVault* vault = nullptr);
AnonymizeResult anonymize_text(std::string_view text, const AnonymizeOptions& options, PlaceholderVault* vault = nullptr);

bool contains_placeholders(std::string_view text);

std::string pii_category_name(PiiCategory category);  // stable id, e.g. "email"
std::string pii_category_label(PiiCategory category);
const std::vector<PiiCategory>& all_pii_categories();
ReplacementStyle replacement_style_from_name(std::string_view name);

// Checksums used to confirm candidates.
bool luhn_valid(std::string_view digits);
bool iban_valid(std::string_view iban);
bool singapore_nric_valid(std::string_view id);
bool china_resident_id_valid(std::string_view id);

}  // namespace pastit
