#include "mermaid.h"
#include "mermaid_ext.h"
#include "c4_label_checks.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

const mermaid::Node* findNode(const mermaid::Diagram& diagram, const std::string& id) {
    for (const auto& node : diagram.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void testStyledFlowchart() {
    const char* source = R"(flowchart TB
classDef start fill:#fff7ed,stroke:#f97316,color:#7c2d12,stroke-width:2px;
Start["Begin<br/>enrollment"]:::start --> Choice{"Assigned?"}
Choice -->|Yes| Done["Complete"]
Choice -->|No| Retry(["Retry"])
)";

    auto result = mermaid::parse(source);
    check(result.success, "styled flowchart parses");
    if (!result.success) return;

    check(result.diagram.direction == mermaid::Direction::TopToBottom,
          "TB direction is detected");
    check(result.diagram.nodes.size() == 4, "all nodes are parsed");
    check(result.diagram.edges.size() == 3, "all edges are parsed");

    const auto* start = findNode(result.diagram, "Start");
    const auto* choice = findNode(result.diagram, "Choice");
    const auto* retry = findNode(result.diagram, "Retry");
    check(start && start->label == "Begin\nenrollment", "HTML breaks become line breaks");
    check(start && start->className == "start", "node class is captured");
    check(choice && choice->shape == mermaid::NodeShape::Diamond,
          "diamond node shape is captured");
    check(retry && retry->shape == mermaid::NodeShape::Stadium,
          "stadium node shape is captured");

    auto style = result.diagram.classStyles.find("start");
    check(style != result.diagram.classStyles.end(), "class style is captured");
    if (style != result.diagram.classStyles.end()) {
        check(style->second.hasFill && style->second.fill.rgb == 0xFFF7ED,
              "class fill color is parsed");
        check(style->second.hasStrokeWidth && style->second.strokeWidth == 2.0f,
              "class stroke width is parsed");
    }
    check(result.diagram.edges[1].label == "Yes", "edge label is captured");
}

void testGraphAliasesAndChaining() {
    const char* source = R"(graph LR
A --> B --> C
class A,B emphasized
style C fill:#abc,stroke:#123456
)";

    auto result = mermaid::parse(source);
    check(result.success, "graph alias parses");
    if (!result.success) return;

    check(result.diagram.direction == mermaid::Direction::LeftToRight,
          "LR direction is detected");
    check(result.diagram.nodes.size() == 3, "chained nodes are parsed once");
    check(result.diagram.edges.size() == 2, "chained edges are expanded");

    const auto* a = findNode(result.diagram, "A");
    const auto* b = findNode(result.diagram, "B");
    const auto* c = findNode(result.diagram, "C");
    check(a && a->className == "emphasized", "class command styles first node");
    check(b && b->className == "emphasized", "class command styles second node");
    check(c && c->style.hasFill && c->style.fill.rgb == 0xAABBCC,
          "short inline color expands correctly");
}

void testLayoutDirections() {
    auto topBottom = mermaid::parse("flowchart TB\nA --> B\nA --> C\n");
    check(topBottom.success, "layout fixture parses");
    if (!topBottom.success) return;

    std::vector<mermaid::Size> sizes(topBottom.diagram.nodes.size(), {100.0f, 50.0f});
    auto vertical = mermaid::layout(topBottom.diagram, sizes, 20.0f, 40.0f);
    check(vertical.nodes.size() == 3, "vertical layout includes every node");
    check(vertical.nodes[0].bottom < vertical.nodes[1].top,
          "TB target is below source");
    check(vertical.nodes[1].right <= vertical.nodes[2].left,
          "same-rank nodes do not overlap");

    auto leftRight = mermaid::parse("flowchart LR\nA --> B\n");
    check(leftRight.success, "LR layout fixture parses");
    if (!leftRight.success) return;
    sizes.assign(leftRight.diagram.nodes.size(), {100.0f, 50.0f});
    auto horizontal = mermaid::layout(leftRight.diagram, sizes, 20.0f, 40.0f);
    check(horizontal.nodes[0].right < horizontal.nodes[1].left,
          "LR target is right of source");
}

void testSubgraphs() {
    const char* source = R"(flowchart TB
subgraph one [Group One]
    A --> B
end
subgraph two
    direction LR
    C --> D
end
A e1@--> C
e1@{ animate: true }
click A callback
linkStyle 0 stroke:#f00
)";
    auto result = mermaid::parse(source);
    check(result.success, "subgraph flowchart parses");
    if (!result.success) return;
    check(result.diagram.subgraphs.size() == 2, "both subgraphs captured");
    if (result.diagram.subgraphs.size() == 2) {
        check(result.diagram.subgraphs[0].label == "Group One",
              "bracket label captured");
        check(result.diagram.subgraphs[0].nodes.size() == 2,
              "first subgraph owns its two nodes");
        check(result.diagram.subgraphs[1].id == "two",
              "bare subgraph name is the id");
    }
    check(result.diagram.edges.size() == 3,
          "edge-ID edge parses alongside subgraph edges");
}

void testUnsupportedDiagram() {
    auto result = mermaid::parse("sequenceDiagram\nAlice->>Bob: Hello\n");
    check(!result.success, "unsupported Mermaid diagram is rejected");
    check(result.errorLine == 1, "unsupported diagram reports its line");
}

void testBomAndSemicolonStatements() {
    const char* source =
        "\xEF\xBB\xBF"
        "flowchart LR; A[\"One; still one label\"] -->|Yes; still yes| B; "
        "%% comment; not a statement\n"
        "B --> C;";
    auto result = mermaid::parse(source);
    check(result.success, "UTF-8 BOM and semicolon-separated statements parse");
    if (!result.success) return;
    check(result.diagram.nodes.size() == 3, "semicolon statements include all nodes");
    check(result.diagram.edges.size() == 2, "semicolon statements include all edges");
    const auto* a = findNode(result.diagram, "A");
    check(a && a->label == "One; still one label",
          "semicolon inside a node label is preserved");
    check(result.diagram.edges[0].label == "Yes; still yes",
          "semicolon inside a pipe-delimited edge label is preserved");
}

