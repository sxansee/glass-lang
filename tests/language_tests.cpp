// Возможности языка «для людей»: print с несколькими значениями, преобразования типов, строки, срезы,
// отрицательные индексы, range(), методы массивов и строк, встроенные функции. Каждая проверка —
// целая программа Glass, прогнанная через весь конвейер (лексер → парсер → сема → типизатор → байткод → VM).

#include <cctype>
#include <iostream>
#include <string>

#include "../src/codegen/Compiler.h"
#include "../src/lexer/Lexer.h"
#include "../src/parser/Parser.h"
#include "../src/prelude/Prelude.h"
#include "../src/sema/BodyChecker.h"
#include "../src/sema/Sema.h"
#include "../src/vm/VM.h"
#include "test_framework.h"

using namespace glass;

namespace {

std::vector<std::string> runSource(const std::string& src) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    if (!parser.errors().empty()) return {"ERROR: parse: " + parser.errors()[0]};
    prependPrelude(prog);   // mold Exception и др. — как в glassc/IDE

    Sema sema;
    auto declErrs = sema.analyze(prog);
    if (!declErrs.empty()) return {"ERROR: sema: " + declErrs[0]};

    BodyChecker checker(sema);
    auto bodyErrs = checker.checkProgram(prog);
    if (!bodyErrs.empty()) return {"ERROR: types: " + bodyErrs[0]};

    Compiler compiler(sema);
    CompiledProgram compiled;
    try {
        compiled = compiler.compileProgram(prog);
    } catch (const std::exception& e) {
        return {std::string("ERROR: codegen: ") + e.what()};
    }

    VM vm(std::move(compiled));
    vm.run();
    if (!vm.errors().empty()) return {"ERROR: runtime: " + vm.errors()[0]};
    return vm.printedLines();
}

// Тело main(): программа из одних операторов.
std::string inMain(const std::string& body) { return "fn main() {\n" + body + "\n}\n"; }

void show(const std::vector<std::string>& lines) {
    for (auto& l : lines) std::cerr << " [" << l << "]";
    std::cerr << "\n";
}

void expectOutput(const std::string& name, const std::string& src, const std::vector<std::string>& expected) {
    auto actual = runSource(src);
    if (actual != expected) {
        std::cerr << "FAILED: " << name << "\n  ожидалось:";
        show(expected);
        std::cerr << "  получено: ";
        show(actual);
    }
    CHECK(actual == expected);
}

// Программа из одних операторов + ожидаемый вывод.
void expectMain(const std::string& name, const std::string& body, const std::vector<std::string>& expected) {
    expectOutput(name, inMain(body), expected);
}

void expectError(const std::string& name, const std::string& src, const std::string& needle) {
    auto actual = runSource(src);
    bool found = !actual.empty() && actual[0].rfind("ERROR:", 0) == 0 && actual[0].find(needle) != std::string::npos;
    if (!found) {
        std::cerr << "FAILED: " << name << " ожидал ошибку с '" << needle << "', получил:";
        show(actual);
    }
    CHECK(found);
}

void expectMainError(const std::string& name, const std::string& body, const std::string& needle) {
    expectError(name, inMain(body), needle);
}

// ---------------------------------------------------------------- print

void test_print() {
    expectMain("print_two_types", R"(print("a", 5))", {"a 5"});
    expectMain("print_many", R"(print(1, 2.5, true, "s"))", {"1 2.5 true s"});
    expectMain("print_empty", "print()", {""});
    expectMain("print_labels", R"(print("x =", 3, "y =", 4.0))", {"x = 3 y = 4.0"});
    expectMain("print_one", "print(42)", {"42"});
    expectMain("print_expression_args", "print(1 + 1, 2 * 3)", {"2 6"});
    expectMain("print_array_ints", "print([1, 2, 3])", {"[1, 2, 3]"});
    expectMain("print_array_strings_quoted", R"(print(["a", "b"]))", {"[\"a\", \"b\"]"});
    expectMain("print_nested_array", "print([[1, 2], [3]])", {"[[1, 2], [3]]"});
    expectMain("print_empty_array", "let xs: [int] = []\nprint(xs)", {"[]"});
    expectMain("print_null_array", "let xs: [int] = null\nprint(xs)", {"null"});
}

// ---------------------------------------------------------------- double и преобразования

void test_double_formatting() {
    expectMain("double_whole", "print(3.0)", {"3.0"});
    expectMain("double_fraction", "print(2.5)", {"2.5"});
    expectMain("double_negative", "print(-0.5)", {"-0.5"});
    expectMain("double_third", "print(1.0 / 3.0)", {"0.333333333333333"});
    expectMain("double_sum_noise_hidden", "print(0.1 + 0.2)", {"0.3"});
    expectMain("double_big_exponent", "print(10000000000.0 * 10000000000.0)", {"1e+20"});
    expectMain("int_stays_int", "print(7 / 2)", {"3"});
    expectMain("int_vs_double_division", "print(7 / 2.0)", {"3.5"});
}

void test_implicit_widening() {
    // раньше int в double-переменной оставался int, и d / 2 считалось как целочисленное деление
    expectMain("widen_let", "let d: double = 5\nprint(d / 2)", {"2.5"});
    expectMain("widen_assign", "let d = 1.5\nd = 4\nprint(d / 8)", {"0.5"});
    expectOutput("widen_param", "fn half(x: double) -> double { return x / 2; }\nfn main() { print(half(5)); }", {"2.5"});
    expectOutput("widen_return", "fn five() -> double { return 5; }\nfn main() { print(five() / 2); }", {"2.5"});
    expectMain("widen_array_elements", "let xs: [double] = [1, 2, 3]\nprint(xs[0] / 2)\nprint(xs)", {"0.5", "[1.0, 2.0, 3.0]"});
    expectMain("widen_mixed_array_literal", "let xs = [1, 2.5]\nprint(xs)", {"[1.0, 2.5]"});
    expectOutput("widen_field",
                 "mold P { let v: double\n P(a: double) { v = a; } }\nfn main() { let p = P(7); print(p.v / 2); }", {"3.5"});
    expectMain("widen_variable", "let n = 7\nlet d: double = n\nprint(d / 2)", {"3.5"});
    expectMain("widen_append", "let xs: [double] = [0.5]\nxs.append(2)\nprint(xs)", {"[0.5, 2.0]"});
    expectMain("widen_compound", "let d: double = 1\nd += 1\nprint(d)", {"2.0"});
}

void test_conversions() {
    expectMain("int_from_string", R"(print(int("42")))", {"42"});
    expectMain("int_from_padded_string", R"(print(int("  7 ")))", {"7"});
    expectMain("int_from_negative_string", R"(print(int("-15") + 1))", {"-14"});
    expectMain("int_truncates_double", "print(int(3.9), int(-3.9))", {"3 -3"});
    expectMain("int_from_bool", "print(int(true), int(false))", {"1 0"});
    expectMain("double_from_int", "print(double(5))", {"5.0"});
    expectMain("double_from_string", R"(print(double("2.5"), double("1e3")))", {"2.5 1000.0"});
    expectMain("double_from_bool", "print(double(true))", {"1.0"});
    expectMain("bool_from_numbers", "print(bool(0), bool(5), bool(0.0), bool(0.5))", {"false true false true"});
    expectMain("bool_from_string", R"(print(bool("true"), bool("FALSE"), bool(" True ")))", {"true false true"});
    expectMain("str_numbers", "print(str(5), str(2.5), str(true))", {"5 2.5 true"});
    expectMain("str_is_a_string", "let s = str(5)\nprint(s + s)", {"55"});
    expectMain("string_alias", "print(string(7) + \"!\")", {"7!"});
    expectMain("str_array", "print(str([1, 2]) + \"|\")", {"[1, 2]|"});
    expectMain("cast_int", "let d = 3.7\nprint((int) d)", {"3"});
    expectMain("cast_double", "let i = 3\nprint((double) i / 2)", {"1.5"});
    expectMain("cast_string", "print((string) 5 + \"x\")", {"5x"});
    expectMain("cast_int_from_string", R"(print((int) "12" + 1))", {"13"});
    expectMain("cast_same_type_is_noop", "print((int) 5, (double) 2.5)", {"5 2.5"});
    expectMain("conversion_in_arithmetic", R"(let a = "10"
let b = "32"
print(int(a) + int(b)))", {"42"});

    expectMainError("int_bad_string", R"(print(int("abc")))", "не удалось преобразовать строку");
    expectMainError("int_empty_string", R"(print(int("")))", "не удалось преобразовать строку");
    expectMainError("int_float_string", R"(print(int("2.5")))", "не удалось преобразовать строку");
    expectMainError("double_bad_string", R"(print(double("x1")))", "не удалось преобразовать строку");
    expectMainError("bool_bad_string", R"(print(bool("maybe")))", "ожидалось true или false");
    expectMainError("int_out_of_range", "print(int(10000000000.0 * 10000000000.0 * 10000000000.0))", "вне диапазона");
}

