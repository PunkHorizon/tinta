#ifndef TINTA_MERMAID_LAYOUT_CHECKS_H
#define TINTA_MERMAID_LAYOUT_CHECKS_H

// Geometry checks on drawn state, class and ER diagrams: transitions keep
// out of every box they pass, and no label chip, note or box overlaps
// another, except what a composite state holds below its header. Shared by
// mermaid_tests (fixed-width text) and code_block_layout (the app's text).

#include "mermaid_ext.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mermaidcheck {

struct Box {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct Segment {
    mermaidext::Point a;
    mermaidext::Point b;
};

// The parts of a drawn graph diagram, told apart by kind and colour role
struct Shapes {
    std::vector<Box> nodes;       // states, pseudo-states, choices, forks
    std::vector<Box> containers;  // composite states
    std::vector<float> headers;   // where each container's body starts
    std::vector<Box> notes;
    std::vector<Box> chips;
    std::vector<Segment> lines;   // transitions
};

inline std::string describe(const Box& box) {
    return "(" + std::to_string(static_cast<int>(box.left)) + "," +
           std::to_string(static_cast<int>(box.top)) + ")-(" +
           std::to_string(static_cast<int>(box.right)) + "," +
           std::to_string(static_cast<int>(box.bottom)) + ")";
}

inline bool overlap(const Box& a, const Box& b, float slack = 0.5f) {
    return a.left < b.right - slack && b.left < a.right - slack &&
           a.top < b.bottom - slack && b.top < a.bottom - slack;
}

inline bool within(const Box& inner, const Box& outer, float slack = 0.5f) {
    return inner.left >= outer.left - slack && inner.right <= outer.right + slack &&
           inner.top >= outer.top - slack && inner.bottom <= outer.bottom + slack;
}

// Whether a segment runs through the inside of a box shrunk by `slack`
inline bool pierces(const Segment& line, const Box& box, float slack = 0.5f) {
    float t0 = 0.0f;
    float t1 = 1.0f;
    float dx = line.b.x - line.a.x;
    float dy = line.b.y - line.a.y;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {line.a.x - (box.left + slack), (box.right - slack) - line.a.x,
                        line.a.y - (box.top + slack), (box.bottom - slack) - line.a.y};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] <= 0.0f) return false;
            continue;
        }
        float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            t0 = std::max(t0, t);
        } else {
            t1 = std::min(t1, t);
        }
        if (t0 >= t1) return false;
    }
    return true;
}

inline bool strictlyInside(const mermaidext::Point& point, const Box& box) {
    return point.x > box.left + 0.5f && point.x < box.right - 0.5f &&
           point.y > box.top + 0.5f && point.y < box.bottom - 0.5f;
}

inline bool outside(const mermaidext::Point& point, const Box& box) {
    return point.x < box.left - 0.5f || point.x > box.right + 0.5f ||
           point.y < box.top - 0.5f || point.y > box.bottom + 0.5f;
}

