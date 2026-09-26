#include "table/table_data.hpp"

#include "util/json.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>

namespace pasteit {
namespace {

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string{text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1)};
}

std::string lower(std::string_view text) {
    std::string out{text};
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool looks_like_date(std::string_view cell) {
    static const std::regex date(R"(^\d{4}-\d{2}-\d{2}([ T]\d{2}:\d{2}(:\d{2})?)?|^\d{1,2}/\d{1,2}/\d{2,4}$)");
    return std::regex_search(cell.begin(), cell.end(), date);
}

std::vector<std::vector<std::string>> parse_records(std::string_view text, char delimiter) {
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string field;
    bool quoted = false;
    bool field_started = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (quoted) {
            if (ch == '"' && index + 1 < text.size() && text[index + 1] == '"') {
                field += '"';
                ++index;
            } else if (ch == '"') {
                quoted = false;
            } else {
                field += ch;
            }
            continue;
        }
        if (ch == '"' && trim(field).empty()) {
            quoted = true;
            field.clear();
            field_started = true;
        } else if (ch == delimiter) {
            record.push_back(trim(field));
            field.clear();
            field_started = false;
        } else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') ++index;
            record.push_back(trim(field));
            field.clear();
            field_started = false;
            const bool blank = record.size() == 1 && record.front().empty();
            if (!blank) records.push_back(std::move(record));
            record.clear();
            if (records.size() > kMaxTableRows + 1) break;
        } else {
            field += ch;
            field_started = true;
        }
    }
    if (field_started || !field.empty() || !record.empty()) {
        record.push_back(trim(field));
        if (!(record.size() == 1 && record.front().empty())) records.push_back(std::move(record));
    }
    // Markdown/pipe tables: drop the "| --- |" separator row and outer pipes.
    if (delimiter == '|') {
        for (auto& row : records) {
            if (!row.empty() && row.front().empty()) row.erase(row.begin());
            if (!row.empty() && row.back().empty()) row.pop_back();
        }
        std::erase_if(records, [](const auto& row) {
            return !row.empty() && std::all_of(row.begin(), row.end(), [](const std::string& cell) {
                return !cell.empty() && cell.find_first_not_of("-: ") == std::string::npos;
            });
        });
    }
    return records;
}

std::string json_cell(const OrderedJson& value) {
    if (value.type == OrderedJson::Type::String) return value.text;
    if (value.type == OrderedJson::Type::Null) return {};
    return ordered_json_compact(value);
}

std::optional<TableData> table_from_objects(const std::vector<const OrderedJson*>& objects) {
    TableData table;
    std::set<std::string> known;
    for (const auto* object : objects) {
        for (const auto& [key, value] : object->members) {
            if (known.insert(key).second) table.headers.push_back(key);
        }
    }
    if (table.headers.empty()) return std::nullopt;
    for (const auto* object : objects) {
        if (table.rows.size() >= kMaxTableRows) {
            table.truncated = true;
            break;
        }
        std::vector<std::string> row;
        row.reserve(table.headers.size());
        for (const auto& header : table.headers) {
            const auto* value = object->get(header);
            row.push_back(value == nullptr ? std::string{} : json_cell(*value));
        }
        table.rows.push_back(std::move(row));
    }
    compute_column_stats(table);
    return table;
}

std::string sql_identifier(std::string_view name) {
    std::string out;
    for (const char ch : name) out += std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front()))) out = "c_" + out;
    return out;
}

}  // namespace

