#pragma once

#include "graph/graph_data.hpp"

#include <cstddef>
#include <vector>

namespace pasteit {

enum class GraphType { Line, Bar, Pie };

// Renders exactly the image shown in the preview; callers can publish or save
// the returned PNG without a browser or external process.
std::vector<std::byte> render_graph_png(const GraphData& data, GraphType type);

}  // namespace pasteit
