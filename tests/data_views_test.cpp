#include "graph/chart.hpp"
#include "render/markdown.hpp"
#include "table/table_data.hpp"

#include <cassert>
#include <string>

using namespace pasteit;

namespace {

void tables() {
    const auto table = parse_delimited_table("name,qty,price\napple,3,\"1,200.50\"\n\"pear, green\",10,0.8\nfig,,2\n", 0, true);
    assert(table && table->headers.size() == 3 && table->rows.size() == 3);
    assert(table->rows[1][0] == "pear, green");
    assert(table->stats[1].type == ColumnType::Number && table->stats[1].filled == 2 && table->stats[1].sum == 13.0);
    assert(table->stats[2].type == ColumnType::Number && table->stats[2].max == 1200.5);
    assert(table->stats[0].type == ColumnType::Text);

    // Quoted multi-line field and escaped quotes.
    const auto quoted = parse_delimited_table("a,b\n\"line1\nline2\",\"say \"\"hi\"\"\"\n", ',', true);
    assert(quoted && quoted->rows.size() == 1 && quoted->rows[0][0] == "line1\nline2" && quoted->rows[0][1] == "say \"hi\"");

    // Markdown pipe table drops the separator row.
    const auto pipe = parse_delimited_table("| a | b |\n|---|---|\n| 1 | 2 |\n", '|', true);
    assert(pipe && pipe->headers[0] == "a" && pipe->rows.size() == 1 && pipe->rows[0][1] == "2");

    const auto json = parse_json_table(R"([{"b":1,"a":"x"},{"a":"y","c":true}])");
    assert(json && json->headers == (std::vector<std::string>{"b", "a", "c"}) && json->rows[1][2] == "true");
    const auto ndjson = parse_json_table("{\"id\":1}\n{\"id\":2}\n");
    assert(ndjson && ndjson->rows.size() == 2);

    // Numeric sort, blanks last, and case-insensitive filter.
    auto rows = table_view_rows(*table, "", 1, true);
    assert(table->rows[rows[0]][1] == "10" && table->rows[rows.back()][1].empty());
    rows = table_view_rows(*table, "PEAR", -1, false);
    assert(rows.size() == 1 && rows[0] == 1);

    const std::vector<std::size_t> all_rows{0, 1};
    const std::vector<std::size_t> columns{0, 1};
    assert(table_to_markdown(*table, all_rows, columns) == "| name | qty |\n| --- | ---: |\n| apple | 3 |\n| pear, green | 10 |");
    assert(table_to_csv(*table, all_rows, columns) == "name,qty\napple,3\n\"pear, green\",10");
    assert(table_to_json(*table, all_rows, columns) == "[\n  {\"name\": \"apple\", \"qty\": 3},\n  {\"name\": \"pear, green\", \"qty\": 10}\n]");
    assert(table_to_sql(*table, {0}, columns, "fruit") ==
           "CREATE TABLE fruit (name TEXT, qty NUMERIC);\nINSERT INTO fruit VALUES ('apple', 3);");
}

void charts() {
    const auto table = parse_delimited_table("month,sales,cost\njan,10,4\nfeb,12,5\nmar,9,6\n", ',', true);
    const auto spec = chart_from_table(*table, {0, 1, 2}, 0, {1, 2}, ChartKind::Bar);
    assert(spec && spec->series.size() == 2 && spec->labels[2] == "mar" && spec->series[1].values[1] == 5.0);
    assert(chart_problem(*spec).empty());
    assert(!render_chart_png(*spec).empty());

    const auto scatter = chart_from_table(*table, {0, 1, 2}, 1, {2}, ChartKind::Scatter);
    assert(scatter && scatter->numeric_x && scatter->x[0] == 10.0);
    assert(!render_chart_png(*scatter).empty());

    const auto histogram = make_histogram("v", {1, 2, 2, 3, 3, 3, 4, 10}, 3);
    assert(histogram.labels.size() == 3 && histogram.series[0].values[0] == 6.0 && histogram.series[0].values[2] == 1.0);
    assert(!render_chart_png(histogram).empty());

    ChartSpec pie = *spec;
    pie.kind = ChartKind::Pie;
    pie.series[0].values[0] = -1.0;
    assert(!chart_problem(pie).empty());
    assert(format_axis_number(12500) == "12.5k");
}

void markdown() {
    const auto blocks = parse_markdown(
        "Title\n=====\n\nSome **bold**, *it*, `code`, ~~gone~~ and [a link](https://x.y \"t\").\n\n"
        "- one\n  continued\n- [x] done\n  1. nested\n\n> quoted\n\n```cpp\nint x;\n```\n\n| a | b |\n|:--|--:|\n| 1 | 2 |\n\n---\n## snake_case stays\n");
    assert(blocks[0].type == MdBlockType::Heading && blocks[0].level == 1);
    const auto& paragraph = blocks[1];
    assert(paragraph.type == MdBlockType::Paragraph);
    bool bold = false, code = false, strike = false, link = false, italic = false;
    for (const auto& span : paragraph.spans) {
        bold |= span.style == MdBold && span.text == "bold";
        italic |= span.style == MdItalic && span.text == "it";
        code |= span.style == MdCode && span.text == "code";
        strike |= span.style == MdStrike;
        link |= span.style == MdLink && span.url == "https://x.y";
    }
    assert(bold && italic && code && strike && link);
    assert(blocks[2].type == MdBlockType::ListItem && blocks[2].spans.front().text == "one continued");
    assert(blocks[3].task && blocks[3].checked);
    assert(blocks[4].ordered && blocks[4].level == 1 && blocks[4].number == 1);
    assert(blocks[5].type == MdBlockType::Quote);
    assert(blocks[6].type == MdBlockType::Code && blocks[6].language == "cpp" && blocks[6].code == "int x;");
    assert(blocks[7].type == MdBlockType::Table && blocks[7].rows.size() == 2 && blocks[7].align[1] == MdAlign::Right);
    assert(blocks[8].type == MdBlockType::Rule);
    assert(blocks[9].type == MdBlockType::Heading && blocks[9].spans.front().text == "snake_case stays");

    const auto html = markdown_to_html(blocks);
    assert(html.find("<h1>Title</h1>") != std::string::npos);
    assert(html.find("<a href=\"https://x.y\">a link</a>") != std::string::npos);
    assert(html.find("<ul>\n<li>one continued</li>") != std::string::npos);
    assert(html.find("<ol>\n<li>nested</li>\n</ol>\n</ul>") != std::string::npos);
    assert(html.find("<td align=\"right\">2</td>") != std::string::npos);
    const auto plain = markdown_to_plain_text(blocks);
    assert(plain.find("- [x] done") != std::string::npos && plain.find("**") == std::string::npos);
}

}  // namespace

