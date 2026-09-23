#include "detect/fast_content_detector.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <string_view>

namespace {

bool has_field(const std::vector<pastit::ContactField>& fields,
               std::string_view kind,
               std::string_view value) {
    return std::any_of(fields.begin(), fields.end(), [&](const auto& field) {
        return field.kind == kind && field.value == value;
    });
}

}  // namespace

int main() {
    using namespace pastit;

    const auto contact = detect_fast_content(
        ContentKind::Text,
        "Ada Lovelace\nAnalytical Engine\nada@example.com\n+44 20 1234 5678\n1 Logic Lane, London");
    assert(contact.contact);
    assert(contact.contact_fields.size() >= 3);
    assert(has_field(contact.contact_fields, "email", "ada@example.com"));
    assert(has_field(contact.contact_fields, "phone", "+44 20 1234 5678"));

    assert(detect_fast_content(ContentKind::Text, "connect 192.0.2.10").ip);
    assert(detect_fast_content(ContentKind::Text, "route to 2001:db8::1").ip);
    assert(detect_fast_content(ContentKind::Url, "https://github.com/acme/widget").github_url);
    assert(detect_fast_content(ContentKind::Url, "https://github.com/acme/widget.git?tab=readme#usage").github_url);
    assert(detect_fast_content(ContentKind::Text, "A -> B -> C").diagram);
    assert(!detect_fast_content(ContentKind::Text, "press -> to continue").diagram);

    const auto iso_time = detect_fast_content(ContentKind::Text, "2026-09-21T08:30:00Z");
    assert(iso_time.date_time);
    assert(iso_time.date_time_value.has_value());
    assert(iso_time.date_time_value->original == "2026-09-21T08:30:00Z");
    assert(iso_time.date_time_value->normalized == "2026-09-21T08:30:00Z");
    assert(iso_time.date_time_value->source_zone == "UTC");
    assert(iso_time.date_time_value->has_epoch);
    assert(detect_fast_content(ContentKind::Text, "2024-02-29T08:30:00+14:00").date_time);
    assert(!detect_fast_content(ContentKind::Text, "2026-02-30T08:30:00Z").date_time);
    assert(!detect_fast_content(ContentKind::Text, "2023-02-29T08:30:00Z").date_time);
    assert(!detect_fast_content(ContentKind::Text, "2026-09-21T08:30:00+24:00").date_time);
    assert(!detect_fast_content(ContentKind::Text, "2026-09-21T08:30:00+14:30").date_time);
    assert(!detect_fast_content(ContentKind::DateTime, "not a supported date").date_time);
    const auto later_valid_time = detect_fast_content(
        ContentKind::Text,
        "ignore 2026-02-30T08:30:00Z but keep 2026-09-21T08:30:00Z");
    assert(later_valid_time.date_time);
    assert(later_valid_time.date_time_value->original == "2026-09-21T08:30:00Z");
    assert(detect_fast_content(ContentKind::Text, "Mon, 21 Sep 2026 08:30:00 GMT").date_time);
    assert(detect_fast_content(ContentKind::Text, "Sep 21, 2026 8:30 PM").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Mon, 30 Feb 2026 08:30:00 GMT").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Mon, 21 Sep 2026 24:30:00 GMT").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Mon, 21 Sep 2026 08:30:00 +2400").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Mon, 21 Sep 2026 08:30:00 +1430").date_time);
    assert(!detect_fast_content(ContentKind::Text, "02/30/2026 08:30").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Feb 29, 2023 08:30").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Sep 21, 2026 13:30 PM").date_time);
    assert(!detect_fast_content(ContentKind::Text, "Sep 21, 2026 24:30").date_time);

    assert(!detect_fast_content(ContentKind::Text, "999.999.1.1").ip);

    assert(detect_fast_content(ContentKind::Text, "int main() { return 0; }").code);
    assert(!detect_fast_content(ContentKind::Url, "http://github.com/acme/widget").github_url);
    assert(!detect_fast_content(ContentKind::Url, "https://github.com/acme/widget/issues").github_url);
    assert(!detect_fast_content(ContentKind::Url, "https://github.com/acme/widget/tree/main").github_url);
    assert(!detect_fast_content(ContentKind::Url, "https://github.com/acme/widget/blob/main/README.md").github_url);
    assert(!detect_fast_content(ContentKind::Text, "The timeline extends into next quarter.").diagram);
    assert(!detect_fast_content(ContentKind::Text, "This inherits the previous policy language.").diagram);
    assert(detect_fast_content(ContentKind::Url, "https://example.com/report").qr);
    assert(detect_fast_content(ContentKind::Email, "ada@example.com").qr);
    assert(detect_fast_content(ContentKind::Text, "short payload").qr);
    assert(!detect_fast_content(ContentKind::Text, "").qr);
    assert(!detect_fast_content(ContentKind::Text, std::string(2049, 'x')).qr);

    const auto large = std::string(9000, 'x') + "\nhttps://github.com/acme/widget";
    const auto large_signals = detect_fast_content(ContentKind::Text, large);
    assert(!large_signals.github_url);
    assert(!large_signals.date_time);
    assert(!large_signals.qr);
}