std::optional<double> parse_cell_number(std::string_view cell) {
    auto text = trim(cell);
    if (text.empty()) return std::nullopt;
    std::string cleaned;
    for (const char ch : text) {
        if (ch == ',' || ch == '_' || ch == '$' || ch == '%' || ch == ' ') continue;
        cleaned += ch;
    }
    if (cleaned.empty()) return std::nullopt;
    double value = 0.0;
    const auto result = std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (result.ec != std::errc{} || result.ptr != cleaned.data() + cleaned.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

char detect_delimiter(std::string_view text) {
    std::size_t best_count = 0;
    char best = ',';
    const auto head = text.substr(0, std::min<std::size_t>(text.size(), 4096));
    for (const char delimiter : {'\t', ',', ';', '|'}) {
        const auto count = static_cast<std::size_t>(std::count(head.begin(), head.end(), delimiter));
        if (count > best_count) {
            best_count = count;
            best = delimiter;
        }
    }
    return best;
}

std::optional<TableData> parse_delimited_table(std::string_view text, char delimiter, bool header) {
    if (delimiter == 0) delimiter = detect_delimiter(text);
    auto records = parse_records(text, delimiter);
    if (records.size() < (header ? 2U : 1U)) return std::nullopt;
    std::size_t columns = 0;
    for (const auto& record : records) columns = std::max(columns, record.size());
    if (columns < 2) return std::nullopt;
    TableData table;
    if (header) {
        table.headers = records.front();
        records.erase(records.begin());
    }
    table.headers.resize(columns);
    for (std::size_t index = 0; index < columns; ++index) {
        if (table.headers[index].empty()) table.headers[index] = "Column " + std::to_string(index + 1);
    }
    if (records.size() > kMaxTableRows) {
        records.resize(kMaxTableRows);
        table.truncated = true;
    }
    for (auto& record : records) {
        record.resize(columns);
        table.rows.push_back(std::move(record));
    }
    compute_column_stats(table);
    return table;
}

std::optional<TableData> parse_json_table(std::string_view text) {
    if (const auto root = parse_ordered_json(text)) {
        if (root->type != OrderedJson::Type::Array || root->items.empty()) return std::nullopt;
        std::vector<const OrderedJson*> objects;
        for (const auto& item : root->items) {
            if (item.type != OrderedJson::Type::Object) return std::nullopt;
            objects.push_back(&item);
        }
        return table_from_objects(objects);
    }
    // NDJSON: one object per line.
    std::vector<OrderedJson> parsed;
    std::istringstream lines{std::string{text}};
    for (std::string line; std::getline(lines, line);) {
        if (trim(line).empty()) continue;
        auto value = parse_ordered_json(line);
        if (!value || value->type != OrderedJson::Type::Object) return std::nullopt;
        parsed.push_back(std::move(*value));
        if (parsed.size() > kMaxTableRows) break;
    }
    if (parsed.empty()) return std::nullopt;
    std::vector<const OrderedJson*> objects;
    for (const auto& value : parsed) objects.push_back(&value);
    return table_from_objects(objects);
}

void compute_column_stats(TableData& table) {
    table.stats.assign(table.headers.size(), {});
    for (std::size_t column = 0; column < table.headers.size(); ++column) {
        auto& stats = table.stats[column];
        std::set<std::string_view> distinct;
        std::size_t dates = 0;
        bool first_number = true;
        for (const auto& row : table.rows) {
            const auto& cell = row[column];
            if (cell.empty()) continue;
            ++stats.filled;
            if (distinct.size() < 10000) distinct.insert(cell);
            if (const auto number = parse_cell_number(cell)) {
                ++stats.numeric;
                stats.sum += *number;
                stats.min = first_number ? *number : std::min(stats.min, *number);
                stats.max = first_number ? *number : std::max(stats.max, *number);
                first_number = false;
            } else if (looks_like_date(cell)) {
                ++dates;
            }
        }
        stats.distinct = distinct.size();
        if (stats.numeric > 0) stats.mean = stats.sum / static_cast<double>(stats.numeric);
        if (stats.filled == 0) stats.type = ColumnType::Empty;
        else if (stats.numeric * 10 >= stats.filled * 9) stats.type = ColumnType::Number;
        else if (dates * 10 >= stats.filled * 9) stats.type = ColumnType::Date;
        else stats.type = ColumnType::Text;
    }
}

std::vector<std::size_t> table_view_rows(const TableData& table, std::string_view filter, int sort_column,
                                         bool descending) {
    std::vector<std::size_t> rows;
    const auto needle = lower(trim(filter));
    for (std::size_t index = 0; index < table.rows.size(); ++index) {
        if (needle.empty() || std::any_of(table.rows[index].begin(), table.rows[index].end(), [&](const std::string& cell) {
                return lower(cell).find(needle) != std::string::npos;
            })) {
            rows.push_back(index);
        }
    }
    if (sort_column >= 0 && static_cast<std::size_t>(sort_column) < table.headers.size()) {
        const auto column = static_cast<std::size_t>(sort_column);
        const bool numeric = table.stats.size() > column && table.stats[column].type == ColumnType::Number;
        std::stable_sort(rows.begin(), rows.end(), [&](std::size_t left, std::size_t right) {
            const auto& a = table.rows[left][column];
            const auto& b = table.rows[right][column];
            if (a.empty() != b.empty()) return !a.empty();  // blanks last either way
            bool less = false;
            if (numeric) {
                const auto x = parse_cell_number(a).value_or(0.0);
                const auto y = parse_cell_number(b).value_or(0.0);
                if (x == y) return false;
                less = x < y;
            } else {
                const auto x = lower(a);
                const auto y = lower(b);
                if (x == y) return false;
                less = x < y;
            }
            return descending ? !less : less;
        });
    }
    return rows;
}

std::string table_to_markdown(const TableData& table, const std::vector<std::size_t>& rows,
                              const std::vector<std::size_t>& columns) {
    const auto escape = [](const std::string& cell) {
        std::string out;
        for (const char ch : cell) {
            if (ch == '|') out += '\\';
            out += ch == '\n' ? ' ' : ch;
        }
        return out;
    };
    std::ostringstream out;
    out << '|';
    for (const auto column : columns) out << ' ' << escape(table.headers[column]) << " |";
    out << "\n|";
    for (const auto column : columns) {
        const bool numeric = table.stats.size() > column && table.stats[column].type == ColumnType::Number;
        out << (numeric ? " ---: |" : " --- |");
    }
    for (const auto row : rows) {
        out << "\n|";
        for (const auto column : columns) out << ' ' << escape(table.rows[row][column]) << " |";
    }
    return out.str();
}

std::string table_to_csv(const TableData& table, const std::vector<std::size_t>& rows,
                         const std::vector<std::size_t>& columns, char delimiter) {
    const auto quote = [delimiter](const std::string& cell) {
        if (cell.find_first_of(std::string{delimiter} + "\"\n\r") == std::string::npos) return cell;
        std::string out = "\"";
        for (const char ch : cell) {
            if (ch == '"') out += '"';
            out += ch;
        }
        return out + "\"";
    };
    std::ostringstream out;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        out << (index ? std::string{delimiter} : "") << quote(table.headers[columns[index]]);
    }
    for (const auto row : rows) {
        out << '\n';
        for (std::size_t index = 0; index < columns.size(); ++index) {
            out << (index ? std::string{delimiter} : "") << quote(table.rows[row][columns[index]]);
        }
    }
    return out.str();
}

std::string table_to_json(const TableData& table, const std::vector<std::size_t>& rows,
                          const std::vector<std::size_t>& columns) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < rows.size(); ++index) {
        out << (index ? ",\n  {" : "\n  {");
        for (std::size_t column_index = 0; column_index < columns.size(); ++column_index) {
            const auto column = columns[column_index];
            const auto& cell = table.rows[rows[index]][column];
            out << (column_index ? ", " : "") << json_quote(table.headers[column]) << ": ";
            const bool numeric = table.stats.size() > column && table.stats[column].type == ColumnType::Number;
            const auto number = numeric ? parse_cell_number(cell) : std::nullopt;
            if (cell.empty()) out << "null";
            else if (number && cell.find_first_of(",$%_ ") == std::string::npos) out << cell;
            else out << json_quote(cell);
        }
        out << "}";
    }
    out << (rows.empty() ? "]" : "\n]");
    return out.str();
}