// ---------------------------------------------------------------- строки

void test_string_concat_and_repeat() {
    expectMain("concat_string_int", R"(print("x = " + 5))", {"x = 5"});
    expectMain("concat_int_string", R"(print(5 + " шт."))", {"5 шт."});
    expectMain("concat_string_double", R"(print("a" + 2.5, "b" + 3.0))", {"a2.5 b3.0"});
    expectMain("concat_string_bool", R"(print("ok: " + true))", {"ok: true"});
    expectMain("concat_chain", R"(let n = 3
print("n=" + n + ", sq=" + n * n))", {"n=3, sq=9"});
    expectMain("concat_array", R"(print("xs=" + [1, 2]))", {"xs=[1, 2]"});
    expectMain("concat_string_string", R"(print("he" + "llo"))", {"hello"});
    expectMain("concat_compound_assign", "let s = \"n=\"\ns += 5\ns += \"!\"\nprint(s)", {"n=5!"});
    expectMain("repeat_operator", R"(print("ab" * 3, "[" + "-" * 5 + "]"))", {"ababab [-----]"});
    expectMain("repeat_int_first", R"(print(2 * "xy"))", {"xyxy"});
    expectMain("repeat_zero_and_negative", R"(print("ab" * 0 + "|" + "ab" * -2 + "|"))", {"||"});
    expectMain("repeat_compound", "let s = \"ha\"\ns *= 3\nprint(s)", {"hahaha"});
}

void test_string_compare() {
    expectMain("string_less", R"(print("a" < "b", "b" < "a", "abc" < "abd"))", {"true false true"});
    expectMain("string_less_eq", R"(print("abc" <= "abc", "abd" <= "abc"))", {"true false"});
    expectMain("string_greater", R"(print("b" > "a", "a" >= "a", "a" >= "b"))", {"true true false"});
    expectMain("string_cyrillic_order", R"(print("а" < "б", "я" > "а", "Привет" == "Привет"))", {"true true true"});
    expectMain("string_prefix_is_smaller", R"(print("ab" < "abc"))", {"true"});
}

void test_string_unicode() {
    expectMain("len_ascii", R"(print(len("hello")))", {"5"});
    expectMain("len_cyrillic", R"(print(len("Привет"), "Привет".length))", {"6 6"});
    expectMain("index_cyrillic", R"(let s = "Привет"
print(s[0], s[1], s[-1]))", {"П р т"});
    expectMain("slice_cyrillic", R"(let s = "Привет"
print(s[1:4], s[:2], s[4:], s[::-1]))", {"рив Пр ет тевирП"});
    expectMain("for_over_string", R"(for c in "abc" { print(c) })", {"a", "b", "c"});
    expectMain("for_over_cyrillic_string", R"(for c in "Да!" { print(c) })", {"Д", "а", "!"});
    expectMain("upper_lower", R"(print("Hello".upper(), "Hello".lower()))", {"HELLO hello"});
    expectMain("upper_lower_cyrillic", R"(print("привет, Мир".upper(), "ПРИВЕТ, Мир".lower()))", {"ПРИВЕТ, МИР привет, мир"});
    expectMain("upper_yo", R"(print("ёжик".upper()))", {"ЁЖИК"});
}

void test_string_methods() {
    expectMain("trim", "print(\"[\" + \"  hi \\n\".trim() + \"]\")", {"[hi]"});
    expectMain("contains", R"(print("hello".contains("ell"), "hello".contains("xyz")))", {"true false"});
    expectMain("starts_ends", R"(print("hello".startsWith("he"), "hello".startsWith("lo"), "hello".endsWith("lo")))", {"true false true"});
    expectMain("index_of", R"(print("hello".indexOf("l"), "hello".indexOf("z")))", {"2 -1"});
    expectMain("index_of_cyrillic_counts_chars", R"(print("привет".indexOf("в")))", {"3"});
    expectMain("replace", R"(print("a-b-c".replace("-", "+")))", {"a+b+c"});
    expectMain("replace_with_empty", R"(print("a1b1c".replace("1", "")))", {"abc"});
    expectMain("replace_empty_pattern_is_noop", R"(print("abc".replace("", "x")))", {"abc"});
    expectMain("split", R"(let parts = "a,b,,c".split(",")
print(parts, len(parts)))", {"[\"a\", \"b\", \"\", \"c\"] 4"});
    expectMain("split_chars", R"(print("абв".split("")))", {"[\"а\", \"б\", \"в\"]"});
    expectMain("repeat_method", R"(print("ab".repeat(3), "x".repeat(0) + "|"))", {"ababab |"});
    expectMain("length_property", R"(print("abc".length))", {"3"});
    expectMain("method_on_variable", R"(let s = "  Glass  "
print(s.trim().upper()))", {"GLASS"});
    expectMain("split_then_join", R"(print("1 2 3".split(" ").join("+")))", {"1+2+3"});
}

void test_string_indexing() {
    expectMain("string_index", R"(let s = "abc"
print(s[0], s[2], s[-1], s[-3]))", {"a c c a"});
    expectMainError("string_index_out_of_range", R"(print("abc"[3]))", "вне границ строки");
    expectMainError("string_negative_out_of_range", R"(print("abc"[-4]))", "вне границ строки");
}

// ---------------------------------------------------------------- массивы: индексы и срезы

void test_negative_index() {
    expectMain("negative_index_read", "let xs = [10, 20, 30]\nprint(xs[-1], xs[-3])", {"30 10"});
    expectMain("negative_index_write", "let xs = [10, 20, 30]\nxs[-1] = 99\nxs[-3] += 1\nprint(xs)", {"[11, 20, 99]"});
    expectMainError("negative_index_out_of_range", "let xs = [1, 2, 3]\nprint(xs[-4])", "вне границ массива");
}

void test_slices() {
    const std::string setup = "let xs = [10, 20, 30, 40, 50]\n";
    expectMain("slice_range", setup + "print(xs[1:3])", {"[20, 30]"});
    expectMain("slice_head", setup + "print(xs[:2])", {"[10, 20]"});
    expectMain("slice_tail", setup + "print(xs[3:])", {"[40, 50]"});
    expectMain("slice_full_copy_is_independent", setup + "let c = xs[:]\nc[0] = 0\nprint(xs[0], c[0])", {"10 0"});
    expectMain("slice_negative_start", setup + "print(xs[-2:])", {"[40, 50]"});
    expectMain("slice_negative_stop", setup + "print(xs[:-1])", {"[10, 20, 30, 40]"});
    expectMain("slice_both_negative", setup + "print(xs[1:-1])", {"[20, 30, 40]"});
    expectMain("slice_step", setup + "print(xs[::2])", {"[10, 30, 50]"});
    expectMain("slice_reverse", setup + "print(xs[::-1])", {"[50, 40, 30, 20, 10]"});
    expectMain("slice_negative_step_range", setup + "print(xs[4:1:-1])", {"[50, 40, 30]"});
    expectMain("slice_negative_step_from_end", setup + "print(xs[-1:-4:-1])", {"[50, 40, 30]"});
    expectMain("slice_out_of_range_is_clamped", setup + "print(xs[10:20], xs[-100:2], xs[3:100])", {"[] [10, 20] [40, 50]"});
    expectMain("slice_empty_when_start_after_stop", setup + "print(xs[3:1])", {"[]"});
    expectMain("slice_with_expressions", setup + "let n = 1\nprint(xs[n:n + 2])", {"[20, 30]"});
    expectMain("slice_of_slice", setup + "print(xs[1:][1:3])", {"[30, 40]"});
    expectMain("slice_of_strings_array", R"(let w = ["a", "b", "c", "d"]
print(w[1:3]))", {"[\"b\", \"c\"]"});
    expectMain("slice_string", R"(let s = "Hello, World"
print(s[7:], s[:5], s[::-1]))", {"World Hello dlroW ,olleH"});
    expectMain("slice_string_step", R"(print("abcdef"[::2], "abcdef"[1::2]))", {"ace bdf"});
    expectMainError("slice_step_zero", setup + "print(xs[::0])", "шаг среза");
    expectMainError("slice_non_int_bound", setup + "print(xs[1:\"a\"])", "должны быть 'int'");
    expectMainError("slice_of_int", "let n = 5\nprint(n[1:2])", "не поддерживает срезы");
}

