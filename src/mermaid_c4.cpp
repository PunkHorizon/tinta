// Native Mermaid requirement, C4, and architecture diagrams.
//
// requirement: typed boxes (<<Requirement>> / <<Element>>) linked by dashed
//              labeled relations, laid out in layers by relation depth.
// C4:          Person/System/Container/Component boxes in their standard
//              palette, nested boundaries, straight labeled relations.
// architecture: services with line-art icons placed on an integer grid by
//              the side constraints of their edges, grouped by dashed boxes.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

// name(arg, "quoted, arg", ...) -> args; returns false without a paren list
bool splitArgs(std::string_view rest, std::vector<std::string>& args) {
    rest = trimView(rest);
    if (rest.empty() || rest.front() != '(') return false;
    size_t depth = 0;
    bool inQuote = false;
    std::string current;
    for (size_t i = 0; i < rest.size(); i++) {
        char c = rest[i];
        if (inQuote) {
            if (c == '"') inQuote = false;
            else current += c;
            continue;
        }
        if (c == '"') {
            inQuote = true;
            continue;
        }
        if (c == '(') {
            depth++;
            if (depth == 1) continue;
        }
        if (c == ')') {
            depth--;
            if (depth == 0) {
                args.push_back(std::string(trimView(current)));
                return true;
            }
        }
        if (c == ',' && depth == 1) {
            args.push_back(std::string(trimView(current)));
            current.clear();
            continue;
        }
        current += c;
    }
    return false;
}

Point clipRect(float cx, float cy, float ox, float oy, float x1, float y1,
               float x2, float y2) {
    float dx = ox - cx;
    float dy = oy - cy;
    float t = 1.0f;
    if (dx > 0.0001f) t = std::min(t, (x2 - cx) / dx);
    if (dx < -0.0001f) t = std::min(t, (x1 - cx) / dx);
    if (dy > 0.0001f) t = std::min(t, (y2 - cy) / dy);
    if (dy < -0.0001f) t = std::min(t, (y1 - cy) / dy);
    return {cx + dx * t, cy + dy * t};
}

}  // namespace

// --------------------------------------------------------------------------
// Requirement diagrams
// --------------------------------------------------------------------------

namespace {

struct ReqNode {
    std::string name;
    std::string typeName;   // shown as <<typeName>>
    std::vector<std::string> bodyLines;
    int rank = 0;
    float x = 0, y = 0, width = 0, height = 0;
};

struct ReqEdge {
    size_t from = 0;
    size_t to = 0;
    std::string label;
};

const char* kReqTypes[] = {
    "requirement",         "functionalRequirement", "interfaceRequirement",
    "performanceRequirement", "physicalRequirement", "designConstraint",
    "element",
};

const char* kReqRelations[] = {
    "contains", "copies", "derives", "satisfies", "verifies", "refines",
    "traces",
};

}  // namespace

