#include "djev/djev_client.hpp"
#include "util/path_utf8.hpp"

#include "decision/candidate_selector.hpp"
#include "util/json.hpp"
#include "util/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <regex>
#include <sstream>
#include <set>
#include <utility>
#include <vector>

#if defined(PASTEIT_HAS_CURL)
#include <curl/curl.h>
#elif !defined(_WIN32)
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace pasteit {
namespace {

std::string json_escape(std::string_view value) {
    const auto quoted = json_quote(sanitize_utf8(value));
    return quoted.size() >= 2 ? quoted.substr(1, quoted.size() - 2) : std::string{};
}

std::string extract_string(std::string_view body, std::string_view key) {
    const std::regex pattern(std::string{"\""} + std::string(key) + "\"\\s*:\\s*\"([^\"]*)\"");
    std::cmatch match;
    const std::string copy(body);
    if (std::regex_search(copy.c_str(), match, pattern)) {
        return match.str(1);
    }
    return {};
}

double extract_number(std::string_view body, std::string_view key, double fallback = 0.0) {
    const std::regex pattern(std::string{"\""} + std::string(key) +
                             "\"\\s*:\\s*(-?(?:\\d+\\.?\\d*|\\d*\\.?\\d+)(?:[eE][+\\-]?\\d+)?)");
    std::cmatch match;
    const std::string copy(body);
    if (std::regex_search(copy.c_str(), match, pattern)) {
        return std::stod(match.str(1));
    }
    return fallback;
}

std::map<std::string, double> extract_probabilities(std::string_view body) {
    std::map<std::string, double> out;
    const auto key = body.find("\"probabilities\"");
    if (key == std::string_view::npos) {
        return out;
    }
    const auto open = body.find('{', key);
    if (open == std::string_view::npos) {
        return out;
    }
    int depth = 0;
    std::size_t close = std::string_view::npos;
    for (std::size_t index = open; index < body.size(); ++index) {
        if (body[index] == '{') {
            ++depth;
        } else if (body[index] == '}') {
            --depth;
            if (depth == 0) {
                close = index;
                break;
            }
        }
    }
    if (close == std::string_view::npos) {
        return out;
    }
    const auto object = std::string(body.substr(open + 1, close - open - 1));
    const std::regex pair_pattern("\"([^\"]+)\"\\s*:\\s*(-?(?:\\d+\\.?\\d*|\\d*\\.?\\d+)(?:[eE][+\\-]?\\d+)?)");
    for (std::sregex_iterator it(object.begin(), object.end(), pair_pattern), end; it != end; ++it) {
        out[it->str(1)] = std::stod(it->str(2));
    }
    return out;
}

std::set<std::string> request_action_ids(const DecisionRequest& request) {
    std::set<std::string> ids;
    for (const auto& action : request.snapshot.available_actions) {
        ids.insert(action.id);
    }
    return ids;
}

std::string content_kind_name(ContentKind kind) {
    switch (kind) {
        case ContentKind::Text:
            return "text";
        case ContentKind::Url:
            return "url";
        case ContentKind::Email:
            return "email";
        case ContentKind::DateTime:
            return "datetime";
        case ContentKind::Image:
            return "image";
        case ContentKind::Path:
            return "path";
        case ContentKind::Json:
            return "json";
        case ContentKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::string path_kind_name(PathKind kind) {
    return kind == PathKind::Directory ? "directory" : "file";
}

std::size_t utf8_width(unsigned char ch) {
    if ((ch & 0x80U) == 0U) return 1;
    if ((ch & 0xE0U) == 0xC0U) return 2;
    if ((ch & 0xF0U) == 0xE0U) return 3;
    if ((ch & 0xF8U) == 0xF0U) return 4;
    return 1;
}

std::string truncate_codepoints(std::string_view value, std::size_t limit) {
    std::size_t bytes = 0;
    std::size_t count = 0;
    while (bytes < value.size() && count < limit) {
        const auto width = utf8_width(static_cast<unsigned char>(value[bytes]));
        if (bytes + width > value.size()) {
            break;
        }
        bytes += width;
        ++count;
    }
    return std::string{value.substr(0, bytes)};
}

std::string mime_summary(const ClipboardItem& item) {
    std::ostringstream out;
    const auto count = std::min<std::size_t>(item.mime_types.size(), 3);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            out << ",";
        }
        out << item.mime_types[index];
    }
    return truncate_codepoints(out.str(), 120);
}

std::vector<ActionInstance> compact_payload_actions(const DecisionRequest& request, std::size_t limit) {
    std::vector<ActionInstance> actions;
    std::set<std::string> ids;
    actions.reserve(std::min(limit, request.snapshot.available_actions.size()));
    for (const auto& action : request.snapshot.available_actions) {
        if (actions.size() >= limit) {
            break;
        }
        if (!action.enabled || !is_current_source_action(action, request.snapshot) || !ids.insert(action.id).second) {
            continue;
        }
        actions.push_back(action);
    }
    return actions;
}

std::vector<PathLocation> destination_paths_for(const DecisionSnapshot& snapshot, const std::vector<ActionInstance>& actions) {
    std::vector<PathLocation> paths;
    std::set<std::string> seen_refs;
    for (const auto& action : actions) {
        if (action.target_ref.empty() || !seen_refs.insert(action.target_ref).second) {
            continue;
        }
        const auto path = std::find_if(snapshot.recent_paths.begin(), snapshot.recent_paths.end(), [&](const PathLocation& candidate) {
            return candidate.ref == action.target_ref;
        });
        if (path != snapshot.recent_paths.end()) {
            paths.push_back(*path);
        }
    }
    if (paths.size() > 6) {
        paths.resize(6);
    }
    return paths;
}

void retain_actions_with_represented_paths(const DecisionSnapshot& snapshot, std::vector<ActionInstance>& actions) {
    const auto paths = destination_paths_for(snapshot, actions);
    std::set<std::string> represented_refs;
    for (const auto& path : paths) {
        represented_refs.insert(path.ref);
    }
    std::erase_if(actions, [&](const ActionInstance& action) {
        return !action.target_ref.empty() && !represented_refs.contains(action.target_ref);
    });
}

bool remove_lowest_priority_duplicate(std::vector<ActionInstance>& actions, bool destination_variant) {
    for (auto action = actions.rbegin(); action != actions.rend(); ++action) {
        if (action->target_ref.empty() == destination_variant) {
            continue;
        }
        const auto same_kind = std::count_if(actions.begin(), actions.end(), [&](const ActionInstance& candidate) {
            return candidate.kind == action->kind;
        });
        if (same_kind > 1) {
            actions.erase(std::next(action).base());
            return true;
        }
    }
    return false;
}

std::string build_compact_payload(const DecisionRequest& request, const std::string& model,
                                  const std::vector<ActionInstance>& actions, bool autojev_mode = false) {
    const ClipboardItem* current = request.snapshot.clipboard_items.empty() ? nullptr : &request.snapshot.clipboard_items.front();
    const auto destinations = destination_paths_for(request.snapshot, actions);

    std::ostringstream out;
    out << "{\"model\":\"" << json_escape(model) << "\",";
    if (!autojev_mode) {
        out << "\"protocol_version\":" << request.protocol_version << ",";
        out << "\"request_id\":\"" << json_escape(request.request_id) << "\",";
        out << "\"snapshot\":{";
        out << "\"clipboard_hash\":\"" << json_escape(request.snapshot.clipboard_hash) << "\",";
        out << "\"focused_target_hash\":\"" << json_escape(request.snapshot.focused_target_hash) << "\",";
        out << "\"captured_at_ms\":" << request.snapshot.captured_at_ms << "},";
    }
    out << "\"state\":{";
    out << "\"clipboard\":";
    if (current == nullptr) {
        out << "null";
    } else {
        out << "{\"ref\":\"" << json_escape(current->ref) << "\",";
        out << "\"kind\":\"" << content_kind_name(current->kind) << "\",";
        out << "\"preview\":\"" << json_escape(truncate_codepoints(current->preview, 256)) << "\",";
        out << "\"size_bytes\":" << current->size_bytes << ",";
        out << "\"mime_summary\":\"" << json_escape(mime_summary(*current)) << "\",";
        out << "\"captured_at_ms\":" << current->captured_at_ms;
        if (const auto& profile = request.snapshot.profile) {
            out << ",\"profile\":{\"shape\":\"" << json_escape(profile->shape) << "\",";
            out << "\"confidence\":" << std::round(profile->confidence * 100.0) / 100.0 << ",";
            out << "\"lines\":" << profile->lines;
            if (profile->columns > 0) {
                out << ",\"columns\":" << profile->columns << ",\"header\":" << (profile->header ? "true" : "false");
            }
            out << ",\"tags\":[";
            for (std::size_t index = 0; index < profile->tags.size() && index < 8; ++index) {
                out << (index ? "," : "") << "\"" << json_escape(profile->tags[index]) << "\"";
            }
            out << "]}";
        }
        out << "}";
    }
    out << ",\"target\":{\"focused_target_hash\":\"" << json_escape(request.snapshot.focused_target_hash) << "\",";
    out << "\"app\":\"" << json_escape(request.snapshot.focused_app) << "\",";
    out << "\"window_title\":\"" << json_escape(truncate_codepoints(request.snapshot.focused_window_title, 160)) << "\",";
    out << "\"current_directory\":\"" << json_escape(truncate_codepoints(request.snapshot.focused_current_directory, 240)) << "\"},";
    out << "\"destination_paths\":[";
    for (std::size_t index = 0; index < destinations.size(); ++index) {
        const auto& path = destinations[index];
        if (index != 0) {
            out << ",";
        }
        out << "{\"ref\":\"" << json_escape(path.ref) << "\",";
        out << "\"path\":\"" << json_escape(truncate_codepoints(path_to_utf8_string(path.path), 240)) << "\",";
        out << "\"kind\":\"" << path_kind_name(path.kind) << "\"}";
    }
    out << "]";
    bool has_habits = false;
    for (const auto& hint : request.snapshot.usage_hints) {
        const bool offered = std::any_of(actions.begin(), actions.end(), [&](const ActionInstance& action) {
            return action.id == hint.action_id;
        });
        if (!offered) continue;
        out << (has_habits ? "," : ",\"habits\":[");
        has_habits = true;
        out << "{\"action_id\":\"" << json_escape(hint.action_id) << "\",";
        out << "\"share\":" << std::round(hint.share * 100.0) / 100.0 << ",";
        out << "\"count\":" << hint.count << "}";
    }
    if (has_habits) out << "]";
    out << "},";
    out << "\"questions\":{\"best_action\":{\"type\":\"choice\",";
    out << "\"instructions\":\"Choose the most useful current clipboard action.";
    if (has_habits) {
        out << " state.habits lists actions this user often picks for similar content (share of past choices);"
               " prefer them unless the clipboard clearly calls for something else.";
    }
    out << "\",";
    out << "\"criteria\":{";
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto& action = actions[index];
        if (index != 0) {
            out << ",";
        }
        out << "\"" << json_escape(action.id) << "\":\"" << json_escape(truncate_codepoints(action.description, 120)) << "\"";
    }
    out << "}}";
    if (request.snapshot.profile && request.snapshot.profile->alternatives.size() >= 2) {
        // The local shape guess is uncertain: let the model settle it too.
        out << ",\"content_type\":{\"type\":\"choice\",";
        out << "\"instructions\":\"Which content type is the current clipboard?\",\"criteria\":{";
        const auto& alternatives = request.snapshot.profile->alternatives;
        for (std::size_t index = 0; index < alternatives.size(); ++index) {
            out << (index ? "," : "") << "\"" << json_escape(alternatives[index].first) << "\":\""
                << json_escape(alternatives[index].second) << "\"";
        }
        out << "}}";
    }
    out << "}}";
    return out.str();
}

bool is_autojev_endpoint(std::string_view endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.remove_suffix(1);
    return endpoint.ends_with("/v1/autojev");
}

std::string response_error_message(std::string_view body) {
    const auto root = parse_json(body);
    if (!root) return {};
    if (const auto* error = root->get("error")) {
        if (const auto* message = error->get("message"); message && message->string()) return *message->string();
    }
    const auto* detail = root->get("detail");
    if (detail == nullptr || detail->array() == nullptr) return {};
    std::ostringstream out;
    bool first_error = true;
    for (const auto& entry : *detail->array()) {
        if (!first_error) out << "; ";
        first_error = false;
        if (const auto* location = entry.get("loc"); location && location->array()) {
            for (const auto& part : *location->array()) {
                if (const auto* value = part.string()) out << *value << '.';
                else if (const auto value = part.number()) out << static_cast<int>(*value) << '.';
            }
        }
        if (const auto* message = entry.get("msg"); message && message->string()) out << *message->string();
    }
    return out.str();
}

const char* api_key_from_env() {
    if (const char* api_key = std::getenv("DJEV_API_KEY")) {
        return api_key;
    }
    if (const char* api_key = std::getenv("API_KEY")) {
        return api_key;
    }
    if (const char* api_key = std::getenv("TYPESAFE_API_KEY")) {
        return api_key;
    }
    return nullptr;
}

#if defined(PASTEIT_HAS_CURL)
std::size_t append_response(void* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* output = static_cast<std::string*>(userdata);
    const auto total = size * nmemb;
    output->append(static_cast<const char*>(ptr), total);
    return total;
}
#elif !defined(_WIN32)
struct ParsedHttpUrl {
    std::string host;
    std::string port = "80";
    std::string path = "/";
};

std::optional<ParsedHttpUrl> parse_plain_http_url(const std::string& url, std::string& error) {
    constexpr std::string_view http = "http://";
    constexpr std::string_view https = "https://";
    if (url.rfind(https, 0) == 0) {
        error = "libcurl not available for HTTPS Djev endpoint";
        return std::nullopt;
    }
    if (url.rfind(http, 0) != 0) {
        error = "Djev endpoint must be http:// when libcurl is unavailable";
        return std::nullopt;
    }
    auto rest = url.substr(http.size());
    const auto slash = rest.find('/');
    auto host_port = slash == std::string::npos ? rest : rest.substr(0, slash);
    ParsedHttpUrl parsed;
    parsed.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = host_port.rfind(':');
    if (colon != std::string::npos) {
        parsed.host = host_port.substr(0, colon);
        parsed.port = host_port.substr(colon + 1);
    } else {
        parsed.host = host_port;
    }
    if (parsed.host.empty() || parsed.port.empty()) {
        error = "Djev endpoint host or port is empty";
        return std::nullopt;
    }
    return parsed;
}

bool send_all(int fd, const std::string& request, std::string& error) {
    const char* data = request.data();
    std::size_t remaining = request.size();
    while (remaining > 0) {
        const auto sent = ::send(fd, data, remaining, 0);
        if (sent < 0) {
            error = std::string{"send failed: "} + std::strerror(errno);
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::optional<std::string> recv_all(int fd, std::string& error) {
    std::string response;
    char buffer[4096];
    while (true) {
        const auto received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received == 0) {
            break;
        }
        if (received < 0) {
            error = std::string{"receive failed: "} + std::strerror(errno);
            return std::nullopt;
        }
        response.append(buffer, static_cast<std::size_t>(received));
    }
    return response;
}

[[maybe_unused]] DecisionResponse decide_via_plain_http(const std::string& url, const std::string& model,
                                                        std::chrono::milliseconds timeout,
                                                        const DecisionRequest& request) {
    DecisionResponse response;
    response.request_id = request.request_id;

    std::string error;
    const auto parsed = parse_plain_http_url(url, error);
    if (!parsed.has_value()) {
        response.valid = false;
        response.error = error;
        return response;
    }

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const int gai = getaddrinfo(parsed->host.c_str(), parsed->port.c_str(), &hints, &addresses);
    if (gai != 0) {
        response.valid = false;
        response.error = std::string{"Djev DNS/host resolution failed: "} + gai_strerror(gai);
        return response;
    }

    int fd = -1;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            continue;
        }
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);

    if (fd < 0) {
        response.valid = false;
        response.error = std::string{"Djev connection failed: "} + std::strerror(errno);
        return response;
    }

    const auto payload = DjevClient::build_payload(request, model);
    std::ostringstream http;
    http << "POST " << parsed->path << " HTTP/1.1\r\n";
    http << "Host: " << parsed->host << "\r\n";
    http << "Content-Type: application/json\r\n";
    http << "Accept: application/json\r\n";
    http << "Content-Length: " << payload.size() << "\r\n";
    http << "Connection: close\r\n";
    if (const char* api_key = api_key_from_env()) {
        http << "Authorization: Bearer " << api_key << "\r\n";
    }
    http << "\r\n" << payload;

    const auto request_text = http.str();
    if (!send_all(fd, request_text, error)) {
        ::close(fd);
        response.valid = false;
        response.error = "Djev request failed: " + error;
        return response;
    }

    const auto raw_response = recv_all(fd, error);
    ::close(fd);
    if (!raw_response.has_value()) {
        response.valid = false;
        response.error = "Djev request failed: " + error;
        return response;
    }

    const auto status_end = raw_response->find("\r\n");
    const auto header_end = raw_response->find("\r\n\r\n");
    if (status_end == std::string::npos || header_end == std::string::npos) {
        response.valid = false;
        response.error = "Djev response was not valid HTTP";
        return response;
    }
    const auto status_line = raw_response->substr(0, status_end);
    int status = 0;
    if (status_line.size() >= 12) {
        status = std::atoi(status_line.substr(9, 3).c_str());
    }
    if (status < 200 || status >= 300) {
        response.valid = false;
        std::ostringstream status_error;
        status_error << "Djev request failed (HTTP " << status << ")";
        response.error = status_error.str();
        return response;
    }

    return DjevClient::parse_response(raw_response->substr(header_end + 4), request);
}
#endif

}  // namespace

