// ^sup^ and ~sub~ spans next to tilde ranges (#277): Chinese text writes a
// range with a single tilde (50~100mm, 周一~周五), and two ranges in a
// paragraph used to pair up into a subscript
#include "document.h"
#include "d2d_init.h"
#include "render.h"
#include "utils.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}

// Compact tree text: 'text' for leaves, Type[children] for spans
std::string shape(const ElementPtr& e) {
    if (e->type == ElementType::Text) return "'" + e->text + "'";
    std::string s = qmd::elementTypeToString(e->type);
    if (e->children.empty()) return s;
    s += "[";
    for (size_t i = 0; i < e->children.size(); i++) {
        if (i) s += " ";
        s += shape(e->children[i]);
    }
    return s + "]";
}

void expect(qmd::MarkdownParser& parser, const std::string& source, const std::string& want) {
    auto result = parser.parse(source);
    std::string got;
    if (result.success && result.root) {
        for (size_t i = 0; i < result.root->children.size(); i++) {
            if (i) got += " ";
            got += shape(result.root->children[i]);
        }
    }
    check(got == want, source + "\n  want " + want + "\n  got  " + got);
}

size_t count(const ElementPtr& e, ElementType type) {
    size_t n = e->type == type;
    for (const auto& c : e->children) n += count(c, type);
    return n;
}
}  // namespace

int main() {
    qmd::MarkdownParser parser;
    // Ranges stay text: digits or CJK on both sides of the tilde
    expect(parser, "长度50~100mm，宽度20~30mm。\n",
           "Paragraph['长度50~100mm，宽度20~30mm。']");
    expect(parser, "周一~周五营业，周六~周日休息\n",
           "Paragraph['周一~周五营业，周六~周日休息']");
    expect(parser, "第1~3章和第5~7章\n", "Paragraph['第1~3章和第5~7章']");
    expect(parser, "温度-10~40℃，湿度20~90%\n", "Paragraph['温度-10~40℃，湿度20~90%']");
    // Subscripts keep working, beside ranges too
    expect(parser, "H~2~O and CO~2~\n",
           "Paragraph['H' Subscript['2'] 'O and CO' Subscript['2']]");
    expect(parser, "x~i~ and a~n-1~\n",
           "Paragraph['x' Subscript['i'] ' and a' Subscript['n-1']]");
    expect(parser, "水（H~2~O）在3~5℃\n",
           "Paragraph['水（H' Subscript['2'] 'O）在3~5℃']");
    expect(parser, "3~5天后测H~2~O\n",
           "Paragraph['3~5天后测H' Subscript['2'] 'O']");
    // Superscripts and strikethrough are not ranges
    expect(parser, "2^10^ and x^2^\n",
           "Paragraph['2' Superscript['10'] ' and x' Superscript['2']]");
    expect(parser, "~~删除线~~ and 50~100mm\n",
           "Paragraph[Strikethrough['删除线'] ' and 50~100mm']");
    // An escaped tilde is a tilde
    expect(parser, "a\\~b and c\\~d\n", "Paragraph['a~b and c~d']");

    // The fixture: every range stays text, every subscript stays one
    auto doc = parser.parse(read(TINTA_TILDE_FIXTURE));
    check(doc.success && doc.root, "the tilde range fixture parses");
    if (doc.root) {
        check(count(doc.root, ElementType::Subscript) == 4, "the fixture's four subscripts parse");
        check(count(doc.root, ElementType::Strikethrough) == 1, "the fixture's strikethrough parses");
        check(count(doc.root, ElementType::Table) == 1, "the fixture's table survives");
    }
    std::cout << "Script spans: " << failures << " failures\n";
    return failures ? 1 : 0;
}