// `state` diagrams draw pseudo-states as ellipses and notes as tinted
// rectangles; class and ER diagrams draw markers as ellipses and entity
// titles as tinted bands
inline Shapes shapesOf(const mermaidext::Built& built, float scale, bool state) {
    using mermaidext::PrimType;
    using mermaidext::Role;
    Shapes shapes;
    std::vector<Box> ellipses;
    std::vector<Segment> rules;  // box-wide separators
    for (const auto& prim : built.prims) {
        Box box{std::min(prim.x1, prim.x2), std::min(prim.y1, prim.y2),
                std::max(prim.x1, prim.x2), std::max(prim.y1, prim.y2)};
        bool elbow = std::abs(prim.x1 - prim.x2) < 0.01f || std::abs(prim.y1 - prim.y2) < 0.01f;
        switch (prim.type) {
            case PrimType::Line:
                // Transitions, and dashed relations; the slanted strokes of
                // arrowheads and crow's feet are markers
                if (prim.stroke == Role::Muted && prim.strokeWidth > 1.2f * scale &&
                    (state ? !prim.dashed : elbow)) {
                    shapes.lines.push_back({{prim.x1, prim.y1}, {prim.x2, prim.y2}});
                } else if (prim.stroke == Role::Stroke) {
                    rules.push_back({{prim.x1, prim.y1}, {prim.x2, prim.y2}});
                }
                break;
            case PrimType::RoundRect:
                if (prim.fill == Role::Fill && prim.stroke == Role::Muted) {
                    shapes.chips.push_back(box);
                } else if ((prim.fill == Role::Fill && prim.stroke == Role::Stroke) ||
                           prim.fill == Role::Accent) {
                    shapes.nodes.push_back(box);
                } else if (prim.fill == Role::None && prim.stroke == Role::Stroke) {
                    shapes.containers.push_back(box);
                }
                break;
            case PrimType::Rect:
                if (prim.fill == Role::AccentSoft) {
                    if (state) shapes.notes.push_back(box);
                } else if (prim.fill == Role::Fill) {
                    shapes.nodes.push_back(box);
                }
                break;
            case PrimType::Ellipse:
                if (state) ellipses.push_back(box);
                break;
            case PrimType::Polygon:
                if (prim.fill == Role::Fill && prim.stroke == Role::Stroke &&
                    !prim.pts.empty()) {
                    Box hull{prim.pts[0].x, prim.pts[0].y, prim.pts[0].x, prim.pts[0].y};
                    for (const auto& point : prim.pts) {
                        hull.left = std::min(hull.left, point.x);
                        hull.top = std::min(hull.top, point.y);
                        hull.right = std::max(hull.right, point.x);
                        hull.bottom = std::max(hull.bottom, point.y);
                    }
                    shapes.nodes.push_back(hull);
                }
                break;
            default:
                break;
        }
    }
    // An end state's dot sits inside its ring
    for (size_t i = 0; i < ellipses.size(); i++) {
        bool dot = false;
        for (size_t j = 0; j < ellipses.size(); j++) {
            if (j != i && within(ellipses[i], ellipses[j]) &&
                !within(ellipses[j], ellipses[i])) {
                dot = true;
            }
        }
        if (!dot) shapes.nodes.push_back(ellipses[i]);
    }
    // A composite's header ends at the separator that spans its width
    for (const Box& container : shapes.containers) {
        float header = container.top;
        for (const Segment& rule : rules) {
            if (std::abs(rule.a.y - rule.b.y) < 0.01f &&
                std::abs(std::min(rule.a.x, rule.b.x) - container.left) < 0.5f &&
                std::abs(std::max(rule.a.x, rule.b.x) - container.right) < 0.5f &&
                rule.a.y > container.top && rule.a.y < container.bottom) {
                header = header == container.top ? rule.a.y : std::min(header, rule.a.y);
            }
        }
        shapes.headers.push_back(header);
    }
    return shapes;
}

