#pragma once

#include "graph/graph_data.hpp"
#include "table/table_data.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pasteit {

enum class ChartKind { Line, Bar, Pie, Scatter, Histogram };

struct ChartSeries {
    std::string name;
    std::vector<double> values;
};

// What to draw, independent of how (live ImGui view or PNG export).
struct ChartSpec {
    ChartKind kind = ChartKind::Line;
    std::string title;
    std::vector<std::string> labels;  // one per point (category or formatted x)
    std::vector<double> x;            // numeric x per point when numeric_x
    bool numeric_x = false;
    std::vector<ChartSeries> series;
};

ChartSpec chart_from_graph(const GraphData& data, ChartKind kind, Aggregate aggregate = Aggregate::None);
// x_column < 0 uses the row number. Rows are the table's visible rows. With an
// aggregate, rows sharing an x value are reduced to one point per series
// (x_column < 0 then reduces every row into a single point).
std::optional<ChartSpec> chart_from_table(const TableData& table, const std::vector<std::size_t>& rows, int x_column,
                                          const std::vector<std::size_t>& y_columns, ChartKind kind,
                                          Aggregate aggregate = Aggregate::None);
// Equal-width bins; bins == 0 picks Sturges' rule.
ChartSpec make_histogram(const std::string& name, const std::vector<double>& values, std::size_t bins = 0);

// Empty when the chart can be drawn, otherwise the reason it cannot.
std::string chart_problem(const ChartSpec& spec);
std::vector<std::byte> render_chart_png(const ChartSpec& spec);
std::string format_axis_number(double value);

}  // namespace pasteit
