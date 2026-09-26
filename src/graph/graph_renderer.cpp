#include "graph/graph_renderer.hpp"
#include "graph/chart.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace pasteit {
namespace {

constexpr int width = 800;
constexpr int height = 480;
using Color = std::array<unsigned char, 3>;
constexpr Color white{255, 255, 255};
constexpr Color ink{45, 52, 65};
constexpr Color grid{224, 229, 237};
constexpr std::array<Color, 8> palette{{
    {53, 116, 219}, {232, 106, 83}, {79, 173, 134}, {157, 110, 199},
    {241, 171, 65}, {76, 178, 198}, {222, 113, 157}, {130, 143, 159},
}};

struct Canvas {
    std::vector<unsigned char> pixels = std::vector<unsigned char>(width * height * 3, 255);
    void dot(int x, int y, Color color) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        const auto at = static_cast<std::size_t>((y * width + x) * 3);
        for (int channel = 0; channel < 3; ++channel) pixels[at + channel] = color[channel];
    }
    void rect(int x0, int y0, int x1, int y1, Color color) {
        for (int y = std::max(0, y0); y < std::min(height, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(width, x1); ++x) dot(x, y, color);
    }
    void line(int x0, int y0, int x1, int y1, Color color) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true) {
            dot(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    }
};

// Small embedded font for the ASCII portions of chart labels. UTF-8 source
// labels remain visible in the dialog even when the PNG font cannot draw them.
constexpr std::array<std::string_view, 10> digits{{
    "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
    "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111",
}};
constexpr std::array<std::string_view, 26> letters{{
    "010101111101101", "110101110101110", "011100100100011", "110101101101110", "111100110100111",
    "111100110100100", "011100101101011", "101101111101101", "111010010010111", "001001001101010",
    "101101110101101", "100100100100111", "101111111101101", "101111111111101", "010101101101010",
    "110101110100100", "010101101111011", "110101110101101", "011100010001110", "111010010010010",
    "101101101101111", "101101101101010", "101101111111101", "101101010101101", "101101010010010",
    "111001010100111",
}};

void label(Canvas& canvas, int x, int y, std::string_view text, Color color = ink) {
    for (unsigned char raw : text) {
        char ch = static_cast<char>(raw);
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        std::string_view bits;
        if (ch >= '0' && ch <= '9') bits = digits[ch - '0'];
        else if (ch >= 'A' && ch <= 'Z') bits = letters[ch - 'A'];
        else if (ch == '-' || ch == '_') bits = "000000111000000";
        else if (ch == '.') bits = "000000000000010";
        else if (ch == ':') bits = "000010000010000";
        else if (ch == '+') bits = "000010111010000";
        if (!bits.empty()) {
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < 3; ++col)
                    if (bits[row * 3 + col] == '1') canvas.rect(x + col * 2, y + row * 2, x + col * 2 + 2, y + row * 2 + 2, color);
        }
        x += 8;
        if (x > width - 10) break;
    }
}

void write_png(void* context, void* bytes, int size) {
    auto& output = *static_cast<std::vector<std::byte>*>(context);
    const auto* first = static_cast<const std::byte*>(bytes);
    output.insert(output.end(), first, first + size);
}

}  // namespace

