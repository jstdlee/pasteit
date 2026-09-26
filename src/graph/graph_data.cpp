#include "graph/graph_data.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <sstream>

namespace pastit {
namespace {

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

std::optional<double> number(std::string_view text);

std::vector<std::string> fields(std::string_view line) {
    const auto comma = line.find(',');
    if (comma != std::string_view::npos && line.find(',', comma + 1) == std::string_view::npos) {
        const auto left = trim(line.substr(0, comma));
        const auto right = trim(line.substr(comma + 1));
        if (!left.empty() && number(right)) return {left, right};
    }
    std::vector<std::string> result;
    std::string token;
    for (const char ch : line) {
        if (ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
            if (!token.empty()) { result.push_back(std::move(token)); token.clear(); }
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

std::optional<double> number(std::string_view text) {
    double value = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<double> date_x(std::string_view value) {
    if (value.size() < 10 || value[4] != '-' || value[7] != '-') return std::nullopt;
    if (value.size() > 10 && value[10] != 'T') return std::nullopt;
    int year = 0, month = 0, day = 0;
    const auto read = [&](std::size_t first, std::size_t last, int& out) {
        const auto parsed = std::from_chars(value.data() + first, value.data() + last, out);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + last;
    };
    if (!read(0, 4, year) || !read(5, 7, month) || !read(8, 10, day)) return std::nullopt;
    const auto date = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
                      std::chrono::day{static_cast<unsigned>(day)};
    if (!date.ok()) return std::nullopt;
    return static_cast<double>(std::chrono::sys_days{date}.time_since_epoch().count());
}

}  // namespace

std::optional<GraphData> parse_graph_data(std::string_view text, bool has_header, bool convert_dates) {
    if (text.empty() || text.size() > 8192) return std::nullopt;
    std::vector<std::vector<std::string>> rows;
    std::istringstream input{std::string{text}};
    std::string line;
    std::string first_line;
    while (std::getline(input, line)) {
        auto row = fields(line);
        if (!row.empty()) {
            if (rows.empty()) first_line = line;
            rows.push_back(std::move(row));
        }
        if (rows.size() > 129) return std::nullopt;
    }
    if (rows.empty()) return std::nullopt;
    GraphData graph;
    if (has_header) {
        if (rows.size() < 3) return std::nullopt;
        const auto comma = first_line.find(',');
        graph.title = comma == std::string::npos ? trim(rows.front().back()) : trim(first_line.substr(comma + 1));
        rows.erase(rows.begin());
    }
    const bool pairs = rows.size() >= 2 &&
                       std::all_of(rows.begin(), rows.end(), [](const auto& row) { return row.size() == 2; });
    if (pairs) {
        for (const auto& row : rows) {
            const auto y = number(row[1]);
            if (!y) return std::nullopt;
            GraphPoint point{.label = row[0], .value = *y, .x = static_cast<double>(graph.points.size())};
            if (convert_dates) {
                const auto date = date_x(row[0]);
                if (!date) return std::nullopt;
                point.x = *date;
                graph.has_dates = true;
                graph.has_x_values = true;
            } else if (const auto x = number(row[0])) {
                point.x = *x;
                graph.has_x_values = true;
            }
            graph.points.push_back(std::move(point));
        }
        if (graph.has_x_values) {
            std::stable_sort(graph.points.begin(), graph.points.end(),
                             [](const GraphPoint& left, const GraphPoint& right) { return left.x < right.x; });
        }
    } else {
        for (const auto& row : rows) {
            for (const auto& token : row) {
                const auto y = number(token);
                if (!y) return std::nullopt;
                graph.points.push_back({.label = std::to_string(graph.points.size() + 1),
                                        .value = *y, .x = static_cast<double>(graph.points.size())});
            }
        }
    }
    if (graph.points.size() < 2 || graph.points.size() > 128) return std::nullopt;
    return graph;
}

}  // namespace pastit
