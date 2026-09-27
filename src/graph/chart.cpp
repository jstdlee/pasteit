#include "graph/chart.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace pasteit {

std::string format_axis_number(double value) {
    std::ostringstream out;
    const double magnitude = std::fabs(value);
    if (magnitude >= 1e9) out << std::setprecision(3) << value / 1e9 << "B";
    else if (magnitude >= 1e6) out << std::setprecision(3) << value / 1e6 << "M";
    else if (magnitude >= 1e4) out << std::setprecision(3) << value / 1e3 << "k";
    else if (magnitude == std::floor(magnitude)) out << static_cast<long long>(value);
    else out << std::setprecision(3) << value;
    return out.str();
}

ChartSpec chart_from_graph(const GraphData& data, ChartKind kind, Aggregate aggregate) {
    if (aggregate != Aggregate::None && kind != ChartKind::Histogram) {
        // Reduce points sharing a label (e.g. "mon 3", "mon 5" -> sum 8) via the table path.
        TableData table;
        table.headers = {"label", data.title.empty() ? "value" : data.title};
        for (const auto& point : data.points) table.rows.push_back({point.label, format_cell_number(point.value)});
        compute_column_stats(table);
        std::vector<std::size_t> rows(table.rows.size());
        std::iota(rows.begin(), rows.end(), std::size_t{0});
        auto spec = chart_from_table(table, rows, 0, {1}, kind, aggregate);
        if (spec) return *spec;
        ChartSpec empty;
        empty.kind = kind;
        return empty;
    }
    ChartSpec spec;
    spec.kind = kind;
    spec.title = data.title;
    spec.numeric_x = data.has_x_values;
    ChartSeries series{.name = data.title.empty() ? "value" : data.title, .values = {}};
    for (const auto& point : data.points) {
        spec.labels.push_back(point.label);
        spec.x.push_back(point.x);
        series.values.push_back(point.value);
    }
    if (kind == ChartKind::Histogram) return make_histogram(series.name, series.values);
    spec.series.push_back(std::move(series));
    return spec;
}