void testCyclicLayout() {
    auto parsed = mermaid::parse(
        "flowchart TB\n"
        "A --> B\n"
        "B --> A\n"
        "A --> A\n");
    check(parsed.success, "cyclic flowchart parses");
    if (!parsed.success) return;

    std::vector<mermaid::Size> sizes(parsed.diagram.nodes.size(), {100.0f, 50.0f});
    auto graph = mermaid::layout(parsed.diagram, sizes, 20.0f, 40.0f);
    check(graph.nodes.size() == 2, "cyclic layout includes every node");
    check(graph.nodes[0].top == 0.0f, "cyclic layout does not leave an empty first rank");
    check(graph.nodes[0].bottom < graph.nodes[1].top,
          "cyclic nodes occupy distinct non-overlapping ranks");
}

void testFiles(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::ifstream file(argv[i], std::ios::binary);
        check(static_cast<bool>(file), "supplied Mermaid file opens");
        if (!file) continue;

        std::string source(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        auto result = mermaid::parse(source);
        if (!result.success) {
            std::cerr << argv[i] << ':' << result.errorLine << ": "
                      << result.error << '\n';
        }
        check(result.success, "supplied Mermaid file parses");
    }
}

void testBackslashNLineBreak() {
    const char* source = R"(graph TD
    BG[background thread\nengine.stop / PlotLan / DNS] --> TK[main]
)";
    auto result = mermaid::parse(source);
    check(result.success, "label with \\n parses");
    if (!result.success) return;
    const auto* bg = findNode(result.diagram, "BG");
    check(bg && bg->label == "background thread\nengine.stop / PlotLan / DNS",
          "literal \\n in a label becomes a line break");
}

// #281: a double-quoted label may run over lines, as Mermaid allows, and its
// line breaks are the label's; quotes around edge text delimit it
const mermaid::Edge* findEdge(const mermaid::Diagram& diagram, const std::string& from,
                              const std::string& to) {
    for (const auto& edge : diagram.edges) {
        if (diagram.nodes[edge.from].id == from && diagram.nodes[edge.to].id == to) return &edge;
    }
    return nullptr;
}

void testMultilineQuotedLabels() {
    // The reporter's shape: front matter, a comment, tabs, CRLF, circles,
    // hyphenated IDs, a loop and edge labels running over two to four lines
    const std::string source =
        "---\r\ntitle: State Machine\r\n---\r\n%% stateDiagram-v2\r\ngraph TB\r\n\r\n"
        "\tSTART((START)) -- \"Nop\" --> MSH((MSH))\r\n"
        "\tMSH((MSH)) -- \"Push:ORDERS\r\nPush:LABORDERS\" --> PID-NTE((PID-NTE))\r\n"
        "\tPID-NTE((PID-NTE)) -- \"Nop\" --> PID-NTE((PID-NTE))\r\n"
        "\tPID-NTE((PID-NTE)) -- \"Pop\r\nPop\r\n    Pop\r\nPop\" --> MSH((MSH))\r\n"
        "\tMSH -->|\"Quoted\"| N[\"first line\r\n   second line\"]\r\n"
        "\tN -- \"an --> inside\" --> START\r\n";
    auto result = mermaid::parse(source);
    check(result.success, "labels running over lines parse");
    if (!result.success) {
        std::cerr << "  line " << result.errorLine << ": " << result.error << '\n';
        return;
    }
    const auto* nop = findEdge(result.diagram, "START", "MSH");
    check(nop && nop->label == "Nop", "quotes around an edge's text are not part of it");
    const auto* push = findEdge(result.diagram, "MSH", "PID-NTE");
    check(push && push->label == "Push:ORDERS\nPush:LABORDERS",
          "a quoted edge text keeps its line break, without the CR");
    const auto* loop = findEdge(result.diagram, "PID-NTE", "PID-NTE");
    check(loop && loop->label == "Nop", "a node's edge to itself parses");
    const auto* pops = findEdge(result.diagram, "PID-NTE", "MSH");
    check(pops && pops->label == "Pop\nPop\nPop\nPop",
          "four lines, the indentation of one not part of the text");
    const auto* quoted = findEdge(result.diagram, "MSH", "N");
    check(quoted && quoted->label == "Quoted", "quotes in a |text| label delimit it too");
    const auto* node = findNode(result.diagram, "N");
    check(node && node->label == "first line\nsecond line", "a quoted node label runs over lines");
    const auto* arrow = findEdge(result.diagram, "N", "START");
    check(arrow && arrow->label == "an --> inside", "an arrow inside quoted edge text stays text");
    check(result.diagram.nodes.size() == 4, "the diagram has its four nodes");

    // Errors after a label that ran over lines still name their own line
    auto broken = mermaid::parse("graph TD\nA -- \"one\ntwo\" --> B\nC ?? D\n");
    check(!broken.success && broken.errorLine == 4, "a later error names its own line");

    // A single quote is not a Mermaid string: an apostrophe joins nothing
    auto apostrophe = mermaid::parse("graph TD\nA[Don't stop] --> B\nB --> C\n");
    check(apostrophe.success && apostrophe.diagram.edges.size() == 2 &&
              findEdge(apostrophe.diagram, "B", "C"),
          "an apostrophe in a label leaves the next line alone");
}

void testAttributeSyntaxRejected() {
    const char* source = R"(flowchart TD
A@{ shape: rounded, label: "Fancy" } --> B[Plain]
)";
    auto result = mermaid::parse(source);
    check(!result.success, "v11 '@{ }' attribute syntax fails instead of mis-rendering");
}

void testLayoutExposesRanks() {
    const char* source = R"(graph LR
A --> B
B --> C
C --> D
A --> D
)";
    auto result = mermaid::parse(source);
    check(result.success, "rank test diagram parses");
    if (!result.success) return;

    std::vector<mermaid::Size> sizes(result.diagram.nodes.size(), {100.0f, 40.0f});
    auto layout = mermaid::layout(result.diagram, sizes, 20.0f, 60.0f);
    check(layout.ranks.size() == result.diagram.nodes.size(),
          "layout exposes one rank per node");
    if (layout.ranks.size() == 4) {
        check(layout.ranks[0] == 0 && layout.ranks[1] == 1 &&
              layout.ranks[2] == 2 && layout.ranks[3] == 3,
              "ranks follow the longest path (A=0, B=1, C=2, D=3)");
    }
}

