#include "app/contact_result_state.hpp"
#include "app/date_time_result_state.hpp"
#include "app/hash_result_state.hpp"
#include "app/network_report_state.hpp"
#include "app/renderer_result_state.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main() {
    using namespace pasteit;

    ContactResultState contact;
    const auto contact_json =
        "{\"fields\":["
        "{\"kind\":\"email\",\"label\":\"Email\",\"value\":\"ada@example.com\"},"
        "{\"kind\":\"phone\",\"label\":\"Phone\",\"value\":\"+1 415 555 0100\"}"
        "]}";
    assert(contact.set_from_json("contact-action", contact_json));
    assert(contact.active().fields.size() == 2);
    assert(contact.active().fields[0].label == "Email");
    assert(contact.active().json == contact_json);
    assert(contact.copy_text().find("ada@example.com") != std::string::npos);
    assert(contact.copy_text().find("+1 415 555 0100") != std::string::npos);

    NetworkReportState network;
    network.start("network-action", "192.0.2.10");
    network.add_probe(NetworkProbe::Ping, ProcessOutput{.exit_code = 0, .stdout_text = "pong"}, 12);
    network.add_probe(NetworkProbe::TraceRoute, ProcessOutput{.exit_code = 1, .stderr_text = "timeout"}, 34);
    network.complete();
    assert(network.active().status == NetworkReportStatus::Completed);
    assert(network.active().results.size() == 2);
    assert(network.copy_text().find("Ping") != std::string::npos);
    assert(network.copy_text().find("timeout") != std::string::npos);

    DateTimeResultState date_time;
    date_time.complete(DateTimeResult{
        .action_id = "date-action",
        .original = "1970-01-01T00:00:05Z",
        .source_zone = "UTC",
        .target_zone = "Asia/Singapore",
        .epoch_seconds = 5,
        .formatted = "1970-01-01T08:00:05+08:00",
    });
    assert(date_time.active().status == DateTimeResultStatus::Completed);
    assert(date_time.copy_text() == "1970-01-01T08:00:05+08:00");

    HashResultState hash;
    hash.complete(HashResult{
        .action_id = "hash-action",
        .algorithm = HashAlgorithm::Sha256,
        .path = "/tmp/source.txt",
        .digest = "abc123",
    });
    assert(hash.active().status == HashResultStatus::Completed);
    assert(hash.copy_text() == "abc123");

    RendererResultState renderer;
    renderer.prepare(RendererResult{
        .action_id = "render-action",
        .kind = RendererResultKind::Mermaid,
        .source = "A -> B",
        .payload = "graph TD\n  A --> B\n",
        .available = true,
        .status = RendererResultStatus::Ready,
        .output_path = "/tmp/diagram.svg",
    });
    assert(renderer.active().available);
    assert(renderer.active().output_path == std::filesystem::path{"/tmp/diagram.svg"});
    assert(renderer.copy_text() == "graph TD\n  A --> B\n");
}
