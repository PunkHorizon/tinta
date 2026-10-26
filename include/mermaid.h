#ifndef TINTA_MERMAID_H
#define TINTA_MERMAID_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mermaid {

enum class Direction {
    TopToBottom,
    BottomToTop,
    LeftToRight,
    RightToLeft,
};

enum class NodeShape {
    Rectangle,
    RoundedRectangle,
    Diamond,
    Stadium,
    Circle,
    Hexagon,
    Asymmetric,  // id>text]: a rectangle notched in on its left side
};

struct Color {
    uint32_t rgb = 0;
    float alpha = 1.0f;
};

struct Style {
    bool hasFill = false;
    bool hasStroke = false;
    bool hasText = false;
    bool hasStrokeWidth = false;
    Color fill;
    Color stroke;
    Color text;
    float strokeWidth = 1.0f;
};

struct Node {
    std::string id;
    std::string label;
    NodeShape shape = NodeShape::Rectangle;
    std::string className;
    Style style;
    size_t sourceOffset = 0;
};

struct Edge {
    size_t from = 0;
    size_t to = 0;
    std::string label;
    bool directed = true;
    bool dashed = false;
    float strokeScale = 1.0f;
};

struct Subgraph {
    std::string id;
    std::string label;
    size_t parent = SIZE_MAX;    // enclosing subgraph, SIZE_MAX = top level
    std::vector<size_t> nodes;   // nodes first defined inside this subgraph
};

struct Diagram {
    Direction direction = Direction::TopToBottom;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Subgraph> subgraphs;
    std::unordered_map<std::string, Style> classStyles;
};

struct ParseResult {
    Diagram diagram;
    bool success = false;
    size_t errorLine = 0;
    std::string error;
};

struct Size {
    float width = 0.0f;
    float height = 0.0f;
};

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct Layout {
    std::vector<Rect> nodes;
    std::vector<size_t> ranks;  // layer index per node, for edge routing
    float width = 0.0f;
    float height = 0.0f;
};

ParseResult parse(std::string_view source);
Layout layout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
              float nodeGap, float rankGap);

// How far a node's left side is notched in at mid-height. Mermaid draws the
// asymmetric shape through (0,0) (w,0) (w,h) (0,h) and (h/2,h/2); its label
// and edges keep to the rectangle beside the notch.
inline float leftNotch(NodeShape shape, float height) {
    return shape == NodeShape::Asymmetric ? height * 0.5f : 0.0f;
}

} // namespace mermaid

#endif // TINTA_MERMAID_H