// --- extended diagram families (mermaidext) ---

// Character-cell measurer: 8px per column, 16px per line
mermaidext::Size fakeMeasure(const std::string& text,
                             const mermaidext::TextStyle&, float) {
    size_t longest = 0, current = 0, lines = text.empty() ? 0 : 1;
    for (char c : text) {
        if (c == '\n') {
            lines++;
            current = 0;
        } else {
            current++;
            longest = std::max(longest, current);
        }
    }
    return {static_cast<float>(longest * 8),
            static_cast<float>(lines * 16)};
}

void testDetectKind() {
    using mermaidext::Kind;
    using mermaidext::detectKind;
    check(detectKind("graph TD\nA-->B") == Kind::Flowchart,
          "graph detects as flowchart");
    check(detectKind("%% comment\nflowchart LR\nA-->B") == Kind::Flowchart,
          "leading comments are skipped in detection");
    check(detectKind("sequenceDiagram\nA->>B: hi") == Kind::Sequence,
          "sequenceDiagram detected");
    check(detectKind("classDiagram\nA <|-- B") == Kind::Class,
          "classDiagram detected");
    check(detectKind("stateDiagram-v2\n[*] --> A") == Kind::State,
          "stateDiagram-v2 detected");
    check(detectKind("erDiagram\nA ||--o{ B : has") == Kind::Er,
          "erDiagram detected");
    check(detectKind("pie\n\"A\" : 1") == Kind::Pie, "pie detected");
    check(detectKind("gitGraph\ncommit") == Kind::Git, "gitGraph detected");
    check(detectKind("gantt\ntitle X") == Kind::Gantt, "gantt detected");
    check(detectKind("mindmap\n  root") == Kind::Mindmap,
          "mindmap detected");
    check(detectKind("timeline\n2020 : x") == Kind::Timeline,
          "timeline detected");
    check(detectKind("journey\ntitle X") == Kind::Journey,
          "journey detected");
    check(detectKind("quadrantChart\ntitle X") == Kind::Quadrant,
          "quadrantChart detected");
    check(detectKind("xychart-beta\nbar [1]") == Kind::XyChart,
          "xychart-beta detected");
    check(detectKind("packet-beta\n0-15: \"x\"") == Kind::Packet,
          "packet-beta detected");
    check(detectKind("kanban\n  Todo") == Kind::Kanban, "kanban detected");
    check(detectKind("treemap-beta\n\"A\"") == Kind::Treemap,
          "treemap-beta detected");
    check(detectKind("zenuml\n...") == Kind::None,
          "unsupported kinds report None");
}

void testExtBuilders() {
    using mermaidext::Kind;
    struct Case {
        Kind kind;
        const char* name;
        const char* source;
    };
    const Case kCases[] = {
        {Kind::Sequence, "sequence",
         "sequenceDiagram\n"
         "    participant A as Service<br/>api\n"
         "    A->>+B: request\n"
         "    B-->>-A: reply\n"
         "    Note over A,B: both\n"
         "    loop retry\n        A-)B: ping\n    end\n"},
        {Kind::Pie, "pie",
         "pie showData title Share\n    \"A\" : 60\n    \"B\" : 40\n"},
        {Kind::State, "state",
         "stateDiagram-v2\n    [*] --> S1\n    S1 --> S2 : go\n"
         "    S2 --> S1 : back\n    S2 --> [*]\n"},
        {Kind::Class, "class",
         "classDiagram\n    Animal <|-- Duck\n    Animal : +int age\n"
         "    class Duck{\n        +swim()\n    }\n"},
        {Kind::Er, "er",
         "erDiagram\n    CUSTOMER ||--o{ ORDER : places\n"
         "    CUSTOMER {\n        string name PK\n    }\n"},
        {Kind::Git, "git",
         "gitGraph\n   commit\n   branch dev\n   commit\n"
         "   checkout main\n   merge dev tag: \"v1\"\n"},
        {Kind::Gantt, "gantt",
         "gantt\n    dateFormat YYYY-MM-DD\n    section S\n"
         "        T1 :a1, 2026-01-01, 5d\n        T2 :after a1, 3d\n"},
        {Kind::Mindmap, "mindmap",
         "mindmap\n  root((center))\n    A\n      A1\n    B\n"},
        {Kind::Timeline, "timeline",
         "timeline\n    title T\n    2020 : one : two\n    2021 : three\n"},
        {Kind::Journey, "journey",
         "journey\n    title T\n    section S\n      Task: 5: Me\n"},
        {Kind::Quadrant, "quadrant",
         "quadrantChart\n    x-axis Low --> High\n    quadrant-1 Q\n"
         "    A: [0.3, 0.6]\n"},
        {Kind::XyChart, "xychart",
         "xychart-beta\n    x-axis [a, b]\n    bar [1, 2]\n    line [2, 1]\n"},
        {Kind::Packet, "packet",
         "packet-beta\ntitle UDP\n0-15: \"Source Port\"\n"
         "16-31: \"Destination Port\"\n32-63: \"Data\"\n"},
        {Kind::Kanban, "kanban",
         "kanban\n  Todo\n    [Write docs]\n    id2[Fix bug]@{ ticket: "
         "'MC-1', assigned: 'knsv', priority: 'High' }\n  [Done]\n"
         "    [Ship it]\n"},
        {Kind::Treemap, "treemap",
         "treemap-beta\ntitle T\n\"Section 1\"\n    \"Leaf 1.1\": 12\n"
         "    \"Leaf 1.2\": 8\n\"Section 2\"\n    \"Leaf 2.1\": 10\n"},
        {Kind::Radar, "radar",
         "radar-beta\ntitle Grades\naxis m[\"Math\"], s[\"Science\"], "
         "e[\"English\"]\ncurve a[\"Alice\"]{85, 90, 80}\n"
         "curve b{70, 75, 85}\nmax 100\nmin 0\n"},
        {Kind::Sankey, "sankey",
         "sankey-beta\nA,B,10\nA,C,5\nB,D,8\nC,D,5\n\"E, e\",D,2\n"},
        {Kind::C4, "c4",
         "C4Context\n    Person(a, \"A\")\n    System(b, \"B\")\n"
         "    Rel(a, b, \"Uses\")\n"},
    };
    for (const auto& testCase : kCases) {
        auto built = mermaidext::build(testCase.kind, testCase.source,
                                       fakeMeasure, 1.0f);
        std::string label = std::string(testCase.name) + " builds";
        check(built.ok, label.c_str());
        if (!built.ok) {
            std::cerr << "  (" << built.error << ")\n";
            continue;
        }
        label = std::string(testCase.name) + " has extent";
        check(built.width > 0.0f && built.height > 0.0f, label.c_str());
        label = std::string(testCase.name) + " emits primitives";
        check(!built.prims.empty(), label.c_str());
    }
}