Built buildRequirement(std::string_view source, const Measure& measure,
                       float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<ReqNode> nodes;
    std::vector<ReqEdge> edges;
    auto findNode = [&](std::string_view name) -> size_t {
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i].name == name) return i;
        }
        return SIZE_MAX;
    };

    bool sawHeader = false;
    size_t openNode = SIZE_MAX;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "requirementDiagram")) {
                result.error = "Expected requirementDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        if (openNode != SIZE_MAX) {
            if (line == "}") {
                openNode = SIZE_MAX;
                continue;
            }
            size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                result.error = "Expected key: value";
                return result;
            }
            std::string key(trimView(line.substr(0, colon)));
            std::string value(trimView(line.substr(colon + 1)));
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            const char* shown = nullptr;
            if (key == "id") shown = "Id";
            else if (key == "text") shown = "Text";
            else if (key == "risk") shown = "Risk";
            else if (key == "verifymethod") shown = "Verification";
            else if (key == "type") shown = "Type";
            else if (key == "docref") shown = "Doc Ref";
            if (!shown) {
                result.error = "Unknown requirement field";
                return result;
            }
            nodes[openNode].bodyLines.push_back(std::string(shown) + ": " +
                                                value);
            continue;
        }

        // Type name { ... } opens a node
        bool matched = false;
        for (const char* type : kReqTypes) {
            std::string_view rest;
            if (!startsWithWord(line, type, &rest)) continue;
            matched = true;
            bool sameLineBrace = !rest.empty() && rest.back() == '{';
            if (sameLineBrace) rest = trimView(rest.substr(0, rest.size() - 1));
            ReqNode node;
            node.name = std::string(rest);
            node.typeName = type;
            if (node.name.empty()) {
                result.error = "Requirement without a name";
                return result;
            }
            nodes.push_back(std::move(node));
            openNode = nodes.size() - 1;
            break;
        }
        if (matched) continue;

        // source - relation -> destination
        size_t dash = line.find(" - ");
        size_t arrow = line.find("-> ");
        if (dash != std::string_view::npos && arrow != std::string_view::npos &&
            arrow > dash) {
            std::string_view from = trimView(line.substr(0, dash));
            std::string_view relation =
                trimView(line.substr(dash + 3, arrow - dash - 3));
            if (!relation.empty() && relation.back() == '-') {
                relation = trimView(relation.substr(0, relation.size() - 1));
            }
            std::string_view to = trimView(line.substr(arrow + 3));
            bool known = false;
            for (const char* name : kReqRelations) {
                if (relation == name) known = true;
            }
            size_t fromIndex = findNode(from);
            size_t toIndex = findNode(to);
            if (!known || fromIndex == SIZE_MAX || toIndex == SIZE_MAX) {
                result.error = "Unsupported requirement relation";
                return result;
            }
            ReqEdge edge;
            edge.from = fromIndex;
            edge.to = toIndex;
            edge.label = "<<" + std::string(relation) + ">>";
            edges.push_back(std::move(edge));
            continue;
        }
        result.error = "Unsupported requirement statement";
        return result;
    }
    if (nodes.empty()) {
        result.error = "Empty requirement diagram";
        return result;
    }

    // Layered by relation depth
    for (size_t pass = 0; pass <= nodes.size(); pass++) {
        bool changed = false;
        for (const auto& edge : edges) {
            if (nodes[edge.to].rank < nodes[edge.from].rank + 1) {
                nodes[edge.to].rank = nodes[edge.from].rank + 1;
                changed = true;
            }
        }
        if (!changed) break;
        if (pass == nodes.size()) {
            result.error = "Requirement relations form a cycle";
            return result;
        }
    }

    TextStyle stereotypeStyle;
    stereotypeStyle.italic = true;
    stereotypeStyle.scale = 0.8f;
    TextStyle nameStyle;
    nameStyle.bold = true;
    TextStyle bodyStyle;
    bodyStyle.scale = 0.85f;

    float padX = 12.0f * scale;
    float padY = 8.0f * scale;
    float bodyWrap = 220.0f * scale;
    for (auto& node : nodes) {
        std::string stereotype = "<<" + node.typeName + ">>";
        Size stereoSize = measure(stereotype, stereotypeStyle, 0.0f);
        Size nameSize = measure(node.name, nameStyle, 0.0f);
        float width = std::max(stereoSize.w, nameSize.w);
        float height = stereoSize.h + nameSize.h + 6.0f * scale;
        for (const auto& lineText : node.bodyLines) {
            Size size = measure(lineText, bodyStyle, bodyWrap);
            width = std::max(width, std::min(size.w, bodyWrap));
            height += size.h + 2.0f * scale;
        }
        node.width = std::max(170.0f * scale, width + padX * 2.0f);
        node.height = height + padY * 2.0f + 4.0f * scale;
    }

    int maxRank = 0;
    for (const auto& node : nodes) maxRank = std::max(maxRank, node.rank);
    float nodeGap = 30.0f * scale;
    float rankGap = 64.0f * scale;
    std::vector<float> rowWidths(maxRank + 1, 0.0f);
    std::vector<float> rowHeights(maxRank + 1, 0.0f);
    for (const auto& node : nodes) {
        rowWidths[node.rank] += node.width + nodeGap;
        rowHeights[node.rank] = std::max(rowHeights[node.rank], node.height);
    }
    float diagramWidth = 0.0f;
    for (float w : rowWidths) diagramWidth = std::max(diagramWidth, w - nodeGap);

    std::vector<float> rowCursor(maxRank + 1);
    std::vector<float> rowTop(maxRank + 1);
    float y = 4.0f * scale;
    for (int r = 0; r <= maxRank; r++) {
        rowCursor[r] = (diagramWidth - (rowWidths[r] - nodeGap)) * 0.5f;
        rowTop[r] = y;
        y += rowHeights[r] + rankGap;
    }
    for (auto& node : nodes) {
        node.x = rowCursor[node.rank];
        node.y = rowTop[node.rank];
        rowCursor[node.rank] += node.width + nodeGap;
    }

    // Edges first, boxes cover their ends. Rank-skipping relations would
    // cut straight through the boxes between them; those detour through an
    // exterior lane on the right like the flowchart engine
    std::vector<Prim> texts;
    size_t exteriorLane = 0;
    float rightExtent = diagramWidth;
    for (const auto& edge : edges) {
        const auto& from = nodes[edge.from];
        const auto& to = nodes[edge.to];
        float fx = from.x + from.width * 0.5f;
        float fy = from.y + from.height * 0.5f;
        float tx = to.x + to.width * 0.5f;
        float ty = to.y + to.height * 0.5f;
        int rankSpan = nodes[edge.to].rank - nodes[edge.from].rank;
        float cx, cy;
        if (rankSpan > 1 || rankSpan < -1) {
            float lane = diagramWidth +
                         (24.0f + exteriorLane++ * 14.0f) * scale;
            float startY = fy;
            float endY = ty;
            Prim out;
            out.type = PrimType::Line;
            out.x1 = from.x + from.width;
            out.y1 = startY;
            out.x2 = lane;
            out.y2 = startY;
            out.stroke = Role::Muted;
            out.strokeWidth = 1.3f * scale;
            out.dashed = true;
            result.prims.push_back(out);
            Prim down = out;
            down.x1 = lane;
            down.y1 = startY;
            down.x2 = lane;
            down.y2 = endY;
            result.prims.push_back(down);
            Prim in = out;
            in.x1 = lane;
            in.y1 = endY;
            in.x2 = to.x + to.width;
            in.y2 = endY;
            in.openArrow = true;
            result.prims.push_back(in);
            cx = lane;
            cy = (startY + endY) * 0.5f;
            rightExtent = std::max(rightExtent, lane + 8.0f * scale);
        } else {
            Point start = clipRect(fx, fy, tx, ty, from.x, from.y,
                                   from.x + from.width,
                                   from.y + from.height);
            Point end = clipRect(tx, ty, fx, fy, to.x, to.y,
                                 to.x + to.width, to.y + to.height);
            Prim line;
            line.type = PrimType::Line;
            line.x1 = start.x;
            line.y1 = start.y;
            line.x2 = end.x;
            line.y2 = end.y;
            line.stroke = Role::Muted;
            line.strokeWidth = 1.3f * scale;
            line.dashed = true;
            line.openArrow = true;
            result.prims.push_back(std::move(line));
            cx = (start.x + end.x) * 0.5f;
            cy = (start.y + end.y) * 0.5f;
        }

        TextStyle labelStyle;
        labelStyle.scale = 0.78f;
        labelStyle.italic = true;
        Size size = measure(edge.label, labelStyle, 0.0f);
        if (rankSpan > 1 || rankSpan < -1) {
            // Lane chips sit fully right of the lane, clear of the boxes
            cx += size.w * 0.5f + 8.0f * scale;
            rightExtent = std::max(rightExtent,
                                   cx + size.w * 0.5f + 8.0f * scale);
        }
        Prim chip;
        chip.type = PrimType::Rect;
        chip.x1 = cx - size.w * 0.5f - 4.0f * scale;
        chip.y1 = cy - size.h * 0.5f - 1.0f * scale;
        chip.x2 = cx + size.w * 0.5f + 4.0f * scale;
        chip.y2 = cy + size.h * 0.5f + 1.0f * scale;
        chip.fill = Role::Background;
        result.prims.push_back(chip);
        Prim label;
        label.type = PrimType::Text;
        label.text = edge.label;
        label.style = labelStyle;
        label.fill = Role::Muted;
        label.x1 = chip.x1;
        label.y1 = chip.y1;
        label.x2 = chip.x2;
        label.y2 = chip.y2;
        texts.push_back(std::move(label));
    }

    for (const auto& node : nodes) {
        Prim box;
        box.type = PrimType::Rect;
        box.x1 = node.x;
        box.y1 = node.y;
        box.x2 = node.x + node.width;
        box.y2 = node.y + node.height;
        box.fill = Role::Fill;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.4f * scale;
        result.prims.push_back(box);

        float textY = node.y + padY;
        std::string stereotype = "<<" + node.typeName + ">>";
        Size stereoSize = measure(stereotype, stereotypeStyle, 0.0f);
        Prim stereo;
        stereo.type = PrimType::Text;
        stereo.text = stereotype;
        stereo.style = stereotypeStyle;
        stereo.fill = Role::Muted;
        stereo.x1 = node.x + padX;
        stereo.y1 = textY;
        stereo.x2 = node.x + node.width - padX;
        stereo.y2 = textY + stereoSize.h;
        texts.push_back(std::move(stereo));
        textY += stereoSize.h;

        Size nameSize = measure(node.name, nameStyle, 0.0f);
        Prim nameText;
        nameText.type = PrimType::Text;
        nameText.text = node.name;
        nameText.style = nameStyle;
        nameText.fill = Role::Text;
        nameText.x1 = node.x + padX;
        nameText.y1 = textY;
        nameText.x2 = node.x + node.width - padX;
        nameText.y2 = textY + nameSize.h;
        texts.push_back(std::move(nameText));
        textY += nameSize.h + 6.0f * scale;

        // Separator under the header
        Prim rule;
        rule.type = PrimType::Line;
        rule.x1 = node.x;
        rule.y1 = textY - 3.0f * scale;
        rule.x2 = node.x + node.width;
        rule.y2 = rule.y1;
        rule.stroke = Role::Stroke;
        rule.strokeWidth = 1.0f * scale;
        result.prims.push_back(std::move(rule));

        for (const auto& lineText : node.bodyLines) {
            Size size = measure(lineText, bodyStyle,
                                node.width - padX * 2.0f);
            Prim body;
            body.type = PrimType::Text;
            body.text = lineText;
            body.style = bodyStyle;
            body.fill = Role::Text;
            body.alignH = -1;
            body.alignV = -1;
            body.x1 = node.x + padX;
            body.y1 = textY;
            body.x2 = node.x + node.width - padX;
            body.y2 = textY + size.h;
            texts.push_back(std::move(body));
            textY += size.h + 2.0f * scale;
        }
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    result.width = rightExtent;
    result.height = rowTop[maxRank] + rowHeights[maxRank] + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

// --------------------------------------------------------------------------
// C4 diagrams
// --------------------------------------------------------------------------

namespace {

enum class C4Kind { Person, System, Container, Component };

struct C4Element {
    std::string alias;
    std::string label;
    std::string techn;
    std::string descr;
    C4Kind kind = C4Kind::System;
    bool external = false;
    bool db = false;
    bool queue = false;
    float x = 0, y = 0, width = 0, height = 0;
};

struct C4Boundary {
    std::string label;
    std::string type;
    // Members in declaration order: true = element index, false = boundary
    std::vector<std::pair<bool, size_t>> members;
    size_t parent = SIZE_MAX;
    float x = 0, y = 0, width = 0, height = 0;
    // Space after each member in its row and after each row, widened where
    // relation labels sit; the rows as placed
    std::vector<float> gapAfter, rowGapAfter;
    std::vector<float> rowTop, rowHeight;
};

struct C4Rel {
    std::string from;
    std::string to;
    std::string label;
    std::string techn;
    bool bidirectional = false;
};

// Free space that holds relation labels: between two members of a row, or
// between two rows. Its labels sit in tiers across it, each tier as deep as
// the largest label
struct C4Gap {
    size_t boundary = 0;
    bool betweenRows = false;
    size_t index = 0;   // the member or row it follows
    float extent = 0;   // the largest label, measured across the gap
    int labels = 0;     // labels that sit in it, which bound its tiers
    int tiers = 1;
};

struct C4Box {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

C4Box inflate(const C4Box& box, float by) {
    return {box.x1 - by, box.y1 - by, box.x2 + by, box.y2 + by};
}

bool overlaps(const C4Box& a, const C4Box& b) {
    return a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2;
}

// Does the segment p-q pass through the box? (Liang-Barsky)
bool crosses(Point p, Point q, const C4Box& box) {
    float dx = q.x - p.x, dy = q.y - p.y;
    const float edges[4][2] = {{-dx, p.x - box.x1},
                               {dx, box.x2 - p.x},
                               {-dy, p.y - box.y1},
                               {dy, box.y2 - p.y}};
    float t0 = 0.0f, t1 = 1.0f;
    for (const auto& edge : edges) {
        if (std::abs(edge[0]) < 1e-6f) {
            if (edge[1] < 0.0f) return false;
            continue;
        }
        float t = edge[1] / edge[0];
        if (edge[0] < 0.0f) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    return true;
}

struct C4Colors {
    float bgR, bgG, bgB;
    float fgR, fgG, fgB;
};

C4Colors c4Palette(const C4Element& element) {
    if (element.kind == C4Kind::Person) {
        return element.external
                   ? C4Colors{0.41f, 0.41f, 0.41f, 1.0f, 1.0f, 1.0f}
                   : C4Colors{0.031f, 0.259f, 0.482f, 1.0f, 1.0f, 1.0f};
    }
    if (element.kind == C4Kind::Container) {
        return element.external
                   ? C4Colors{0.70f, 0.70f, 0.70f, 0.10f, 0.10f, 0.10f}
                   : C4Colors{0.263f, 0.553f, 0.835f, 1.0f, 1.0f, 1.0f};
    }
    if (element.kind == C4Kind::Component) {
        return element.external
                   ? C4Colors{0.80f, 0.80f, 0.80f, 0.10f, 0.10f, 0.10f}
                   : C4Colors{0.522f, 0.733f, 0.941f, 0.08f, 0.08f, 0.08f};
    }
    return element.external
               ? C4Colors{0.60f, 0.60f, 0.60f, 1.0f, 1.0f, 1.0f}
               : C4Colors{0.067f, 0.408f, 0.741f, 1.0f, 1.0f, 1.0f};
}

constexpr float kC4ElementWidth = 200.0f;
constexpr int kC4PerRow = 3;
// Layout passes that may widen a gap for a label that did not fit
constexpr int kC4LabelPasses = 12;

}  // namespace

Built buildC4(std::string_view source, const Measure& measure, float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<C4Element> elements;
    std::vector<C4Boundary> boundaries;
    boundaries.push_back({});  // implicit root
    std::vector<size_t> stack = {0};
    std::vector<C4Rel> rels;

    auto keyword = [](std::string_view line) {
        size_t end = 0;
        while (end < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[end])) ||
                line[end] == '_')) {
            end++;
        }
        return std::string(line.substr(0, end));
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            sawHeader = true;  // C4Context / C4Container / ... all accepted
            continue;
        }
        if (line == "}") {
            if (stack.size() <= 1) {
                result.error = "Unmatched }";
                return result;
            }
            stack.pop_back();
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "title", &rest)) {
            title = cleanLabel(rest);
            continue;
        }
        std::string word = keyword(line);
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        std::string_view tail = line.substr(word.size());

        // Style and layout tweaks do not affect the native rendering
        if (lower.rfind("update", 0) == 0 || lower.rfind("show_", 0) == 0 ||
            lower == "show_legend" || lower.rfind("layout", 0) == 0) {
            continue;
        }

        bool opensScope = false;
        std::string_view scopeCheck = trimView(tail);
        if (!scopeCheck.empty() && scopeCheck.back() == '{') {
            opensScope = true;
        }

        std::vector<std::string> args;
        bool hasArgs = splitArgs(tail, args);

        // Boundaries and deployment nodes nest
        if (lower == "boundary" || lower == "enterprise_boundary" ||
            lower == "system_boundary" || lower == "container_boundary" ||
            lower == "deployment_node" || lower == "node" ||
            lower == "node_l" || lower == "node_r") {
            if (!hasArgs || args.size() < 2) {
                result.error = "Bad boundary";
                return result;
            }
            C4Boundary boundary;
            boundary.label = cleanLabel(args[1]);
            if (lower == "enterprise_boundary") boundary.type = "enterprise";
            else if (lower == "system_boundary") boundary.type = "system";
            else if (lower == "container_boundary") boundary.type = "container";
            else if (lower == "deployment_node" || lower == "node" ||
                     lower == "node_l" || lower == "node_r") {
                boundary.type = args.size() > 2 ? args[2] : "node";
            } else if (args.size() > 2) {
                boundary.type = args[2];
            }
            boundary.parent = stack.back();
            boundaries.push_back(std::move(boundary));
            boundaries[stack.back()].members.push_back(
                {false, boundaries.size() - 1});
            stack.push_back(boundaries.size() - 1);
            if (!opensScope) stack.pop_back();  // empty boundary
            continue;
        }

        if (lower.rfind("rel", 0) == 0 || lower.rfind("birel", 0) == 0) {
            if (!hasArgs || args.size() < 3) {
                result.error = "Bad relation";
                return result;
            }
            C4Rel rel;
            rel.from = args[0];
            rel.to = args[1];
            rel.label = cleanLabel(args[2]);
            if (args.size() > 3) rel.techn = cleanLabel(args[3]);
            rel.bidirectional = lower.rfind("birel", 0) == 0;
            rels.push_back(std::move(rel));
            continue;
        }

        // Element constructs
        C4Element element;
        bool isElement = true;
        if (lower.rfind("person", 0) == 0) {
            element.kind = C4Kind::Person;
        } else if (lower.rfind("systemdb", 0) == 0) {
            element.kind = C4Kind::System;
            element.db = true;
        } else if (lower.rfind("systemqueue", 0) == 0) {
            element.kind = C4Kind::System;
            element.queue = true;
        } else if (lower.rfind("system", 0) == 0) {
            element.kind = C4Kind::System;
        } else if (lower.rfind("containerdb", 0) == 0) {
            element.kind = C4Kind::Container;
            element.db = true;
        } else if (lower.rfind("containerqueue", 0) == 0) {
            element.kind = C4Kind::Container;
            element.queue = true;
        } else if (lower.rfind("container", 0) == 0) {
            element.kind = C4Kind::Container;
        } else if (lower.rfind("componentdb", 0) == 0) {
            element.kind = C4Kind::Component;
            element.db = true;
        } else if (lower.rfind("componentqueue", 0) == 0) {
            element.kind = C4Kind::Component;
            element.queue = true;
        } else if (lower.rfind("component", 0) == 0) {
            element.kind = C4Kind::Component;
        } else {
            isElement = false;
        }
        if (!isElement) {
            result.error = "Unsupported C4 statement";
            return result;
        }
        if (!hasArgs || args.size() < 2) {
            result.error = "Bad C4 element";
            return result;
        }
        element.external =
            lower.find("_ext") != std::string::npos;
        element.alias = args[0];
        element.label = cleanLabel(args[1]);
        bool hasTechn = element.kind == C4Kind::Container ||
                        element.kind == C4Kind::Component;
        if (hasTechn) {
            if (args.size() > 2) element.techn = cleanLabel(args[2]);
            if (args.size() > 3) element.descr = cleanLabel(args[3]);
        } else if (args.size() > 2) {
            element.descr = cleanLabel(args[2]);
        }
        elements.push_back(std::move(element));
        boundaries[stack.back()].members.push_back(
            {true, elements.size() - 1});
    }
    if (elements.empty()) {
        result.error = "Empty C4 diagram";
        return result;
    }

    TextStyle labelStyle;
    labelStyle.bold = true;
    labelStyle.scale = 0.95f;
    TextStyle technStyle;
    technStyle.scale = 0.75f;
    technStyle.italic = true;
    TextStyle descrStyle;
    descrStyle.scale = 0.8f;

    // Element sizes
    float elementWidth = kC4ElementWidth * scale;
    float wrap = elementWidth - 24.0f * scale;
    for (auto& element : elements) {
        float height = 14.0f * scale;
        if (element.kind == C4Kind::Person) height += 28.0f * scale;
        if (element.db) height += 10.0f * scale;
        height += measure(element.label, labelStyle, wrap).h;
        if (!element.techn.empty()) {
            height +=
                measure("[" + element.techn + "]", technStyle, wrap).h +
                2.0f * scale;
        }
        if (!element.descr.empty()) {
            height += measure(element.descr, descrStyle, wrap).h +
                      4.0f * scale;
        }
        height += 12.0f * scale;
        element.width = elementWidth;
        element.height = std::max(height, 56.0f * scale);
    }

    // Recursive boundary layout: members flow in rows of kC4PerRow
    float memberGapX = 24.0f * scale;
    float memberGapY = 26.0f * scale;
    float boundaryPad = 18.0f * scale;
    float boundaryLabelSpace = 26.0f * scale;
    // Relation labels keep this far from the boxes, which leaves their
    // arrowheads in view, and labelGap from frames, captions and each other
    float labelClear = 16.0f * scale;
    float labelGap = 6.0f * scale;

    // Where each element and boundary sits: holding boundary, member index
    std::vector<std::pair<size_t, size_t>> elementSlot(elements.size());
    std::vector<std::pair<size_t, size_t>> boundarySlot(boundaries.size());
    for (size_t b = 0; b < boundaries.size(); b++) {
        auto& boundary = boundaries[b];
        for (size_t i = 0; i < boundary.members.size(); i++) {
            auto member = boundary.members[i];
            (member.first ? elementSlot : boundarySlot)[member.second] = {b, i};
        }
        boundary.gapAfter.assign(boundary.members.size(), memberGapX);
        boundary.rowGapAfter.assign(
            (boundary.members.size() + kC4PerRow - 1) / kC4PerRow, memberGapY);
    }

    struct SizeF {
        float w = 0, h = 0;
    };
    auto memberSize = [&](const C4Boundary& boundary, size_t i) -> SizeF {
        auto member = boundary.members[i];
        if (member.first) {
            return {elements[member.second].width,
                    elements[member.second].height};
        }
        return {boundaries[member.second].width,
                boundaries[member.second].height};
    };
    auto memberLeft = [&](const C4Boundary& boundary, size_t i) {
        auto member = boundary.members[i];
        return member.first ? elements[member.second].x
                            : boundaries[member.second].x;
    };
    // Members [start, end) of a row side by side, with the gaps between
    auto rowExtent = [&](const C4Boundary& boundary, size_t start,
                         size_t end) {
        SizeF extent;
        for (size_t i = start; i < end; i++) {
            SizeF size = memberSize(boundary, i);
            extent.w += size.w + (i + 1 < end ? boundary.gapAfter[i] : 0.0f);
            extent.h = std::max(extent.h, size.h);
        }
        return extent;
    };

    std::function<SizeF(size_t)> measureBoundary =
        [&](size_t index) -> SizeF {
        auto& boundary = boundaries[index];
        for (const auto& member : boundary.members) {
            if (!member.first) measureBoundary(member.second);
        }
        // Members flow in rows in declaration order
        float width = 0.0f, height = 0.0f;
        size_t count = boundary.members.size();
        for (size_t start = 0; start < count; start += kC4PerRow) {
            size_t end = std::min(start + kC4PerRow, count);
            SizeF row = rowExtent(boundary, start, end);
            width = std::max(width, row.w);
            height += row.h;
            if (end < count) height += boundary.rowGapAfter[start / kC4PerRow];
        }
        SizeF size;
        if (index == 0) {
            size = {width, height};
        } else {
            size = {width + boundaryPad * 2.0f,
                    height + boundaryPad * 2.0f + boundaryLabelSpace};
        }
        boundary.width = size.w;
        boundary.height = size.h;
        return size;
    };

    std::function<void(size_t, float, float)> placeBoundary =
        [&](size_t index, float x, float y) {
        auto& boundary = boundaries[index];
        boundary.x = x;
        boundary.y = y;
        float innerX = index == 0 ? x : x + boundaryPad;
        float innerY = index == 0 ? y : y + boundaryPad;
        float innerWidth =
            index == 0 ? boundary.width : boundary.width - boundaryPad * 2.0f;
        boundary.rowTop.clear();
        boundary.rowHeight.clear();
        float rowTop = innerY;
        size_t count = boundary.members.size();
        for (size_t start = 0; start < count; start += kC4PerRow) {
            size_t end = std::min(start + kC4PerRow, count);
            SizeF row = rowExtent(boundary, start, end);
            float cursor = innerX + (innerWidth - row.w) * 0.5f;
            for (size_t i = start; i < end; i++) {
                SizeF size = memberSize(boundary, i);
                float memberY = rowTop + (row.h - size.h) * 0.5f;
                auto member = boundary.members[i];
                if (member.first) {
                    elements[member.second].x = cursor;
                    elements[member.second].y = memberY;
                } else {
                    placeBoundary(member.second, cursor, memberY);
                }
                cursor += size.w + (i + 1 < end ? boundary.gapAfter[i] : 0.0f);
            }
            boundary.rowTop.push_back(rowTop);
            boundary.rowHeight.push_back(row.h);
            if (end < count) {
                rowTop += row.h + boundary.rowGapAfter[start / kC4PerRow];
            }
        }
    };

    // Relations: their elements and their label chips' sizes
    struct C4Link {
        size_t from = 0, to = 0;
        size_t loop = 0;        // among the element's loops to itself
        size_t gap = SIZE_MAX;  // the gap its label sits in
        Size labelSize, technSize;
        std::string technText;
        float chipW = 0, chipH = 0;  // no chip without a label
        std::vector<std::pair<Point, Point>> segments;
        Point a, b;  // the stretch its label rides on
        bool placed = false;
        bool leader = false;  // placed off the diagram, joined by a leader
        C4Box chip;
    };
    auto findElement = [&](const std::string& alias) -> size_t {
        for (size_t i = 0; i < elements.size(); i++) {
            if (elements[i].alias == alias) return i;
        }
        return SIZE_MAX;
    };
    TextStyle relStyle;
    relStyle.scale = 0.78f;
    TextStyle relTechnStyle;
    relTechnStyle.scale = 0.7f;
    relTechnStyle.italic = true;
    float relWrap = 150.0f * scale;
    std::vector<C4Link> links(rels.size());
    for (size_t r = 0; r < rels.size(); r++) {
        auto& link = links[r];
        link.from = findElement(rels[r].from);
        link.to = findElement(rels[r].to);
        if (link.from == SIZE_MAX || link.to == SIZE_MAX) {
            result.error = "Unknown alias in relation";
            return result;
        }
        if (link.from == link.to) {
            for (size_t earlier = 0; earlier < r; earlier++) {
                if (links[earlier].from == link.from &&
                    links[earlier].to == link.from) {
                    link.loop++;
                }
            }
        }
        if (rels[r].label.empty()) continue;
        link.labelSize = measure(rels[r].label, relStyle, relWrap);
        float textW = std::min(link.labelSize.w, relWrap);
        if (!rels[r].techn.empty()) {
            link.technText = "[" + rels[r].techn + "]";
            link.technSize = measure(link.technText, relTechnStyle, relWrap);
            textW = std::max(textW, link.technSize.w);
        }
        link.chipW = textW + 10.0f * scale;
        link.chipH = link.labelSize.h + link.technSize.h + 4.0f * scale;
    }

    // Each label's gap, in the innermost boundary that holds both elements:
    // beside the member the relation leaves, toward the member it reaches,
    // in their row or between their rows
    std::vector<C4Gap> gaps;
    auto chainOf = [&](size_t element) {
        std::vector<std::pair<size_t, size_t>> chain = {elementSlot[element]};
        while (chain.back().first != 0) {
            chain.push_back(boundarySlot[chain.back().first]);
        }
        return chain;
    };
    for (auto& link : links) {
        if (link.chipW <= 0.0f) continue;
        C4Gap home;
        home.index = SIZE_MAX;
        float extent = link.chipW;
        if (link.from == link.to) {
            // A loop off the element's right side, into the gap after it
            auto slot = elementSlot[link.from];
            if ((slot.second + 1) % kC4PerRow != 0 &&
                slot.second + 1 < boundaries[slot.first].members.size()) {
                home.boundary = slot.first;
                home.index = slot.second;
            }
        } else {
            auto chainA = chainOf(link.from);
            auto chainB = chainOf(link.to);
            size_t memberA = 0, memberB = 0;
            for (const auto& slotA : chainA) {
                auto slotB = std::find_if(
                    chainB.begin(), chainB.end(),
                    [&](const auto& slot) { return slot.first == slotA.first; });
                if (slotB == chainB.end()) continue;
                home.boundary = slotA.first;
                memberA = slotA.second;
                memberB = slotB->second;
                break;
            }
            size_t rowA = memberA / kC4PerRow, rowB = memberB / kC4PerRow;
            if (rowA == rowB) {
                home.index = memberA < memberB ? memberA : memberA - 1;
            } else {
                home.betweenRows = true;
                home.index = rowA < rowB ? rowA : rowA - 1;
                extent = link.chipH;
            }
        }
        if (home.index == SIZE_MAX) continue;
        for (size_t g = 0; g < gaps.size() && link.gap == SIZE_MAX; g++) {
            if (gaps[g].boundary == home.boundary &&
                gaps[g].betweenRows == home.betweenRows &&
                gaps[g].index == home.index) {
                link.gap = g;
            }
        }
        if (link.gap == SIZE_MAX) {
            link.gap = gaps.size();
            gaps.push_back(home);
        }
        gaps[link.gap].extent = std::max(gaps[link.gap].extent, extent);
        gaps[link.gap].labels++;
    }

    float top = 4.0f * scale;
    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    Size titleSize;
    if (!title.empty()) {
        titleSize = measure(title, titleStyle, 0.0f);
        top += titleSize.h + 14.0f * scale;
    }
    TextStyle boundaryStyle;
    boundaryStyle.bold = true;
    boundaryStyle.scale = 0.85f;
    auto caption = [&](const C4Boundary& boundary) {
        std::string text = boundary.label;
        if (!boundary.type.empty()) text += " [" + boundary.type + "]";
        return text;
    };
    auto captionBox = [&](const C4Boundary& boundary) {
        Size size = measure(caption(boundary), boundaryStyle, boundary.width);
        return C4Box{boundary.x + 8.0f * scale,
                     boundary.y + boundary.height - size.h - 4.0f * scale,
                     boundary.x + boundary.width - 8.0f * scale,
                     boundary.y + boundary.height - 4.0f * scale};
    };
    auto center = [&](size_t index) {
        const auto& element = elements[index];
        return Point{element.x + element.width * 0.5f,
                     element.y + element.height * 0.5f};
    };
    // Do two relations' lines run along each other: nearly parallel, and
    // touching over a stretch of both? Between the same two elements they
    // do, and also when one passes behind the box the other ends at
    auto alongside = [&](const C4Link& one, const C4Link& two) {
        Point p = center(one.from), q = center(one.to);
        Point r = center(two.from), s = center(two.to);
        float length = std::hypot(q.x - p.x, q.y - p.y);
        float other = std::hypot(s.x - r.x, s.y - r.y);
        if (length < 1.0f || other < 1.0f) return false;
        float ux = (q.x - p.x) / length, uy = (q.y - p.y) / length;
        if (std::abs(ux * (s.y - r.y) - uy * (s.x - r.x)) > 0.08f * other) {
            return false;
        }
        // The second line's ends along the first, and across it
        float a0 = (r.x - p.x) * ux + (r.y - p.y) * uy;
        float a1 = (s.x - p.x) * ux + (s.y - p.y) * uy;
        float d0 = (r.y - p.y) * ux - (r.x - p.x) * uy;
        float d1 = (s.y - p.y) * ux - (s.x - p.x) * uy;
        float low = std::max(0.0f, std::min(a0, a1));
        float high = std::min(length, std::max(a0, a1));
        if (high - low < 30.0f * scale) return false;
        float e0 = d0 + (d1 - d0) * (low - a0) / (a1 - a0);
        float e1 = d0 + (d1 - d0) * (high - a0) / (a1 - a0);
        return (e0 < 0.0f) != (e1 < 0.0f) ||
               std::min(std::abs(e0), std::abs(e1)) < 4.0f * scale;
    };

    // Lines between the element boxes. Lines that would run along each
    // other share a corridor, where they take lanes side by side, far
    // enough apart for their labels; a relation to its own element loops
    // out of the right side
    auto route = [&]() {
        size_t count = links.size();
        std::vector<size_t> corridor(count);
        for (size_t i = 0; i < count; i++) corridor[i] = i;
        auto corridorOf = [&](size_t i) {
            while (corridor[i] != i) i = corridor[i] = corridor[corridor[i]];
            return i;
        };
        for (size_t i = 0; i < count; i++) {
            for (size_t j = i + 1; j < count; j++) {
                if (links[i].from == links[i].to ||
                    links[j].from == links[j].to) {
                    continue;
                }
                if (alongside(links[i], links[j])) {
                    corridor[corridorOf(j)] = corridorOf(i);
                }
            }
        }
        for (size_t i = 0; i < count; i++) {
            auto& link = links[i];
            const auto& from = elements[link.from];
            const auto& to = elements[link.to];
            link.placed = false;
            link.leader = false;
            if (link.from == link.to) {
                // High on the side, above the lines to the neighbours
                float right = from.x + from.width;
                float reach = (22.0f + link.loop * 12.0f) * scale;
                float half = (8.0f + link.loop * 6.0f) * scale;
                float midY = from.y + std::min(from.height * 0.5f,
                                               half + 6.0f * scale +
                                                   from.height * 0.1f);
                Point out{right, midY - half}, far{right + reach, midY - half};
                Point turn{right + reach, midY + half}, back{right, midY + half};
                link.segments = {{out, far}, {far, turn}, {turn, back}};
                link.a = far;
                link.b = turn;
                continue;
            }
            std::vector<size_t> lanes;
            for (size_t j = 0; j < count; j++) {
                if (corridorOf(j) == corridorOf(i)) lanes.push_back(j);
            }
            Point start = center(link.from), end = center(link.to);
            if (lanes.size() > 1) {
                // Lanes across the corridor's first line
                const auto& first = links[lanes.front()];
                Point p = center(first.from), q = center(first.to);
                float dx = q.x - p.x, dy = q.y - p.y;
                if (dx < 0.0f || (dx == 0.0f && dy < 0.0f)) {
                    dx = -dx;
                    dy = -dy;
                }
                float length = std::hypot(dx, dy);
                float nx = -dy / length, ny = dx / length;
                float spacing = 10.0f * scale;
                // Every lane enters its boxes away from their corners
                float reach = 1e9f;
                float inset = 12.0f * scale;
                for (size_t lane : lanes) {
                    const auto& other = links[lane];
                    if (other.chipW > 0.0f) {
                        spacing = std::max(spacing,
                                           std::abs(nx) * other.chipW +
                                               std::abs(ny) * other.chipH +
                                               labelGap);
                    }
                    for (size_t element : {other.from, other.to}) {
                        const auto& box = elements[element];
                        if (std::abs(nx) > 0.001f) {
                            reach = std::min(reach, (box.width * 0.5f - inset) /
                                                        std::abs(nx));
                        }
                        if (std::abs(ny) > 0.001f) {
                            reach = std::min(reach, (box.height * 0.5f - inset) /
                                                        std::abs(ny));
                        }
                    }
                }
                spacing = std::min(spacing, 2.0f * std::max(reach, 0.0f) /
                                                (lanes.size() - 1));
                size_t lane = static_cast<size_t>(
                    std::find(lanes.begin(), lanes.end(), i) - lanes.begin());
                float offset = (lane - (lanes.size() - 1) * 0.5f) * spacing;
                start = {start.x + nx * offset, start.y + ny * offset};
                end = {end.x + nx * offset, end.y + ny * offset};
            }
            link.a = clipRect(start.x, start.y, end.x, end.y, from.x, from.y,
                              from.x + from.width, from.y + from.height);
            link.b = clipRect(end.x, end.y, start.x, start.y, to.x, to.y,
                              to.x + to.width, to.y + to.height);
            link.segments = {{link.a, link.b}};
        }
    };

    // A gap's span along its axis, between its two members or rows
    auto gapSpan = [&](const C4Gap& gap, float& low, float& high) {
        const auto& boundary = boundaries[gap.boundary];
        if (gap.betweenRows) {
            low = boundary.rowTop[gap.index] + boundary.rowHeight[gap.index];
            high = boundary.rowTop[gap.index + 1];
        } else {
            low = memberLeft(boundary, gap.index) +
                  memberSize(boundary, gap.index).w;
            high = memberLeft(boundary, gap.index + 1);
        }
    };

    // Label chips, in relation order: on the line in a tier of the label's
    // gap, else anywhere along the line, through or just beside the chip,
    // clear of boxes, frames, captions, the title and earlier chips, and
    // preferably of other relations' lines. Returns true when a label did
    // not fit and its gap gained a tier, for another layout pass.
    float diagramWidth = 0.0f;
    auto placeLabels = [&]() {
        std::vector<C4Box> blocked;
        for (const auto& element : elements) {
            blocked.push_back(inflate({element.x, element.y,
                                       element.x + element.width,
                                       element.y + element.height},
                                      labelClear));
        }
        // A chip may sit inside a boundary or outside it, not across its
        // dashed edges or on its caption
        for (size_t i = 1; i < boundaries.size(); i++) {
            const auto& boundary = boundaries[i];
            float x1 = boundary.x, y1 = boundary.y;
            float x2 = x1 + boundary.width, y2 = y1 + boundary.height;
            blocked.push_back(inflate({x1, y1, x2, y1}, labelGap));
            blocked.push_back(inflate({x1, y2, x2, y2}, labelGap));
            blocked.push_back(inflate({x1, y1, x1, y2}, labelGap));
            blocked.push_back(inflate({x2, y1, x2, y2}, labelGap));
            blocked.push_back(inflate(captionBox(boundary), labelGap));
        }
        if (!title.empty()) {
            blocked.push_back(inflate(
                {0.0f, 4.0f * scale, diagramWidth, 4.0f * scale + titleSize.h},
                labelGap));
        }
        blocked.push_back({-1e9f, -1e9f, 1e9f, 0.0f});  // above the diagram

        constexpr float kNone = 1e30f;
        float step = 2.0f * scale;
        float crossCost = 40.0f * scale;
        std::vector<C4Box> chips;
        std::vector<bool> grown(gaps.size(), false);
        bool grew = false;
        for (auto& link : links) {
            if (link.chipW <= 0.0f) continue;
            float w = link.chipW, h = link.chipH;
            // Only what lies near the line can block its chip
            C4Box around = inflate({std::min(link.a.x, link.b.x),
                                    std::min(link.a.y, link.b.y),
                                    std::max(link.a.x, link.b.x),
                                    std::max(link.a.y, link.b.y)},
                                   w + h);
            std::vector<C4Box> nearby;
            for (const auto& other : blocked) {
                if (overlaps(other, around)) nearby.push_back(other);
            }
            for (const auto& chip : chips) {
                C4Box kept = inflate(chip, labelGap);
                if (overlaps(kept, around)) nearby.push_back(kept);
            }
            float bestCost = kNone;
            C4Box best;
            auto consider = [&](float cx, float cy, float cost) {
                if (cost >= bestCost) return;
                C4Box box{cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f,
                          cy + h * 0.5f};
                for (const auto& other : nearby) {
                    if (overlaps(box, other)) return;
                }
                for (const auto& other : links) {
                    if (&other == &link) continue;
                    for (const auto& segment : other.segments) {
                        if (crosses(segment.first, segment.second, box)) {
                            cost += crossCost;
                            break;
                        }
                    }
                }
                if (cost < bestCost) {
                    bestCost = cost;
                    best = box;
                }
            };
            float dx = link.b.x - link.a.x, dy = link.b.y - link.a.y;
            float prefer = 0.5f;
            if (link.gap != SIZE_MAX && link.from != link.to) {
                const C4Gap& gap = gaps[link.gap];
                float low, high;
                gapSpan(gap, low, high);
                float axisStart = gap.betweenRows ? link.a.y : link.a.x;
                float axisRun = gap.betweenRows ? dy : dx;
                if (std::abs(axisRun) > 0.001f) {
                    float middle = ((low + high) * 0.5f - axisStart) / axisRun;
                    if (middle >= 0.0f && middle <= 1.0f) prefer = middle;
                    // Tiers centered across the gap, the middle ones first
                    float used = gap.tiers * gap.extent +
                                 (gap.tiers - 1) * labelGap;
                    float slide = gap.betweenRows
                                      ? w * 0.25f
                                      : std::max(0.0f, h * 0.5f - 3.0f * scale);
                    for (int k = 0; k < gap.tiers; k++) {
                        float at = (low + high - used) * 0.5f +
                                   gap.extent * 0.5f +
                                   k * (gap.extent + labelGap);
                        float t = (at - axisStart) / axisRun;
                        if (t < 0.0f || t > 1.0f) continue;
                        Point on{link.a.x + dx * t, link.a.y + dy * t};
                        float rank =
                            std::abs(k - (gap.tiers - 1) * 0.5f) * 2.0f * scale;
                        // Slid along the tier while the line still crosses
                        // the chip
                        for (float s = 0.0f; s <= slide; s += step) {
                            for (float sign : {1.0f, -1.0f}) {
                                if (gap.betweenRows) {
                                    consider(on.x + sign * s, on.y, rank + s);
                                } else {
                                    consider(on.x, on.y + sign * s, rank + s);
                                }
                                if (s == 0.0f) break;
                            }
                        }
                    }
                }
            }
            if (bestCost >= kNone) {
                // Anywhere along the line, nearest the gap's middle first
                float length = std::sqrt(dx * dx + dy * dy);
                bool flat = std::abs(dx) >= std::abs(dy);
                float slide = (flat ? h : w) * 0.5f + 3.0f * scale;
                int samples =
                    std::max(8, static_cast<int>(length / (4.0f * scale)));
                std::vector<std::pair<float, Point>> candidates;
                for (int i = 0; i <= samples; i++) {
                    float t = static_cast<float>(i) / samples;
                    Point on{link.a.x + dx * t, link.a.y + dy * t};
                    for (float s = -slide; s <= slide; s += 1.5f * step) {
                        float cost = std::abs(t - prefer) * length +
                                     std::abs(s) * 1.5f + 8.0f * scale;
                        candidates.push_back(
                            {cost, flat ? Point{on.x, on.y + s}
                                        : Point{on.x + s, on.y}});
                    }
                }
                std::sort(candidates.begin(), candidates.end(),
                          [](const auto& l, const auto& r) {
                              return l.first < r.first;
                          });
                for (const auto& candidate : candidates) {
                    if (candidate.first >= bestCost) break;
                    consider(candidate.second.x, candidate.second.y,
                             candidate.first);
                }
            }
            if (bestCost < kNone) {
                link.placed = true;
                link.chip = best;
                chips.push_back(best);
            } else if (link.gap != SIZE_MAX && !grown[link.gap] &&
                       gaps[link.gap].tiers <= gaps[link.gap].labels) {
                gaps[link.gap].tiers++;
                grown[link.gap] = true;
                grew = true;
            }
        }
        return grew;
    };

    for (int pass = 0;; pass++) {
        for (const auto& gap : gaps) {
            auto& boundary = boundaries[gap.boundary];
            float size = gap.tiers * gap.extent + (gap.tiers - 1) * labelGap +
                         2.0f * labelClear;
            if (gap.betweenRows) {
                boundary.rowGapAfter[gap.index] = std::max(memberGapY, size);
            } else {
                boundary.gapAfter[gap.index] = std::max(memberGapX, size);
            }
        }
        SizeF rootSize = measureBoundary(0);
        diagramWidth = std::max(rootSize.w, titleSize.w);
        placeBoundary(0, (diagramWidth - rootSize.w) * 0.5f, top);
        route();
        if (!placeLabels() || pass == kC4LabelPasses) break;
    }

    // Last resort for a label no gap could hold: right of the diagram,
    // joined to the middle of its line by a leader
    float spill = diagramWidth;
    for (const auto& element : elements) {
        spill = std::max(spill, element.x + element.width);
    }
    for (const auto& link : links) {
        if (link.placed) spill = std::max(spill, link.chip.x2);
        for (const auto& segment : link.segments) {
            spill = std::max({spill, segment.first.x, segment.second.x});
        }
    }
    spill += labelClear;
    float spillY = top;
    for (auto& link : links) {
        if (link.chipW <= 0.0f || link.placed) continue;
        float y = std::max(spillY, (link.a.y + link.b.y - link.chipH) * 0.5f);
        link.chip = {spill, y, spill + link.chipW, y + link.chipH};
        link.placed = true;
        link.leader = true;
        spillY = y + link.chipH + labelGap;
    }

    if (!title.empty()) {
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = 4.0f * scale;
        text.x2 = diagramWidth;
        text.y2 = 4.0f * scale + titleSize.h;
        result.prims.push_back(std::move(text));
    }

    std::vector<Prim> texts;

    // Boundary frames under everything else
    for (size_t i = 1; i < boundaries.size(); i++) {
        const auto& boundary = boundaries[i];
        Prim frame;
        frame.type = PrimType::Rect;
        frame.x1 = boundary.x;
        frame.y1 = boundary.y;
        frame.x2 = boundary.x + boundary.width;
        frame.y2 = boundary.y + boundary.height;
        frame.stroke = Role::Muted;
        frame.strokeWidth = 1.2f * scale;
        frame.dashed = true;
        result.prims.push_back(std::move(frame));

        C4Box box = captionBox(boundary);
        Prim label;
        label.type = PrimType::Text;
        label.text = caption(boundary);
        label.style = boundaryStyle;
        label.fill = Role::Muted;
        label.alignH = -1;
        label.x1 = box.x1;
        label.y1 = box.y1;
        label.x2 = box.x2;
        label.y2 = box.y2;
        texts.push_back(std::move(label));
    }

    // Relations under the element boxes
    for (size_t r = 0; r < links.size(); r++) {
        const auto& link = links[r];
        for (size_t s = 0; s < link.segments.size(); s++) {
            Prim line;
            line.type = PrimType::Line;
            line.x1 = link.segments[s].first.x;
            line.y1 = link.segments[s].first.y;
            line.x2 = link.segments[s].second.x;
            line.y2 = link.segments[s].second.y;
            line.stroke = Role::Muted;
            line.strokeWidth = 1.3f * scale;
            line.dashed = true;
            line.arrow = s + 1 == link.segments.size();
            result.prims.push_back(line);
        }
        if (rels[r].bidirectional) {
            Prim back;
            back.type = PrimType::Line;
            back.x1 = link.segments.front().second.x;
            back.y1 = link.segments.front().second.y;
            back.x2 = link.segments.front().first.x;
            back.y2 = link.segments.front().first.y;
            back.stroke = Role::Muted;
            back.strokeWidth = 1.3f * scale;
            back.dashed = true;
            back.arrow = true;
            result.prims.push_back(std::move(back));
        }
        if (!link.placed) continue;
        if (link.leader) {
            Prim leader;
            leader.type = PrimType::Line;
            leader.x1 = (link.a.x + link.b.x) * 0.5f;
            leader.y1 = (link.a.y + link.b.y) * 0.5f;
            leader.x2 = link.chip.x1;
            leader.y2 = (link.chip.y1 + link.chip.y2) * 0.5f;
            leader.stroke = Role::Muted;
            leader.strokeWidth = 1.0f * scale;
            leader.dashed = true;
            result.prims.push_back(std::move(leader));
        }

        // A visible chip like the flowchart edge labels: an invisible
        // background mask would punch holes through box edges instead
        Prim chip;
        chip.type = PrimType::RoundRect;
        chip.radius = 4.0f * scale;
        chip.x1 = link.chip.x1;
        chip.y1 = link.chip.y1;
        chip.x2 = link.chip.x2;
        chip.y2 = link.chip.y2;
        chip.fill = Role::Fill;
        chip.stroke = Role::Muted;
        chip.strokeWidth = 1.0f * scale;
        // Rides with the texts so it draws over the relation lines
        texts.push_back(chip);
        Prim label;
        label.type = PrimType::Text;
        label.text = rels[r].label;
        label.style = relStyle;
        label.fill = Role::Text;
        label.x1 = chip.x1 + 5.0f * scale;
        label.y1 = chip.y1 + 2.0f * scale;
        label.x2 = chip.x2 - 5.0f * scale;
        label.y2 = label.y1 + link.labelSize.h;
        texts.push_back(std::move(label));
        if (!link.technText.empty()) {
            Prim techn;
            techn.type = PrimType::Text;
            techn.text = link.technText;
            techn.style = relTechnStyle;
            techn.fill = Role::Muted;
            techn.x1 = label.x1;
            techn.y1 = label.y2;
            techn.x2 = label.x2;
            techn.y2 = label.y2 + link.technSize.h;
            texts.push_back(std::move(techn));
        }
    }

    // Element boxes
    for (const auto& element : elements) {
        C4Colors colors = c4Palette(element);
        float bodyTop = element.y;
        if (element.db) {
            // A real cylinder: silhouette plus a lid seam in a lighter mix
            Prim paint;
            paint.fill = Role::Custom;
            paint.customR = colors.bgR;
            paint.customG = colors.bgG;
            paint.customB = colors.bgB;
            paint.stroke = Role::None;
            emitCylinder(result.prims, paint, element.x, bodyTop,
                         element.x + element.width,
                         element.y + element.height, 14.0f * scale);
            Prim seamPaint = paint;
            seamPaint.stroke = Role::Custom;
            seamPaint.fill = Role::None;
            seamPaint.customR = colors.bgR * 0.7f + colors.fgR * 0.3f;
            seamPaint.customG = colors.bgG * 0.7f + colors.fgG * 0.3f;
            seamPaint.customB = colors.bgB * 0.7f + colors.fgB * 0.3f;
            seamPaint.strokeWidth = 1.3f * scale;
            // Only the seam from this second pass: skip its silhouette
            std::vector<Prim> seamPrims;
            emitCylinder(seamPrims, seamPaint, element.x, bodyTop,
                         element.x + element.width,
                         element.y + element.height, 14.0f * scale);
            for (auto& prim : seamPrims) {
                if (prim.type == PrimType::Line) {
                    result.prims.push_back(std::move(prim));
                }
            }
        } else {
            Prim box;
            box.type = PrimType::RoundRect;
            box.radius = element.queue
                             ? (element.y + element.height - bodyTop) * 0.5f
                             : 8.0f * scale;
            box.x1 = element.x;
            box.y1 = bodyTop;
            box.x2 = element.x + element.width;
            box.y2 = element.y + element.height;
            box.fill = Role::Custom;
            box.customR = colors.bgR;
            box.customG = colors.bgG;
            box.customB = colors.bgB;
            result.prims.push_back(box);
        }

        if (element.kind == C4Kind::Person) {
            // Small person glyph (head over a shoulder dome) above the label
            float cx = element.x + element.width * 0.5f;
            float glyphTop = element.y + 7.0f * scale;
            Prim shoulders;
            shoulders.type = PrimType::Slice;
            shoulders.x1 = cx;
            shoulders.y1 = glyphTop + 24.0f * scale;
            shoulders.radius = 10.0f * scale;
            shoulders.a0 = 3.14159265f;
            shoulders.a1 = 6.28318531f;
            shoulders.fill = Role::Custom;
            shoulders.customR = colors.fgR;
            shoulders.customG = colors.fgG;
            shoulders.customB = colors.fgB;
            result.prims.push_back(std::move(shoulders));
            Prim head;
            head.type = PrimType::Ellipse;
            head.x1 = cx - 6.0f * scale;
            head.y1 = glyphTop;
            head.x2 = cx + 6.0f * scale;
            head.y2 = glyphTop + 12.0f * scale;
            head.fill = Role::Custom;
            head.customR = colors.fgR;
            head.customG = colors.fgG;
            head.customB = colors.fgB;
            result.prims.push_back(std::move(head));
        }

        float textTop = bodyTop + (element.db ? 20.0f
                                   : element.kind == C4Kind::Person ? 34.0f
                                                                    : 8.0f) *
                                      scale;
        float textLeft = element.x + 12.0f * scale;
        float textRight = element.x + element.width - 12.0f * scale;
        Size labelSize = measure(element.label, labelStyle, wrap);
        Prim label;
        label.type = PrimType::Text;
        label.text = element.label;
        label.style = labelStyle;
        label.fill = Role::Custom;
        label.customR = colors.fgR;
        label.customG = colors.fgG;
        label.customB = colors.fgB;
        label.x1 = textLeft;
        label.y1 = textTop;
        label.x2 = textRight;
        label.y2 = textTop + labelSize.h;
        texts.push_back(std::move(label));
        textTop += labelSize.h;
        if (!element.techn.empty()) {
            std::string technText = "[" + element.techn + "]";
            Size technSize = measure(technText, technStyle, wrap);
            Prim techn;
            techn.type = PrimType::Text;
            techn.text = technText;
            techn.style = technStyle;
            techn.fill = Role::Custom;
            techn.customR = colors.bgR * 0.30f + colors.fgR * 0.70f;
            techn.customG = colors.bgG * 0.30f + colors.fgG * 0.70f;
            techn.customB = colors.bgB * 0.30f + colors.fgB * 0.70f;
            techn.x1 = textLeft;
            techn.y1 = textTop + 2.0f * scale;
            techn.x2 = textRight;
            techn.y2 = textTop + 2.0f * scale + technSize.h;
            texts.push_back(std::move(techn));
            textTop += technSize.h + 2.0f * scale;
        }
        if (!element.descr.empty()) {
            Size descrSize = measure(element.descr, descrStyle, wrap);
            Prim descr;
            descr.type = PrimType::Text;
            descr.text = element.descr;
            descr.style = descrStyle;
            descr.fill = Role::Custom;
            descr.customR = colors.bgR * 0.22f + colors.fgR * 0.78f;
            descr.customG = colors.bgG * 0.22f + colors.fgG * 0.78f;
            descr.customB = colors.bgB * 0.22f + colors.fgB * 0.78f;
            descr.x1 = textLeft;
            descr.y1 = textTop + 4.0f * scale;
            descr.x2 = textRight;
            descr.y2 = textTop + 4.0f * scale + descrSize.h;
            texts.push_back(std::move(descr));
        }
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    float right = diagramWidth, bottom = 0.0f;
    for (const auto& element : elements) {
        bottom = std::max(bottom, element.y + element.height);
    }
    for (const auto& boundary : boundaries) {
        bottom = std::max(bottom, boundary.y + boundary.height);
    }
    // Chips and loops may stand out past the boxes
    for (const auto& link : links) {
        for (const auto& segment : link.segments) {
            right = std::max({right, segment.first.x, segment.second.x});
        }
        if (!link.placed) continue;
        right = std::max(right, link.chip.x2);
        bottom = std::max(bottom, link.chip.y2);
    }
    result.width = right > diagramWidth ? right + 2.0f * scale : diagramWidth;
    result.height = bottom + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

// --------------------------------------------------------------------------
// Architecture diagrams
// --------------------------------------------------------------------------

namespace {

struct ArchNode {
    std::string id;
    std::string icon;
    std::string label;
    std::string group;
    bool junction = false;
    bool placed = false;
    int gridX = 0, gridY = 0;
    float x = 0, y = 0;  // icon box top-left after layout
};

struct ArchGroup {
    std::string id;
    std::string icon;
    std::string label;
    std::string parent;
};

struct ArchEdge {
    std::string from;
    std::string to;
    char fromSide = 'R';
    char toSide = 'L';
    bool arrowFrom = false;
    bool arrowTo = false;
};

constexpr float kArchIcon = 56.0f;
constexpr float kArchCell = 120.0f;

// id(icon)[Label] -> parts; icon and label optional
void parseArchEntity(std::string_view spec, std::string& id,
                     std::string& icon, std::string& label) {
    size_t paren = spec.find('(');
    size_t bracket = spec.find('[');
    size_t idEnd = std::min(paren == std::string_view::npos ? spec.size()
                                                            : paren,
                            bracket == std::string_view::npos ? spec.size()
                                                              : bracket);
    id = std::string(trimView(spec.substr(0, idEnd)));
    if (paren != std::string_view::npos) {
        size_t close = spec.find(')', paren);
        if (close != std::string_view::npos) {
            icon = std::string(trimView(
                spec.substr(paren + 1, close - paren - 1)));
        }
    }
    if (bracket != std::string_view::npos) {
        size_t close = spec.rfind(']');
        if (close != std::string_view::npos && close > bracket) {
            label = cleanLabel(
                spec.substr(bracket + 1, close - bracket - 1));
        }
    }
    if (label.empty()) label = id;
}

}  // namespace

Built buildArchitecture(std::string_view source, const Measure& measure,
                        float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<ArchNode> nodes;
    std::vector<ArchGroup> groups;
    std::vector<ArchEdge> edges;
    auto findNode = [&](std::string_view id) -> ArchNode* {
        for (auto& node : nodes) {
            if (node.id == id) return &node;
        }
        return nullptr;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "architecture-beta") &&
                !startsWithWord(line, "architecture")) {
                result.error = "Expected architecture header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "group", &rest)) {
            ArchGroup group;
            std::string_view spec = rest;
            size_t inWord = rest.rfind(" in ");
            if (inWord != std::string_view::npos) {
                group.parent = std::string(trimView(rest.substr(inWord + 4)));
                spec = trimView(rest.substr(0, inWord));
            }
            parseArchEntity(spec, group.id, group.icon, group.label);
            groups.push_back(std::move(group));
            continue;
        }
        if (startsWithWord(line, "service", &rest) ||
            startsWithWord(line, "junction", &rest)) {
            ArchNode node;
            node.junction = line[0] == 'j' || line[0] == 'J';
            std::string_view spec = rest;
            size_t inWord = rest.rfind(" in ");
            if (inWord != std::string_view::npos) {
                node.group = std::string(trimView(rest.substr(inWord + 4)));
                spec = trimView(rest.substr(0, inWord));
            }
            parseArchEntity(spec, node.id, node.icon, node.label);
            if (node.junction) node.label.clear();
            nodes.push_back(std::move(node));
            continue;
        }
        // Edge: id{group}?:S (<)?--(>)? T:id
        size_t firstColon = line.find(':');
        if (firstColon != std::string_view::npos) {
            std::string_view left = trimView(line.substr(0, firstColon));
            std::string_view remainder = line.substr(firstColon + 1);
            // strip {group} modifiers
            size_t brace = left.find('{');
            if (brace != std::string_view::npos) {
                left = left.substr(0, brace);
            }
            remainder = trimView(remainder);
            if (!remainder.empty()) {
                char fromSide =
                    static_cast<char>(std::toupper(remainder.front()));
                std::string_view afterSide = trimView(remainder.substr(1));
                bool arrowFrom = false, arrowTo = false;
                if (!afterSide.empty() && afterSide.front() == '<') {
                    arrowFrom = true;
                    afterSide = trimView(afterSide.substr(1));
                }
                size_t dashEnd = 0;
                while (dashEnd < afterSide.size() &&
                       afterSide[dashEnd] == '-') {
                    dashEnd++;
                }
                if (dashEnd >= 2) {
                    afterSide = trimView(afterSide.substr(dashEnd));
                    if (!afterSide.empty() && afterSide.front() == '>') {
                        arrowTo = true;
                        afterSide = trimView(afterSide.substr(1));
                    }
                    size_t toColon = afterSide.find(':');
                    if (toColon != std::string_view::npos &&
                        toColon == 1) {
                        char toSide = static_cast<char>(
                            std::toupper(afterSide.front()));
                        std::string_view right =
                            trimView(afterSide.substr(2));
                        size_t rightBrace = right.find('{');
                        if (rightBrace != std::string_view::npos) {
                            right = right.substr(0, rightBrace);
                        }
                        if (findNode(left) && findNode(right) &&
                            std::strchr("LRTB", fromSide) &&
                            std::strchr("LRTB", toSide)) {
                            ArchEdge edge;
                            edge.from = std::string(left);
                            edge.to = std::string(right);
                            edge.fromSide = fromSide;
                            edge.toSide = toSide;
                            edge.arrowFrom = arrowFrom;
                            edge.arrowTo = arrowTo;
                            edges.push_back(std::move(edge));
                            continue;
                        }
                    }
                }
            }
        }
        result.error = "Unsupported architecture statement";
        return result;
    }
    if (nodes.empty()) {
        result.error = "Empty architecture diagram";
        return result;
    }

    // Grid placement from the edge side constraints
    auto sideDelta = [](char side, int& dx, int& dy) {
        switch (side) {
            case 'L': dx = -1; dy = 0; break;
            case 'R': dx = 1; dy = 0; break;
            case 'T': dx = 0; dy = -1; break;
            default: dx = 0; dy = 1; break;
        }
    };
    auto cellTaken = [&](int gx, int gy) {
        for (const auto& node : nodes) {
            if (node.placed && node.gridX == gx && node.gridY == gy) {
                return true;
            }
        }
        return false;
    };

    nodes[0].placed = true;
    for (size_t pass = 0; pass < nodes.size() + edges.size() + 1; pass++) {
        bool changed = false;
        for (const auto& edge : edges) {
            ArchNode* from = findNode(edge.from);
            ArchNode* to = findNode(edge.to);
            if (from->placed == to->placed) continue;
            ArchNode* anchor = from->placed ? from : to;
            ArchNode* free = from->placed ? to : from;
            char side = from->placed ? edge.fromSide : edge.toSide;
            int dx, dy;
            sideDelta(side, dx, dy);
            int gx = anchor->gridX + dx;
            int gy = anchor->gridY + dy;
            while (cellTaken(gx, gy)) gy++;
            free->gridX = gx;
            free->gridY = gy;
            free->placed = true;
            changed = true;
        }
        if (!changed) break;
    }
    // Anything unconstrained flows into fresh rows below
    int fallbackY = 0;
    for (const auto& node : nodes) {
        if (node.placed) fallbackY = std::max(fallbackY, node.gridY + 1);
    }
    int fallbackX = 0;
    for (auto& node : nodes) {
        if (!node.placed) {
            while (cellTaken(fallbackX, fallbackY)) fallbackX++;
            node.gridX = fallbackX;
            node.gridY = fallbackY;
            node.placed = true;
        }
    }

    int minX = nodes[0].gridX, minY = nodes[0].gridY;
    for (const auto& node : nodes) {
        minX = std::min(minX, node.gridX);
        minY = std::min(minY, node.gridY);
    }
    float cell = kArchCell * scale;
    float icon = kArchIcon * scale;
    float top = 24.0f * scale;   // room for group labels
    float left = 16.0f * scale;
    for (auto& node : nodes) {
        node.x = left + (node.gridX - minX) * cell +
                 (cell - icon) * 0.5f;
        node.y = top + (node.gridY - minY) * cell + 6.0f * scale;
    }

    TextStyle labelStyle;
    labelStyle.scale = 0.85f;
    TextStyle groupStyle;
    groupStyle.bold = true;
    groupStyle.scale = 0.85f;

    std::vector<Prim> texts;

    // Group frames from member bounding boxes (nested groups included)
    struct GroupBox {
        float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
        bool any = false;
    };
    std::map<std::string, GroupBox> groupBoxes;
    std::function<void(const std::string&, float, float, float, float)>
        extendGroup = [&](const std::string& id, float x1, float y1,
                          float x2, float y2) {
        auto& box = groupBoxes[id];
        box.x1 = std::min(box.x1, x1);
        box.y1 = std::min(box.y1, y1);
        box.x2 = std::max(box.x2, x2);
        box.y2 = std::max(box.y2, y2);
        box.any = true;
        for (const auto& group : groups) {
            if (group.id == id && !group.parent.empty()) {
                extendGroup(group.parent, x1, y1, x2, y2);
            }
        }
    };
    for (const auto& node : nodes) {
        if (node.group.empty()) continue;
        float labelSpace = node.junction ? 0.0f : 20.0f * scale;
        extendGroup(node.group, node.x - 8.0f * scale,
                    node.y - 8.0f * scale, node.x + icon + 8.0f * scale,
                    node.y + icon + labelSpace + 8.0f * scale);
    }
    for (const auto& group : groups) {
        auto found = groupBoxes.find(group.id);
        if (found == groupBoxes.end() || !found->second.any) continue;
        const auto& box = found->second;
        Prim frame;
        frame.type = PrimType::RoundRect;
        frame.radius = 8.0f * scale;
        frame.x1 = box.x1;
        frame.y1 = box.y1 - 18.0f * scale;
        frame.x2 = box.x2;
        frame.y2 = box.y2;
        frame.stroke = Role::Muted;
        frame.strokeWidth = 1.2f * scale;
        frame.dashed = true;
        result.prims.push_back(std::move(frame));

        Prim label;
        label.type = PrimType::Text;
        label.text = group.label;
        label.style = groupStyle;
        label.fill = Role::Muted;
        label.alignH = -1;
        label.x1 = box.x1 + 8.0f * scale;
        label.y1 = box.y1 - 16.0f * scale;
        label.x2 = box.x2 - 8.0f * scale;
        label.y2 = box.y1 + 2.0f * scale;
        texts.push_back(std::move(label));
    }

    // Edges between side anchor points
    auto anchor = [&](const ArchNode& node, char side, float& x, float& y) {
        float half = node.junction ? 4.0f * scale : icon * 0.5f;
        float cx = node.x + icon * 0.5f;
        float cy = node.y + icon * 0.5f;
        switch (side) {
            case 'L': x = cx - half; y = cy; break;
            case 'R': x = cx + half; y = cy; break;
            case 'T': x = cx; y = cy - half; break;
            default: x = cx; y = cy + half; break;
        }
    };
    for (const auto& edge : edges) {
        ArchNode* from = findNode(edge.from);
        ArchNode* to = findNode(edge.to);
        float x1, y1, x2, y2;
        anchor(*from, edge.fromSide, x1, y1);
        anchor(*to, edge.toSide, x2, y2);
        Prim line;
        line.type = PrimType::Line;
        line.x1 = x1;
        line.y1 = y1;
        line.x2 = x2;
        line.y2 = y2;
        line.stroke = Role::Muted;
        line.strokeWidth = 1.6f * scale;
        line.arrow = edge.arrowTo;
        result.prims.push_back(std::move(line));
        if (edge.arrowFrom) {
            Prim back;
            back.type = PrimType::Line;
            back.x1 = x2;
            back.y1 = y2;
            back.x2 = x1;
            back.y2 = y1;
            back.stroke = Role::Muted;
            back.strokeWidth = 1.6f * scale;
            back.arrow = true;
            result.prims.push_back(std::move(back));
        }
    }

    // Icons: simple line art in the theme accent
    for (const auto& node : nodes) {
        float x = node.x, y = node.y;
        if (node.junction) {
            Prim dot;
            dot.type = PrimType::Ellipse;
            dot.x1 = x + icon * 0.5f - 4.0f * scale;
            dot.y1 = y + icon * 0.5f - 4.0f * scale;
            dot.x2 = x + icon * 0.5f + 4.0f * scale;
            dot.y2 = y + icon * 0.5f + 4.0f * scale;
            dot.fill = Role::Muted;
            result.prims.push_back(std::move(dot));
            continue;
        }
        std::string kind = node.icon;
        std::transform(kind.begin(), kind.end(), kind.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        float stroke = 1.6f * scale;
        if (kind == "database" || kind == "disk") {
            float lidH = icon * (kind == "disk" ? 0.34f : 0.24f);
            float bodyTop = kind == "disk" ? y + icon * 0.18f : y;
            float bodyBottom = y + icon - (kind == "disk" ? icon * 0.18f : 0);
            Prim paint;
            paint.fill = Role::Fill;
            paint.stroke = Role::Stroke;
            paint.strokeWidth = stroke;
            emitCylinder(result.prims, paint, x, bodyTop, x + icon,
                         bodyBottom, lidH);
        } else if (kind == "cloud") {
            Prim shape;
            shape.type = PrimType::Polygon;
            shape.fill = Role::Fill;
            shape.stroke = Role::Stroke;
            shape.strokeWidth = stroke;
            // Three arcs over a flat base, sampled into a polygon
            struct Arc {
                float cx, cy, r, a0, a1;
            };
            const Arc kArcs[] = {
                {0.26f, 0.62f, 0.22f, 3.34f, 4.90f},
                {0.50f, 0.42f, 0.26f, 3.50f, 5.90f},
                {0.74f, 0.62f, 0.22f, 4.55f, 6.10f},
            };
            for (const auto& arc : kArcs) {
                for (int i = 0; i <= 8; i++) {
                    float t = arc.a0 + (arc.a1 - arc.a0) * i / 8.0f;
                    shape.pts.push_back(
                        {x + (arc.cx + arc.r * std::cos(t)) * icon,
                         y + (arc.cy + arc.r * std::sin(t)) * icon});
                }
            }
            shape.pts.push_back({x + 0.92f * icon, y + 0.80f * icon});
            shape.pts.push_back({x + 0.08f * icon, y + 0.80f * icon});
            result.prims.push_back(std::move(shape));
        } else if (kind == "internet") {
            Prim globe;
            globe.type = PrimType::Ellipse;
            globe.x1 = x;
            globe.y1 = y;
            globe.x2 = x + icon;
            globe.y2 = y + icon;
            globe.fill = Role::Fill;
            globe.stroke = Role::Stroke;
            globe.strokeWidth = stroke;
            result.prims.push_back(std::move(globe));
            // Equator plus two latitude chords: globe line art without any
            // inner ellipses
            float radius = icon * 0.5f;
            float centerY = y + radius;
            float centerX = x + radius;
            for (float offset : {-0.24f, 0.0f, 0.24f}) {
                float dy = offset * icon;
                float halfChord =
                    std::sqrt(std::max(0.0f, radius * radius - dy * dy));
                Prim latitude;
                latitude.type = PrimType::Line;
                latitude.x1 = centerX - halfChord;
                latitude.y1 = centerY + dy;
                latitude.x2 = centerX + halfChord;
                latitude.y2 = centerY + dy;
                latitude.stroke = Role::Stroke;
                latitude.strokeWidth = stroke * 0.8f;
                result.prims.push_back(std::move(latitude));
            }
        } else {  // server and anything unknown
            Prim box;
            box.type = PrimType::RoundRect;
            box.radius = 8.0f * scale;
            box.x1 = x;
            box.y1 = y;
            box.x2 = x + icon;
            box.y2 = y + icon;
            box.fill = Role::Fill;
            box.stroke = Role::Stroke;
            box.strokeWidth = stroke;
            result.prims.push_back(std::move(box));
            if (kind == "server" || kind.empty()) {
                for (int i = 1; i <= 2; i++) {
                    Prim slot;
                    slot.type = PrimType::Line;
                    slot.x1 = x + icon * 0.16f;
                    slot.y1 = y + icon * i / 3.0f;
                    slot.x2 = x + icon * 0.84f;
                    slot.y2 = slot.y1;
                    slot.stroke = Role::Stroke;
                    slot.strokeWidth = stroke * 0.8f;
                    result.prims.push_back(std::move(slot));
                }
                Prim light;
                light.type = PrimType::Ellipse;
                light.x1 = x + icon * 0.70f;
                light.y1 = y + icon * 0.78f;
                light.x2 = x + icon * 0.78f;
                light.y2 = y + icon * 0.86f;
                light.fill = Role::Stroke;
                result.prims.push_back(std::move(light));
            }
        }

        if (!node.label.empty()) {
            Size size = measure(node.label, labelStyle, cell - 8.0f * scale);
            Prim label;
            label.type = PrimType::Text;
            label.text = node.label;
            label.style = labelStyle;
            label.fill = Role::Text;
            label.x1 = x + icon * 0.5f - cell * 0.5f + 4.0f * scale;
            label.y1 = y + icon + 2.0f * scale;
            label.x2 = x + icon * 0.5f + cell * 0.5f - 4.0f * scale;
            label.y2 = y + icon + 2.0f * scale + size.h;
            label.alignV = -1;
            texts.push_back(std::move(label));
        }
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    float right = 0.0f, bottom = 0.0f;
    for (const auto& prim : result.prims) {
        if (prim.type == PrimType::Polygon) {
            for (const auto& point : prim.pts) {
                right = std::max(right, point.x);
                bottom = std::max(bottom, point.y);
            }
        } else {
            right = std::max(right, std::max(prim.x1, prim.x2));
            bottom = std::max(bottom, std::max(prim.y1, prim.y2));
        }
    }
    result.width = right + 8.0f * scale;
    result.height = bottom + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

}  // namespace detail
}  // namespace mermaidext