std::optional<ChartSpec> chart_from_table(const TableData& table, const std::vector<std::size_t>& rows, int x_column,
                                          const std::vector<std::size_t>& y_columns, ChartKind kind, Aggregate aggregate) {
    if (rows.empty() || y_columns.empty()) return std::nullopt;
    for (const auto column : y_columns) {
        if (column >= table.headers.size()) return std::nullopt;
    }
    if (aggregate != Aggregate::None && kind != ChartKind::Histogram && x_column < 0) {
        // No grouping column: reduce every row, one category per Y column
        // (e.g. total sales vs total cost).
        const auto totals = aggregate_table(table, rows, -1, y_columns, aggregate);
        if (totals.rows.empty()) return std::nullopt;
        ChartSpec spec;
        spec.kind = kind == ChartKind::Line || kind == ChartKind::Scatter ? ChartKind::Bar : kind;
        spec.title = aggregate_name(aggregate);
        spec.series.push_back({aggregate_name(aggregate), {}});
        for (std::size_t index = 0; index < y_columns.size(); ++index) {
            const auto value = parse_cell_number(totals.rows[0][index + 1]);
            if (!value) continue;
            spec.labels.push_back(table.headers[y_columns[index]]);
            spec.x.push_back(static_cast<double>(spec.x.size()));
            spec.series[0].values.push_back(*value);
        }
        if (spec.labels.empty()) return std::nullopt;
        return spec;
    }
    if (aggregate != Aggregate::None && kind != ChartKind::Histogram) {
        const auto grouped = aggregate_table(table, rows, x_column, y_columns, aggregate);
        std::vector<std::size_t> grouped_rows(grouped.rows.size());
        std::iota(grouped_rows.begin(), grouped_rows.end(), std::size_t{0});
        std::vector<std::size_t> value_columns(y_columns.size());
        std::iota(value_columns.begin(), value_columns.end(), std::size_t{1});
        return chart_from_table(grouped, grouped_rows, 0, value_columns, kind);
    }
    if (kind == ChartKind::Histogram) {
        std::vector<double> values;
        for (const auto row : rows) {
            if (const auto number = parse_cell_number(table.rows[row][y_columns.front()])) values.push_back(*number);
        }
        if (values.size() < 2) return std::nullopt;
        return make_histogram(table.headers[y_columns.front()], values);
    }
    ChartSpec spec;
    spec.kind = kind;
    const bool has_x = x_column >= 0 && static_cast<std::size_t>(x_column) < table.headers.size();
    spec.numeric_x = has_x && table.stats.size() > static_cast<std::size_t>(x_column) &&
                     table.stats[static_cast<std::size_t>(x_column)].type == ColumnType::Number;
    if (!has_x && kind == ChartKind::Scatter) spec.numeric_x = true;
    for (const auto column : y_columns) spec.series.push_back({table.headers[column], {}});
    for (const auto row : rows) {
        // Skip rows where any plotted value is missing so series stay aligned.
        std::vector<double> values;
        for (const auto column : y_columns) {
            const auto number = parse_cell_number(table.rows[row][column]);
            if (!number) break;
            values.push_back(*number);
        }
        if (values.size() != y_columns.size()) continue;
        const std::string label = has_x ? table.rows[row][static_cast<std::size_t>(x_column)] : std::to_string(row + 1);
        double x = static_cast<double>(spec.labels.size());
        if (spec.numeric_x && has_x) {
            const auto number = parse_cell_number(label);
            if (!number) continue;
            x = *number;
        }
        spec.labels.push_back(label);
        spec.x.push_back(x);
        for (std::size_t index = 0; index < values.size(); ++index) spec.series[index].values.push_back(values[index]);
    }
    // One bar or one pie slice is still a chart; lines and scatters need two.
    if (spec.labels.empty() || (spec.labels.size() < 2 && kind != ChartKind::Bar && kind != ChartKind::Pie)) return std::nullopt;
    spec.title = has_x ? table.headers[static_cast<std::size_t>(x_column)] : "";
    return spec;
}

ChartSpec make_histogram(const std::string& name, const std::vector<double>& values, std::size_t bins) {
    ChartSpec spec;
    spec.kind = ChartKind::Histogram;
    spec.title = name;
    if (values.empty()) return spec;
    const auto [low_it, high_it] = std::minmax_element(values.begin(), values.end());
    double low = *low_it;
    double high = *high_it;
    if (bins == 0) bins = static_cast<std::size_t>(std::ceil(std::log2(static_cast<double>(values.size())) + 1.0));
    bins = std::clamp<std::size_t>(bins, 1, 40);
    if (low == high) {
        low -= 0.5;
        high += 0.5;
        bins = 1;
    }
    const double width = (high - low) / static_cast<double>(bins);
    ChartSeries counts{.name = "count", .values = std::vector<double>(bins, 0.0)};
    for (const double value : values) {
        auto bin = static_cast<std::size_t>((value - low) / width);
        if (bin >= bins) bin = bins - 1;
        counts.values[bin] += 1.0;
    }
    for (std::size_t index = 0; index < bins; ++index) {
        const double from = low + width * static_cast<double>(index);
        spec.labels.push_back(format_axis_number(from) + "-" + format_axis_number(from + width));
        spec.x.push_back(from);
    }
    spec.series.push_back(std::move(counts));
    return spec;
}

std::string chart_problem(const ChartSpec& spec) {
    const std::size_t minimum = spec.kind == ChartKind::Line || spec.kind == ChartKind::Scatter ? 2U : 1U;
    if (spec.series.empty() || spec.labels.size() < minimum) {
        return "Not enough numeric values to plot.";
    }
    if (spec.kind == ChartKind::Pie) {
        double total = 0.0;
        for (const double value : spec.series.front().values) {
            if (value < 0.0) return "Pie charts need values that are not negative.";
            total += value;
        }
        if (total <= 0.0) return "Pie charts need a positive total.";
    }
    return {};
}

}  // namespace pasteit