// The ```mermaid fences of a Markdown file, in order
std::vector<std::string> mermaidFences(const std::string& markdown) {
    std::vector<std::string> fences;
    std::string current;
    bool inside = false;
    size_t position = 0;
    while (position < markdown.size()) {
        size_t newline = markdown.find('\n', position);
        if (newline == std::string::npos) newline = markdown.size();
        std::string line = markdown.substr(position, newline - position);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inside && line.rfind("```mermaid", 0) == 0) {
            inside = true;
            current.clear();
        } else if (inside && line == "```") {
            inside = false;
            fences.push_back(current);
        } else if (inside) {
            current += line + "\n";
        }
        position = newline + 1;
    }
    return fences;
}

// What the app does with a fence: draws it, or shows its source
bool drawsDiagram(const std::string& source, std::string& error) {
    const auto kind = mermaidext::detectKind(source);
    if (kind == mermaidext::Kind::None) {
        error = "diagram type not recognised";
        return false;
    }
    if (kind == mermaidext::Kind::Flowchart) {
        auto parsed = mermaid::parse(source);
        error = parsed.error;
        return parsed.success;
    }
    auto built = mermaidext::build(kind, source, fakeMeasure, 1.0f);
    error = built.error;
    return built.ok && !built.prims.empty();
}

// #261: a diagram may open with YAML front matter (title, config). The
// fixture's first five fences put it on other diagram types; the rest are
// the 25 examples of Mermaid's ER documentation, in order.
void testFrontMatterFixture() {
    std::ifstream file(TINTA_MERMAID_FRONT_MATTER_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "front matter fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = mermaidFences(markdown);
    check(fences.size() == 30, "the fixture has 5 front matter and 25 ER docs fences");
    // Fences that draw: every one with front matter (1-6, 9, 30) and the
    // ER docs examples whose syntax the ER renderer knows. The others use
    // ER syntax it does not support yet (cardinalities in words, a quoted
    // entity on its own, subgraphs, style and classDef), and show their
    // source instead of a wrong diagram.
    const std::vector<size_t> draws = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 14, 15, 16, 18, 19, 30};
    for (size_t i = 0; i < fences.size(); ++i) {
        std::string error;
        const bool drawn = drawsDiagram(fences[i], error);
        const bool expected =
            std::find(draws.begin(), draws.end(), i + 1) != draws.end();
        if (drawn != expected) {
            std::cerr << "  fence " << i + 1 << (drawn ? " draws" : " shows source: " + error) << '\n';
        }
        check(drawn == expected, "a fence draws exactly when its syntax is supported");
    }
    // The front matter is skipped whole, whatever YAML it holds, and only
    // when it opens the source
    using mermaidext::skipFrontMatter;
    check(skipFrontMatter("---\ntitle: T\n---\nerDiagram\n") == "erDiagram\n",
          "a title block is skipped");
    check(skipFrontMatter("\xEF\xBB\xBF\n---\r\nconfig:\r\n  theme: dark\r\n---  \r\npie\r\n") ==
              "pie\r\n",
          "a byte order mark, a blank line, CRLF and trailing spaces are fine");
    check(skipFrontMatter("---\ntitle: never closed\nerDiagram\n") ==
              "---\ntitle: never closed\nerDiagram\n",
          "an unclosed block is not front matter");
    check(skipFrontMatter("graph TD\nA---B\n---\n") == "graph TD\nA---B\n---\n",
          "a --- line after the diagram starts is not front matter");
    check(skipFrontMatter("---\n---\n").empty(), "an empty block leaves nothing");
    // The flowchart parser blanks the block, so error lines still count
    // from the top of the fence
    auto bad = mermaid::parse("---\ntitle: T\n---\nflowchart LR\n  A -- B\n");
    check(!bad.success && bad.errorLine == 5,
          "a flowchart error after front matter reports its own line");
}

// The ids of a diagram's asymmetric nodes, in order, separated by spaces
std::string asymmetricIds(const mermaid::Diagram& diagram) {
    std::string ids;
    for (const auto& node : diagram.nodes) {
        if (node.shape != mermaid::NodeShape::Asymmetric) continue;
        if (!ids.empty()) ids += ' ';
        ids += node.id;
    }
    return ids;
}