// What collides in a drawn diagram, empty when nothing does. Chips must
// also sit on a transition, and everything inside the diagram's extent.
inline std::vector<std::string> layoutProblems(const mermaidext::Built& built, float scale,
                                               bool state) {
    std::vector<std::string> problems;
    if (!built.ok) {
        problems.push_back("does not build: " + built.error);
        return problems;
    }
    Shapes shapes = shapesOf(built, scale, state);
    auto body = [&](size_t k) {
        Box box = shapes.containers[k];
        box.top = shapes.headers[k];
        return box;
    };
    // Something either inside a composite's body or clear of the composite
    auto housed = [&](const Box& box, const char* what) {
        for (size_t k = 0; k < shapes.containers.size(); k++) {
            if (overlap(box, shapes.containers[k]) && !within(box, body(k)) &&
                !within(shapes.containers[k], box)) {
                problems.push_back(std::string(what) + " " + describe(box) +
                                   " straddles composite " + describe(shapes.containers[k]));
            }
        }
    };

    for (const Segment& line : shapes.lines) {
        if (std::abs(line.a.x - line.b.x) > 0.01f && std::abs(line.a.y - line.b.y) > 0.01f) {
            problems.push_back("a transition segment is not an elbow");
        }
        for (const Box& node : shapes.nodes) {
            if (pierces(line, node)) {
                problems.push_back(
                    "transition (" + std::to_string(static_cast<int>(line.a.x)) + "," +
                    std::to_string(static_cast<int>(line.a.y)) + ")-(" +
                    std::to_string(static_cast<int>(line.b.x)) + "," +
                    std::to_string(static_cast<int>(line.b.y)) + ") crosses state " +
                    describe(node));
            }
        }
        for (size_t k = 0; k < shapes.containers.size(); k++) {
            const Box& container = shapes.containers[k];
            bool aIn = strictlyInside(line.a, container);
            bool bIn = strictlyInside(line.b, container);
            if ((aIn && outside(line.b, container)) || (bIn && outside(line.a, container)) ||
                (!aIn && !bIn && pierces(line, container))) {
                problems.push_back("a transition crosses the border of composite " +
                                   describe(container));
            } else if (aIn && bIn && std::min(line.a.y, line.b.y) < shapes.headers[k] - 0.5f) {
                problems.push_back("a transition runs through the header of " +
                                   describe(container));
            }
        }
    }
    for (size_t i = 0; i < shapes.chips.size(); i++) {
        const Box& chip = shapes.chips[i];
        for (const Box& node : shapes.nodes) {
            if (overlap(chip, node)) {
                problems.push_back("chip " + describe(chip) + " overlaps state " + describe(node));
            }
        }
        for (size_t j = i + 1; j < shapes.chips.size(); j++) {
            if (overlap(chip, shapes.chips[j])) {
                problems.push_back("chips " + describe(chip) + " and " +
                                   describe(shapes.chips[j]) + " overlap");
            }
        }
        for (const Box& note : shapes.notes) {
            if (overlap(chip, note)) {
                problems.push_back("chip " + describe(chip) + " overlaps note " + describe(note));
            }
        }
        housed(chip, "chip");
        float reach = 4.0f * scale + 1.0f;
        Box around{chip.left - reach, chip.top - reach, chip.right + reach, chip.bottom + reach};
        bool onLine = false;
        for (const Segment& line : shapes.lines) {
            if (pierces(line, around, 0.0f)) onLine = true;
        }
        if (!onLine) {
            problems.push_back("chip " + describe(chip) + " is away from every transition");
        }
    }
    for (size_t i = 0; i < shapes.notes.size(); i++) {
        const Box& note = shapes.notes[i];
        for (const Box& node : shapes.nodes) {
            if (overlap(note, node)) {
                problems.push_back("note " + describe(note) + " overlaps state " + describe(node));
            }
        }
        for (size_t j = i + 1; j < shapes.notes.size(); j++) {
            if (overlap(note, shapes.notes[j])) {
                problems.push_back("notes " + describe(note) + " and " +
                                   describe(shapes.notes[j]) + " overlap");
            }
        }
        housed(note, "note");
    }
    for (size_t i = 0; i < shapes.nodes.size(); i++) {
        for (size_t j = i + 1; j < shapes.nodes.size(); j++) {
            if (overlap(shapes.nodes[i], shapes.nodes[j])) {
                problems.push_back("states " + describe(shapes.nodes[i]) + " and " +
                                   describe(shapes.nodes[j]) + " overlap");
            }
        }
        housed(shapes.nodes[i], "state");
    }
    for (size_t i = 0; i < shapes.containers.size(); i++) {
        for (size_t j = i + 1; j < shapes.containers.size(); j++) {
            const Box& a = shapes.containers[i];
            const Box& b = shapes.containers[j];
            if (overlap(a, b) && !within(a, body(j)) && !within(b, body(i))) {
                problems.push_back("composites " + describe(a) + " and " + describe(b) +
                                   " overlap");
            }
        }
    }
    for (const auto& prim : built.prims) {
        float left = std::min(prim.x1, prim.x2);
        float top = std::min(prim.y1, prim.y2);
        float right = std::max(prim.x1, prim.x2);
        float bottom = std::max(prim.y1, prim.y2);
        if (prim.type == mermaidext::PrimType::Polygon && !prim.pts.empty()) {
            left = right = prim.pts[0].x;
            top = bottom = prim.pts[0].y;
            for (const auto& point : prim.pts) {
                left = std::min(left, point.x);
                top = std::min(top, point.y);
                right = std::max(right, point.x);
                bottom = std::max(bottom, point.y);
            }
        }
        if (left < -0.5f || top < -0.5f || right > built.width + 0.5f ||
            bottom > built.height + 0.5f) {
            problems.push_back("a prim lies outside the diagram's extent");
            break;
        }
    }
    return problems;
}

// The rectangle of the first text prim reading `text`
inline Box textBox(const mermaidext::Built& built, const std::string& text) {
    for (const auto& prim : built.prims) {
        if (prim.type == mermaidext::PrimType::Text && prim.text == text) {
            return {prim.x1, prim.y1, prim.x2, prim.y2};
        }
    }
    return {};
}

inline bool hasText(const mermaidext::Built& built, const std::string& text) {
    for (const auto& prim : built.prims) {
        if (prim.type == mermaidext::PrimType::Text && prim.text == text) return true;
    }
    return false;
}

}  // namespace mermaidcheck

#endif  // TINTA_MERMAID_LAYOUT_CHECKS_H
