#include "ui/data_views.hpp"

#include "graph/graph_data.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/icons.hpp"
#include "ui/imgui_widgets.hpp"
#include "ui/theme.hpp"

#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pasteit {

void open_table_view(TableViewState& state, TableData table, std::string title) {
    state = {};
    state.open = true;
    state.focus_pending = true;
    state.title = std::move(title);
    state.visible.assign(table.headers.size(), true);
    state.table = std::move(table);
}

void open_markdown_view(MarkdownViewState& state, std::string source) {
    state = {};
    state.open = true;
    state.focus_pending = true;
    state.blocks = parse_markdown(source);
    state.source = std::move(source);
}

void open_chart_from_series(ChartViewState& state, std::string source, std::string save_path) {
    const int kind = state.kind;  // keep the last chart type
    state = {};
    state.kind = kind;
    state.open = true;
    state.focus_pending = true;
    state.series_source = std::move(source);
    state.header = !parse_graph_data(state.series_source, false, false).has_value();
    state.save_path = std::move(save_path);
}

void open_chart_from_table(ChartViewState& state, const TableData& table, std::vector<std::size_t> rows,
                           std::string save_path) {
    const int kind = state.kind;  // keep the last chart type
    state = {};
    state.kind = kind;
    state.open = true;
    state.focus_pending = true;
    state.table = table;
    state.rows = std::move(rows);
    state.y_columns.assign(table.headers.size(), false);
    // Defaults: first text/date column as X, numeric columns as series.
    for (std::size_t column = 0; column < table.stats.size(); ++column) {
        if (table.stats[column].type == ColumnType::Number) {
            state.y_columns[column] = true;
        } else if (state.x_column < 0) {
            state.x_column = static_cast<int>(column);
        }
    }
    if (std::none_of(state.y_columns.begin(), state.y_columns.end(), [](bool value) { return value; }) &&
        !state.y_columns.empty()) {
        state.y_columns.back() = true;
    }
    state.kind = static_cast<int>(ChartKind::Bar);
    state.save_path = std::move(save_path);
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)

namespace {

constexpr double kPi = 3.14159265358979323846;

bool is_cjk_lead(unsigned char byte) {
    return byte >= 0xE3 && byte <= 0xE9;
}

// Minimal inline flow layout: words wrap at the available width, CJK breaks
// between characters, and each span keeps its font and color.
class FlowWriter {
public:
    explicit FlowWriter(float indent = 0.0F) {
        origin_ = ImGui::GetCursorScreenPos();
        origin_.x += indent;
        max_x_ = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        x_ = origin_.x;
        y_ = origin_.y;
    }

    void write(std::string_view text, ImFont* font, float size, ImU32 color, unsigned style, const std::string& url,
               const std::function<void(std::string_view)>& open_uri) {
        auto* draw = ImGui::GetWindowDrawList();
        line_height_ = std::max(line_height_, size * 1.35F);
        std::size_t index = 0;
        while (index < text.size()) {
            std::size_t end = index;
            const auto byte = static_cast<unsigned char>(text[index]);
            if (text[index] == ' ') {
                while (end < text.size() && text[end] == ' ') ++end;
            } else if (is_cjk_lead(byte)) {
                end = std::min(text.size(), index + 3);
            } else {
                while (end < text.size() && text[end] != ' ' && !is_cjk_lead(static_cast<unsigned char>(text[end]))) ++end;
            }
            const auto word = text.substr(index, end - index);
            const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0F, word.data(), word.data() + word.size()).x;
            if (x_ + width > max_x_ && x_ > origin_.x) {
                new_line();
                if (word.front() == ' ') {
                    index = end;
                    continue;
                }
            }
            const ImVec2 at(x_, y_ + (line_height_ - size) * 0.5F);
            if (style & MdCode) {
                draw->AddRectFilled(ImVec2(at.x - 2.0F, at.y - 1.0F), ImVec2(at.x + width + 2.0F, at.y + size + 2.0F),
                                    ImGui::GetColorU32(palette().surface_hover), 3.0F);
            }
            draw->AddText(font, size, at, color, word.data(), word.data() + word.size());
            if (style & MdStrike) {
                draw->AddLine(ImVec2(at.x, at.y + size * 0.55F), ImVec2(at.x + width, at.y + size * 0.55F), color);
            }
            if (style & MdLink) {
                draw->AddLine(ImVec2(at.x, at.y + size + 1.0F), ImVec2(at.x + width, at.y + size + 1.0F), color);
                if (ImGui::IsMouseHoveringRect(at, ImVec2(at.x + width, at.y + size + 2.0F))) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", url.c_str());
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && open_uri && !url.empty()) open_uri(url);
                }
            }
            x_ += width;
            index = end;
        }
    }

    void finish() {
        const float height = y_ + std::max(line_height_, ImGui::GetTextLineHeight()) - origin_.y;
        ImGui::Dummy(ImVec2(max_x_ - origin_.x, height));
    }

private:
    void new_line() {
        y_ += line_height_;
        x_ = origin_.x;
    }

    ImVec2 origin_;
    float max_x_ = 0.0F;
    float x_ = 0.0F;
    float y_ = 0.0F;
    float line_height_ = 0.0F;
};