// id>text] is Mermaid's asymmetric shape. It drew as one rectangle whose id
// and label were the whole "S>Screen]".
void testAsymmetricShape() {
    auto screen = mermaid::parse("flowchart LR\nR -.-> S>Screen]\n");
    check(screen.success, "an edge to an asymmetric node parses");
    if (screen.success) {
        const auto* node = findNode(screen.diagram, "S");
        check(node && node->label == "Screen" &&
                  node->shape == mermaid::NodeShape::Asymmetric,
              "S>Screen] is node S, labelled Screen, asymmetric");
        check(screen.diagram.nodes.size() == 2 && screen.diagram.edges.size() == 1 &&
                  screen.diagram.edges[0].dashed && findEdge(screen.diagram, "R", "S"),
              "the dotted edge runs from R to S");
    }

    // Quoted text as for the other shapes: '>', ']' and ';' inside, a label
    // over two lines; unquoted text keeps ';' and '|' as a bracketed one does
    auto labels = mermaid::parse(
        "flowchart LR\n"
        "A>\"a > b ] c; d\"] --> B>\"first line\n   second line\"]\n"
        "C>one; two | three] --> D; D --> E\n"
        "F>Classed]:::hot\n"
        "G --> H\n"
        "H>Defined later]\n"
        "subgraph Group\n"
        "    I>Inside]\n"
        "end\n");
    check(labels.success, "asymmetric labels parse");
    if (labels.success) {
        const auto& diagram = labels.diagram;
        const auto* a = findNode(diagram, "A");
        check(a && a->label == "a > b ] c; d", "a quoted label keeps '>', ']' and ';'");
        const auto* b = findNode(diagram, "B");
        check(b && b->label == "first line\nsecond line", "a quoted label runs over two lines");
        const auto* c = findNode(diagram, "C");
        check(c && c->label == "one; two | three", "an unquoted label keeps ';' and '|'");
        check(findEdge(diagram, "C", "D") && findEdge(diagram, "D", "E"),
              "a ';' after the label still separates statements");
        const auto* f = findNode(diagram, "F");
        check(f && f->className == "hot", "a class follows the label");
        const auto* h = findNode(diagram, "H");
        check(h && h->label == "Defined later", "a node used before takes the shape later");
        check(diagram.subgraphs.size() == 1 && diagram.subgraphs[0].nodes.size() == 1 &&
                  diagram.nodes[diagram.subgraphs[0].nodes[0]].id == "I",
              "an asymmetric node belongs to its subgraph");
        check(asymmetricIds(diagram) == "A B C F H I", "every '>' node is asymmetric");
    }

    // A '>' that belongs to an arrow or to text never opens the shape
    struct Case {
        const char* line;
        const char* asymmetric;  // ids of the asymmetric nodes
        const char* edgeLabel;
    };
    const Case cases[] = {
        {"A-->B", "", ""},
        {"A --> B", "", ""},
        {"A ==> B", "", ""},
        {"A -.-> B", "", ""},
        {"A==>B>Text]", "B", ""},
        {"A-.->B>Text]", "B", ""},
        {"A --> B>Text]", "B", ""},
        {"A>Text] --> B", "A", ""},
        {"A>Text]-->B", "A", ""},
        {"A -- text --> B", "", "text"},
        {"A -- a > b --> B", "", "a > b"},
        {"A -->|a > b| B", "", "a > b"},
        {"A>Text] -->|a > b| B>\"c > d\"]", "A B", "a > b"},
        {"A[\"a > b\"] --> B(\"c > d\")", "", ""},
    };
    for (const auto& item : cases) {
        const std::string name = std::string("'") + item.line + "'";
        auto parsed = mermaid::parse(std::string("flowchart LR\n") + item.line + "\n");
        check(parsed.success, (name + " parses").c_str());
        if (!parsed.success) continue;
        const auto& diagram = parsed.diagram;
        check(diagram.nodes.size() == 2 && findNode(diagram, "A") && findNode(diagram, "B"),
              (name + " has nodes A and B, and no others").c_str());
        check(asymmetricIds(diagram) == item.asymmetric,
              (name + " has asymmetric nodes '" + item.asymmetric + "', not '" +
               asymmetricIds(diagram) + "'").c_str());
        check(diagram.edges.size() == 1 && diagram.edges[0].directed &&
                  diagram.edges[0].label == item.edgeLabel,
              (name + " has one directed edge labelled '" + item.edgeLabel + "'").c_str());
    }
    auto thick = mermaid::parse("flowchart LR\nA==>B\n");
    check(thick.success && thick.diagram.edges.size() == 1 &&
              thick.diagram.edges[0].strokeScale == 2.0f,
          "'A==>B' stays a thick arrow");
    auto dotted = mermaid::parse("flowchart LR\nA-.->B\n");
    check(dotted.success && dotted.diagram.edges.size() == 1 && dotted.diagram.edges[0].dashed,
          "'A-.->B' stays a dotted arrow");

    // In edge text, a '>' after a letter opens no label either: the ';'
    // after the edge still separates
    auto edgeText = mermaid::parse("flowchart LR\nA -- a>b --> B; B --> C\nC -->|x>y| D; D --> E\n");
    check(edgeText.success && edgeText.diagram.edges.size() == 4 &&
              edgeText.diagram.edges[0].label == "a>b" &&
              edgeText.diagram.edges[2].label == "x>y" && asymmetricIds(edgeText.diagram).empty(),
          "a '>' in edge text stays text");

    // Circle and cross ends are not drawn natively, so those edges show
    // their source, as before. A '>' heading a longer arrow does too, as in
    // Mermaid's own "B ---->|No| E", rather than open a node named '-'.
    for (const char* line : {"A --o B", "A --x B", "B ---->|No| E[End]", "A--->B", "A->B"}) {
        auto parsed = mermaid::parse(std::string("flowchart LR\n") + line + "\n");
        check(!parsed.success && parsed.error == "Unsupported edge syntax",
              (std::string("'") + line + "' shows its source as an unsupported edge").c_str());
    }

    check(mermaid::leftNotch(mermaid::NodeShape::Asymmetric, 52.0f) == 26.0f &&
              mermaid::leftNotch(mermaid::NodeShape::Rectangle, 52.0f) == 0.0f &&
              mermaid::leftNotch(mermaid::NodeShape::Hexagon, 52.0f) == 0.0f,
          "only the asymmetric shape is notched, half its height in");
}

