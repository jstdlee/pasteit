#pragma once

#include "config/app_settings.hpp"
#include "graph/chart.hpp"
#include "render/markdown.hpp"
#include "table/table_data.hpp"
#include "ui/localization.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

// What the data-view windows need from the desktop shell.
struct DataViewHost {
    std::function<void(std::string_view)> copy_text;
    std::function<bool(const std::vector<std::byte>&)> copy_png;
    std::function<void(std::string_view)> open_uri;
    // Saves bytes to a path chosen in the window; returns an error or "".
    std::function<std::string(const std::string& path, const std::vector<std::byte>& bytes)> save_file;
};

struct ChartViewState {
    bool open = false;
    bool focus_pending = false;
    int kind = 0;  // ChartKind
    // Source: a table (column pickers) or a plain number series.
    std::optional<TableData> table;
    std::vector<std::size_t> rows;
    int x_column = -1;
    std::vector<bool> y_columns;
    std::string series_source;
    bool header = false;
    bool convert_dates = false;
    bool dirty = true;
    ChartSpec spec;
    std::string problem;
    std::string save_path;
    std::string status;
};

struct TableViewState {
    bool open = false;
    bool focus_pending = false;
    std::string title;
    TableData table;
    std::string filter;
    int sort_column = -1;
    bool descending = false;
    std::vector<bool> visible;
    bool show_stats = false;
    std::string status;
};

struct MarkdownViewState {
    bool open = false;
    bool focus_pending = false;
    std::string source;
    std::vector<MdBlock> blocks;
    bool show_source = false;
    std::string status;
};

void open_table_view(TableViewState& state, TableData table, std::string title);
void open_markdown_view(MarkdownViewState& state, std::string source);
void open_chart_from_series(ChartViewState& state, std::string source, std::string save_path);
void open_chart_from_table(ChartViewState& state, const TableData& table, std::vector<std::size_t> rows,
                           std::string save_path);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_markdown_blocks(const std::vector<MdBlock>& blocks, const std::function<void(std::string_view)>& open_uri);
// Live, anti-aliased chart with hover values.
void draw_chart(const ChartSpec& spec, float width, float height);

void draw_table_view(TableViewState& state, ChartViewState& chart, const DataViewHost& host, UiLanguage language,
                     const std::string& default_chart_path);
void draw_markdown_view(MarkdownViewState& state, const DataViewHost& host, UiLanguage language);
void draw_chart_view(ChartViewState& state, const DataViewHost& host, UiLanguage language);
#endif

}  // namespace pastit