void write_spans(FlowWriter& writer, const std::vector<MdSpan>& spans, float size, ImVec4 base,
                 const std::function<void(std::string_view)>& open_uri, bool force_bold = false) {
    const auto& fonts = ui_fonts();
    const auto& p = palette();
    for (const auto& span : spans) {
        ImFont* font = (span.style & MdBold) || force_bold ? fonts.bold : fonts.regular;
        ImVec4 color = base;
        if (span.style & MdLink) color = p.accent;
        else if (span.style & MdCode) color = p.warning;
        else if (span.style & MdItalic) color = ImVec4(base.x * 0.85F + 0.1F, base.y * 0.85F + 0.1F, base.z * 0.85F + 0.15F, base.w);
        writer.write(span.text, font, size, ImGui::GetColorU32(color), span.style, span.url, open_uri);
    }
}

ImU32 series_color(std::size_t index, float alpha = 1.0F) {
    static constexpr ImVec4 colors[] = {
        {0.36F, 0.55F, 0.94F, 1.0F}, {0.91F, 0.42F, 0.33F, 1.0F}, {0.25F, 0.73F, 0.52F, 1.0F}, {0.62F, 0.48F, 0.92F, 1.0F},
        {0.95F, 0.67F, 0.25F, 1.0F}, {0.23F, 0.65F, 0.88F, 1.0F}, {0.87F, 0.38F, 0.60F, 1.0F}, {0.55F, 0.58F, 0.64F, 1.0F},
    };
    auto color = colors[index % std::size(colors)];
    color.w = alpha;
    return ImGui::GetColorU32(color);
}

std::string format_value(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

}  // namespace