// The asymmetric shape fixture: every fence parses, with its asymmetric nodes
void testAsymmetricFixture() {
    std::ifstream file(TINTA_MERMAID_ASYMMETRIC_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "asymmetric shape fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = mermaidFences(markdown);
    const std::vector<std::string> expected = {"Route Screen", "S A C D", "Parse Out", "Flag Back"};
    check(fences.size() == expected.size(), "the fixture has four Mermaid fences");
    for (size_t i = 0; i < fences.size() && i < expected.size(); ++i) {
        auto parsed = mermaid::parse(fences[i]);
        if (!parsed.success) {
            std::cerr << "  fence " << i + 1 << ':' << parsed.errorLine << ": " << parsed.error << '\n';
        }
        check(parsed.success && asymmetricIds(parsed.diagram) == expected[i],
              ("fixture fence " + std::to_string(i + 1) + " has asymmetric nodes " +
               expected[i]).c_str());
    }
}

void testExtFallbacks() {
    auto sequence = mermaidext::build(
        mermaidext::Kind::Sequence,
        "sequenceDiagram\n    createParticipant A\n", fakeMeasure, 1.0f);
    check(!sequence.ok, "unknown sequence statement falls back");
    auto composite = mermaidext::build(
        mermaidext::Kind::State,
        "stateDiagram-v2\n    state Outer {\n        [*] --> A\n    }\n",
        fakeMeasure, 1.0f);
    check(!composite.ok, "composite states fall back");
    auto pie = mermaidext::build(mermaidext::Kind::Pie,
                                 "pie\n    \"A\" : -3\n", fakeMeasure, 1.0f);
    check(!pie.ok, "negative pie values fall back");
}

// --- C4 relation labels ---
// A relation's label sat on the middle of its line, so between two shapes
// 24 px apart it covered both of them and their text, and the labels of
// relations between the same shapes covered each other

// Builds a C4 source and checks every label: clear of the boxes, frames,
// captions, text, arrowheads and other labels, and on its own line
mermaidext::Built checkC4Labels(const std::string& name,
                                const std::string& source, float scale) {
    auto built = mermaidext::build(mermaidext::Kind::C4, source, fakeMeasure,
                                   scale);
    const std::string at = name + (scale == 1.0f ? " at 1x" : " at 1.5x");
    check(built.ok, (at + " builds").c_str());
    if (!built.ok) {
        std::cerr << "  (" << built.error << ")\n";
        return built;
    }
    const auto problems = c4checks::labelProblems(built, source, scale);
    for (const auto& problem : problems) {
        std::cerr << "  " << at << ": " << problem << '\n';
    }
    check(problems.empty(),
          (at + ": each label is clear of boxes, text, frames, arrowheads "
                "and the other labels, on its own line").c_str());
    return built;
}

// The lines drawn between two element boxes
std::vector<const mermaidext::Prim*> linesBetween(const c4checks::Parts& parts,
                                                  const c4checks::Box& a,
                                                  const c4checks::Box& b) {
    std::vector<const mermaidext::Prim*> lines;
    for (const auto* line : parts.lines) {
        mermaidext::Point p{line->x1, line->y1}, q{line->x2, line->y2};
        if ((c4checks::onEdge(p, a) && c4checks::onEdge(q, b)) ||
            (c4checks::onEdge(p, b) && c4checks::onEdge(q, a))) {
            lines.push_back(line);
        }
    }
    return lines;
}

const char* const kC4Reported = R"(C4Context
    title Tinta in context
    Person(writer, "Writer", "Reads and edits Markdown")
    System(tinta, "Tinta", "Native Markdown viewer and editor")
    System_Ext(plantuml, "PlantUML", "Optional diagram tool")
    System_Ext(github, "GitHub Releases", "Update check for portable builds")
    Rel(writer, tinta, "Opens files in")
    Rel(tinta, plantuml, "Renders diagrams with")
    Rel(tinta, github, "Checks for updates")
)";

void testC4ReportedLabels() {
    for (float scale : {1.0f, 1.5f}) {
        auto built = checkC4Labels("reported diagram", kC4Reported, scale);
        if (!built.ok) continue;
        auto parts = c4checks::partsOf(built);
        c4checks::Box writer, tinta, plantuml, github, opens, renders, updates;
        bool found = c4checks::elementBox(parts, "Writer", writer) &&
                     c4checks::elementBox(parts, "Tinta", tinta) &&
                     c4checks::elementBox(parts, "PlantUML", plantuml) &&
                     c4checks::elementBox(parts, "GitHub Releases", github) &&
                     c4checks::chipBox(parts, "Opens files in", opens) &&
                     c4checks::chipBox(parts, "Renders diagrams with", renders) &&
                     c4checks::chipBox(parts, "Checks for updates", updates);
        check(found, "the reported diagram draws its boxes and labels");
        if (!found) continue;
        // The gaps widen to hold the labels, which keep to the free space
        check(opens.x1 > writer.x2 && opens.x2 < tinta.x1,
              "\"Opens files in\" sits between Writer and Tinta");
        check(renders.x1 > tinta.x2 && renders.x2 < plantuml.x1,
              "\"Renders diagrams with\" sits between Tinta and PlantUML");
        check(updates.y1 > tinta.y2 && updates.y2 < github.y1,
              "\"Checks for updates\" sits between Tinta and GitHub Releases");
        check(!c4checks::overlaps(opens, tinta) &&
                  !c4checks::overlaps(renders, tinta),
              "no label covers the Tinta box or its description");
    }
}