// ---------------------------------------------------------------- range и for

void test_range() {
    expectMain("range_one_arg", "for i in range(3) { print(i) }", {"0", "1", "2"});
    expectMain("range_two_args", "for i in range(2, 5) { print(i) }", {"2", "3", "4"});
    expectMain("range_three_args", "for i in range(0, 10, 3) { print(i) }", {"0", "3", "6", "9"});
    expectMain("range_negative_step", "for i in range(5, 0, -2) { print(i) }", {"5", "3", "1"});
    expectMain("range_zero", "for i in range(0) { print(i) }\nprint(\"end\")", {"end"});
    expectMain("range_empty_when_wrong_direction", "for i in range(5, 2) { print(i) }\nprint(\"end\")", {"end"});
    expectMain("range_with_variable", "let n = 3\nfor i in range(n + 1) { print(i) }", {"0", "1", "2", "3"});
    expectMain("range_as_value", "let r = range(4)\nprint(r, len(r))", {"[0, 1, 2, 3] 4"});
    expectMain("range_three_args_as_value", "print(range(1, 10, 4))", {"[1, 5, 9]"});
    expectMain("range_sum", "print(sum(range(5)), sum(range(1, 101)))", {"10 5050"});
    expectMain("range_loop_var_modification_does_not_break_iteration",
               "for i in range(3) { i += 10\nprint(i) }", {"10", "11", "12"});
    expectMain("dotdot_loop_var_modification_does_not_break_iteration",
               "for i in 0..3 { i += 10\nprint(i) }", {"10", "11", "12"});
    expectMain("dotdot_range", "for i in 0..3 { print(i) }", {"0", "1", "2"});
    expectMain("dotdot_with_expression", "let n = 3\nfor i in 1..n + 1 { print(i) }", {"1", "2", "3"});
    expectMain("for_with_length", "let xs = [5, 6, 7]\nfor i in 0..xs.length { print(i, xs[i]) }", {"0 5", "1 6", "2 7"});
    expectMain("for_range_len", "let xs = [5, 6]\nfor i in range(len(xs)) { print(xs[i]) }", {"5", "6"});
    expectMain("for_over_array", "for x in [10, 20] { print(x) }", {"10", "20"});
    expectMain("for_over_range_value", "for x in range(2, 4) { print(x * x) }", {"4", "9"});
    expectMain("for_over_array_variable_modification", "for x in [1, 2, 3] { x += 100\nprint(x) }", {"101", "102", "103"});
    expectMain("for_break_continue_one_line",
               "for i in range(6) {\n  if i == 4 { break }\n  if i % 2 == 0 { continue }\n  print(i)\n}", {"1", "3"});
    expectMain("for_nested", "for i in range(2) { for j in range(2) { print(i, j) } }", {"0 0", "0 1", "1 0", "1 1"});
    expectOutput("range_user_function_wins",
                 "fn range(n: int) -> int { return n * 100; }\nfn main() { print(range(5)); }", {"500"});
    expectMainError("range_step_zero", "for i in range(0, 5, 0) { print(i) }", "шаг range()");
    expectMainError("range_double_arg", "print(range(1.5))", "не совместим с ожидаемым 'int'");
    expectMainError("range_no_args", "print(range())", "ожидает от 1 до 3 аргументов");
}

// ---------------------------------------------------------------- методы массивов

void test_array_methods() {
    expectMain("append", "let xs = [1, 2]\nxs.append(3)\nprint(xs, xs.length)", {"[1, 2, 3] 3"});
    expectMain("append_to_empty", "let xs: [int] = []\nxs.append(7)\nxs.append(8)\nprint(xs)", {"[7, 8]"});
    expectMain("append_strings", R"(let w = ["a"]
w.append("b")
print(w))", {"[\"a\", \"b\"]"});
    expectMain("pop_last", "let xs = [1, 2, 3]\nprint(xs.pop(), xs)", {"3 [1, 2]"});
    expectMain("pop_index", "let xs = [1, 2, 3]\nprint(xs.pop(0), xs)", {"1 [2, 3]"});
    expectMain("pop_negative_index", "let xs = [1, 2, 3]\nprint(xs.pop(-2), xs)", {"2 [1, 3]"});
    expectMainError("pop_empty", "let xs: [int] = []\nprint(xs.pop())", "пустого массива");
    expectMainError("pop_bad_index", "let xs = [1]\nprint(xs.pop(5))", "вне границ");
    expectMain("insert", "let xs = [1, 3]\nxs.insert(1, 2)\nxs.insert(0, 0)\nprint(xs)", {"[0, 1, 2, 3]"});
    expectMain("insert_clamps", "let xs = [1, 2]\nxs.insert(100, 3)\nxs.insert(-100, 0)\nprint(xs)", {"[0, 1, 2, 3]"});
    expectMain("insert_negative_goes_before_last", "let xs = [1, 3]\nxs.insert(-1, 2)\nprint(xs)", {"[1, 2, 3]"});
    expectMain("remove_first_match", "let xs = [1, 2, 1]\nxs.remove(1)\nprint(xs)", {"[2, 1]"});
    expectMainError("remove_missing", "let xs = [1]\nxs.remove(9)", "не найдено");
    expectMain("contains", "let xs = [1, 2, 3]\nprint(xs.contains(2), xs.contains(9))", {"true false"});
    expectMain("contains_strings", R"(print(["a", "b"].contains("b")))", {"true"});
    expectMain("index_of", "let xs = [5, 6, 7]\nprint(xs.indexOf(7), xs.indexOf(1))", {"2 -1"});
    expectMain("reverse", "let xs = [1, 2, 3]\nxs.reverse()\nprint(xs)", {"[3, 2, 1]"});
    expectMain("sort_ints", "let xs = [3, 1, 2]\nxs.sort()\nprint(xs)", {"[1, 2, 3]"});
    expectMain("sort_doubles", "let xs = [2.5, 0.5, 1.5]\nxs.sort()\nprint(xs)", {"[0.5, 1.5, 2.5]"});
    expectMain("sort_strings", R"(let w = ["pear", "apple", "fig"]
w.sort()
print(w))", {"[\"apple\", \"fig\", \"pear\"]"});
    expectMain("sort_is_stable_for_equal", "let xs = [2, 2, 1]\nxs.sort()\nprint(xs)", {"[1, 2, 2]"});
    expectMain("clear", "let xs = [1, 2]\nxs.clear()\nprint(xs, xs.length)", {"[] 0"});
    expectMain("join_strings", R"(print(["a", "b", "c"].join(", ")))", {"a, b, c"});
    expectMain("join_numbers", "print([1, 2, 3].join(\"-\"))", {"1-2-3"});
    expectMain("join_empty", "let xs: [string] = []\nprint(\"[\" + xs.join(\",\") + \"]\")", {"[]"});
    expectMain("array_plus_array", "let a = [1, 2]\nlet b = [3]\nlet c = a + b\nc[0] = 9\nprint(a, b, c)", {"[1, 2] [3] [9, 2, 3]"});
    expectMain("array_compound_add", "let a = [1]\na += [2, 3]\nprint(a)", {"[1, 2, 3]"});
    expectMain("length_property_and_len", "let xs = [1, 2, 3]\nprint(xs.length, len(xs))", {"3 3"});
    expectMain("methods_chain_via_slice", "let xs = [3, 1, 2]\nlet s = xs[:]\ns.sort()\nprint(xs, s)", {"[3, 1, 2] [1, 2, 3]"});
    expectMain("method_on_call_result", R"(print("a b c".split(" ").length))", {"3"});
    expectMain("loop_with_append", "let sq: [int] = []\nfor i in range(1, 5) { sq.append(i * i) }\nprint(sq)", {"[1, 4, 9, 16]"});
    expectOutput("array_passed_to_function",
                 "fn fill(xs: [int], n: int) { for i in range(n) { xs.append(i) } }\n"
                 "fn main() { let xs: [int] = []\nfill(xs, 3)\nprint(xs) }", {"[0, 1, 2]"});
}