void draw_markdown_blocks(const std::vector<MdBlock>& blocks, const std::function<void(std::string_view)>& open_uri) {
    const auto& fonts = ui_fonts();
    const auto& p = palette();
    const float body = fonts.body;
    for (const auto& block : blocks) {
        switch (block.type) {
            case MdBlockType::Heading: {
                static constexpr float scale[] = {1.6F, 1.35F, 1.18F, 1.05F, 1.0F, 0.95F};
                ImGui::Dummy(ImVec2(0.0F, block.level <= 2 ? 6.0F : 2.0F));
                FlowWriter writer;
                write_spans(writer, block.spans, body * scale[std::clamp(block.level, 1, 6) - 1], p.text, open_uri, true);
                writer.finish();
                if (block.level <= 2) ImGui::Separator();
                break;
            }
            case MdBlockType::Paragraph: {
                FlowWriter writer;
                write_spans(writer, block.spans, body, p.text, open_uri);
                writer.finish();
                ImGui::Dummy(ImVec2(0.0F, 4.0F));
                break;
            }
            case MdBlockType::Quote: {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                FlowWriter writer(14.0F);
                write_spans(writer, block.spans, body, p.text_muted, open_uri);
                writer.finish();
                ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + 3.0F, ImGui::GetCursorScreenPos().y - 4.0F),
                                                          ImGui::GetColorU32(p.accent), 2.0F);
                break;
            }
            case MdBlockType::ListItem: {
                const float indent = 18.0F * static_cast<float>(block.level);
                const ImVec2 start = ImGui::GetCursorScreenPos();
                auto* draw = ImGui::GetWindowDrawList();
                if (block.task) {
                    // Drawn box: the UI fonts have no ballot-box glyphs.
                    const ImVec2 box(start.x + indent, start.y + body * 0.3F);
                    const float side = body * 0.8F;
                    if (block.checked) {
                        draw->AddRectFilled(box, ImVec2(box.x + side, box.y + side), ImGui::GetColorU32(p.accent), 3.0F);
                        draw->AddLine(ImVec2(box.x + side * 0.22F, box.y + side * 0.52F), ImVec2(box.x + side * 0.42F, box.y + side * 0.72F),
                                      ImGui::GetColorU32(p.background), 2.0F);
                        draw->AddLine(ImVec2(box.x + side * 0.42F, box.y + side * 0.72F), ImVec2(box.x + side * 0.8F, box.y + side * 0.28F),
                                      ImGui::GetColorU32(p.background), 2.0F);
                    } else {
                        draw->AddRect(box, ImVec2(box.x + side, box.y + side), ImGui::GetColorU32(p.text_muted), 3.0F, 0, 1.5F);
                    }
                } else {
                    const std::string marker = block.ordered ? std::to_string(block.number) + "." : "\xE2\x80\xA2";
                    draw->AddText(ImVec2(start.x + indent, start.y + body * 0.15F), ImGui::GetColorU32(p.text_muted), marker.c_str());
                }
                FlowWriter writer(indent + 20.0F);
                write_spans(writer, block.spans, body, block.task && block.checked ? p.text_muted : p.text, open_uri);
                writer.finish();
                break;
            }
            case MdBlockType::Code: {
                const auto lines = static_cast<float>(std::count(block.code.begin(), block.code.end(), '\n') + 1 +
                                                      (block.language.empty() ? 0 : 1));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
                ImGui::PushID(&block);
                ImGui::BeginChild("code", ImVec2(0.0F, std::min(lines, 24.0F) * ImGui::GetTextLineHeightWithSpacing() + 20.0F),
                                  ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
                if (!block.language.empty()) {
                    ImGui::TextColored(p.text_muted, "%s", block.language.c_str());
                }
                ImGui::TextUnformatted(block.code.c_str());
                ImGui::EndChild();
                ImGui::PopID();
                ImGui::PopStyleColor();
                break;
            }
            case MdBlockType::Rule:
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                break;
            case MdBlockType::Table: {
                if (block.rows.empty()) break;
                const int columns = static_cast<int>(std::max<std::size_t>(1, block.align.size()));
                ImGui::PushID(&block);
                if (ImGui::BeginTable("md-table", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    for (std::size_t row = 0; row < block.rows.size(); ++row) {
                        ImGui::TableNextRow(row == 0 ? ImGuiTableRowFlags_Headers : 0);
                        for (int column = 0; column < columns; ++column) {
                            ImGui::TableNextColumn();
                            if (static_cast<std::size_t>(column) >= block.rows[row].size()) continue;
                            FlowWriter writer;
                            write_spans(writer, block.rows[row][static_cast<std::size_t>(column)], body, p.text, open_uri, row == 0);
                            writer.finish();
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::PopID();
                ImGui::Spacing();
                break;
            }
        }
    }
}

void draw_chart(const ChartSpec& spec, float width, float height) {
    const auto& p = palette();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("chart-canvas", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), ImGui::GetColorU32(p.surface), 8.0F);
    if (!chart_problem(spec).empty()) return;

    if (spec.kind == ChartKind::Pie) {
        const auto& values = spec.series.front().values;
        double total = 0.0;
        for (const double value : values) total += value;
        const ImVec2 center(origin.x + height * 0.5F, origin.y + height * 0.5F);
        const float radius = height * 0.4F;
        double angle = -kPi / 2.0;
        for (std::size_t index = 0; index < values.size(); ++index) {
            const double sweep = values[index] / total * 2.0 * kPi;
            draw->PathLineTo(center);
            draw->PathArcTo(center, radius, static_cast<float>(angle), static_cast<float>(angle + sweep), 48);
            draw->PathFillConvex(series_color(index));
            const double mid = angle + sweep / 2.0;
            const ImVec2 delta(mouse.x - center.x, mouse.y - center.y);
            double mouse_angle = std::atan2(delta.y, delta.x);
            if (mouse_angle < -kPi / 2.0) mouse_angle += 2.0 * kPi;
            if (hovered && delta.x * delta.x + delta.y * delta.y <= radius * radius && mouse_angle >= angle &&
                mouse_angle < angle + sweep) {
                ImGui::SetTooltip("%s: %s (%.1f%%)", spec.labels[index].c_str(), format_value(values[index]).c_str(),
                                  values[index] / total * 100.0);
            }
            (void)mid;
            angle += sweep;
        }
        const float legend_x = origin.x + height + 10.0F;
        for (std::size_t index = 0; index < values.size() && index < 16; ++index) {
            const float y = origin.y + 16.0F + static_cast<float>(index) * 20.0F;
            draw->AddRectFilled(ImVec2(legend_x, y + 3.0F), ImVec2(legend_x + 10.0F, y + 13.0F), series_color(index), 2.0F);
            const auto label = spec.labels[index] + "  " + format_value(values[index]);
            draw->AddText(ImVec2(legend_x + 16.0F, y), ImGui::GetColorU32(p.text), label.c_str());
        }
        return;
    }

    // Cartesian charts.
    const float left = origin.x + 58.0F, right = origin.x + width - 16.0F;
    const float top = origin.y + (spec.series.size() > 1 ? 34.0F : 16.0F), bottom = origin.y + height - 34.0F;
    double minimum = 0.0, maximum = 0.0;
    bool first = true;
    for (const auto& series : spec.series) {
        for (const double value : series.values) {
            minimum = first ? std::min(0.0, value) : std::min(minimum, value);
            maximum = first ? std::max(0.0, value) : std::max(maximum, value);
            first = false;
        }
    }
    if (minimum == maximum) maximum = minimum + 1.0;
    const auto y_for = [&](double value) {
        return bottom - static_cast<float>((value - minimum) / (maximum - minimum)) * (bottom - top);
    };
    const std::size_t count = spec.labels.size();
    double first_x = 0.0, last_x = 1.0;
    const bool numeric_positions = spec.numeric_x && spec.kind != ChartKind::Bar && spec.kind != ChartKind::Histogram;
    if (numeric_positions) {
        const auto [low, high] = std::minmax_element(spec.x.begin(), spec.x.end());
        first_x = *low;
        last_x = *high > *low ? *high : *low + 1.0;
    }
    const float plot_width = right - left;
    const auto x_for = [&](std::size_t index) {
        const double position = numeric_positions ? (spec.x[index] - first_x) / (last_x - first_x) * 0.94 + 0.03
                                                  : (static_cast<double>(index) + 0.5) / static_cast<double>(count);
        return left + static_cast<float>(position) * plot_width;
    };
    for (int step = 0; step <= 4; ++step) {
        const double value = minimum + (maximum - minimum) * step / 4.0;
        const float y = y_for(value);
        draw->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(p.border));
        const auto text = format_axis_number(value);
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        draw->AddText(ImVec2(left - size.x - 8.0F, y - size.y * 0.5F), ImGui::GetColorU32(p.text_muted), text.c_str());
    }
    const float slot = plot_width / static_cast<float>(std::max<std::size_t>(count, 1));
    const std::size_t groups = spec.series.size();
    int hover_index = -1;
    if (hovered && mouse.x >= left && mouse.x <= right) {
        float best = FLT_MAX;
        for (std::size_t index = 0; index < count; ++index) {
            const float distance = std::fabs(x_for(index) - mouse.x);
            if (distance < best) {
                best = distance;
                hover_index = static_cast<int>(index);
            }
        }
    }
    for (std::size_t series_index = 0; series_index < groups; ++series_index) {
        const auto& values = spec.series[series_index].values;
        const ImU32 color = series_color(series_index);
        for (std::size_t index = 0; index < values.size() && index < count; ++index) {
            const float x = x_for(index), y = y_for(values[index]);
            if (spec.kind == ChartKind::Bar || spec.kind == ChartKind::Histogram) {
                const float group_width = spec.kind == ChartKind::Histogram ? slot - 2.0F : slot * 0.7F;
                const float bar = group_width / static_cast<float>(spec.kind == ChartKind::Histogram ? 1 : groups);
                const float x0 = x - group_width / 2.0F + bar * static_cast<float>(spec.kind == ChartKind::Histogram ? 0 : series_index);
                const bool active = hover_index == static_cast<int>(index);
                draw->AddRectFilled(ImVec2(x0, std::min(y, y_for(0.0))), ImVec2(x0 + bar - 1.0F, std::max(y, y_for(0.0))),
                                    series_color(series_index, active ? 1.0F : 0.85F), 3.0F, ImDrawFlags_RoundCornersTop);
            } else if (spec.kind == ChartKind::Scatter) {
                draw->AddCircleFilled(ImVec2(x, y), 4.0F, series_color(series_index, 0.8F));
            } else {
                if (index > 0) draw->AddLine(ImVec2(x_for(index - 1), y_for(values[index - 1])), ImVec2(x, y), color, 2.0F);
                draw->AddCircleFilled(ImVec2(x, y), count <= 60 ? 3.0F : 1.5F, color);
            }
        }
    }
    const std::size_t label_step = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(count * 70.0 / plot_width)));
    for (std::size_t index = 0; index < count; index += label_step) {
        auto text = spec.labels[index];
        if (text.size() > 12) text = text.substr(0, 11) + "\xE2\x80\xA6";
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        draw->AddText(ImVec2(x_for(index) - size.x * 0.5F, bottom + 8.0F), ImGui::GetColorU32(p.text_muted), text.c_str());
    }
    if (groups > 1) {
        float legend_x = left;
        for (std::size_t index = 0; index < groups; ++index) {
            draw->AddRectFilled(ImVec2(legend_x, origin.y + 12.0F), ImVec2(legend_x + 10.0F, origin.y + 22.0F), series_color(index), 2.0F);
            draw->AddText(ImVec2(legend_x + 14.0F, origin.y + 9.0F), ImGui::GetColorU32(p.text), spec.series[index].name.c_str());
            legend_x += 30.0F + ImGui::CalcTextSize(spec.series[index].name.c_str()).x;
        }
    }
    if (hover_index >= 0) {
        const float x = x_for(static_cast<std::size_t>(hover_index));
        draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), ImGui::GetColorU32(ImVec4(p.text_muted.x, p.text_muted.y, p.text_muted.z, 0.4F)));
        std::string tip = spec.labels[static_cast<std::size_t>(hover_index)];
        for (const auto& series : spec.series) {
            if (static_cast<std::size_t>(hover_index) < series.values.size()) {
                tip += "\n" + series.name + ": " + format_value(series.values[static_cast<std::size_t>(hover_index)]);
            }
        }
        ImGui::SetTooltip("%s", tip.c_str());
    }
}

