#pragma once

// Geometry checks for C4 relation labels, on the primitives a C4 diagram
// builds into: each label chip clear of the element boxes, boundary frames
// and captions, the title, all other text, the other chips and the
// arrowheads, and lying on (or just beside) a line between its relation's
// two elements. Shared by mermaid_tests, which measures text in character
// cells, and code_block_layout, which measures it with the app's fonts.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace c4checks {

using mermaidext::Point;
using mermaidext::Prim;
using mermaidext::PrimType;
using mermaidext::Role;

struct Box {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

inline Box boxOf(const Prim& prim) {
    if (prim.type == PrimType::Polygon && !prim.pts.empty()) {
        Box box{prim.pts[0].x, prim.pts[0].y, prim.pts[0].x, prim.pts[0].y};
        for (const auto& point : prim.pts) {
            box.x1 = std::min(box.x1, point.x);
            box.y1 = std::min(box.y1, point.y);
            box.x2 = std::max(box.x2, point.x);
            box.y2 = std::max(box.y2, point.y);
        }
        return box;
    }
    return {std::min(prim.x1, prim.x2), std::min(prim.y1, prim.y2),
            std::max(prim.x1, prim.x2), std::max(prim.y1, prim.y2)};
}

inline Box grown(const Box& box, float by) {
    return {box.x1 - by, box.y1 - by, box.x2 + by, box.y2 + by};
}

inline bool overlaps(const Box& a, const Box& b) {
    return a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2;
}

inline bool inside(const Box& inner, const Box& outer) {
    return inner.x1 >= outer.x1 && inner.y1 >= outer.y1 &&
           inner.x2 <= outer.x2 && inner.y2 <= outer.y2;
}

inline bool contains(const Box& box, Point p) {
    return p.x >= box.x1 && p.x <= box.x2 && p.y >= box.y1 && p.y <= box.y2;
}

// Within a pixel of the box's outline
inline bool onEdge(Point p, const Box& box) {
    return contains(grown(box, 1.0f), p) && !contains(grown(box, -1.0f), p);
}

// Does the segment p-q pass through the box?
inline bool crosses(Point p, Point q, const Box& box) {
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

inline float distanceToSegment(Point p, Point a, Point b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float lengthSq = dx * dx + dy * dy;
    float t = lengthSq > 0.0f
                  ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq,
                               0.0f, 1.0f)
                  : 0.0f;
    float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

// The filled arrowhead at a line's end, as the viewer draws it
inline Box arrowhead(const Prim& line, float scale) {
    float dx = line.x2 - line.x1, dy = line.y2 - line.y1;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01f) return {line.x2, line.y2, line.x2, line.y2};
    dx /= length;
    dy /= length;
    float head = 9.0f * scale, wing = 4.5f * scale;
    Point tip{line.x2, line.y2};
    Point left{tip.x - dx * head + dy * wing, tip.y - dy * head - dx * wing};
    Point right{tip.x - dx * head - dy * wing, tip.y - dy * head + dx * wing};
    return {std::min({tip.x, left.x, right.x}), std::min({tip.y, left.y, right.y}),
            std::max({tip.x, left.x, right.x}), std::max({tip.y, left.y, right.y})};
}

// What the checks need from a C4 source: element labels by alias, and the
// labelled relations in order, which is the order of their chips
struct Relation {
    std::string from, to, label;
};
struct Source {
    std::vector<std::pair<std::string, std::string>> elements;
    std::vector<Relation> relations;
};

inline std::string trimmed(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r");
    if (start == std::string::npos) return {};
    size_t end = text.find_last_not_of(" \t\r");
    return text.substr(start, end - start + 1);
}

inline Source readSource(const std::string& source) {
    Source out;
    size_t position = 0;
    while (position < source.size()) {
        size_t newline = source.find('\n', position);
        if (newline == std::string::npos) newline = source.size();
        std::string line = trimmed(source.substr(position, newline - position));
        position = newline + 1;
        size_t open = line.find('(');
        size_t close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos ||
            close < open) {
            continue;
        }
        std::string name = trimmed(line.substr(0, open));
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        // Arguments, split on commas outside quotes
        std::vector<std::string> args(1);
        bool quoted = false;
        for (char c : line.substr(open + 1, close - open - 1)) {
            if (c == '"') {
                quoted = !quoted;
            } else if (c == ',' && !quoted) {
                args.emplace_back();
            } else {
                args.back() += c;
            }
        }
        for (auto& arg : args) arg = trimmed(arg);
        bool relation = name.rfind("rel", 0) == 0 || name.rfind("birel", 0) == 0;
        bool element = name.rfind("person", 0) == 0 ||
                       name.rfind("system", 0) == 0 ||
                       name.rfind("container", 0) == 0 ||
                       name.rfind("component", 0) == 0;
        if (relation && args.size() >= 3) {
            std::string label = mermaidext::detail::cleanLabel(args[2]);
            if (!label.empty()) out.relations.push_back({args[0], args[1], label});
        } else if (element && name.find("boundary") == std::string::npos &&
                   args.size() >= 2) {
            out.elements.push_back(
                {args[0], mermaidext::detail::cleanLabel(args[1])});
        }
    }
    return out;
}

struct Parts {
    std::vector<Box> chips, shapes, frames;
    std::vector<const Prim*> texts, lines;
};

