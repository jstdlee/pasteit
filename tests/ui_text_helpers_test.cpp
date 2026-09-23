#include "ui/imgui_widgets.hpp"

#include <cassert>
#include <string>

int main() {
    assert(pastit::multiline_editor_row_count("") == 1);
    assert(pastit::multiline_editor_row_count("one\ntwo\nthree") == 3);
    assert(pastit::multiline_editor_visible_rows(std::string(100, 'x')) == 3);
    assert(pastit::multiline_editor_visible_rows("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11") == 10);
}
