#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

enum class ColumnType { Text, Number, Date, Empty };

struct ColumnStats {
    ColumnType type = ColumnType::Empty;
    std::size_t filled = 0;
    std::size_t distinct = 0;
    std::size_t numeric = 0;
    double sum = 0.0;
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
};

struct TableData {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;  // every row has headers.size() cells
    std::vector<ColumnStats> stats;
    bool truncated = false;  // rows beyond the limit were dropped
};

inline constexpr std::size_t kMaxTableRows = 20000;

// RFC 4180 style: quoted fields may contain delimiters, quotes ("") and
// newlines. delimiter 0 means detect from the first lines.
std::optional<TableData> parse_delimited_table(std::string_view text, char delimiter, bool header);
// A JSON array of objects, or newline-delimited JSON objects.
std::optional<TableData> parse_json_table(std::string_view text);

char detect_delimiter(std::string_view text);
void compute_column_stats(TableData& table);
std::optional<double> parse_cell_number(std::string_view cell);

// Row indexes after filtering (case-insensitive substring over all cells)
// and a stable sort by one column (numbers numerically, otherwise text).
std::vector<std::size_t> table_view_rows(const TableData& table, std::string_view filter, int sort_column,
                                         bool descending);

// Exports of the visible rows and columns.
std::string table_to_markdown(const TableData& table, const std::vector<std::size_t>& rows,
                              const std::vector<std::size_t>& columns);
std::string table_to_csv(const TableData& table, const std::vector<std::size_t>& rows,
                         const std::vector<std::size_t>& columns, char delimiter = ',');
std::string table_to_json(const TableData& table, const std::vector<std::size_t>& rows,
                          const std::vector<std::size_t>& columns);
std::string table_to_sql(const TableData& table, const std::vector<std::size_t>& rows,
                         const std::vector<std::size_t>& columns, std::string_view table_name = "data");

std::string column_type_name(ColumnType type);

// Reduction applied to value columns per group (the chart's Y operator).
enum class Aggregate { None, Sum, Avg, Min, Max, Count };
inline constexpr Aggregate kAggregates[] = {Aggregate::None, Aggregate::Sum, Aggregate::Avg,
                                            Aggregate::Min, Aggregate::Max, Aggregate::Count};
std::string aggregate_name(Aggregate aggregate);

// Groups the given rows by group_column (< 0: one group of all rows) and
// reduces each value column. Groups keep first-seen order, or ascending order
// when every key is a number. Non-numeric cells are skipped; Count counts
// non-empty cells. The result is a new table: group, then one column per value.
TableData aggregate_table(const TableData& table, const std::vector<std::size_t>& rows, int group_column,
                          const std::vector<std::size_t>& value_columns, Aggregate aggregate);

enum class FormulaOp { Add, Subtract, Multiply, Divide };
struct FormulaColumn {
    std::size_t left = 0;
    FormulaOp op = FormulaOp::Add;
    std::optional<std::size_t> right_column;  // empty: use constant
    double constant = 0.0;
    std::string name;                         // empty: derived from the formula
};
std::string formula_label(const TableData& table, const FormulaColumn& formula);
// Appends a computed number column (cells are empty where an operand is not a
// number or on division by zero) and refreshes the stats.
bool add_formula_column(TableData& table, const FormulaColumn& formula, std::string& error);
std::string format_cell_number(double value);

}  // namespace pasteit