DjevClient::DjevClient(std::string url, std::string model, std::chrono::milliseconds timeout,
                       std::shared_ptr<HttpTransport> transport, std::string api_key)
    : url_(normalize_endpoint(url)), model_(std::move(model)), timeout_(timeout),
      transport_(transport ? std::move(transport) : make_default_http_transport()), api_key_(std::move(api_key)),
      autojev_mode_(is_autojev_endpoint(url) || model_ == "autojev" || model_ == "jev-latest" ||
                    model_ == "jev-preview" || model_ == "jev-1.13.0") {
    if (api_key_.empty()) {
        if (const char* key = api_key_from_env()) api_key_ = key;
    }
}

DecisionResponse DjevClient::decide(const DecisionRequest& request) const {
    auto actions = compact_payload_actions(request, 26);
    retain_actions_with_represented_paths(request.snapshot, actions);
    auto payload = build_compact_payload(request, model_, actions, autojev_mode_);
    while (payload.size() > 10 * 1024 && actions.size() > 1) {
        if (!remove_lowest_priority_duplicate(actions, true) &&
            !remove_lowest_priority_duplicate(actions, false)) actions.pop_back();
        payload = build_compact_payload(request, model_, actions, autojev_mode_);
    }
    HttpRequest http{.url=url_,.body=std::move(payload),.headers={},.timeout=timeout_};
    if(!api_key_.empty()) http.headers["Authorization"]="Bearer "+api_key_;
    const auto wire=transport_->post_json(http);
    if(!wire.transport_error.empty() || wire.status<200 || wire.status>=300){
        DecisionResponse response;response.request_id=request.request_id;response.valid=false;response.http_status=wire.status;
        if(wire.status>=400) response.error = response_error_message(wire.body);
        if(response.error.empty()){
            std::ostringstream out;out<<"Djev request failed";
            if(!wire.transport_error.empty())out<<": "<<wire.transport_error;
            if(wire.status)out<<" (HTTP "<<wire.status<<")";
            response.error=out.str();
        }
        return response;
    }
    auto response=parse_response(wire.body,request);response.http_status=wire.status;return response;
}