// ---------------------------------------------------------------- функции и константы

void test_builtin_functions() {
    expectMain("len_array_string", R"(print(len([1, 2]), len("abc"), len("")))", {"2 3 0"});
    expectMain("min_max_ints", "print(min(3, 5), max(3, 5))", {"3 5"});
    expectMain("min_max_mixed_become_double", "print(max(1, 2.5), max(3, 2.5), min(2, 0.5), min(1, 2.5))", {"2.5 3.0 0.5 1.0"});
    expectMain("min_max_array", "print(min([4, 2, 9]), max([4, 2, 9]), max([1.5, 0.5]))", {"2 9 1.5"});
    expectMain("sum_ints", "print(sum([1, 2, 3]))", {"6"});
    expectMain("sum_doubles", "print(sum([0.5, 0.25]))", {"0.75"});
    expectMain("sum_empty_ints", "let e: [int] = []\nprint(sum(e))", {"0"});
    expectMain("sum_empty_doubles_is_double", "let e: [double] = []\nprint(sum(e))", {"0.0"});
    expectMain("average", "let xs = [1, 2, 3, 4]\nprint(sum(xs) / len(xs), double(sum(xs)) / len(xs))", {"2 2.5"});
    expectMain("abs", "print(abs(-3), abs(3), abs(-2.5))", {"3 3 2.5"});
    expectMain("sqrt_pow", "print(sqrt(16), sqrt(2) * sqrt(2) > 1.99, pow(2, 10), pow(2, 0.5) > 1.41)", {"4.0 true 1024.0 true"});
    expectMain("floor_ceil_round", "print(floor(2.7), ceil(2.1), round(2.4), round(2.5), round(-2.5), floor(-2.1))", {"2 3 2 3 -3 -3"});
    expectMain("floor_of_int", "print(floor(5), ceil(5), round(5))", {"5 5 5"});
    expectMain("constants", "print(PI > 3.14, PI < 3.15, E > 2.71, E < 2.72)", {"true true true true"});
    expectMain("circle_area", "let r = 2\nprint(round(PI * r * r * 100))", {"1257"});
    expectMainError("min_empty", "let e: [int] = []\nprint(min(e))", "пустого массива");
    expectMainError("sqrt_negative", "print(sqrt(-1))", "отрицательного");
    expectOutput("user_function_shadows_builtin",
                 "fn max(a: int, b: int) -> int { return a * 1000 + b; }\nfn main() { print(max(1, 2)); }", {"1002"});
}

// ---------------------------------------------------------------- парсер: ';' перед '}'

void test_optional_semicolon_before_brace() {
    expectOutput("one_line_main", "fn main() { print(1) }", {"1"});
    expectMain("one_line_if", "if true { print(1) }", {"1"});
    expectMain("one_line_while_break", "let i = 0\nwhile true { i += 1\nif i == 3 { break } }\nprint(i)", {"3"});
    expectMain("one_line_continue", "let s = 0\nfor i in range(5) { if i % 2 == 0 { continue }\ns += i }\nprint(s)", {"4"});
    expectOutput("one_line_return", "fn f(x: int) -> int { if x > 0 { return 1 } return 0 }\nfn main() { print(f(5), f(-5)) }", {"1 0"});
    expectOutput("one_line_return_void", "fn f(x: int) { if x > 0 { return } print(\"neg\") }\nfn main() { f(1)\nf(-1) }", {"neg"});
    expectOutput("one_line_ctor", "mold A { let x: int\n  A(v: int) { x = v } }\nfn main() { let a = A(4)\nprint(a.x) }", {"4"});
    expectMain("one_line_throw_catch", "try { throw Exception(\"boom\") } catch e: Exception { print(e.message) }", {"boom"});
}

// ---------------------------------------------------------------- ошибки типов (до запуска)

void test_type_errors() {
    expectMainError("len_of_int", "print(len(5))", "len() ожидает массив или строку");
    expectMainError("len_two_args", "print(len([1], [2]))", "ожидает 1 аргумент, передано 2");
    expectMainError("int_no_args", "print(int())", "ожидает 1 аргумент, передано 0");
    expectMainError("int_of_array", "print(int([1]))", "принимает int, double, bool или string");
    expectMainError("array_unknown_method", "let xs = [1]\nxs.foo()", "у массива нет метода 'foo'");
    expectMainError("array_length_as_method", "let xs = [1]\nprint(xs.length())", "длина — свойство");
    expectMainError("string_unknown_method", "let s = \"a\"\ns.foo()", "у строки нет метода 'foo'");
    expectMainError("int_has_no_methods", "let n = 5\nn.foo()", "у значений типа 'int' нет методов");
    expectMainError("int_has_no_fields", "let n = 5\nprint(n.value)", "у значений типа 'int' нет полей");
    expectMainError("array_has_no_other_fields", "let xs = [1]\nprint(xs.size)", "у массива нет поля 'size'");
    expectMainError("append_wrong_type", "let xs = [1]\nxs.append(\"a\")", "не совместим");
    expectMainError("append_two_args", "let xs = [1]\nxs.append(1, 2)", "ожидает 1 аргумент");
    expectMainError("contains_wrong_type", "let xs = [1]\nprint(xs.contains(\"a\"))", "не совместим");
    expectMainError("sort_of_arrays", "let xs = [[1], [2]]\nxs.sort()", "умеет сортировать массивы int, double и string");
    expectMainError("mixed_array_literal", "let xs = [1, \"a\"]", "не согласуется");
    expectMainError("string_minus_int", "print(\"a\" - 1)", "требует числовые операнды");
    expectMainError("string_times_double", "print(\"a\" * 2.5)", "требует числовые операнды");
    expectMainError("index_by_double", "let xs = [1]\nprint(xs[1.5])", "индекс должен быть 'int'");
    expectMainError("index_by_string", "let xs = [1]\nprint(xs[\"a\"])", "индекс должен быть 'int'");
    expectMainError("index_of_int", "let n = 5\nprint(n[0])", "не поддерживает индексацию");
    expectMainError("print_void", "let x = 1\nprint(print(x))", "нельзя напечатать значение типа void");
    expectMainError("concat_void", "let s = \"a\" + print(1)", "нельзя склеить строку со значением типа void");
    expectMainError("cast_array_to_int", "let xs = [1]\nprint((int) xs)", "нельзя привести");
    expectMainError("int_plus_equals_double", "let x = 1\nx += 1.5", "не определён для");
    expectMainError("string_plus_equals_void", "let s = \"a\"\ns += print(1)", "не определён для");
    expectMainError("shadowed_builtin_call", "let len = 3\nprint(len([1]))", "переименуйте её");
    expectMainError("for_over_int", "for i in 5 { print(i) }", "ожидает массив, строку или диапазон");
    expectMainError("min_one_non_array", "print(min(5))", "ожидает массив чисел");
    expectMainError("abs_of_string", "print(abs(\"a\"))", "ожидает число");
    expectMainError("pow_one_arg", "print(pow(2))", "ожидает 2 аргумента, передано 1");
}

// ---------------------------------------------------------------- маленькие программы целиком