std::vector<std::byte> render_graph_png(const GraphData& data, GraphType type) {
    if (data.points.size() < 2) return {};
    Canvas canvas;
    label(canvas, 42, 24, data.title.empty() ? "GRAPH" : data.title);
    if (type == GraphType::Pie) {
        double total = 0.0;
        for (const auto& point : data.points) {
            if (point.value < 0) return {};
            total += point.value;
        }
        if (total <= 0.0) return {};
        constexpr int cx = 260, cy = 245, radius = 175;
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                const int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > radius * radius) continue;
                double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) + 1.5707963267948966;
                if (angle < 0) angle += 6.283185307179586;
                double cumulative = 0.0;
                for (std::size_t index = 0; index < data.points.size(); ++index) {
                    cumulative += data.points[index].value / total * 6.283185307179586;
                    if (angle <= cumulative || index + 1 == data.points.size()) {
                        canvas.dot(x, y, palette[index % palette.size()]);
                        break;
                    }
                }
            }
        }
        const auto count = std::min<std::size_t>(data.points.size(), 16);
        for (std::size_t index = 0; index < count; ++index) {
            const int y = 65 + static_cast<int>(index) * 24;
            canvas.rect(495, y, 511, y + 16, palette[index % palette.size()]);
            label(canvas, 523, y + 3, data.points[index].label.substr(0, 14));
            label(canvas, 650, y + 3, std::to_string(data.points[index].value).substr(0, 12));
        }
    } else {
        constexpr int left = 76, right = 755, top = 65, bottom = 405;
        double minimum = 0.0, maximum = 0.0;
        for (const auto& point : data.points) {
            minimum = std::min(minimum, point.value);
            maximum = std::max(maximum, point.value);
        }
        if (minimum == maximum) { minimum -= 1.0; maximum += 1.0; }
        const auto y_for = [&](double value) {
            return bottom - static_cast<int>((value - minimum) / (maximum - minimum) * (bottom - top));
        };
        for (int step = 0; step <= 4; ++step) {
            const int y = top + (bottom - top) * step / 4;
            canvas.line(left, y, right, y, grid);
            label(canvas, 6, y - 5, std::to_string(maximum - (maximum - minimum) * step / 4).substr(0, 8));
        }
        canvas.line(left, top, left, bottom, ink);
        canvas.line(left, bottom, right, bottom, ink);
        const auto count = data.points.size();
        const double first_x = data.points.front().x;
        const double last_x = data.points.back().x;
        const bool scaled_x = data.has_x_values && last_x > first_x;
        const auto x_for = [&](std::size_t index) {
            const double position = scaled_x ? (data.points[index].x - first_x) / (last_x - first_x)
                                             : (static_cast<double>(index) + 0.5) / count;
            return left + 15 + static_cast<int>(position * (right - left - 30));
        };
        for (std::size_t index = 0; index < count; ++index) {
            const int x = x_for(index), y = y_for(data.points[index].value);
            if (type == GraphType::Bar) {
                const int half = std::max(2, (right - left) / static_cast<int>(count * 3));
                canvas.rect(x - half, std::min(y, y_for(0.0)), x + half + 1,
                            std::max(y, y_for(0.0)) + 1, palette[index % palette.size()]);
            } else {
                if (index > 0) canvas.line(x_for(index - 1), y_for(data.points[index - 1].value), x, y, palette[0]);
                canvas.rect(x - 3, y - 3, x + 4, y + 4, palette[0]);
            }
            if (count <= 12 || index % std::max<std::size_t>(1, count / 8) == 0) {
                label(canvas, std::max(left, x - 16), bottom + 12, data.points[index].label.substr(0, 9));
            }
        }
    }
    std::vector<std::byte> output;
    if (!stbi_write_png_to_func(write_png, &output, width, height, 3, canvas.pixels.data(), width * 3)) return {};
    return output;
}

