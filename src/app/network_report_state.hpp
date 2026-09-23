#pragma once

#include "platform/fast_action_services.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace pastit {

enum class NetworkReportStatus { Idle, Running, Completed, Failed };

struct NetworkProbeResult {
    NetworkProbe probe = NetworkProbe::Ping;
    ProcessOutput output;
    int elapsed_ms = 0;
};

struct NetworkReportRecord {
    std::string action_id;
    std::string target;
    NetworkReportStatus status = NetworkReportStatus::Idle;
    std::vector<NetworkProbeResult> results;
    std::string error;
};

class NetworkReportState {
public:
    void start(std::string action_id, std::string target);
    void add_probe(NetworkProbe probe, ProcessOutput output, int elapsed_ms);
    void complete();
    void fail(std::string error);
    std::string copy_text() const;
    const NetworkReportRecord& active() const { return active_; }

private:
    NetworkReportRecord active_;
};

std::string_view network_probe_label(NetworkProbe probe);

}  // namespace pastit