namespace {

const char* formula_op_symbol(int op) {
    static const char* symbols[] = {"+", "\xE2\x88\x92", "\xC3\x97", "\xC3\xB7"};
    return symbols[std::clamp(op, 0, 3)];
}

std::string aggregate_label(Aggregate aggregate, UiLanguage language) {
    return aggregate == Aggregate::None ? tr(language, UiTextKey::NoAggregate) : aggregate_name(aggregate);
}

bool column_combo(const char* id, const TableData& table, int& column, bool numbers_only, const char* extra = nullptr) {
    const std::string preview = column < 0 ? std::string{extra ? extra : ""}
        : static_cast<std::size_t>(column) < table.headers.size() ? table.headers[static_cast<std::size_t>(column)] : std::string{};
    bool changed = false;
    if (begin_combo(id, preview.c_str())) {
        if (extra != nullptr && ImGui::Selectable(extra, column < 0)) {
            column = -1;
            changed = true;
        }
        for (std::size_t index = 0; index < table.headers.size(); ++index) {
            if (numbers_only && table.stats[index].type != ColumnType::Number) continue;
            if (ImGui::Selectable((table.headers[index] + "##" + std::to_string(index)).c_str(), column == static_cast<int>(index))) {
                column = static_cast<int>(index);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Returns true when the table was replaced by its summary.
bool draw_summarize_popup(TableViewState& state, const std::vector<std::size_t>& rows, UiLanguage language) {
    if (!ImGui::BeginPopup("table-summarize")) return false;
    const auto& table = state.table;
    if (state.group_values.size() != table.headers.size()) {
        state.group_values.assign(table.headers.size(), false);
        for (std::size_t column = 0; column < table.headers.size(); ++column) {
            state.group_values[column] = table.stats[column].type == ColumnType::Number;
        }
    }
    if (state.group_column >= static_cast<int>(table.headers.size())) state.group_column = -1;
    ImGui::TextColored(palette().text_muted, "%s", tr(language, UiTextKey::SummarizeHelp).c_str());
    ImGui::SetNextItemWidth(200.0F);
    (void)column_combo((tr(language, UiTextKey::GroupBy) + "##group").c_str(), table, state.group_column, false, "(all rows)");
    ImGui::TextUnformatted(tr(language, UiTextKey::Operator).c_str());
    for (int index = 1; index < static_cast<int>(std::size(kAggregates)); ++index) {
        ImGui::SameLine();
        if (ImGui::RadioButton(aggregate_name(kAggregates[index]).c_str(), state.group_aggregate == index)) state.group_aggregate = index;
    }
    ImGui::TextUnformatted(tr(language, UiTextKey::Values).c_str());
    std::vector<std::size_t> values;
    for (std::size_t column = 0; column < table.headers.size(); ++column) {
        const bool count = kAggregates[state.group_aggregate] == Aggregate::Count;
        if (!count && table.stats[column].type != ColumnType::Number) continue;
        if (static_cast<int>(column) == state.group_column) continue;
        bool selected = state.group_values[column];
        if (ImGui::Checkbox((table.headers[column] + "##sum" + std::to_string(column)).c_str(), &selected)) state.group_values[column] = selected;
        if (selected) values.push_back(column);
    }
    bool replaced = false;
    ImGui::BeginDisabled(values.empty());
    if (ImGui::Button(with_icon(icon::kCheck, tr(language, UiTextKey::Summarize)).c_str())) {
        auto summary = aggregate_table(table, rows, state.group_column, values, kAggregates[state.group_aggregate]);
        const auto group_name = state.group_column >= 0 ? table.headers[static_cast<std::size_t>(state.group_column)] : std::string{"all"};
        if (!state.original) {
            state.original = table;
            state.original_title = state.title;
        }
        state.title = aggregate_name(kAggregates[state.group_aggregate]) + " by " + group_name;
        state.table = std::move(summary);
        state.visible.assign(state.table.headers.size(), true);
        state.group_values.clear();
        state.sort_column = -1;
        state.filter.clear();
        replaced = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
    return replaced;
}

// Returns true when a formula column was added.
bool draw_formula_popup(TableViewState& state, UiLanguage language) {
    if (!ImGui::BeginPopup("table-formula")) return false;
    auto& table = state.table;
    ImGui::TextColored(palette().text_muted, "%s", tr(language, UiTextKey::FormulaHelp).c_str());
    const auto first_number = [&] {
        for (std::size_t column = 0; column < table.headers.size(); ++column) {
            if (table.stats[column].type == ColumnType::Number) return static_cast<int>(column);
        }
        return -1;
    };
    const auto is_number_column = [&](int column) {
        return column >= 0 && static_cast<std::size_t>(column) < table.headers.size() &&
               table.stats[static_cast<std::size_t>(column)].type == ColumnType::Number;
    };
    if (!is_number_column(state.formula_left)) state.formula_left = first_number();
    if (state.formula_right >= 0 && !is_number_column(state.formula_right)) state.formula_right = -1;
    bool added = false;
    if (state.formula_left < 0) {
        ImGui::TextColored(palette().warning, "No number columns.");
    } else {
        ImGui::SetNextItemWidth(160.0F);
        (void)column_combo("##formula-a", table, state.formula_left, true);
        for (int op = 0; op < 4; ++op) {
            ImGui::SameLine(0.0F, op == 0 ? 8.0F : 2.0F);
            const bool active = state.formula_op == op;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, palette().accent_soft);
            if (ImGui::Button((std::string{formula_op_symbol(op)} + "##op" + std::to_string(op)).c_str(), ImVec2(ImGui::GetFrameHeight(), 0.0F))) state.formula_op = op;
            if (active) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0.0F, 8.0F);
        ImGui::SetNextItemWidth(160.0F);
        (void)column_combo("##formula-b", table, state.formula_right, true, tr(language, UiTextKey::Constant).c_str());
        if (state.formula_right < 0) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0F);
            ImGui::InputDouble("##formula-constant", &state.formula_constant, 0.0, 0.0, "%.6g");
        }
        FormulaColumn formula{.left = static_cast<std::size_t>(state.formula_left),
                              .op = static_cast<FormulaOp>(state.formula_op),
                              .right_column = state.formula_right >= 0 ? std::optional<std::size_t>{static_cast<std::size_t>(state.formula_right)} : std::nullopt,
                              .constant = state.formula_constant,
                              .name = state.formula_name};
        ImGui::SetNextItemWidth(260.0F);
        input_text_hint("##formula-name", formula_label(table, {formula.left, formula.op, formula.right_column, formula.constant, {}}).c_str(),
                        state.formula_name);
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kPlus, tr(language, UiTextKey::AddColumn)).c_str())) {
            std::string error;
            if (add_formula_column(table, formula, error)) {
                state.visible.push_back(true);
                state.group_values.clear();
                state.formula_name.clear();
                state.status = formula_label(table, formula);
                added = true;
                ImGui::CloseCurrentPopup();
            } else {
                state.status = error;
            }
        }
    }
    ImGui::EndPopup();
    return added;
}

}  // namespace

