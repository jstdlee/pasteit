#include "graph/graph_data.hpp"
#include "graph/graph_renderer.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace pasteit;
    const auto series = parse_graph_data("1, 2 3,4", false, false);
    assert(series && series->points.size() == 4);
    assert(series->points[2].value == 3);
    assert(!parse_graph_data("Please handle this note", false, false));
    assert(!parse_graph_data("1,2,hello", false, false));

    const std::string table = "Date,Sales\n2026-01-01,10\n2026-01-03,20\n";
    const auto dated = parse_graph_data(table, true, true);
    assert(dated && dated->has_dates && dated->title == "Sales");
    assert(dated->points[1].x - dated->points[0].x == 2);
    const auto xy = parse_graph_data("10,4\n2,8\n", false, false);
    assert(xy && xy->has_x_values && xy->points.front().x == 2);
    const auto labeled = parse_graph_data("Date,Total Sales\nEast Coast,10\nWest Coast,20", true, false);
    assert(labeled && labeled->title == "Total Sales" && labeled->points[0].label == "East Coast");
    assert(!parse_graph_data(table, false, true));
    const auto png = render_graph_png(*dated, GraphType::Line);
    assert(png.size() > 8);
    assert(static_cast<unsigned char>(png[0]) == 137);
    assert(!render_graph_png(*dated, GraphType::Pie).empty());
    const auto negative = parse_graph_data("-1,2,3", false, false);
    assert(negative && render_graph_png(*negative, GraphType::Pie).empty());
}
