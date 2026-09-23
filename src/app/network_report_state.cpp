#include "app/network_report_state.hpp"

#include <sstream>

namespace pastit {

std::string_view network_probe_label(NetworkProbe probe) {
    switch (probe) {
        case NetworkProbe::Ping:
            return "Ping";
        case NetworkProbe::TraceRoute:
            return "Trace route";
        case NetworkProbe::ReverseDns:
            return "Reverse DNS";
        case NetworkProbe::Dig:
            return "DNS lookup";
    }
    return "Probe";
}

void NetworkReportState::start(std::string action_id, std::string target) {
    active_ = NetworkReportRecord{
        .action_id = std::move(action_id),
        .target = std::move(target),
        .status = NetworkReportStatus::Running,
        .results = {},
        .error = {},
    };
}

void NetworkReportState::add_probe(NetworkProbe probe, ProcessOutput output, int elapsed_ms) {
    active_.results.push_back(NetworkProbeResult{
        .probe = probe,
        .output = std::move(output),
        .elapsed_ms = elapsed_ms,
    });
}

void NetworkReportState::complete() {
    active_.status = NetworkReportStatus::Completed;
    active_.error.clear();
}

void NetworkReportState::fail(std::string error) {
    active_.status = NetworkReportStatus::Failed;
    active_.error = std::move(error);
}

std::string NetworkReportState::copy_text() const {
    std::ostringstream out;
    if (!active_.target.empty()) {
        out << "Target: " << active_.target << "\n\n";
    }
    for (std::size_t index = 0; index < active_.results.size(); ++index) {
        const auto& result = active_.results[index];
        if (index != 0) {
            out << '\n';
        }
        out << network_probe_label(result.probe) << " (exit " << result.output.exit_code
            << ", " << result.elapsed_ms << " ms)\n";
        if (!result.output.stdout_text.empty()) {
            out << result.output.stdout_text;
            if (!result.output.stdout_text.ends_with('\n')) {
                out << '\n';
            }
        }
        if (!result.output.stderr_text.empty()) {
            out << result.output.stderr_text;
            if (!result.output.stderr_text.ends_with('\n')) {
                out << '\n';
            }
        }
    }
    if (!active_.error.empty()) {
        out << "\nError: " << active_.error << '\n';
    }
    return out.str();
}

}  // namespace pastit