void test_whole_programs() {
    expectOutput("fizzbuzz", R"(fn main() {
    for i in range(1, 16) {
        if i % 15 == 0 {
            print("FizzBuzz")
        } else if i % 3 == 0 {
            print("Fizz")
        } else if i % 5 == 0 {
            print("Buzz")
        } else {
            print(i)
        }
    }
})", {"1", "2", "Fizz", "4", "Buzz", "Fizz", "7", "8", "Fizz", "Buzz", "11", "Fizz", "13", "14", "FizzBuzz"});
    expectOutput("average_of_list", R"(fn average(xs: [int]) -> double {
    return double(sum(xs)) / len(xs)
}
fn main() {
    let marks = [5, 4, 5, 3]
    print("средний балл:", average(marks))
})", {"средний балл: 4.25"});
    expectOutput("palindrome", R"(fn isPalindrome(s: string) -> bool {
    return s == s[::-1]
}
fn main() {
    print(isPalindrome("level"), isPalindrome("glass"), isPalindrome("шалаш"))
})", {"true false true"});
    expectOutput("word_count", R"(fn main() {
    let text = "to be or not to be"
    let words = text.split(" ")
    print(len(words), words[0], words[-1])
})", {"6 to be"});
    expectOutput("celsius_table", R"(fn main() {
    for c in range(0, 41, 20) {
        let f = c * 9 / 5 + 32
        print(c, "C =", f, "F")
    }
})", {"0 C = 32 F", "20 C = 68 F", "40 C = 104 F"});
    expectOutput("string_builder", R"(fn main() {
    let line = ""
    for i in range(1, 6) {
        line += i
        if i < 5 { line += "," }
    }
    print(line)
})", {"1,2,3,4,5"});
    expectOutput("bubble_sort", R"(fn main() {
    let xs = [5, 2, 9, 1]
    for i in range(len(xs)) {
        for j in range(len(xs) - 1 - i) {
            if xs[j] > xs[j + 1] {
                let t = xs[j]
                xs[j] = xs[j + 1]
                xs[j + 1] = t
            }
        }
    }
    print(xs)
})", {"[1, 2, 5, 9]"});
    expectOutput("object_array_slice", R"(mold B {
    let v: int
    B(x: int) { v = x }
}
fn main() {
    let xs = [B(1), B(2), B(3)]
    let part = xs[1:]
    part[0].v = 20
    print(len(part), xs[1].v)
})", {"2 20"});
}


// ---------------------------------------------------------------------
// else / catch / finally на новой строке после '}' (ASI не вставляет ';')
// ---------------------------------------------------------------------

void test_else_on_new_line() {
    expectMain("else_new_line", R"GL(
    let x = 5
    if x > 3 {
        print("big")
    }
    else {
        print("small")
    }
    if x > 10 {
        print("huge")
    }
    else if x > 4 {
        print("medium")
    }
    else {
        print("tiny")
    })GL", {"big", "medium"});

    expectMain("else_new_line_comment_between", R"GL(
    if false {
        print("a")
    }
    // комментарий между закрывающей скобкой и else
    /* и блочный */
    else {
        print("b")
    })GL", {"b"});

    expectMain("else_new_line_nested", R"GL(
    let n = 7
    if n > 0 {
        if n > 5 {
            print("A")
        }
        else {
            print("B")
        }
    }
    else {
        print("C")
    })GL", {"A"});

    expectMain("else_same_line_still_works", R"GL(
    if 1 > 2 { print("x") } else { print("y") })GL", {"y"});

    // Слово только НАЧИНАЕТСЯ с else/catch/finally — это обычный идентификатор, ';' обязана вставиться.
    expectMain("identifier_starting_with_else", R"GL(
    if true {
        print("a")
    }
    let elsewhere = 5
    let finallyDone = 6
    print(elsewhere + finallyDone))GL", {"a", "11"});

    expectOutput("try_catch_finally_new_lines", R"GL(
mold Boom : Exception {
    Boom(msg: string) { super(msg) }
}
fn main() {
    try {
        throw Boom("x")
    }
    catch e: Boom {
        print("поймано", e.message)
    }
    finally {
        print("финал")
    }
})GL", {"поймано x", "финал"});

    // Висячий else без if по-прежнему ошибка, а не молчаливое проглатывание.
    expectError("dangling_else_is_error", inMain(R"GL(
    while false {
        print("x")
    }
    else {
        print("y")
    })GL"), "else");
}

// ---------------------------------------------------------------------
// print(объект): Класс@0x… и пользовательский toString()
// ---------------------------------------------------------------------

bool isObjectDisplay(const std::string& text, const std::string& className) {
    const std::string prefix = className + "@0x";
    if (text.rfind(prefix, 0) != 0 || text.size() == prefix.size()) return false;
    for (size_t i = prefix.size(); i < text.size(); i++) {
        if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    return true;
}

const char* kToStringPrelude = R"GL(
mold Animal {
    let name: string
    Animal(n: string) { name = n }
    fn toString() -> string { return "Animal(" + name + ")" }
}
mold Puppy : Animal {
    Puppy(n: string) { super(n) }
    override fn toString() -> string { return "Puppy " + name + " / " + super.toString() }
}
mold Plain {
    let v: int
    Plain(x: int) { v = x }
}
)GL";

void test_print_objects_and_tostring() {
    {
        auto out = runSource("mold Dog {\n    let name: string\n    Dog(n: string) { name = n }\n}\n" +
                             inMain("let d = Dog(\"Рекс\")\nprint(d)\nprint(\"x:\", d)"));
        CHECK_EQ(out.size(), static_cast<size_t>(2));
        if (out.size() == 2) {
            CHECK(isObjectDisplay(out[0], "Dog"));
            CHECK(out[1].rfind("x: Dog@0x", 0) == 0);
        }
    }
    {
        // Тот же адрес, что и у ~address (в hex): print показывает тот самый указатель.
        auto out = runSource("mold Dog {\n    Dog() {}\n}\n" +
                             inMain("let d = Dog()\nprint(d)\nprint(d~address)"));
        CHECK_EQ(out.size(), static_cast<size_t>(2));
        if (out.size() == 2 && isObjectDisplay(out[0], "Dog")) {
            unsigned long long hex = std::stoull(out[0].substr(std::string("Dog@0x").size()), nullptr, 16);
            CHECK_EQ(std::to_string(hex), out[1]);
        }
    }

    const std::string pre = kToStringPrelude;
    expectOutput("tostring_print", pre + inMain("print(Animal(\"Мурка\"))"), {"Animal(Мурка)"});
    expectOutput("tostring_virtual_through_base_type",
                 pre + inMain("let a: Animal = Puppy(\"Бим\")\nprint(a)"), {"Puppy Бим / Animal(Бим)"});
    expectOutput("tostring_in_concat_both_sides",
                 pre + inMain("let a = Animal(\"Тузик\")\nprint(\"а: \" + a)\nprint(a + \" !\")\nprint(\"[\" + a + \"]\")"),
                 {"а: Animal(Тузик)", "Animal(Тузик) !", "[Animal(Тузик)]"});
    expectOutput("tostring_in_plus_assign",
                 pre + inMain("let s = \"итог: \"\ns += Puppy(\"Бим\")\nprint(s)"), {"итог: Puppy Бим / Animal(Бим)"});
    expectOutput("tostring_str_and_cast",
                 pre + inMain("let a = Animal(\"Ы\")\nprint(str(a), (string) a)"), {"Animal(Ы) Animal(Ы)"});
    expectOutput("tostring_explicit_call_is_virtual",
                 pre + inMain("let a: Animal = Puppy(\"Бим\")\nprint(a.toString())"), {"Puppy Бим / Animal(Бим)"});
    expectOutput("tostring_several_args",
                 pre + inMain("print(1, Animal(\"A\"), \"s\", Puppy(\"B\"))"),
                 {"1 Animal(A) s Puppy B / Animal(B)"});
    expectOutput("tostring_null_prints_null",
                 pre + inMain("let a: Animal = null\nprint(a, \"и\", a + \"!\")"), {"null и null!"});
    expectOutput("tostring_via_facet",
                 R"GL(
facet Named {
    fn toString() -> string;
}
mold Cat : Named {
    fn toString() -> string { return "кот" }
}
mold Fox : Named {
    fn toString() -> string { return "лис" }
}
fn show(n: Named) { print("это " + n) }
fn main() {
    show(Cat())
    show(Fox())
})GL", {"это кот", "это лис"});
    expectOutput("tostring_numbers_inside",
                 R"GL(
mold P {
    let x: int
    let y: double
    P(a: int, b: double) { x = a
        y = b }
    fn toString() -> string { return "(" + x + ", " + y + ")" }
}
fn main() { print(P(3, 1.5)) })GL", {"(3, 1.5)"});
    // Без toString — обычный Класс@0x…; рядом с классом, у которого он есть, это не мешает.
    {
        auto out = runSource(pre + inMain("print(Plain(1))\nprint(Animal(\"Z\"))"));
        CHECK_EQ(out.size(), static_cast<size_t>(2));
        if (out.size() == 2) {
            CHECK(isObjectDisplay(out[0], "Plain"));
            CHECK_EQ(out[1], std::string("Animal(Z)"));
        }
    }
    // toString создаёт объекты внутри — сборщик мусора не должен съесть ни получателя, ни результат.
    expectOutput("tostring_survives_gc",
                 R"GL(
mold Tmp {
    let v: int
    Tmp(x: int) { v = x }
}
mold Item {
    let id: int
    Item(i: int) { id = i }
    fn toString() -> string {
        let t = Tmp(id * 2)
        let pad = Tmp(1)
        return "item#" + t.v
    }
}
fn main() {
    let i = 0
    let last = ""
    while i < 3000 {
        last = Item(i) + ""
        i += 1
    }
    print(last)
})GL", {"item#5998"});

    // Договорённость языка проверяется: toString с другой сигнатурой — ошибка, а не тихо игнорируемый метод.
    expectError("tostring_wrong_signature",
                "mold Bad {\n    fn toString(x: int) -> int { return x }\n}\nfn main() { }\n", "toString");
    expectError("tostring_wrong_return",
                "mold Bad {\n    fn toString() -> int { return 1 }\n}\nfn main() { }\n", "toString");
}

// ---------------------------------------------------------------------
// Приведение ссылочных типов проверяется по реальному классу объекта
// ---------------------------------------------------------------------

const char* kCastPrelude = R"GL(
facet Shape {
    fn area() -> double;
}
mold Circle : Shape {
    fn area() -> double { return 3.0 }
    fn radius() -> int { return 1 }
}
mold Square : Shape {
    fn area() -> double { return 4.0 }
}
mold Base {
    virtual fn who() -> string { return "base" }
}
mold Mid : Base {
    override fn who() -> string { return "mid" }
}
mold Leaf : Mid {
    override fn who() -> string { return "leaf" }
    fn only() -> string { return "только у Leaf" }
}
mold Cat : Base {
    override fn who() -> string { return "cat" }
}
)GL";