void draw_table_view(TableViewState& state, ChartViewState& chart, const DataViewHost& host, UiLanguage language,
                     const std::string& default_chart_path) {
    if (!state.open) return;
    const auto title = with_icon(icon::kTable, state.title.empty() ? tr(language, UiTextKey::ViewTable) : state.title) + "###table-view";
    const float width = std::clamp(140.0F * static_cast<float>(state.table.headers.size()), 560.0F, 1100.0F);
    // Summarize / formula replace the table mid-frame; finish the window
    // without drawing the stale grid.
    const auto end_table_view = [] { ImGui::End(); };
    if (begin_tool_window(title, &state.open, ImVec2(width, 560.0F), &state.focus_pending)) {
        const auto& p = palette();
        auto& table = state.table;
        std::vector<std::size_t> columns;
        for (std::size_t column = 0; column < table.headers.size(); ++column) {
            if (column < state.visible.size() && state.visible[column]) columns.push_back(column);
        }
        const auto rows = table_view_rows(table, state.filter, state.sort_column, state.descending);

        // Toolbar: filter, columns, stats, chart, summarize, formula. Buttons
        // wrap to a second line when the window is narrow.
        const auto toolbar_button = [](const std::string& label) {
            const float width = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0F;
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
            return ImGui::Button(label.c_str());
        };
        ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * 0.3F, 140.0F, 260.0F));
        input_text_hint("##table-filter", (std::string{ui_fonts().icons ? icon::kSearch : ""} + " " + tr(language, UiTextKey::Filter)).c_str(), state.filter);
        if (toolbar_button(with_icon(icon::kLayers, tr(language, UiTextKey::Columns)))) ImGui::OpenPopup("table-columns");
        if (ImGui::BeginPopup("table-columns")) {
            for (std::size_t column = 0; column < table.headers.size(); ++column) {
                bool visible = state.visible[column];
                if (ImGui::Checkbox((table.headers[column] + "##col" + std::to_string(column)).c_str(), &visible)) {
                    state.visible[column] = visible;
                }
            }
            ImGui::EndPopup();
        }
        if (toolbar_button(with_icon(icon::kInfo, tr(language, UiTextKey::Statistics)))) state.show_stats = !state.show_stats;
        if (toolbar_button(with_icon(icon::kChart, tr(language, UiTextKey::Chart)))) {
            open_chart_from_table(chart, table, rows, default_chart_path);
        }
        if (toolbar_button(with_icon(icon::kListOrdered, tr(language, UiTextKey::Summarize)))) ImGui::OpenPopup("table-summarize");
        if (draw_summarize_popup(state, rows, language)) return end_table_view();
        if (toolbar_button(with_icon(icon::kPlus, tr(language, UiTextKey::FormulaColumn)))) ImGui::OpenPopup("table-formula");
        if (draw_formula_popup(state, language)) return end_table_view();
        if (state.original && toolbar_button(with_icon(icon::kUndo, tr(language, UiTextKey::ResetTable)))) {
            state.table = std::move(*state.original);
            state.title = state.original_title;
            state.original.reset();
            state.visible.assign(state.table.headers.size(), true);
            state.sort_column = -1;
            return end_table_view();
        }

        const float footer = footer_height();
        const int column_count = static_cast<int>(std::max<std::size_t>(1, columns.size()));
        // Few columns stretch to the window; many keep their width and scroll.
        const auto sizing = columns.size() <= 8 ? ImGuiTableFlags_SizingStretchProp : ImGuiTableFlags_SizingFixedFit;
        const auto flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_ScrollX |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | sizing;
        if (ImGui::BeginTable("table-view-grid", column_count, flags, ImVec2(0.0F, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            for (const auto column : columns) {
                const bool numeric = table.stats[column].type == ColumnType::Number;
                ImGui::TableSetupColumn(table.headers[column].c_str(),
                                        numeric ? ImGuiTableColumnFlags_PreferSortDescending : ImGuiTableColumnFlags_None,
                                        numeric ? 0.6F : 1.0F, static_cast<ImGuiID>(column));
            }
            ImGui::TableHeadersRow();
            if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsDirty) {
                if (sort->SpecsCount > 0) {
                    state.sort_column = static_cast<int>(sort->Specs[0].ColumnUserID);
                    state.descending = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                } else {
                    state.sort_column = -1;
                }
                sort->SpecsDirty = false;
            }
            if (state.show_stats) {
                ImGui::TableNextRow();
                for (const auto column : columns) {
                    ImGui::TableNextColumn();
                    const auto& stats = table.stats[column];
                    std::string summary = column_type_name(stats.type) + " \xC2\xB7 " + std::to_string(stats.filled) + " filled";
                    if (stats.type == ColumnType::Number) {
                        summary += "\nsum " + format_axis_number(stats.sum) + "  mean " + format_axis_number(stats.mean) +
                                   "\nmin " + format_axis_number(stats.min) + "  max " + format_axis_number(stats.max);
                    } else {
                        summary += "\n" + std::to_string(stats.distinct) + " distinct";
                    }
                    ImGui::TextColored(p.accent, "%s", summary.c_str());
                }
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    for (const auto column : columns) {
                        ImGui::TableNextColumn();
                        const auto& cell = table.rows[rows[static_cast<std::size_t>(row)]][column];
                        if (table.stats[column].type == ColumnType::Number) {
                            const float text_width = ImGui::CalcTextSize(cell.c_str()).x;
                            const float offset = ImGui::GetColumnWidth() - text_width - ImGui::GetStyle().CellPadding.x;
                            if (offset > 0.0F) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
                        }
                        ImGui::TextUnformatted(cell.c_str());
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && host.copy_text) {
                            host.copy_text(cell);
                            state.status = tr(language, UiTextKey::Copied);
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
        const auto status = std::to_string(rows.size()) + " / " + std::to_string(table.rows.size()) + " " +
                            tr(language, UiTextKey::Rows) + (table.truncated ? " (truncated)" : "") +
                            (state.status.empty() ? "" : "  \xC2\xB7  " + state.status);
        const int clicked = footer_buttons({{tr(language, UiTextKey::CopyAsSql)}, {tr(language, UiTextKey::CopyAsJson)},
                                            {tr(language, UiTextKey::CopyAsCsv)}, {tr(language, UiTextKey::CopyAsMarkdown), true}},
                                           status);
        if (clicked >= 0 && host.copy_text) {
            const std::string text = clicked == 0 ? table_to_sql(table, rows, columns)
                                   : clicked == 1 ? table_to_json(table, rows, columns)
                                   : clicked == 2 ? table_to_csv(table, rows, columns)
                                                  : table_to_markdown(table, rows, columns);
            host.copy_text(text);
            state.status = tr(language, UiTextKey::Copied);
        }
    }
    ImGui::End();
}

void draw_markdown_view(MarkdownViewState& state, const DataViewHost& host, UiLanguage language) {
    if (!state.open) return;
    const auto title = with_icon(icon::kBook, tr(language, UiTextKey::PreviewMarkdown)) + "###markdown-view";
    if (begin_tool_window(title, &state.open, ImVec2(720.0F, 620.0F), &state.focus_pending)) {
        ImGui::Checkbox(tr(language, UiTextKey::ShowSource).c_str(), &state.show_source);
        const auto& p = palette();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, p.background);
        ImGui::BeginChild("markdown-body", ImVec2(0.0F, -footer_height()), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (state.show_source) {
            ImGui::TextUnformatted(state.source.c_str());
        } else {
            ImGui::PushTextWrapPos(0.0F);
            draw_markdown_blocks(state.blocks, host.open_uri);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        const int clicked = footer_buttons({{tr(language, UiTextKey::CopyPlainText)}, {tr(language, UiTextKey::CopyHtml), true}},
                                           state.status);
        if (clicked >= 0 && host.copy_text) {
            host.copy_text(clicked == 0 ? markdown_to_plain_text(state.blocks) : markdown_to_html(state.blocks));
            state.status = tr(language, UiTextKey::Copied);
        }
    }
    ImGui::End();
}

void draw_chart_view(ChartViewState& state, const DataViewHost& host, UiLanguage language) {
    if (!state.open) return;
    if (state.dirty) {
        state.dirty = false;
        state.status.clear();
        const auto kind = static_cast<ChartKind>(state.kind);
        if (state.table) {
            std::vector<std::size_t> y_columns;
            for (std::size_t column = 0; column < state.y_columns.size(); ++column) {
                if (state.y_columns[column]) y_columns.push_back(column);
            }
            if (kind == ChartKind::Pie || kind == ChartKind::Histogram) y_columns.resize(std::min<std::size_t>(y_columns.size(), 1));
            const auto spec = chart_from_table(*state.table, state.rows, state.x_column, y_columns, kind,
                                               kAggregates[std::clamp(state.aggregate, 0, static_cast<int>(std::size(kAggregates)) - 1)]);
            state.spec = spec.value_or(ChartSpec{});
        } else {
            const auto data = parse_graph_data(state.series_source, state.header, state.convert_dates);
            state.spec = data ? chart_from_graph(*data, kind, kAggregates[std::clamp(state.aggregate, 0, static_cast<int>(std::size(kAggregates)) - 1)])
                              : ChartSpec{};
        }
        state.problem = chart_problem(state.spec);
    }
    const auto title = with_icon(icon::kChart, tr(language, UiTextKey::GraphPreview)) + "###chart-view";
    if (begin_tool_window(title, &state.open, ImVec2(820.0F, 600.0F), &state.focus_pending)) {
        const auto& p = palette();
        const std::string kinds[] = {tr(language, UiTextKey::ChartLine), tr(language, UiTextKey::ChartBar),
                                     tr(language, UiTextKey::ChartPie), tr(language, UiTextKey::ChartScatter),
                                     tr(language, UiTextKey::ChartHistogram)};
        for (int index = 0; index < 5; ++index) {
            if (index) ImGui::SameLine(0.0F, 4.0F);
            const bool active = state.kind == index;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, p.accent_soft);
            if (ImGui::Button(kinds[index].c_str())) {
                state.kind = index;
                state.dirty = true;
            }
            if (active) ImGui::PopStyleColor();
        }
        // Second row: X: [column]  Y: [series]  Agg: [operator].
        const auto field_label = [&](const char* text, bool first) {
            if (!first) ImGui::SameLine(0.0F, 16.0F);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(p.text_muted, "%s", text);
            ImGui::SameLine(0.0F, 6.0F);
        };
        if (state.table) {
            const auto& table = *state.table;
            field_label("X:", true);
            ImGui::SetNextItemWidth(170.0F);
            const std::string x_preview = state.x_column < 0 ? tr(language, UiTextKey::RowNumber)
                                                             : table.headers[static_cast<std::size_t>(state.x_column)];
            if (begin_combo("##chart-x", x_preview.c_str())) {
                if (ImGui::Selectable(tr(language, UiTextKey::RowNumber).c_str(), state.x_column < 0)) {
                    state.x_column = -1;
                    state.dirty = true;
                }
                for (std::size_t column = 0; column < table.headers.size(); ++column) {
                    if (ImGui::Selectable((table.headers[column] + "##x" + std::to_string(column)).c_str(),
                                          state.x_column == static_cast<int>(column))) {
                        state.x_column = static_cast<int>(column);
                        state.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            field_label("Y:", false);
            std::string y_preview;
            for (std::size_t column = 0; column < state.y_columns.size(); ++column) {
                if (!state.y_columns[column]) continue;
                y_preview += (y_preview.empty() ? "" : ", ") + table.headers[column];
            }
            if (y_preview.empty()) y_preview = "(none)";
            ImGui::SetNextItemWidth(200.0F);
            if (begin_combo("##chart-y", y_preview.c_str())) {
                for (std::size_t column = 0; column < table.headers.size(); ++column) {
                    if (table.stats[column].type != ColumnType::Number) continue;
                    bool selected = state.y_columns[column];
                    if (ImGui::Checkbox((table.headers[column] + "##y" + std::to_string(column)).c_str(), &selected)) {
                        state.y_columns[column] = selected;
                        state.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            state.dirty |= ImGui::Checkbox(tr(language, UiTextKey::HeaderRow).c_str(), &state.header);
            ImGui::SameLine();
            state.dirty |= ImGui::Checkbox(tr(language, UiTextKey::ConvertDates).c_str(), &state.convert_dates);
        }
        if (static_cast<ChartKind>(state.kind) != ChartKind::Histogram) {
            // Agg reduces rows sharing an X value; with X = row number it
            // reduces all rows to one value per Y column.
            field_label("Agg:", false);
            ImGui::SetNextItemWidth(110.0F);
            const int count = static_cast<int>(std::size(kAggregates));
            state.aggregate = std::clamp(state.aggregate, 0, count - 1);
            if (begin_combo("##chart-aggregate", aggregate_label(kAggregates[state.aggregate], language).c_str())) {
                for (int index = 0; index < count; ++index) {
                    if (ImGui::Selectable(aggregate_label(kAggregates[index], language).c_str(), state.aggregate == index)) {
                        state.aggregate = index;
                        state.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(language, UiTextKey::Operator).c_str());
        }
        const auto available = ImGui::GetContentRegionAvail();
        const float chart_height = std::max(160.0F, available.y - footer_height() - ImGui::GetFrameHeightWithSpacing() - 8.0F);
        draw_chart(state.spec, available.x, chart_height);
        if (!state.problem.empty()) {
            ImGui::TextColored(p.warning, "%s", state.problem.c_str());
        } else {
            ImGui::TextColored(p.text_muted, "%zu %s", state.spec.labels.size(), tr(UiTextKey::Points).c_str());
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        input_text_string("##chart-save-path", state.save_path);
        const bool drawable = state.problem.empty();
        const int clicked = footer_buttons({{tr(language, UiTextKey::SaveGraphImage), false, drawable},
                                            {with_icon(icon::kCopy, tr(language, UiTextKey::CopyGraphImage)), true, drawable}},
                                           state.status);
        if (clicked >= 0) {
            const auto png = render_chart_png(state.spec);
            if (png.empty()) {
                state.status = state.problem;
            } else if (clicked == 1) {
                state.status = host.copy_png && host.copy_png(png) ? tr(language, UiTextKey::Copied) : "Could not copy image";
            } else if (host.save_file) {
                const auto error = host.save_file(state.save_path, png);
                state.status = error.empty() ? tr(language, UiTextKey::Saved) : error;
            }
        }
    }
    ImGui::End();
}

#endif

}  // namespace pasteit
