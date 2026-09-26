#include "pipeline/pipeline.hpp"
#include "pipeline/process_runner.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace pastit;

namespace {

std::string run(std::string_view input, std::string_view command, PipelineOptions options = {}) {
    const auto result = run_pipeline(input, command, options);
    if (!result.ok) std::cerr << "pipeline failed: " << command << " -> " << result.error << "\n";
    assert(result.ok);
    return result.output;
}

std::string error_of(std::string_view input, std::string_view command, PipelineOptions options = {}) {
    const auto result = run_pipeline(input, command, options);
    assert(!result.ok);
    return result.error;
}

}  // namespace

int main() {
    // Tokenizing: quotes, escapes, and refusal of shell operators.
    const auto parse = parse_pipeline(R"(grep -i 'a b' | sed "s/x/\"y\"/" | cut -d \, -f 2)");
    assert(parse.error.empty() && parse.stages.size() == 3);
    assert(parse.stages[0].argv[2] == "a b" && parse.stages[1].argv[1] == "s/x/\"y\"/" && parse.stages[2].argv[2] == ",");
    assert(!parse_pipeline("sort; rm -rf /").error.empty());
    assert(!parse_pipeline("sort > out.txt").error.empty());
    assert(!parse_pipeline("echo $(whoami)").error.empty());
    assert(!parse_pipeline("sort | | uniq").error.empty());
    assert(!parse_pipeline("grep 'open").error.empty());

    // Stage editor round trip keeps quoted and escaped pipes inside a stage.
    const auto stages = split_pipeline_text(R"(grep 'a|b' | sed s/x\|y/z/ |  wc -l )");
    assert(stages.size() == 3 && stages[0] == "grep 'a|b'" && stages[1] == R"(sed s/x\|y/z/)" && stages[2] == "wc -l");
    assert(join_pipeline_stages(stages) == R"(grep 'a|b' | sed s/x\|y/z/ | wc -l)");
    assert(split_pipeline_text("  ").empty());

    const std::string words = "pear\napple\npear\nfig\napple\npear";
    assert(run(words, "sort") == "apple\napple\nfig\npear\npear\npear");
    assert(run(words, "sort -u") == "apple\nfig\npear");
    assert(run(words, "sort | uniq -c | sort -nr | head -n 1") == "      3 pear");
    assert(run(words, "sort | uniq -d") == "apple\npear");
    assert(run(words, "wc -l") == "6");
    assert(run(words, "wc") == "6 6 30");
    assert(run(words, "head -2") == "pear\napple");
    assert(run(words, "tail -n 1") == "pear");
    assert(run(words, "grep -c p") == "5");
    assert(run(words, "grep -v -n p") == "4:fig");
    assert(run("a1b22c333", "grep -o '[0-9]+'") == "1\n22\n333");
    assert(run("10\n9\n100", "sort -n -r") == "100\n10\n9");
    assert(run("b,2\na,10\nc,1", "sort -t , -k 2 -n") == "c,1\nb,2\na,10");
    assert(run("x,y,z\n1,2,3", "cut -d , -f 1,3") == "x,z\n1,3");
    assert(run("hello", "cut -c 2-4") == "ell");
    assert(run("Hello World", "tr a-z A-Z") == "HELLO WORLD");
    assert(run("a-b-c", "tr -d -") == "abc");
    assert(run("2026-09-27", R"(sed 's/(\d+)-(\d+)-(\d+)/\3.\2.\1/')") == "27.09.2026");
    assert(run("aaa", "sed s/a/b/") == "baa");
    assert(run("aaa", "sed s/a/b/g") == "bbb");
    assert(run("x\n\ny", "nl") == "     1\tx\n\n     2\ty");
    assert(run("abc\n你好", "rev") == "cba\n好你");
    assert(run("1\n2\n3", "tac") == "3\n2\n1");
    assert(run("a bb c\nddd e f", "column") == "a    bb  c\nddd  e   f");
    assert(run(" a \n b", "trim | upper") == "A\nB");
    assert(run("a\nb\nc", "join ' + '") == "a + b + c");

    // Errors are specific and nothing outside the allowlist runs.
    assert(error_of(words, "frobnicate").find("not a built-in command") != std::string::npos);
    assert(error_of(words, "grep '('").find("invalid pattern") != std::string::npos);
    assert(error_of(words, "cut -f 0").find("numbered from 1") != std::string::npos);
    assert(error_of(words, "anonymize").find("not available") != std::string::npos);

    // Custom commands expand into stages, may contain quoted |, and pass
    // extra arguments to their last stage.
    PipelineOptions custom;
    custom.custom_commands = {{"errors", "grep -i 'error|fail'"}, {"top", "sort | uniq -c | sort -nr | head -n"},
                              {"loop", "loop"}};
    assert(run("ok\nERROR x\nfailed y\nok", "errors | wc -l", custom) == "2");
    assert(run("a\nb\na\na", "top 1", custom) == "      3 a");
    assert(run("x|y", R"(sed 's/\|/+/')", custom) == "x+y");
    assert(error_of("x", "loop", custom).find("nest too deeply") != std::string::npos);

    PipelineOptions anonymizing;
    anonymizing.anonymize = [](std::string_view text) { return std::string(text.size(), '*'); };
    assert(run("abc", "anonymize", anonymizing) == "***");

    // External tools run by argv with the text on stdin, only when allowed.
    if (find_executable("awk")) {
        PipelineOptions options;
        options.allowed_tools = {"awk"};
        assert(run("a,1\nb,2\nc,3", "awk -F, '{s+=$2} END {print s}'", options) == "6");
        // Clipboard text is data: shell syntax inside it is never executed.
        assert(run("$(touch /tmp/pastit-pwned); `id`", "awk '{print length($0)}'", options) == "32");
        options.timeout = std::chrono::milliseconds{300};
        const auto slow = run_pipeline("", "awk 'BEGIN { while (1) {} }'", options);
        assert(!slow.ok && slow.error.find("timed out") != std::string::npos);
    }
    if (find_executable("jq")) {
        PipelineOptions options;
        options.allowed_tools = {"jq"};
        assert(run(R"({"b":1,"a":2})", "jq -c keys", options) == R"(["a","b"])");
    }
    assert(!find_executable("../bin/sh"));
    if (find_executable("printf")) {
        PipelineOptions any;
        assert(!run_pipeline("", "printf hi", any).ok);
        any.allow_any_program = true;
        assert(run("", "printf hi", any) == "hi");
    }
    std::cout << "pipeline ok\n";
}