std::vector<std::byte> render_chart_png(const ChartSpec& spec) {
    if (!chart_problem(spec).empty()) return {};
    if (spec.kind == ChartKind::Pie) {
        GraphData data;
        data.title = spec.title;
        for (std::size_t index = 0; index < spec.labels.size(); ++index) {
            data.points.push_back({spec.labels[index], spec.series.front().values[index], 0.0});
        }
        if (data.points.size() < 2) data.points.push_back({"", 0.0, 0.0});
        return render_graph_png(data, GraphType::Pie);
    }
    Canvas canvas;
    label(canvas, 42, 24, spec.title.empty() ? "CHART" : spec.title);
    constexpr int left = 76, right = 755, top = 65, bottom = 405;
    double minimum = 0.0, maximum = 0.0;
    bool first = true;
    for (const auto& series : spec.series) {
        for (const double value : series.values) {
            minimum = first ? std::min(0.0, value) : std::min(minimum, value);
            maximum = first ? value : std::max(maximum, value);
            first = false;
        }
    }
    if (minimum == maximum) { minimum -= 1.0; maximum += 1.0; }
    const auto y_for = [&](double value) {
        return bottom - static_cast<int>((value - minimum) / (maximum - minimum) * (bottom - top));
    };
    for (int step = 0; step <= 4; ++step) {
        const int y = top + (bottom - top) * step / 4;
        canvas.line(left, y, right, y, grid);
        label(canvas, 6, y - 5, format_axis_number(maximum - (maximum - minimum) * step / 4).substr(0, 8));
    }
    canvas.line(left, top, left, bottom, ink);
    canvas.line(left, bottom, right, bottom, ink);
    const auto count = spec.labels.size();
    double first_x = 0.0, last_x = 1.0;
    if (spec.numeric_x && !spec.x.empty()) {
        const auto [low, high] = std::minmax_element(spec.x.begin(), spec.x.end());
        first_x = *low;
        last_x = *high > *low ? *high : *low + 1.0;
    }
    const auto x_for = [&](std::size_t index) {
        const double position = spec.numeric_x && spec.kind != ChartKind::Bar && spec.kind != ChartKind::Histogram
            ? (spec.x[index] - first_x) / (last_x - first_x)
            : (static_cast<double>(index) + 0.5) / static_cast<double>(count);
        return left + 15 + static_cast<int>(position * (right - left - 30));
    };
    const int groups = static_cast<int>(spec.series.size());
    const int slot = std::max(3, (right - left - 30) / static_cast<int>(std::max<std::size_t>(count, 1)));
    for (std::size_t series_index = 0; series_index < spec.series.size(); ++series_index) {
        const auto& values = spec.series[series_index].values;
        const auto color = palette[series_index % palette.size()];
        for (std::size_t index = 0; index < values.size() && index < count; ++index) {
            const int x = x_for(index), y = y_for(values[index]);
            if (spec.kind == ChartKind::Bar || spec.kind == ChartKind::Histogram) {
                const int bar = spec.kind == ChartKind::Histogram ? slot - 2 : std::max(2, slot * 2 / 3 / groups);
                const int x0 = spec.kind == ChartKind::Histogram ? x - bar / 2
                             : x - slot / 3 + static_cast<int>(series_index) * bar;
                canvas.rect(x0, std::min(y, y_for(0.0)), x0 + bar, std::max(y, y_for(0.0)) + 1, color);
            } else if (spec.kind == ChartKind::Scatter) {
                canvas.rect(x - 3, y - 3, x + 4, y + 4, color);
            } else {
                if (index > 0) canvas.line(x_for(index - 1), y_for(values[index - 1]), x, y, color);
                canvas.rect(x - 2, y - 2, x + 3, y + 3, color);
            }
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (count <= 12 || index % std::max<std::size_t>(1, count / 8) == 0) {
            label(canvas, std::max(left, x_for(index) - 16), bottom + 12, spec.labels[index].substr(0, 9));
        }
    }
    if (spec.series.size() > 1) {
        for (std::size_t index = 0; index < spec.series.size() && index < 6; ++index) {
            const int x = 420 + static_cast<int>(index % 3) * 115, y = 22 + static_cast<int>(index / 3) * 16;
            canvas.rect(x, y, x + 10, y + 10, palette[index % palette.size()]);
            label(canvas, x + 14, y, spec.series[index].name.substr(0, 11));
        }
    }
    std::vector<std::byte> output;
    if (!stbi_write_png_to_func(write_png, &output, width, height, 3, canvas.pixels.data(), width * 3)) return {};
    return output;
}

}  // namespace pasteit