void aggregates_and_formulas() {
    using namespace pasteit;
    auto table = parse_delimited_table("region,month,sales,cost\nnorth,2,10,4\nsouth,1,5,1\nnorth,1,30,x\nsouth,2,,2\n", ',', true);
    assert(table);
    const std::vector<std::size_t> rows{0, 1, 2, 3};

    auto sum = aggregate_table(*table, rows, 0, {2, 3}, Aggregate::Sum);
    assert((sum.headers == std::vector<std::string>{"region", "sum(sales)", "sum(cost)"}));
    assert(sum.rows.size() == 2);
    assert((sum.rows[0] == std::vector<std::string>{"north", "40", "4"}));  // "x" skipped
    assert((sum.rows[1] == std::vector<std::string>{"south", "5", "3"}));
    assert(aggregate_table(*table, rows, 0, {2}, Aggregate::Avg).rows[0][1] == "20");
    assert(aggregate_table(*table, rows, 0, {2}, Aggregate::Min).rows[0][1] == "10");
    assert(aggregate_table(*table, rows, 0, {2}, Aggregate::Max).rows[0][1] == "30");
    assert(aggregate_table(*table, rows, 0, {3}, Aggregate::Count).rows[0][1] == "2");  // filled cells, text included
    // Numeric group keys sort ascending; missing group column means one group.
    const auto by_month = aggregate_table(*table, rows, 1, {2}, Aggregate::Sum);
    assert(by_month.rows[0][0] == "1" && by_month.rows[0][1] == "35" && by_month.rows[1][1] == "10");
    const auto all = aggregate_table(*table, rows, -1, {2}, Aggregate::Avg);
    assert(all.rows.size() == 1 && all.rows[0][1] == "15");

    std::string error;
    assert(add_formula_column(*table, {.left = 2, .op = FormulaOp::Subtract, .right_column = 3}, error));
    assert(table->headers.back() == "sales - cost");
    assert(table->rows[0].back() == "6" && table->rows[2].back().empty() && table->rows[3].back().empty());
    assert(add_formula_column(*table, {.left = 2, .op = FormulaOp::Divide, .constant = 4.0, .name = "quarter"}, error));
    assert(table->headers.back() == "quarter" && table->rows[0].back() == "2.5");
    assert(add_formula_column(*table, {.left = 2, .op = FormulaOp::Divide, .right_column = 2}, error));
    assert(table->rows[3].back().empty());  // empty operand
    assert(table->stats.back().type == ColumnType::Number);
    assert(!add_formula_column(*table, {.left = 99}, error) && !error.empty());

    const auto chart = chart_from_table(*table, rows, 0, {2}, ChartKind::Bar, Aggregate::Sum);
    assert(chart && chart->labels.size() == 2 && chart->series[0].values[0] == 40.0);
    assert(chart->series[0].name == "sum(sales)");
    // X = row number with an aggregate: one bar per Y column.
    const auto totals = chart_from_table(*table, rows, -1, {2, 3}, ChartKind::Bar, Aggregate::Sum);
    assert(totals && (totals->labels == std::vector<std::string>{"sales", "cost"}));
    assert(totals->series[0].values[0] == 45.0 && totals->series[0].values[1] == 7.0 && chart_problem(*totals).empty());
    const auto single = chart_from_table(*table, rows, -1, {2}, ChartKind::Pie, Aggregate::Max);
    assert(single && single->labels.size() == 1 && chart_problem(*single).empty());
    // Number series: points sharing a label are reduced.
    const auto series = parse_graph_data("mon,3\ntue,4\nmon,5\n", false, false);
    assert(series);
    const auto summed = chart_from_graph(*series, ChartKind::Bar, Aggregate::Sum);
    assert(summed.labels.size() == 2 && summed.series[0].values[0] == 8.0);
}