void test_reference_casts() {
    const std::string pre = kCastPrelude;
    expectOutput("cast_downcast_ok", pre + inMain("let b: Base = Leaf()\nlet l = (Leaf) b\nprint(l.only())"),
                 {"только у Leaf"});
    expectOutput("cast_downcast_through_middle",
                 pre + inMain("let b: Base = Leaf()\nlet m = (Mid) b\nprint(m.who())"), {"leaf"});
    expectOutput("cast_upcast_ok", pre + inMain("let l = Leaf()\nlet b = (Base) l\nprint(b.who())"), {"leaf"});
    expectOutput("cast_to_same_type_ok", pre + inMain("let l = Leaf()\nprint(((Leaf) l).only())"), {"только у Leaf"});
    expectOutput("cast_in_expression", pre + inMain("let b: Base = Leaf()\nprint(((Leaf) b).only())"),
                 {"только у Leaf"});
    expectOutput("cast_null_passes", pre + inMain("let b: Base = null\nlet l = (Leaf) b\nprint(l == null)"), {"true"});
    expectOutput("cast_facet_to_mold_ok",
                 pre + inMain("let s: Shape = Circle()\nlet c = (Circle) s\nprint(c.radius())"), {"1"});
    expectOutput("cast_mold_to_facet_ok",
                 pre + inMain("let c = Circle()\nlet s = (Shape) c\nprint(s.area())"), {"3.0"});

    expectError("cast_wrong_class_runtime",
                pre + inMain("let b: Base = Cat()\nlet l = (Leaf) b\nprint(\"недостижимо\")"),
                "нельзя привести объект класса 'Cat' к 'Leaf'");
    expectError("cast_base_object_to_derived_runtime",
                pre + inMain("let b: Base = Base()\nlet m = (Mid) b"), "нельзя привести объект класса 'Base' к 'Mid'");
    expectError("cast_facet_wrong_implementor_runtime",
                pre + inMain("let s: Shape = Square()\nlet c = (Circle) s"),
                "нельзя привести объект класса 'Square' к 'Circle'");
    // Вывод до ошибки сохраняется — программа останавливается именно на приведении.
    {
        Lexer lexer(pre + inMain("print(\"до\")\nlet b: Base = Cat()\nlet l = (Leaf) b\nprint(\"после\")"));
        Parser parser(lexer.tokenize());
        Program prog = parser.parseProgram();
        prependPrelude(prog);
        Sema sema;
        sema.analyze(prog);
        BodyChecker checker(sema);
        checker.checkProgram(prog);
        Compiler compiler(sema);
        VM vm(compiler.compileProgram(prog));
        vm.run();
        CHECK_EQ(vm.printedLines().size(), static_cast<size_t>(1));
        CHECK(!vm.errors().empty());
    }

    // Статически невозможные приведения отсекаются на этапе типов.
    expectError("cast_unrelated_molds_static", pre + inMain("let l = Leaf()\nlet c = (Square) l"), "не связаны наследованием");
    expectError("cast_unrelated_siblings_static", pre + inMain("let c: Cat = Cat()\nlet l = (Leaf) c"),
                "не связаны наследованием");
    expectError("cast_int_to_mold_static", pre + inMain("let x = (Leaf) 5"), "нельзя привести 'int' к 'Leaf'");
    expectError("cast_string_to_mold_static", pre + inMain("let x = (Leaf) \"s\""), "нельзя привести 'string' к 'Leaf'");
    expectError("cast_unknown_type", pre + inMain("let b = Base()\nlet x = (Nope) b"), "Nope");
}

// ---------------------------------------------------------------------
// Правила диспетчеризации: virtual/override, сокрытие, facet
// ---------------------------------------------------------------------

void test_dispatch_rules() {
    expectOutput("override_over_virtual_dispatches_by_real_class", R"GL(
mold Base { virtual fn who() -> string { return "base" } }
mold Leaf : Base { override fn who() -> string { return "leaf" } }
fn main() {
    let b: Base = Leaf()
    let l = Leaf()
    print(b.who(), l.who())
})GL", {"leaf leaf"});

    // Обычный (не virtual) метод связывается по СТАТИЧЕСКОМУ типу переменной — сокрытие, как в C#.
    expectOutput("plain_hiding_binds_by_static_type", R"GL(
mold Base { fn who() -> string { return "base" } }
mold Leaf : Base { fn who() -> string { return "leaf" } }
fn main() {
    let b: Base = Leaf()
    let l = Leaf()
    print(b.who(), l.who())
})GL", {"base leaf"});

    expectOutput("override_over_facet_implementation_dispatches", R"GL(
facet Shape { fn area() -> double; }
mold Circle : Shape { fn area() -> double { return 3.0 } }
mold Big : Circle { override fn area() -> double { return 9.0 } }
fn main() {
    let s: Shape = Big()
    let c: Circle = Big()
    print(s.area(), c.area())
})GL", {"9.0 9.0"});

    expectError("override_over_plain_method_rejected", R"GL(
mold Base { fn who() -> string { return "base" } }
mold Leaf : Base { override fn who() -> string { return "leaf" } }
fn main() { })GL", "не virtual");

    expectError("missing_override_rejected", R"GL(
mold Base { virtual fn who() -> string { return "base" } }
mold Leaf : Base { fn who() -> string { return "leaf" } }
fn main() { })GL", "добавьте override");
}