std::string table_to_sql(const TableData& table, const std::vector<std::size_t>& rows,
                         const std::vector<std::size_t>& columns, std::string_view table_name) {
    std::ostringstream out;
    const auto name = sql_identifier(table_name);
    out << "CREATE TABLE " << name << " (";
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const auto column = columns[index];
        const bool numeric = table.stats.size() > column && table.stats[column].type == ColumnType::Number;
        out << (index ? ", " : "") << sql_identifier(table.headers[column]) << (numeric ? " NUMERIC" : " TEXT");
    }
    out << ");";
    for (const auto row : rows) {
        out << "\nINSERT INTO " << name << " VALUES (";
        for (std::size_t index = 0; index < columns.size(); ++index) {
            const auto column = columns[index];
            const auto& cell = table.rows[row][column];
            const bool numeric = table.stats.size() > column && table.stats[column].type == ColumnType::Number;
            out << (index ? ", " : "");
            if (cell.empty()) {
                out << "NULL";
            } else if (numeric && parse_cell_number(cell) && cell.find_first_of(",$%_ ") == std::string::npos) {
                out << cell;
            } else {
                out << '\'';
                for (const char ch : cell) out << (ch == '\'' ? "''" : std::string(1, ch));
                out << '\'';
            }
        }
        out << ");";
    }
    return out.str();
}

std::string column_type_name(ColumnType type) {
    switch (type) {
        case ColumnType::Text: return "text";
        case ColumnType::Number: return "number";
        case ColumnType::Date: return "date";
        case ColumnType::Empty: return "empty";
    }
    return "text";
}

}  // namespace pasteit
