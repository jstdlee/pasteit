#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

struct GraphPoint {
    std::string label;
    double value = 0.0;
    double x = 0.0;
};

struct GraphData {
    std::string title;
    std::vector<GraphPoint> points;
    bool has_dates = false;
    bool has_x_values = false;
};

// Accepts a numeric sequence, or rows of label/value pairs separated by commas
// or whitespace. The two switches let the preview reinterpret the same source.
std::optional<GraphData> parse_graph_data(std::string_view text, bool has_header, bool convert_dates);

}  // namespace pasteit