inline Parts partsOf(const mermaidext::Built& built) {
    Parts parts;
    for (const auto& prim : built.prims) {
        if (prim.type == PrimType::RoundRect && prim.fill == Role::Fill &&
            prim.stroke == Role::Muted) {
            parts.chips.push_back(boxOf(prim));
        } else if ((prim.type == PrimType::RoundRect ||
                    prim.type == PrimType::Polygon) &&
                   prim.fill == Role::Custom) {
            parts.shapes.push_back(boxOf(prim));
        } else if (prim.type == PrimType::Rect && prim.dashed) {
            parts.frames.push_back(boxOf(prim));
        } else if (prim.type == PrimType::Text) {
            parts.texts.push_back(&prim);
        } else if (prim.type == PrimType::Line) {
            parts.lines.push_back(&prim);
        }
    }
    return parts;
}

// The box drawn for the element whose label (its bold name, not its
// description) is `label`
inline bool elementBox(const Parts& parts, const std::string& label, Box& box) {
    for (const auto* text : parts.texts) {
        if (text->fill != Role::Custom || !text->style.bold ||
            text->text != label) {
            continue;
        }
        Box area = boxOf(*text);
        Point middle{(area.x1 + area.x2) * 0.5f, (area.y1 + area.y2) * 0.5f};
        for (const auto& shape : parts.shapes) {
            if (contains(shape, middle)) {
                box = shape;
                return true;
            }
        }
    }
    return false;
}

// The chip that holds the relation label `label`
inline bool chipBox(const Parts& parts, const std::string& label, Box& box) {
    for (const auto* text : parts.texts) {
        if (text->text != label || text->fill != Role::Text) continue;
        for (const auto& chip : parts.chips) {
            if (inside(boxOf(*text), grown(chip, 0.5f))) {
                box = chip;
                return true;
            }
        }
    }
    return false;
}

// Everything wrong with the label chips of a built C4 diagram
inline std::vector<std::string> labelProblems(const mermaidext::Built& built,
                                              const std::string& source,
                                              float scale) {
    std::vector<std::string> problems;
    Source parsed = readSource(source);
    Parts parts = partsOf(built);
    if (parts.chips.size() != parsed.relations.size()) {
        problems.push_back(std::to_string(parts.chips.size()) +
                           " chips for " +
                           std::to_string(parsed.relations.size()) +
                           " labelled relations");
        return problems;
    }
    for (size_t i = 0; i < parts.chips.size(); i++) {
        const Box& chip = parts.chips[i];
        const Relation& relation = parsed.relations[i];
        const std::string name = "\"" + relation.label + "\"";
        Box own;
        if (!chipBox(parts, relation.label, own) || own.x1 != chip.x1 ||
            own.y1 != chip.y1) {
            problems.push_back(name + ": its chip does not hold its label");
        }
        for (const auto& shape : parts.shapes) {
            if (overlaps(chip, shape)) {
                problems.push_back(name + " overlaps an element box");
                break;
            }
        }
        for (const auto& frame : parts.frames) {
            if (overlaps(chip, frame) && !inside(chip, frame)) {
                problems.push_back(name + " crosses a boundary frame");
                break;
            }
        }
        for (size_t j = 0; j < parts.chips.size(); j++) {
            if (j != i && overlaps(chip, parts.chips[j])) {
                problems.push_back(name + " overlaps another label");
                break;
            }
        }
        for (const auto* text : parts.texts) {
            Box area = boxOf(*text);
            if (inside(area, grown(chip, 0.5f))) continue;  // its own
            if (overlaps(chip, area)) {
                problems.push_back(name + " overlaps the text \"" +
                                   text->text + "\"");
                break;
            }
        }
        for (const auto* line : parts.lines) {
            if (line->arrow && overlaps(chip, arrowhead(*line, scale))) {
                problems.push_back(name + " covers an arrowhead");
                break;
            }
        }
        // On a line between its elements, or joined to one by a leader
        Box from, to;
        std::string fromLabel, toLabel;
        for (const auto& element : parsed.elements) {
            if (element.first == relation.from) fromLabel = element.second;
            if (element.first == relation.to) toLabel = element.second;
        }
        if (!elementBox(parts, fromLabel, from) ||
            !elementBox(parts, toLabel, to)) {
            problems.push_back(name + ": its elements are not drawn");
            continue;
        }
        std::vector<const Prim*> ownLines;
        for (const auto* line : parts.lines) {
            Point p{line->x1, line->y1}, q{line->x2, line->y2};
            bool joins = relation.from == relation.to
                             ? contains(grown(from, 60.0f * scale), p) &&
                                   contains(grown(from, 60.0f * scale), q)
                             : (onEdge(p, from) && onEdge(q, to)) ||
                                   (onEdge(p, to) && onEdge(q, from));
            if (joins) ownLines.push_back(line);
        }
        bool attached = false;
        for (const auto* line : ownLines) {
            if (crosses({line->x1, line->y1}, {line->x2, line->y2},
                        grown(chip, 3.0f * scale))) {
                attached = true;
            }
        }
        for (const auto* leader : parts.lines) {
            if (attached || leader->arrow) continue;
            Point start{leader->x1, leader->y1}, end{leader->x2, leader->y2};
            if (!onEdge(end, chip)) continue;
            for (const auto* line : ownLines) {
                if (distanceToSegment(start, {line->x1, line->y1},
                                      {line->x2, line->y2}) < 1.0f) {
                    attached = true;
                }
            }
        }
        if (!attached) problems.push_back(name + " is off its relation's line");
    }
    return problems;
}

}  // namespace c4checks