std::string DjevClient::build_payload(const DecisionRequest& request, const std::string& model) {
    auto actions = compact_payload_actions(request, 26);
    retain_actions_with_represented_paths(request.snapshot, actions);
    auto payload = build_compact_payload(request, model, actions);
    while (payload.size() > 10 * 1024 && actions.size() > 1) {
        if (!remove_lowest_priority_duplicate(actions, true) &&
            !remove_lowest_priority_duplicate(actions, false)) {
            actions.pop_back();
        }
        payload = build_compact_payload(request, model, actions);
    }
    return payload;
}

DecisionResponse DjevClient::parse_response(std::string_view body, const DecisionRequest& request) {
    DecisionResponse response;
    if (!is_valid_json(body)) {
        response.request_id = request.request_id;
        response.valid = false;
        response.error = "Djev response was not valid JSON";
        return response;
    }

    response.request_id = extract_string(body, "request_id");
    const auto root = parse_json(body);
    const JsonValue* answers = root ? root->get("answers") : nullptr;
    const JsonValue* best = answers ? answers->get("best_action") : nullptr;
    if (best != nullptr && best->object()) {
        // Multi-question responses: read each answer by name, never by the
        // first "choice" key in the body.
        if (const auto* choice = best->get("choice"); choice && choice->string()) response.choice = *choice->string();
        if (const auto* confidence = best->get("confidence")) response.confidence = confidence->number().value_or(0.0);
        if (const auto* probabilities = best->get("probabilities"); probabilities && probabilities->object()) {
            for (const auto& [id, value] : *probabilities->object()) {
                if (const auto number = value.number()) response.probabilities[id] = *number;
            }
        }
        if (const auto* type = answers->get("content_type"); type && type->object()) {
            if (const auto* choice = type->get("choice"); choice && choice->string()) response.content_type = *choice->string();
            if (const auto* confidence = type->get("confidence")) {
                response.content_type_confidence = confidence->number().value_or(0.0);
            }
        }
    } else {
        response.choice = extract_string(body, "choice");
        response.confidence = extract_number(body, "confidence", 0.0);
        response.probabilities = extract_probabilities(body);
    }

    const auto ids = request_action_ids(request);
    if (response.request_id.empty()) {
        response.request_id = request.request_id;
    } else if (response.request_id != request.request_id) {
        response.valid = false;
        response.error = "Djev response request_id did not match the active request";
        return response;
    }
    if (response.choice.empty()) {
        response.valid = false;
        response.error = "Djev response did not include answers.best_action.choice or choice";
        return response;
    }
    if (!ids.contains(response.choice)) {
        response.valid = false;
        response.error = "Djev response choice is not in the request-local catalog";
        return response;
    }
    if (!response.probabilities.contains(response.choice)) {
        response.valid = false;
        response.error = "Djev response probabilities did not include the selected choice";
        return response;
    }
    for (const auto& [id, probability] : response.probabilities) {
        (void)probability;
        if (!ids.contains(id)) {
            response.valid = false;
            response.error = "Djev response probabilities contained an unknown action ID";
            return response;
        }
    }
    response.valid = true;
    return response;
}

std::string DjevClient::endpoint_from_env() {
    std::string url = std::getenv("DJEV_URL") != nullptr ? std::getenv("DJEV_URL") : "http://127.0.0.1:8011/v1/systemone";
    return normalize_endpoint(std::move(url));
}

std::string DjevClient::normalize_endpoint(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    if (url.size() >= std::string{"/v1/systemone"}.size() &&
        url.substr(url.size() - std::string{"/v1/systemone"}.size()) == "/v1/systemone") {
        return url;
    }
    constexpr std::string_view autojev_suffix = "/v1/autojev";
    if (url.size() >= autojev_suffix.size() && url.ends_with(autojev_suffix)) {
        url.resize(url.size() - autojev_suffix.size());
        return url + "/v1/systemone";
    }
    return url + "/v1/systemone";
}

std::string DjevClient::model_from_env() {
    return std::getenv("DJEV_MODEL") != nullptr ? std::getenv("DJEV_MODEL") : "typed-decisions";
}

}  // namespace pasteit