// ---------------------------------------------------------------------
// «Все пути возвращают значение»: функция с '-> тип' обязана вернуть значение на любой ветке
// ---------------------------------------------------------------------

void test_all_paths_return() {
    // Допустимые формы: программа компилируется и считает правильно.
    expectOutput("return_if_else_if_else_chain", R"GL(
fn sign(x: int) -> int {
    if x > 0 {
        return 1
    } else if x < 0 {
        return -1
    } else {
        return 0
    }
}
fn main() { print(sign(5), sign(-3), sign(0)) })GL", {"1 -1 0"});

    expectOutput("return_after_if_is_enough", R"GL(
fn pick(x: int) -> int {
    if x > 0 { return 1 }
    return 0
}
fn main() { print(pick(4), pick(-4)) })GL", {"1 0"});

    expectOutput("return_while_true_with_return_inside", R"GL(
fn firstOver(limit: int) -> int {
    let i = 0
    while true {
        if i * i > limit { return i }
        i += 1
    }
}
fn main() { print(firstOver(50)) })GL", {"8"});

    // break вложенного for относится к for, а не к while true: цикл по-прежнему бесконечен.
    expectOutput("return_inner_loop_break_is_not_ours", R"GL(
fn find(xs: [int], target: int) -> int {
    while true {
        for x in xs {
            if x == target { break }
        }
        return 7
    }
}
fn main() { print(find([1, 2, 3], 2)) })GL", {"7"});

    expectOutput("return_try_and_all_catches_return", R"GL(
mold Boom : Exception {
    Boom(msg: string) { super(msg) }
}
fn risky(x: int) {
    if x > 2 { throw Boom("big") }
}
fn tryIt(x: int) -> string {
    try {
        risky(x)
        return "ok"
    } catch e: Boom {
        return "caught " + e.message
    }
}
fn main() { print(tryIt(1), tryIt(5)) })GL", {"ok caught big"});

    expectOutput("return_throw_at_the_end", R"GL(
fn fail(msg: string) -> int {
    throw Exception(msg)
}
fn main() {
    try {
        print(fail("не получилось"))
    } catch e: Exception {
        print("поймано:", e.message)
    }
})GL", {"поймано: не получилось"});

    expectOutput("return_in_method_all_branches", R"GL(
mold Shape {
    let sides: int
    Shape(n: int) { sides = n }
    fn kind() -> string {
        if sides == 3 {
            return "треугольник"
        } else if sides == 4 {
            return "четырёхугольник"
        } else {
            return "многоугольник"
        }
    }
}
fn main() { print(Shape(3).kind(), Shape(4).kind(), Shape(9).kind()) })GL",
                 {"треугольник четырёхугольник многоугольник"});

    // Ничего не возвращающие функции, методы и конструкторы проверка не касается.
    expectOutput("return_void_function_unaffected", R"GL(
mold Box {
    let n: int
    Box(v: int) { if v > 0 { n = v } }
    fn show() { if n > 0 { print("n =", n) } }
}
fn g(x: int) { if x > 0 { print("g", x) } }
fn main() { g(2)
    Box(3).show() })GL", {"g 2", "n = 3"});

    // Недопустимые формы.
    const std::string expected = "не все ветки функции 'pick' возвращают значение типа int";
    expectError("return_missing_if_without_else", R"GL(
fn pick(x: int) -> int {
    if x > 0 { return 1 }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_else_if_chain_without_else", R"GL(
fn pick(x: int) -> int {
    if x > 0 { return 1 } else if x < 0 { return -1 }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_else_branch_does_not_return", R"GL(
fn pick(x: int) -> int {
    if x > 0 { return 1 } else { print("нет") }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_for_may_not_run", R"GL(
fn pick(xs: [int]) -> int {
    for x in xs { return x }
}
fn main() { print(pick([1])) })GL", "не все ветки функции 'pick' возвращают значение типа int");

    expectError("return_missing_while_with_condition", R"GL(
fn pick(x: int) -> int {
    while x > 0 { return 1 }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_while_true_with_break", R"GL(
fn pick(x: int) -> int {
    while true {
        if x > 0 { break }
        return 5
    }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_break_inside_try_in_loop", R"GL(
fn pick(x: int) -> int {
    while true {
        try {
            if x > 0 { break }
        } catch e: Exception {
            return 2
        }
    }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_catch_does_not_return", R"GL(
fn pick(x: int) -> int {
    try {
        return 1
    } catch e: Exception {
        print("ошибка")
    }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_try_does_not_return", R"GL(
fn pick(x: int) -> int {
    try {
        print("почти")
    } catch e: Exception {
        return 2
    }
}
fn main() { print(pick(1)) })GL", expected);

    expectError("return_missing_empty_body", R"GL(
fn pick(x: int) -> int { }
fn main() { print(pick(1)) })GL", expected);

    // В методе сообщение называет и метод, и mold.
    expectError("return_missing_in_method", R"GL(
mold Account {
    let balance: int
    Account(b: int) { balance = b }
    fn level() -> string {
        if balance > 100 { return "high" }
    }
}
fn main() { print(Account(5).level()) })GL",
                "не все ветки метода 'level' mold'а 'Account' возвращают значение типа string");

    // Сообщение показывает тип результата — в том числе массив и свой mold.
    expectError("return_missing_names_array_type", R"GL(
fn evens(n: int) -> [int] {
    if n > 0 { return [2] }
}
fn main() { print(evens(1)) })GL", "возвращают значение типа [int]");
}

// ---------------------------------------------------------------------
// Модификаторы доступа: private, protected, public
// ---------------------------------------------------------------------

void test_access_modifiers() {
    // Инкапсуляция работает и при этом программа делает своё дело.
    expectOutput("access_encapsulated_account", R"GL(
mold Account {
    private let balance: int
    Account(initial: int) { balance = initial }
    fn deposit(amount: int) { balance += amount }
    fn current() -> int { return balance }
}
fn main() {
    let a = Account(100)
    a.deposit(50)
    print(a.current())
})GL", {"150"});

    // Другой экземпляр того же mold'а — «свой»: можно и читать, и писать его private-поле.
    expectOutput("access_private_through_other_instance", R"GL(
mold Money {
    private let cents: int
    Money(c: int) { cents = c }
    fn moreThan(other: Money) -> bool { return cents > other.cents }
    fn takeFrom(other: Money) { cents += other.cents
        other.cents = 0 }
    fn show() -> int { return cents }
}
fn main() {
    let a = Money(500)
    let b = Money(300)
    print(a.moreThan(b), b.moreThan(a))
    b.takeFrom(a)
    print(a.show(), b.show())
})GL", {"true false", "0 800"});

    // protected: потомок видит и поле, и метод — и по простому имени, и через this/super.
    expectOutput("access_protected_in_descendant", R"GL(
mold Animal {
    protected let name: string
    Animal(n: string) { name = n }
    protected fn sound() -> string { return "..." }
}
mold Dog : Animal {
    Dog(n: string) { super(n) }
    fn speak() -> string { return name + " says " + this.sound() + super.sound() }
}
fn main() { print(Dog("Rex").speak()) })GL", {"Rex says ......"});

    // private-метод — рабочий «внутренний» помощник открытого метода.
    expectOutput("access_private_helper_method", R"GL(
mold Counter {
    private let n: int
    Counter() { n = 0 }
    private fn bump() { n += 1 }
    fn tick() -> int {
        this.bump()
        this.bump()
        return n
    }
}
fn main() { print(Counter().tick()) })GL", {"2"});

    // Явный public и отсутствие модификатора — одно и то же: открыто всем.
    expectOutput("access_public_and_unmarked_are_open", R"GL(
mold P {
    public let a: int
    let b: int
    P() { a = 1
        b = 2 }
    public fn f() -> int { return 10 }
    fn g() -> int { return 20 }
}
fn main() {
    let p = P()
    p.a = p.a + 5
    print(p.a, p.b, p.f(), p.g())
})GL", {"6 2 10 20"});

    // Частное поле предка не перекрывает одноимённую функцию в потомке: потомок его просто не видит.
    expectOutput("access_private_field_does_not_hide_function", R"GL(
fn count() -> int { return 7 }
mold A {
    private let count: int
    A() { count = 1 }
}
mold B : A {
    fn f() -> int { return count() }
}
fn main() { print(B().f()) })GL", {"7"});

    // Инспекция показывает все поля независимо от доступа: ~layout — инструмент, а не обход инкапсуляции.
    expectOutput("access_layout_shows_private_fields", R"GL(
mold P {
    private let secret: int
    protected let hidden: int
    P() { secret = 42
        hidden = 1 }
}
fn main() { print(P()~layout) })GL", {"layout(P): secret@0 (8 байт), hidden@1 (8 байт)"});

    // Исключения: поле message стандартного Exception — открытое.
    expectOutput("access_exception_message_is_public", R"GL(
mold Boom : Exception {
    private let code: int
    Boom(m: string, c: int) { super(m)
        code = c }
    fn getCode() -> int { return code }
}
fn main() {
    try { throw Boom("взрыв", 7) } catch e: Boom { print(e.message, e.getCode()) }
})GL", {"взрыв 7"});

    // Ошибки: снаружи, из потомка, из свободной функции.
    const std::string account = R"GL(
mold Account {
    private let balance: int
    protected let owner: string
    let title: string
    Account(b: int) { balance = b
        owner = "x"
        title = "t" }
    private fn secret() -> int { return balance }
    protected fn hook() -> int { return 1 }
}
)GL";
    expectError("access_private_field_read_from_main", account + "fn main() { let a = Account(1)\n print(a.balance) }",
                "поле 'balance' объявлено private в mold'е 'Account': обращаться к нему можно только из самого 'Account'");
    expectError("access_private_field_write_from_main", account + "fn main() { let a = Account(1)\n a.balance = 0 }",
                "поле 'balance' объявлено private");
    expectError("access_private_method_from_main", account + "fn main() { let a = Account(1)\n print(a.secret()) }",
                "метод 'secret' объявлен private в mold'е 'Account': вызывать его можно только из самого 'Account'");
    expectError("access_protected_field_from_main", account + "fn main() { let a = Account(1)\n print(a.owner) }",
                "поле 'owner' объявлено protected в mold'е 'Account': доступ только из 'Account' и его потомков");
    expectError("access_protected_method_from_main", account + "fn main() { let a = Account(1)\n print(a.hook()) }",
                "метод 'hook' объявлен protected в mold'е 'Account': доступ только из 'Account' и его потомков");
    expectError("access_private_field_from_descendant", account +
                "mold Savings : Account {\n    Savings() { super(1) }\n    fn peek() -> int { return balance }\n}\nfn main() { }",
                "поле 'balance' объявлено private в mold'е 'Account'");
    expectError("access_super_private_method_from_descendant", account +
                "mold Savings : Account {\n    Savings() { super(1) }\n    fn peek() -> int { return super.secret() }\n}\nfn main() { }",
                "метод 'secret' объявлен private в mold'е 'Account'");
    expectError("access_private_field_from_free_function", account + "fn peek(a: Account) -> int { return a.balance }\nfn main() { }",
                "поле 'balance' объявлено private");

    // Конструктор: private — «закрытый» mold, protected — только для наследников.
    expectError("access_private_constructor_from_main", R"GL(
mold Single {
    private Single() { }
}
fn main() { let s = Single() })GL", "конструктор mold'а 'Single' объявлен private");
    expectError("access_protected_constructor_from_main", R"GL(
mold Base {
    protected Base() { }
}
fn main() { let b = Base() })GL", "конструктор mold'а 'Base' объявлен protected");
    expectOutput("access_protected_constructor_super_is_fine", R"GL(
mold Base {
    protected Base() { print("база") }
}
mold Leaf : Base {
    Leaf() { super() }
}
fn main() { Leaf() })GL", {"база"});
}

// ---------------------------------------------------------------------
// Вызов метода того же объекта без this.
// ---------------------------------------------------------------------

void test_method_call_without_this() {
    expectOutput("nothis_basic_and_recursion", R"GL(
mold Calc {
    fn fib(n: int) -> int {
        if n < 2 { return n }
        return fib(n - 1) + fib(n - 2)
    }
    fn tenth() -> int { return fib(10) }
}
fn main() { print(Calc().tenth()) })GL", {"55"});

    expectOutput("nothis_in_constructor", R"GL(
mold A {
    let x: int
    A() { x = twice(21) }
    fn twice(n: int) -> int { return n * 2 }
}
fn main() { print(A().x) })GL", {"42"});

    // Виртуальный метод, вызванный без this., по-прежнему выбирается по реальному классу объекта.
    expectOutput("nothis_virtual_dispatch", R"GL(
mold Animal {
    virtual fn sound() -> string { return "..." }
    fn describe() -> string { return "говорит " + sound() }
}
mold Dog : Animal {
    override fn sound() -> string { return "Гав" }
}
fn main() {
    let a: Animal = Dog()
    print(a.describe(), Animal().describe())
})GL", {"говорит Гав говорит ..."});

    // Метод предка, private-помощник собственного mold'а.
    expectOutput("nothis_inherited_and_private_helper", R"GL(
mold Account {
    private let balance: int
    Account(b: int) { balance = b }
    private fn isValid(amount: int) -> bool { return amount > 0 }
    fn deposit(amount: int) {
        if isValid(amount) { balance += amount }
    }
    fn current() -> int { return balance }
}
mold Savings : Account {
    Savings() { super(10) }
    fn bonus() { deposit(5)
        deposit(-100) }
}
fn main() {
    let s = Savings()
    s.bonus()
    print(s.current())
})GL", {"15"});

    // Встроенные функции и глобальные функции имеют приоритет: метод len() вызывает встроенную len(items),
    // а g() внутри mold'а — глобальную функцию; метод с тем же именем зовётся только как this.g().
    expectOutput("nothis_builtin_and_function_priority", R"GL(
fn g() -> int { return 1 }
mold Stack {
    let items: [int]
    Stack() { items = [3, 1, 2] }
    fn len() -> int { return len(items) }
    fn g() -> int { return 2 }
    fn viaFunction() -> int { return g() }
    fn viaThis() -> int { return this.g() }
}
fn main() {
    let s = Stack()
    print(s.len(), s.viaFunction(), s.viaThis())
})GL", {"3 1 2"});

    // Ошибки: приватный метод предка, арность, static и локальная переменная ведут себя как раньше.
    expectError("nothis_private_base_method", R"GL(
mold Base { private fn secret() -> int { return 1 } }
mold Leaf : Base { fn f() -> int { return secret() } }
fn main() { })GL", "метод 'secret' объявлен private в mold'е 'Base'");
    expectError("nothis_arity_checked", R"GL(
mold A {
    fn two(a: int, b: int) -> int { return a + b }
    fn f() -> int { return two(1) }
}
fn main() { })GL", "неверное число аргументов: ожидалось 2, передано 1");
    expectError("nothis_static_not_callable", R"GL(
mold A {
    static fn util() -> int { return 5 }
    fn f() -> int { return util() }
}
fn main() { })GL", "неизвестная функция или mold 'util'");
    expectError("nothis_free_function_has_no_methods", R"GL(
mold A { fn g() -> int { return 1 } }
fn f() -> int { return g() }
fn main() { })GL", "неизвестная функция или mold 'g'");
}

}  // namespace

int main() {
    test_print();
    test_double_formatting();
    test_implicit_widening();
    test_conversions();
    test_string_concat_and_repeat();
    test_string_compare();
    test_string_unicode();
    test_string_methods();
    test_string_indexing();
    test_negative_index();
    test_slices();
    test_range();
    test_array_methods();
    test_builtin_functions();
    test_optional_semicolon_before_brace();
    test_type_errors();
    test_whole_programs();
    test_else_on_new_line();
    test_print_objects_and_tostring();
    test_reference_casts();
    test_dispatch_rules();
    test_all_paths_return();
    test_access_modifiers();
    test_method_call_without_this();

    testkit::reportSummary();
    return testkit::g_testsFailed == 0 ? 0 : 1;
}