void table_edits() {
    auto table = *parse_delimited_table("city,pop\nTokyo,\"13,960,000\"\nParis,2161000\nOslo,n/a\n", ',', true);
    assert(table.has_header && table.headers[0] == "city");

    // Header row toggles both ways and keeps the data intact.
    assert(set_header_row(table, false) && !table.has_header);
    assert(table.headers[0] == "Column 1" && table.rows.size() == 4 && table.rows[0][0] == "city");
    assert(!set_header_row(table, false));
    assert(set_header_row(table, true) && table.headers[1] == "pop" && table.rows.size() == 3);
    const auto no_header = parse_delimited_table("a,1\nb,2\n", ',', false);
    assert(no_header && !no_header->has_header && no_header->headers[1] == "Column 2");

    rename_column(table, 1, "  population ");
    assert(table.headers[1] == "population");
    rename_column(table, 1, " ");
    assert(table.headers[1] == "Column 2");

    // Forced type: "n/a" keeps the column Text until the user says Number,
    // then numbers sort numerically and the odd cell goes last.
    assert(table.stats[1].type == ColumnType::Text);
    set_column_type(table, 1, ColumnType::Number);
    assert(table.stats[1].type == ColumnType::Number);
    auto rows = table_view_rows(table, "", 1, false);
    assert(table.rows[rows[0]][0] == "Paris" && table.rows[rows[2]][0] == "Oslo");
    set_column_type(table, 1, std::nullopt);
    assert(table.stats[1].type == ColumnType::Text);

    // Literal replace: case-insensitive by default, $ is not special, dry run counts only.
    ReplaceSpec literal{.column = 0, .find = "O", .replacement = "$0"};
    auto counted = replace_in_table(table, literal, nullptr, false);
    assert(counted.cells == 2 && table.rows[0][0] == "Tokyo");
    literal.match_case = true;
    assert(replace_in_table(table, literal, nullptr, true).cells == 1 && table.rows[2][0] == "$0slo");

    // Regex with groups, limited to some rows; numbers become a Number column.
    ReplaceSpec regex{.column = 1, .find = R"((\d+),(\d+),(\d+))", .replacement = "$1$2$3", .regex = true};
    const std::vector<std::size_t> first{0};
    assert(replace_in_table(table, regex, &first, true).cells == 1 && table.rows[0][1] == "13960000");
    ReplaceSpec cleanup{.column = 1, .find = "^n/a$", .replacement = "", .regex = true};
    assert(replace_in_table(table, cleanup, nullptr, true).cells == 1 && table.stats[1].type == ColumnType::Number);

    // Every column, and an invalid pattern reports an error without changes.
    ReplaceSpec all{.find = "a", .replacement = "A"};
    assert(replace_in_table(table, all, nullptr, true).cells == 1 && table.rows[1][0] == "PAris");
    ReplaceSpec broken{.find = "(", .regex = true};
    const auto failed = replace_in_table(table, broken, nullptr, true);
    assert(!failed.error.empty() && failed.cells == 0);
}

int main() {
    table_edits();
    aggregates_and_formulas();
    tables();
    charts();
    markdown();
}