void testC4LabelLayouts() {
    struct Case {
        const char* name;
        const char* source;
    };
    const Case kCases[] = {
        {"direction keywords", R"(C4Container
    Container(a, "Alpha", "Go")
    Container(b, "Beta", "Go")
    Container(c, "Gamma", "Go")
    Container(d, "Delta", "Go")
    Container(e, "Epsilon", "Go")
    Container(f, "Zeta", "Go")
    Rel_R(a, b, "Right to")
    Rel_Right(b, c, "Right again")
    Rel_L(c, b, "Left to")
    Rel_Left(b, a, "Left again")
    Rel_D(a, d, "Down to")
    Rel_Down(b, e, "Down again")
    Rel_U(f, c, "Up to")
    Rel_Up(e, b, "Up again")
    Rel_Back(d, e, "Back to")
    Rel_Neighbor(e, f, "Next to")
)"},
        {"BiRel", R"(C4Context
    System(shop, "Bookshop", "Sells books online")
    System_Ext(warehouse, "Warehouse", "Picks and ships orders")
    System_Ext(publisher, "Publisher feed", "New titles and prices")
    System_Ext(accounting, "Accounting", "Invoices and ledgers")
    BiRel(shop, warehouse, "Syncs stock with", "EDI")
    Rel(warehouse, shop, "Reports shipments to")
    Rel(publisher, warehouse, "Announces new titles to")
    BiRel(shop, accounting, "Exchanges invoices with")
    BiRel(accounting, warehouse, "Reconciles stock with")
)"},
        {"nested boundaries", R"(C4Container
    title Nested boundaries
    Person(customer, "Customer", "Orders books")
    Enterprise_Boundary(company, "Bookshop Ltd") {
        System_Boundary(shop, "Bookshop") {
            Container(web, "Web shop", "TypeScript", "Catalogue")
            Container(api, "Shop API", "Go", "Orders")
            Container(admin, "Back office", "Django")
            ContainerDb(db, "Orders", "PostgreSQL", "Orders and stock")
        }
        Boundary(ops, "Operations", "team") {
            Container(jobs, "Jobs", "Go", "Nightly work")
        }
    }
    System_Ext(pay, "Payment provider")
    Rel(customer, web, "Orders through", "HTTPS")
    Rel(web, api, "Calls", "JSON")
    Rel(api, db, "Stores orders in", "SQL")
    Rel(admin, api, "Manages stock with")
    Rel(jobs, db, "Archives old orders in")
    Rel(api, pay, "Charges cards with")
    Rel(pay, customer, "Sends receipts to")
)"},
        {"three rows", R"(C4Component
    Container(spa, "Single-page app", "TypeScript", "Runs in the browser")
    Component(router, "Router", "Go", "Maps URLs to handlers")
    Component(auth, "Auth", "Go", "Checks sessions")
    Component(catalogue, "Catalogue", "Go", "Lists and searches books")
    Component(basket, "Basket", "Go", "Holds chosen books")
    Component(orders, "Orders", "Go", "Turns baskets into orders")
    ComponentDb(store, "Store", "PostgreSQL", "Books and orders")
    ComponentQueue(events, "Events", "NATS", "Order events")
    Rel(spa, router, "Sends requests to", "JSON/HTTPS")
    Rel(router, auth, "Checks sessions with")
    Rel(router, catalogue, "Routes searches to")
    Rel(router, basket, "Routes basket edits to")
    Rel(basket, orders, "Checks out through")
    Rel(catalogue, store, "Reads books from", "SQL")
    Rel(orders, store, "Writes orders to", "SQL")
    Rel(orders, events, "Publishes to")
    Rel(auth, events, "Logs sign-ins to")
    Rel(spa, auth, "Refreshes tokens with")
)"},
        {"dense neighbours", R"(C4Container
    Person(user, "User", "Reads and writes")
    Container(spa, "Single-page app", "TypeScript")
    Container(api, "API", "Go")
    ContainerDb(db, "Documents", "PostgreSQL")
    ContainerQueue(bus, "Event bus", "Kafka")
    Container(worker, "Indexer", "Go")
    Rel(user, spa, "Edits in")
    Rel(spa, user, "Shows changes to")
    Rel(spa, api, "Saves through", "JSON/HTTPS")
    Rel(api, spa, "Pushes edits to", "WebSocket")
    BiRel(spa, api, "Keeps a session with")
    Rel(api, db, "Reads and writes", "SQL")
    Rel(db, api, "Notifies", "LISTEN")
    Rel(api, bus, "Publishes changes to")
    Rel(bus, worker, "Feeds")
    Rel(worker, bus, "Acknowledges")
    BiRel(worker, api, "Reports progress to")
)"},
        {"fan-out", R"(C4Context
    System(left, "Left", "Beside the person")
    Person(person, "Person", "Uses everything")
    System(right, "Right", "Beside the person")
    System(a, "A", "Below, left")
    System(b, "B", "Below, middle")
    System(c, "C", "Below, right")
    Rel(person, a, "Uses A for this")
    Rel(person, b, "Uses B for that")
    Rel(person, c, "Uses C for the rest")
    Rel(person, left, "Talks to")
    Rel(person, right, "Talks to as well")
    Rel(left, a, "Feeds")
)"},
        {"deployment", R"(C4Deployment
    Deployment_Node(laptop, "Laptop", "Windows 11") {
        Deployment_Node(folder, "Portable folder", "tinta.exe") {
            Container(viewer, "Viewer", "C++17", "Draws Markdown")
            Container(settings, "Settings", "INI file")
        }
        Node(java, "Java", "OpenJDK") {
            Container(plantuml, "PlantUML", "Java")
        }
    }
    Node_R(cloud, "GitHub", "Cloud") {
        Container(releases, "Releases", "JSON API")
    }
    Node_L(spare, "Spare node")
    Rel(viewer, settings, "Reads", "INI")
    Rel(viewer, plantuml, "Renders through", "stdin")
    Rel(viewer, releases, "Checks for updates", "HTTPS")
)"},
        {"self relation", R"(C4Container
    Container(api, "API", "Go", "Serves requests")
    Container(db, "Store", "SQL")
    Rel(api, api, "Retries failed calls")
    Rel(api, db, "Reads")
    Rel(db, db, "Vacuums")
)"},
    };
    for (const auto& testCase : kCases) {
        for (float scale : {1.0f, 1.5f}) {
            checkC4Labels(testCase.name, testCase.source, scale);
        }
    }

    // Relations between the same two shapes get lines of their own, side
    // by side, rather than one line under two labels
    auto dense = mermaidext::build(mermaidext::Kind::C4, kCases[4].source,
                                   fakeMeasure, 1.0f);
    auto parts = c4checks::partsOf(dense);
    c4checks::Box spa, api, user;
    if (c4checks::elementBox(parts, "Single-page app", spa) &&
        c4checks::elementBox(parts, "API", api) &&
        c4checks::elementBox(parts, "User", user)) {
        auto threeLanes = linesBetween(parts, spa, api);
        auto twoLanes = linesBetween(parts, user, spa);
        // BiRel draws its line twice, once per arrowhead
        check(threeLanes.size() == 4 && twoLanes.size() == 2,
              "each relation between two shapes has its own line");
        bool apart = true;
        for (const auto* lanes : {&threeLanes, &twoLanes}) {
            for (size_t i = 0; i < lanes->size(); i++) {
                for (size_t j = i + 1; j < lanes->size(); j++) {
                    const auto* p = (*lanes)[i];
                    const auto* q = (*lanes)[j];
                    bool reversed = p->x1 == q->x2 && p->y1 == q->y2 &&
                                    p->x2 == q->x1 && p->y2 == q->y1;
                    if (!reversed && std::abs(p->y1 - q->y1) < 10.0f &&
                        std::abs(p->y2 - q->y2) < 10.0f) {
                        apart = false;
                    }
                }
            }
        }
        check(apart, "lanes between the same two shapes run apart");
    } else {
        check(false, "the dense diagram draws its boxes");
    }

    // A relation passing behind a box keeps a lane beside the relation that
    // ends at that box, so neither label sits on the other's line
    {
        auto rows = mermaidext::build(mermaidext::Kind::C4, kCases[3].source,
                                      fakeMeasure, 1.0f);
        auto rowParts = c4checks::partsOf(rows);
        c4checks::Box browser, router, auth;
        bool found =
            c4checks::elementBox(rowParts, "Single-page app", browser) &&
            c4checks::elementBox(rowParts, "Router", router) &&
            c4checks::elementBox(rowParts, "Auth", auth);
        auto toRouter = linesBetween(rowParts, browser, router);
        auto toAuth = linesBetween(rowParts, browser, auth);
        check(found && toRouter.size() == 1 && toAuth.size() == 1 &&
                  std::abs(toRouter[0]->y1 - toAuth[0]->y1) >= 10.0f,
              "a line passing behind a box runs beside the line ending there");
    }

    // Without labels the layout keeps its 24 px and 26 px gaps
    const char* plain = R"(C4Context
    Person(a, "A")
    System(b, "B")
    System(c, "C")
    System(d, "D")
    Rel(a, b, "")
    Rel(a, d, "")
)";
    auto unlabelled =
        mermaidext::build(mermaidext::Kind::C4, plain, fakeMeasure, 1.0f);
    auto plainParts = c4checks::partsOf(unlabelled);
    c4checks::Box a, b, c, d;
    check(c4checks::elementBox(plainParts, "A", a) &&
              c4checks::elementBox(plainParts, "B", b) &&
              c4checks::elementBox(plainParts, "C", c) &&
              c4checks::elementBox(plainParts, "D", d) &&
              std::abs(b.x1 - a.x2 - 24.0f) < 0.01f &&
              std::abs(c.x1 - b.x2 - 24.0f) < 0.01f &&
              std::abs(d.y1 - std::max({a.y2, b.y2, c.y2}) - 26.0f) < 0.01f &&
              plainParts.chips.empty(),
          "a diagram without labels keeps its gaps");

    // The last resort: a loop on an element boxed in by its own boundary's
    // frame and a close neighbour takes its label off to the right of the
    // diagram, joined to the loop by a leader
    const char* boxedIn = R"(C4Context
    System_Boundary(box, "Box") {
        System(alone, "Alone", "Alone in its boundary")
    }
    System(next, "Next", "Right beside it")
    Rel(alone, alone, "Checks itself")
)";
    auto spilled = checkC4Labels("boxed-in loop", boxedIn, 1.0f);
    auto spilledParts = c4checks::partsOf(spilled);
    c4checks::Box chip, next;
    check(c4checks::chipBox(spilledParts, "Checks itself", chip) &&
              c4checks::elementBox(spilledParts, "Next", next) &&
              chip.x1 > next.x2 && chip.x2 <= spilled.width,
          "a label with no room left goes right of the diagram");
}

