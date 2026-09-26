#include "privacy/anonymizer.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

using namespace pasteit;

namespace {

bool found(const std::vector<PiiFinding>& findings, PiiCategory category, const std::string& text) {
    return std::any_of(findings.begin(), findings.end(), [&](const PiiFinding& finding) {
        return finding.category == category && finding.text == text;
    });
}

void dump(const std::vector<PiiFinding>& findings) {
    for (const auto& finding : findings) std::cerr << pii_category_name(finding.category) << ": [" << finding.text << "]\n";
}

}  // namespace

int main() {
    // Checksums.
    assert(luhn_valid("4111 1111 1111 1111") && !luhn_valid("4111 1111 1111 1112"));
    assert(iban_valid("GB82 WEST 1234 5698 7654 32") && !iban_valid("GB82 WEST 1234 5698 7654 33"));
    assert(singapore_nric_valid("S1234567D") && !singapore_nric_valid("S1234567A"));
    assert(china_resident_id_valid("11010519491231002X") && !china_resident_id_valid("110105194912310021"));

    const std::string text =
        "Dear John Smith,\n"
        "Please call +65 9123 4567 or email john.smith@acme.com.\n"
        "Server 10.0.0.12 and fe80::1ff:fe23:4567:890a are down; MAC 00:1A:2B:3C:4D:5E.\n"
        "Card 4111 1111 1111 1111, IBAN GB82 WEST 1234 5698 7654 32, NRIC S1234567D.\n"
        "api_key=sk-abcdefghijklmnopqrstuv123 and password: hunter22\n"
        "Logs in /home/user/app.log. DOB: 1990-05-17. Ship to 12 Orchard Road 238823.\n"
        "Meeting at 12:30:45 on 2026-09-27, version 1.2.3, order 20260927.\n"
        "联系人：张伟，王芳女士负责。Project Falcon is secret.\n";
    AnonymizeOptions options;
    options.always_hide = {"Project Falcon"};
    const auto findings = find_pii(text, options);
    const auto expect = [&](PiiCategory category, const std::string& value) {
        if (!found(findings, category, value)) {
            dump(findings);
            std::cerr << "missing " << pii_category_name(category) << ": " << value << "\n";
            assert(false);
        }
    };
    expect(PiiCategory::Person, "John Smith");
    expect(PiiCategory::Phone, "+65 9123 4567");
    expect(PiiCategory::Email, "john.smith@acme.com");
    expect(PiiCategory::Ipv4, "10.0.0.12");
    expect(PiiCategory::Ipv6, "fe80::1ff:fe23:4567:890a");
    expect(PiiCategory::Mac, "00:1A:2B:3C:4D:5E");
    expect(PiiCategory::CreditCard, "4111 1111 1111 1111");
    expect(PiiCategory::Iban, "GB82 WEST 1234 5698 7654 32");
    expect(PiiCategory::NationalId, "S1234567D");
    expect(PiiCategory::Secret, "sk-abcdefghijklmnopqrstuv123");
    expect(PiiCategory::Secret, "hunter22");
    expect(PiiCategory::HomePath, "user");
    expect(PiiCategory::BirthDate, "1990-05-17");
    expect(PiiCategory::Address, "12 Orchard Road 238823");
    expect(PiiCategory::Person, "张伟");
    expect(PiiCategory::Person, "王芳");
    expect(PiiCategory::Custom, "Project Falcon");
    // Times, dates, versions and order numbers are not personal data.
    for (const auto& finding : findings) {
        assert(finding.text.find("12:30:45") == std::string::npos);
        assert(finding.text != "2026-09-27" && finding.text != "20260927" && finding.text != "1.2.3");
    }

    // Placeholders are consistent and reversible through the vault.
    PlaceholderVault vault;
    const auto result = anonymize_text("Mail anna@x.io, then anna@x.io again. Mr Brown agrees.", {}, &vault);
    assert(result.text == "Mail [EMAIL_1], then [EMAIL_1] again. Mr [NAME_1] agrees.");
    assert(contains_placeholders(result.text));
    assert(vault.restore("Reply to [EMAIL_1] and [NAME_1]; keep [EMAIL_9].") == "Reply to anna@x.io and Brown; keep [EMAIL_9].");
    assert(vault.restorable_count(result.text) == 2);

    // Other styles.
    AnonymizeOptions mask{.style = ReplacementStyle::Mask};
    assert(anonymize_text("call +65 9123 4567", mask).text == "call +** **** **67");
    assert(anonymize_text("to bob@mail.com", mask).text == "to b***@m***.com");
    AnonymizeOptions fake{.style = ReplacementStyle::Fake};
    assert(anonymize_text("10.1.2.3 and 10.1.2.3 and bob@mail.com", fake).text == "192.0.2.2 and 192.0.2.2 and user1@example.com");
    AnonymizeOptions redact{.style = ReplacementStyle::Redact};
    assert(anonymize_text("ip 10.1.2.3", redact).text == "ip " + [] { std::string out; for (int i = 0; i < 8; ++i) out += "\xE2\x96\x88"; return out; }());

    // Disabled categories and never-hide values are respected.
    AnonymizeOptions narrow;
    narrow.disabled = {PiiCategory::Ipv4};
    narrow.never_hide = {"support@acme.com"};
    const auto narrow_findings = find_pii("10.0.0.1 support@acme.com sales@acme.com", narrow);
    assert(narrow_findings.size() == 1 && narrow_findings[0].text == "sales@acme.com");

    // Ordinary prose stays untouched.
    assert(find_pii("The quick brown fox jumps over the lazy dog on Monday at 3pm.").empty());
    std::cout << "privacy ok\n";
}