// The fixture's diagrams, among other Markdown, all keep their labels clear
void testC4LabelFixture() {
    std::ifstream file(TINTA_MERMAID_C4_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "C4 label fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = mermaidFences(markdown);
    check(fences.size() == 8, "the C4 label fixture has its eight diagrams");
    for (size_t i = 0; i < fences.size(); i++) {
        check(mermaidext::detectKind(fences[i]) == mermaidext::Kind::C4,
              "every fixture fence is a C4 diagram");
        for (float scale : {1.0f, 1.5f}) {
            checkC4Labels("fixture fence " + std::to_string(i + 1), fences[i],
                          scale);
        }
    }
}

// Crowded diagrams: eight rows of elements, each related to the next three,
// so lines cross every row and gap; and sixteen relations between the same
// two elements, more lanes than their boxes can hold apart
void testC4CrowdedLabels() {
    std::string rows = "C4Container\n";
    for (int i = 0; i < 24; i++) {
        rows += "Container(e" + std::to_string(i) + ", \"Element " +
                std::to_string(i) + "\", \"Go\", \"Does part " +
                std::to_string(i) + " of the work\")\n";
    }
    for (int i = 0; i < 24; i++) {
        for (int step = 1; step <= 3 && i + step < 24; step++) {
            rows += "Rel(e" + std::to_string(i) + ", e" +
                    std::to_string(i + step) + ", \"Calls " +
                    std::to_string(i) + " to " + std::to_string(i + step) +
                    "\", \"JSON\")\n";
        }
    }
    checkC4Labels("eight crowded rows", rows, 1.0f);

    std::string pair = "C4Container\n"
                       "Container(a, \"Client\", \"Go\")\n"
                       "Container(b, \"Server\", \"Go\")\n";
    for (int i = 0; i < 16; i++) {
        pair += "Rel(" + std::string(i % 2 ? "b, a" : "a, b") +
                ", \"Message " + std::to_string(i) + "\", \"HTTPS\")\n";
    }
    checkC4Labels("sixteen relations between two elements", pair, 1.0f);
}

} // namespace

int main(int argc, char** argv) {
    testStyledFlowchart();
    testGraphAliasesAndChaining();
    testLayoutDirections();
    testSubgraphs();
    testUnsupportedDiagram();
    testBomAndSemicolonStatements();
    testCyclicLayout();
    testBackslashNLineBreak();
    testMultilineQuotedLabels();
    testAttributeSyntaxRejected();
    testLayoutExposesRanks();
    testDetectKind();
    testExtBuilders();
    testExtFallbacks();
    testFrontMatterFixture();
    testC4ReportedLabels();
    testC4LabelLayouts();
    testC4CrowdedLabels();
    testC4LabelFixture();
    testAsymmetricShape();
    testAsymmetricFixture();
    testFiles(argc, argv);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Mermaid tests passed\n";
    return 0;
}
