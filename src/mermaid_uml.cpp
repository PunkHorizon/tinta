// Native Mermaid renderers for the graph-shaped UML families: state
// diagrams, class diagrams, and entity-relationship diagrams. All three
// parse into the flowchart engine's mermaid::Diagram to reuse its rank
// layout, then route edges and draw nodes through the primitive pipeline.

#include "mermaid.h"
#include "mermaid_ext.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

constexpr float kNodeGap = 34.0f;
constexpr float kRankGap = 64.0f;
constexpr float kEdgeLabelPad = 5.0f;
constexpr float kMarkerDepth = 11.0f;
constexpr float kNoteGap = 16.0f;    // a node to the note beside it
constexpr float kNoteStack = 8.0f;   // between notes on one side
constexpr float kLaneClear = 26.0f;  // a lane to the nodes it passes
constexpr float kLaneStep = 12.0f;   // between parallel lanes
constexpr float kTrackStep = 8.0f;   // between parallel runs in a gap
constexpr float kPortStep = 9.0f;    // between stubs on one side of a node
constexpr float kLoopReach = 26.0f;  // a self-loop out from its node
constexpr float kChipClear = 3.0f;   // a chip to what it must not touch
constexpr float kCrossCost = 150.0f;  // route length a crossing is worth
constexpr float kHuge = std::numeric_limits<float>::max() / 4.0f;

enum class Marker {
    None,
    FilledArrow,
    OpenArrow,
    HollowTriangle,
    HollowDiamond,
    FilledDiamond,
    CrowMany,     // crow's foot (+ bar/circle by flags below)
    One,          // double bar
    ZeroOne,      // circle + bar
    ZeroMany,     // circle + crow's foot
    OneMany,      // bar + crow's foot
};

struct EdgeDecor {
    Marker atFrom = Marker::None;
    Marker atTo = Marker::None;
    std::string labelFrom;  // multiplicity near the from end
    std::string labelTo;
    bool dashed = false;
    std::string label;
};

struct GraphNodes {
    mermaid::Diagram diagram;
    std::map<std::string, size_t, std::less<>> ids;

    size_t ensure(std::string_view id, std::string_view label) {
        auto found = ids.find(id);
        if (found != ids.end()) {
            if (!label.empty()) {
                diagram.nodes[found->second].label = cleanLabel(label);
            }
            return found->second;
        }
        mermaid::Node node;
        node.id = std::string(id);
        node.label = cleanLabel(label.empty() ? id : label);
        diagram.nodes.push_back(std::move(node));
        ids.emplace(std::string(id), diagram.nodes.size() - 1);
        return diagram.nodes.size() - 1;
    }
};

struct RoutedEdge {
    std::vector<Point> points;
    bool chipBeside = false;  // a self-loop's chip has a spot kept for it
    Point chipAt;
};

// The rank layout is longest-path over a DAG; cyclic graphs (the norm for
// state machines: start/stop, pause/resume) would fall into its
// sequential-order fallback. Reverse DFS back-edges for RANKING ONLY —
// routing still uses the original edge directions.
mermaid::Diagram acyclicForRanking(const mermaid::Diagram& diagram) {
    size_t count = diagram.nodes.size();
    std::vector<std::vector<size_t>> outgoing(count);
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        const auto& edge = diagram.edges[i];
        if (edge.from < count && edge.to < count && edge.from != edge.to) {
            outgoing[edge.from].push_back(i);
        }
    }
    std::vector<size_t> indegree(count, 0);
    for (const auto& edge : diagram.edges) {
        if (edge.to < count && edge.from != edge.to) indegree[edge.to]++;
    }

    enum : unsigned char { White, Gray, Black };
    std::vector<unsigned char> color(count, White);
    std::vector<bool> reversed(diagram.edges.size(), false);

    struct StackFrame {
        size_t node;
        size_t next = 0;
    };
    auto dfs = [&](size_t root) {
        std::vector<StackFrame> stack{{root}};
        color[root] = Gray;
        while (!stack.empty()) {
            StackFrame& frame = stack.back();
            if (frame.next < outgoing[frame.node].size()) {
                size_t edgeIndex = outgoing[frame.node][frame.next++];
                size_t target = diagram.edges[edgeIndex].to;
                if (color[target] == Gray) {
                    reversed[edgeIndex] = true;  // back edge
                } else if (color[target] == White) {
                    color[target] = Gray;
                    stack.push_back({target});
                }
            } else {
                color[frame.node] = Black;
                stack.pop_back();
            }
        }
    };
    for (size_t i = 0; i < count; i++) {
        if (indegree[i] == 0 && color[i] == White) dfs(i);
    }
    for (size_t i = 0; i < count; i++) {
        if (color[i] == White) dfs(i);
    }

    mermaid::Diagram ranking = diagram;
    for (size_t i = 0; i < ranking.edges.size(); i++) {
        if (reversed[i]) std::swap(ranking.edges[i].from, ranking.edges[i].to);
    }
    // A self-loop keeps its node's in-degree above zero for good, which
    // would drop the node and everything after it into the fallback order
    ranking.edges.erase(
        std::remove_if(ranking.edges.begin(), ranking.edges.end(),
                       [](const mermaid::Edge& edge) {
                           return edge.from == edge.to;
                       }),
        ranking.edges.end());
    return ranking;
}

// Layout and routing work in a frame where ranks run down the y axis: LR
// and RL diagrams are transposed into it, BT and RL flipped, and every
// result is mapped back to the diagram's own frame at the end
struct Frame {
    bool transpose = false;
    bool flip = false;
    float extent = 0.0f;  // the height the flip mirrors

    Size in(Size size) const {
        return transpose ? Size{size.h, size.w} : size;
    }
    Point out(Point point) const {
        if (flip) point.y = extent - point.y;
        return transpose ? Point{point.y, point.x} : point;
    }
    mermaid::Rect out(const mermaid::Rect& rect) const {
        Point a = out(Point{rect.left, rect.top});
        Point b = out(Point{rect.right, rect.bottom});
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x),
                std::max(a.y, b.y)};
    }
};

// A note laid out beside its node across the rank direction, inside the
// node's slot, so no neighbour can overlap it. "after" puts it right of
// the node in vertical layouts and below it in horizontal ones, where
// Mermaid puts a "right of" note.
struct Attachment {
    size_t node = 0;
    bool after = true;
    Size size;  // diagram frame
};

// Laid-out node boxes, notes and edge routes, in the diagram's frame
struct Scene {
    std::vector<mermaid::Rect> boxes;
    std::vector<mermaid::Rect> attachments;  // one per Attachment
    std::vector<RoutedEdge> routes;
};

struct Segment {
    Point a;
    Point b;
};

float pathLength(const std::vector<Point>& points) {
    float total = 0.0f;
    for (size_t p = 1; p < points.size(); p++) {
        total += std::abs(points[p].x - points[p - 1].x) +
                 std::abs(points[p].y - points[p - 1].y);
    }
    return total;
}

// How far two intervals overlap; negative when they are apart
float sharedSpan(float a0, float a1, float b0, float b1) {
    return std::min(std::max(a0, a1), std::max(b0, b1)) -
           std::max(std::min(a0, a1), std::min(b0, b1));
}

// Places where a path's axis-aligned segments cross others, or run along
// one of them
int crossings(const std::vector<Point>& path,
              const std::vector<Segment>& others) {
    constexpr float kTouch = 0.5f;
    int count = 0;
    for (size_t p = 1; p < path.size(); p++) {
        Segment mine{path[p - 1], path[p]};
        bool mineFlat = std::abs(mine.a.y - mine.b.y) < kTouch;
        for (const Segment& other : others) {
            bool otherFlat = std::abs(other.a.y - other.b.y) < kTouch;
            if (mineFlat == otherFlat) {
                float offset = mineFlat ? mine.a.y - other.a.y
                                        : mine.a.x - other.a.x;
                float along =
                    mineFlat
                        ? sharedSpan(mine.a.x, mine.b.x, other.a.x, other.b.x)
                        : sharedSpan(mine.a.y, mine.b.y, other.a.y, other.b.y);
                if (std::abs(offset) < kTouch && along > 1.0f) count++;
                continue;
            }
            const Segment& flat = mineFlat ? mine : other;
            const Segment& upright = mineFlat ? other : mine;
            if (std::min(flat.a.x, flat.b.x) + kTouch < upright.a.x &&
                std::max(flat.a.x, flat.b.x) - kTouch > upright.a.x &&
                std::min(upright.a.y, upright.b.y) + kTouch < flat.a.y &&
                std::max(upright.a.y, upright.b.y) - kTouch > flat.a.y) {
                count++;
            }
        }
    }
    return count;
}

// Elbow routes in the canonical frame. An edge to the next rank drops
// through the gap between the two. Back edges and edges that skip ranks
// run in a lane beside every node and note of the ranks they pass, joined
// straight from the node's side, or, when something lies between the node
// and the lane, from its top or bottom through the gap there; a back edge
// to the rank just above may instead cross that gap directly. Ports, gap
// runs and lanes are handed out so parallel runs keep apart, and each edge
// takes its shortest route, counting a crossing as a detour.
struct Router {
    struct PortMark {
        float at;
        float travel;  // where the stub's lane heads: -1 up, 1 down
    };
    struct Run {
        float at;
        float from;
        float to;
        float half;  // half the band its chip needs, 0 without a chip
    };
    struct Lane {
        float x;
        float from;
        float to;
        float half;  // half the chip that sits on it
    };
    struct PortUse {
        size_t node;
        int face;
        PortMark mark;
    };
    struct RunUse {
        size_t gap;
        Run run;
    };
    struct Plan {
        std::vector<Point> points;
        float cost = 0.0f;
        std::vector<PortUse> ports;
        std::vector<RunUse> runs;
        int laneSide = -1;
        Lane lane{};
    };
    struct Forward {
        size_t edge;
        size_t from;
        size_t to;
        float half = 0.0f;  // half its chip's band across the gap
        float fromX = 0.0f;
        float toX = 0.0f;
        int track = 0;  // order across the gap, -1 for a straight drop
    };

    float scale = 1.0f;
    std::vector<mermaid::Rect> boxes;
    std::vector<mermaid::Rect> slots;  // boxes with their notes and loops
    std::vector<size_t> ranks;
    std::vector<std::array<bool, 2>> flanked;  // a note or loop before/after
    std::vector<size_t> order;                 // place within the rank
    std::vector<size_t> rankCount;
    std::vector<float> rowTop;
    std::vector<float> rowBottom;
    // Stubs taken per face: 0 top, 1 bottom, 2 before (left), 3 after
    std::vector<std::array<std::vector<PortMark>, 4>> ports;
    std::vector<std::vector<Run>> runs;     // per gap, the gap below a rank
    // Per gap, chip bands kept for back edges that may cross it, above and
    // below the runs of the edges to the next rank
    std::vector<std::array<std::vector<float>, 2>> spare;
    std::array<std::vector<Lane>, 2> lanes;
    std::vector<Segment> drawn;

    void setup() {
        size_t count = boxes.size();
        size_t rankTotal = 0;
        for (size_t rank : ranks) rankTotal = std::max(rankTotal, rank + 1);
        rowTop.assign(rankTotal, kHuge);
        rowBottom.assign(rankTotal, -kHuge);
        std::vector<std::vector<size_t>> members(rankTotal);
        for (size_t i = 0; i < count; i++) {
            size_t rank = ranks[i];
            members[rank].push_back(i);
            rowTop[rank] = std::min(rowTop[rank], slots[i].top);
            rowBottom[rank] = std::max(rowBottom[rank], slots[i].bottom);
        }
        order.assign(count, 0);
        rankCount.assign(rankTotal, 0);
        for (size_t rank = 0; rank < rankTotal; rank++) {
            auto& row = members[rank];
            std::sort(row.begin(), row.end(), [&](size_t a, size_t b) {
                return slots[a].left < slots[b].left;
            });
            for (size_t k = 0; k < row.size(); k++) order[row[k]] = k;
            rankCount[rank] = row.size();
            if (row.empty()) {  // longest-path ranks leave none empty
                rowTop[rank] = rowBottom[rank] =
                    rank > 0 ? rowBottom[rank - 1] : 0.0f;
            }
        }
        ports.assign(count, {});
        runs.assign(rankTotal, {});
    }

    size_t gapCount() const { return rowTop.empty() ? 0 : rowTop.size() - 1; }

    float gapMid(size_t gap) const {
        return (rowBottom[gap] + rowTop[gap + 1]) * 0.5f;
    }

    float centerX(size_t node) const {
        return (boxes[node].left + boxes[node].right) * 0.5f;
    }

    // Something besides the node lies on that side of it in its rank
    bool blocked(size_t node, int side) const {
        if (flanked[node][side]) return true;
        return side ? order[node] + 1 < rankCount[ranks[node]]
                    : order[node] > 0;
    }

    // A free stub position on a face: `preferred` when free, else the
    // nearest free step from it, `toward` first, within [lo, hi]
    float pickPort(size_t node, int face, float preferred, float lo,
                   float hi, float toward) const {
        if (lo > hi) lo = hi = (lo + hi) * 0.5f;
        float step = kPortStep * scale;
        const auto& used = ports[node][face];
        auto free = [&](float at) {
            for (const PortMark& mark : used) {
                if (std::abs(mark.at - at) < step * 0.8f) return false;
            }
            return true;
        };
        for (int k = 0; k <= 8; k++) {
            for (float sign : {toward, -toward}) {
                float at = preferred + sign * static_cast<float>(k) * step;
                if (at >= lo - 0.01f && at <= hi + 0.01f && free(at)) {
                    return at;
                }
                if (k == 0) break;
            }
        }
        return std::min(std::max(preferred, lo), hi);
    }

    // A height in a gap for a run over [x1, x2] with a chip band `half`
    // either side: off the gap's middle, on the `prefer` side first, inside
    // the gap and clear of the runs it overlaps by both their bands;
    // failing that by the wider band, then by a step
    float pickRun(size_t gap, float x1, float x2, float prefer,
                  float half) const {
        float lo = std::min(x1, x2);
        float hi = std::max(x1, x2);
        float step = kTrackStep * scale;
        float mid = gapMid(gap);
        float top = rowBottom[gap];
        float bottom = rowTop[gap + 1];
        int most = std::max(
            1, static_cast<int>(((bottom - top) * 0.5f - 2.0f * scale) / step));
        for (int strict = 2; strict >= 0; strict--) {
            for (int k = 1; k <= most; k++) {
                for (float sign : {prefer, -prefer}) {
                    float at = mid + sign * static_cast<float>(k) * step;
                    if (strict > 0 &&
                        (at - half < top || at + half > bottom)) {
                        continue;
                    }
                    bool free = true;
                    for (const Run& run : runs[gap]) {
                        if (run.from > hi + step || run.to < lo - step) {
                            continue;
                        }
                        float need = strict == 2   ? run.half + half
                                     : strict == 1 ? std::max(run.half, half)
                                                   : 0.0f;
                        if (std::abs(run.at - at) <
                            std::max(need, step * 0.6f)) {
                            free = false;
                            break;
                        }
                    }
                    if (free) return at;
                }
            }
        }
        return mid + prefer * step;
    }

    // The nearest lane outside `base` that keeps clear of the lanes it
    // runs beside, with room for a chip `half` wide either side of it
    float pickLane(int side, float base, float from, float to,
                   float half) const {
        float dir = side ? 1.0f : -1.0f;
        float spacing = kLaneStep * scale;
        float x = base + dir * std::max(kLaneClear * scale,
                                        half + (kChipClear + 5.0f) * scale);
        bool moved = true;
        while (moved) {
            moved = false;
            for (const Lane& lane : lanes[side]) {
                if (lane.to + spacing < from || lane.from - spacing > to) {
                    continue;
                }
                float need = lane.half + half + spacing;
                float past = lane.x + dir * need;
                if (std::abs(x - lane.x) < need && (past - x) * dir > 0.01f) {
                    x = past;
                    moved = true;
                }
            }
        }
        return x;
    }

    float cost(const std::vector<Point>& points) const {
        return pathLength(points) +
               kCrossCost * scale *
                   static_cast<float>(crossings(points, drawn));
    }

    void addDrawn(const std::vector<Point>& points) {
        for (size_t p = 1; p < points.size(); p++) {
            drawn.push_back({points[p - 1], points[p]});
        }
    }

    void commit(const Plan& plan) {
        for (const PortUse& use : plan.ports) {
            ports[use.node][use.face].push_back(use.mark);
        }
        for (const RunUse& use : plan.runs) runs[use.gap].push_back(use.run);
        if (plan.laneSide >= 0) lanes[plan.laneSide].push_back(plan.lane);
        addDrawn(plan.points);
    }

    // Ports and order across the gap for the edges to the next rank.
    // Fanned, each edge has its own port on both faces, in the order of
    // the nodes at its other end, and rightward runs nest under the ones
    // right of them, leftward runs under the ones left of them, so edges
    // that can avoid crossing do. Unfanned, edges share the middle of each
    // face, and only edges between the same two nodes part.
    void planForward(std::vector<Forward>& edges, bool fan) const {
        float inset = 2.0f * scale;
        if (fan) {
            auto spread = [&](std::vector<Forward*>& group, size_t node,
                              bool out) {
                const mermaid::Rect& rect = boxes[node];
                float n = static_cast<float>(group.size());
                float step = std::min((rect.right - rect.left) / (n + 1.0f),
                                      16.0f * scale);
                for (size_t k = 0; k < group.size(); k++) {
                    float x = centerX(node) +
                              (static_cast<float>(k) - (n - 1.0f) * 0.5f) *
                                  step;
                    (out ? group[k]->fromX : group[k]->toX) = x;
                }
            };
            std::map<size_t, std::vector<Forward*>> bySource;
            std::map<size_t, std::vector<Forward*>> byTarget;
            for (Forward& edge : edges) bySource[edge.from].push_back(&edge);
            for (auto& [node, group] : bySource) {
                std::stable_sort(group.begin(), group.end(),
                                 [&](const Forward* a, const Forward* b) {
                                     return centerX(a->to) < centerX(b->to);
                                 });
                spread(group, node, true);
            }
            for (Forward& edge : edges) byTarget[edge.to].push_back(&edge);
            for (auto& [node, group] : byTarget) {
                std::stable_sort(group.begin(), group.end(),
                                 [](const Forward* a, const Forward* b) {
                                     return a->fromX < b->fromX;
                                 });
                spread(group, node, false);
            }
        } else {
            std::map<std::pair<size_t, size_t>, std::vector<Forward*>> pairs;
            for (Forward& edge : edges) {
                pairs[{edge.from, edge.to}].push_back(&edge);
            }
            for (auto& [ends, group] : pairs) {
                float n = static_cast<float>(group.size());
                bool right = centerX(ends.second) > centerX(ends.first);
                for (size_t k = 0; k < group.size(); k++) {
                    float shift = (static_cast<float>(k) - (n - 1.0f) * 0.5f) *
                                  kPortStep * scale;
                    group[k]->fromX = centerX(ends.first) + shift;
                    group[k]->toX = centerX(ends.second) + shift;
                    group[k]->track =
                        static_cast<int>(right ? group.size() - 1 - k : k);
                }
            }
        }
        for (Forward& edge : edges) {
            const mermaid::Rect& f = boxes[edge.from];
            const mermaid::Rect& t = boxes[edge.to];
            edge.fromX = std::min(std::max(edge.fromX, f.left + inset),
                                  f.right - inset);
            edge.toX = std::min(std::max(edge.toX, t.left + inset),
                                t.right - inset);
            if (std::abs(edge.toX - edge.fromX) < 0.5f) edge.track = -1;
        }
        if (!fan) return;

        std::vector<std::vector<size_t>> byGap(gapCount());
        for (size_t i = 0; i < edges.size(); i++) {
            if (edges[i].track >= 0) byGap[ranks[edges[i].from]].push_back(i);
        }
        for (auto& group : byGap) {
            std::stable_sort(group.begin(), group.end(),
                             [&](size_t a, size_t b) {
                                 const Forward& ea = edges[a];
                                 const Forward& eb = edges[b];
                                 bool aRight = ea.toX >= ea.fromX;
                                 bool bRight = eb.toX >= eb.fromX;
                                 if (aRight != bRight) return aRight;
                                 return aRight ? ea.fromX > eb.fromX
                                               : ea.fromX < eb.fromX;
                             });
            for (size_t k = 0; k < group.size(); k++) {
                Forward& edge = edges[group[k]];
                bool right = edge.toX >= edge.fromX;
                int track = 0;
                std::vector<int> others;
                for (size_t j = 0; j < k; j++) {
                    const Forward& other = edges[group[j]];
                    if (sharedSpan(edge.fromX, edge.toX, other.fromX,
                                   other.toX) <= -6.0f * scale) {
                        continue;
                    }
                    if ((other.toX >= other.fromX) == right) {
                        track = std::max(track, other.track + 1);
                    } else {
                        others.push_back(other.track);
                    }
                }
                while (std::find(others.begin(), others.end(), track) !=
                       others.end()) {
                    track++;
                }
                edge.track = track;
            }
        }
    }

    // Offsets of a gap's tracks from its first, far enough apart for the
    // chip bands on them
    std::vector<float> trackOffsets(const std::vector<float>& halves) const {
        std::vector<float> offsets(halves.size(), 0.0f);
        for (size_t k = 1; k < halves.size(); k++) {
            offsets[k] = offsets[k - 1] + std::max(kTrackStep * scale,
                                                   halves[k - 1] + halves[k]);
        }
        return offsets;
    }

    size_t spareAbove(size_t gap) const {
        return gap < spare.size() ? spare[gap][0].size() : 0;
    }

    // Per gap, the chip band of each track: the bands kept for back edges
    // above the forward tracks, the forward tracks, the bands kept below
    std::vector<std::vector<float>> trackHalves(
        const std::vector<Forward>& edges) const {
        std::vector<std::vector<float>> forward(gapCount());
        for (const Forward& edge : edges) {
            if (edge.track < 0) continue;
            auto& gap = forward[ranks[edge.from]];
            size_t track = static_cast<size_t>(edge.track);
            if (gap.size() <= track) gap.resize(track + 1, 0.0f);
            gap[track] = std::max(gap[track], edge.half);
        }
        std::vector<std::vector<float>> halves(gapCount());
        for (size_t gap = 0; gap < halves.size(); gap++) {
            if (gap < spare.size()) halves[gap] = spare[gap][0];
            halves[gap].insert(halves[gap].end(), forward[gap].begin(),
                               forward[gap].end());
            if (gap < spare.size()) {
                halves[gap].insert(halves[gap].end(), spare[gap][1].begin(),
                                   spare[gap][1].end());
            }
        }
        return halves;
    }

    // How tall each gap must be for its tracks with the chips on them, and
    // for a chip between arrowheads at both faces of the gap, which is
    // where it goes when the run across is short
    std::vector<float> gapNeeds(const std::vector<Forward>& edges) const {
        auto halves = trackHalves(edges);
        std::vector<float> needs(halves.size(), 0.0f);
        float margin = 8.0f * scale;
        for (size_t gap = 0; gap < halves.size(); gap++) {
            if (halves[gap].empty()) continue;
            needs[gap] = std::max(margin, halves[gap].front()) +
                         trackOffsets(halves[gap]).back() +
                         std::max(margin, halves[gap].back());
        }
        for (const Forward& edge : edges) {
            if (edge.half <= 0.0f) continue;
            float& need = needs[ranks[edge.from]];
            need = std::max(need, 2.0f * edge.half +
                                      2.0f * (kMarkerDepth + 6.0f) * scale);
        }
        return needs;
    }

    // Routes the forward edges: each gap's tracks centred in it, between
    // the bands kept for back edges
    std::vector<std::vector<Point>> placeForward(
        const std::vector<Forward>& edges) {
        auto halves = trackHalves(edges);
        std::vector<std::vector<float>> heights(halves.size());
        for (size_t gap = 0; gap < halves.size(); gap++) {
            if (halves[gap].empty()) continue;
            std::vector<float> offsets = trackOffsets(halves[gap]);
            float top = rowBottom[gap];
            float bottom = rowTop[gap + 1];
            float span = offsets.back();
            float room = std::max(0.0f, bottom - top - 8.0f * scale);
            float squeeze = span > room ? room / span : 1.0f;
            float start = (top + bottom - span * squeeze) * 0.5f;
            heights[gap].resize(offsets.size());
            for (size_t k = 0; k < offsets.size(); k++) {
                heights[gap][k] = start + offsets[k] * squeeze;
            }
        }
        std::vector<std::vector<Point>> paths(edges.size());
        for (size_t i = 0; i < edges.size(); i++) {
            const Forward& edge = edges[i];
            const mermaid::Rect& f = boxes[edge.from];
            const mermaid::Rect& t = boxes[edge.to];
            size_t gap = ranks[edge.from];
            ports[edge.from][1].push_back({edge.fromX, 1.0f});
            ports[edge.to][0].push_back({edge.toX, 1.0f});
            if (edge.track < 0) {
                paths[i] = {{edge.fromX, f.bottom}, {edge.fromX, t.top}};
            } else {
                float y = heights[gap][spareAbove(gap) +
                                       static_cast<size_t>(edge.track)];
                paths[i] = {{edge.fromX, f.bottom},
                            {edge.fromX, y},
                            {edge.toX, y},
                            {edge.toX, t.top}};
                runs[gap].push_back({y, std::min(edge.fromX, edge.toX),
                                     std::max(edge.fromX, edge.toX),
                                     edge.half});
            }
            addDrawn(paths[i]);
        }
        return paths;
    }

    // A route through a lane before (side 0) or after (side 1) every node
    // of the ranks from `from` to `to`
    Plan planLane(size_t from, size_t to, int side, float half) const {
        Plan plan;
        size_t fromRank = ranks[from];
        size_t toRank = ranks[to];
        bool up = toRank < fromRank;
        size_t first = std::min(fromRank, toRank);
        size_t last = std::max(fromRank, toRank);
        float base = side ? -kHuge : kHuge;
        for (size_t i = 0; i < slots.size(); i++) {
            if (ranks[i] < first || ranks[i] > last) continue;
            base = side ? std::max(base, slots[i].right)
                        : std::min(base, slots[i].left);
        }
        float outward = side ? 1.0f : -1.0f;

        // Each end leaves its node straight from the side facing the lane,
        // or, past a neighbour, from the face toward the other end and
        // along the gap there
        struct End {
            std::vector<Point> path;  // from the node up to the lane
            float join = 0.0f;        // where it meets the lane
            bool viaGap = false;
            size_t gap = 0;
            float portX = 0.0f;
        };
        auto leave = [&](size_t node, bool isFrom) {
            End end;
            const mermaid::Rect& rect = boxes[node];
            float travel = isFrom == up ? -1.0f : 1.0f;
            if (!blocked(node, side)) {
                int face = 2 + side;
                const auto& used = ports[node][face];
                float toward = used.empty() ? travel : -used.back().travel;
                float y = pickPort(node, face, (rect.top + rect.bottom) * 0.5f,
                                   rect.top + 2.0f * scale,
                                   rect.bottom - 2.0f * scale, toward);
                plan.ports.push_back({node, face, {y, travel}});
                end.path = {{side ? rect.right : rect.left, y}};
                end.join = y;
                return end;
            }
            bool top = travel < 0.0f;
            int face = top ? 0 : 1;
            end.viaGap = true;
            end.gap = top ? ranks[node] - 1 : ranks[node];
            float inset = std::min(kPortStep * scale,
                                   (rect.right - rect.left) * 0.5f -
                                       2.0f * scale);
            end.portX = pickPort(node, face,
                                 side ? rect.right - inset : rect.left + inset,
                                 rect.left + 2.0f * scale,
                                 rect.right - 2.0f * scale, -outward);
            end.join = pickRun(end.gap, end.portX, outward * kHuge,
                               top ? 1.0f : -1.0f, 0.0f);
            plan.ports.push_back({node, face, {end.portX, travel}});
            end.path = {{end.portX, top ? rect.top : rect.bottom},
                        {end.portX, end.join}};
            return end;
        };
        End start = leave(from, true);
        End finish = leave(to, false);
        float low = std::min(start.join, finish.join);
        float high = std::max(start.join, finish.join);
        float x = pickLane(side, base, low, high, half);
        plan.points = start.path;
        plan.points.push_back({x, start.join});
        plan.points.push_back({x, finish.join});
        plan.points.insert(plan.points.end(), finish.path.rbegin(),
                           finish.path.rend());
        for (const End* end : {&start, &finish}) {
            if (!end->viaGap) continue;
            plan.runs.push_back({end->gap,
                                 {end->join, std::min(end->portX, x),
                                  std::max(end->portX, x), 0.0f}});
        }
        plan.laneSide = side;
        plan.lane = {x, low, high, half};
        plan.cost = cost(plan.points) + (side ? 0.0f : 20.0f * scale);
        return plan;
    }

    // A back edge to the rank just above, straight across the gap between
    // the two, beside the edges that go down it; `half` is its chip's band
    Plan planGap(size_t from, size_t to, float half) const {
        Plan plan;
        const mermaid::Rect& f = boxes[from];
        const mermaid::Rect& t = boxes[to];
        float step = kPortStep * scale;
        float fromMid = centerX(from);
        float toMid = centerX(to);
        float fromX = pickPort(from, 0, fromMid + step, f.left + 2.0f * scale,
                               f.right - 2.0f * scale, 1.0f);
        float toX = pickPort(to, 1, toMid + step, t.left + 2.0f * scale,
                             t.right - 2.0f * scale, 1.0f);
        plan.ports.push_back({from, 0, {fromX, -1.0f}});
        plan.ports.push_back({to, 1, {toX, -1.0f}});
        if (std::abs(fromX - toX) < 0.5f) {
            plan.points = {{fromX, f.top}, {fromX, t.bottom}};
        } else {
            // On the side of the gap's middle where it nests with a
            // forward edge between the same two nodes
            size_t gap = ranks[to];
            float y = pickRun(gap, fromX, toX, fromMid > toMid ? -1.0f : 1.0f,
                              half);
            plan.points = {{fromX, f.top}, {fromX, y}, {toX, y}, {toX, t.bottom}};
            plan.runs.push_back(
                {gap, {y, std::min(fromX, toX), std::max(fromX, toX), half}});
        }
        plan.cost = cost(plan.points);
        return plan;
    }
};

// Lays the graph out by rank with each node's notes and self-loops in its
// slot, grows the gaps between ranks to fit the runs and chips crossing
// them, then routes every edge, fanning edges to the next rank out over
// their nodes' faces when `fan` is set. `boxSizes` and `chipSizes` (zero
// for an unlabeled edge) are in the diagram's frame.
Scene layoutScene(const mermaid::Diagram& diagram,
                  const std::vector<mermaid::Size>& boxSizes,
                  const std::vector<Attachment>& attachments,
                  const std::vector<Size>& chipSizes, float nodeGap,
                  float rankGap, float scale, bool fan) {
    Scene scene;
    const size_t count = diagram.nodes.size();
    if (count == 0 || boxSizes.size() != count) return scene;
    Frame frame;
    frame.transpose = diagram.direction == mermaid::Direction::LeftToRight ||
                      diagram.direction == mermaid::Direction::RightToLeft;
    frame.flip = diagram.direction == mermaid::Direction::BottomToTop ||
                 diagram.direction == mermaid::Direction::RightToLeft;
    auto chipOf = [&](size_t edge) {
        return edge < chipSizes.size() ? frame.in(chipSizes[edge]) : Size{};
    };
    // Half the band an edge's chip takes across the rank direction
    auto band = [&](size_t edge) {
        Size chip = chipOf(edge);
        return chip.w > 0.0f ? chip.h * 0.5f + kChipClear * scale : 0.0f;
    };

    // Beside each box across the ranks: its notes, stacked along the
    // ranks, or its self-loops, nested, with their chips stacked beyond
    // them. Loops take the sides without notes in turn, and go over the
    // top when both sides have notes.
    struct Flank {
        std::vector<size_t> notes;
        float noteMain = 0.0f;
        std::vector<size_t> loops;
        float chipCross = 0.0f;
        float chipMain = 0.0f;
        Size room;
    };
    std::vector<std::array<Flank, 2>> flanks(count);
    std::vector<std::vector<size_t>> overTop(count);
    for (size_t a = 0; a < attachments.size(); a++) {
        const Attachment& attachment = attachments[a];
        if (attachment.node >= count) continue;
        Flank& flank = flanks[attachment.node][attachment.after ? 1 : 0];
        Size size = frame.in(attachment.size);
        if (!flank.notes.empty()) flank.noteMain += kNoteStack * scale;
        flank.notes.push_back(a);
        flank.noteMain += size.h;
        flank.room.w = std::max(flank.room.w, size.w + kNoteGap * scale);
        flank.room.h = flank.noteMain;
    }
    std::vector<size_t> loopsSoFar(count, 0);
    for (size_t e = 0; e < diagram.edges.size(); e++) {
        const auto& edge = diagram.edges[e];
        if (edge.from != edge.to || edge.from >= count) continue;
        auto& sides = flanks[edge.from];
        bool afterFree = sides[1].notes.empty();
        bool beforeFree = sides[0].notes.empty();
        size_t k = loopsSoFar[edge.from]++;
        if (!afterFree && !beforeFree) {
            overTop[edge.from].push_back(e);
            continue;
        }
        Flank& flank = sides[afterFree && (!beforeFree || k % 2 == 0) ? 1 : 0];
        flank.loops.push_back(e);
        Size chip = chipOf(e);
        if (chip.w <= 0.0f) continue;
        if (flank.chipMain > 0.0f) flank.chipMain += kNoteStack * scale;
        flank.chipMain += chip.h;
        flank.chipCross = std::max(flank.chipCross, chip.w);
    }
    auto loopReach = [&](size_t k) {
        return (kLoopReach + 10.0f * static_cast<float>(k)) * scale;
    };
    auto loopHalf = [&](size_t k, float boxMain) {
        return std::max(1.0f, std::min((7.0f + 4.0f * static_cast<float>(k)) *
                                           scale,
                                       boxMain * 0.5f - 1.5f * scale));
    };

    std::vector<Size> box(count);
    std::vector<mermaid::Size> slotSizes(count);
    for (size_t i = 0; i < count; i++) {
        box[i] = frame.in({boxSizes[i].width, boxSizes[i].height});
        for (Flank& flank : flanks[i]) {
            if (flank.loops.empty()) continue;
            size_t outer = flank.loops.size() - 1;
            flank.room.w = loopReach(outer) +
                           (flank.chipCross > 0.0f
                                ? kChipClear * scale + flank.chipCross
                                : 0.0f);
            flank.room.h =
                std::max(2.0f * loopHalf(outer, box[i].h), flank.chipMain);
        }
        slotSizes[i] = {box[i].w + flanks[i][0].room.w + flanks[i][1].room.w,
                        std::max({box[i].h, flanks[i][0].room.h,
                                  flanks[i][1].room.h})};
    }

    mermaid::Diagram ranking = acyclicForRanking(diagram);
    ranking.direction = mermaid::Direction::TopToBottom;
    mermaid::Layout layout =
        mermaid::layout(ranking, slotSizes, nodeGap, rankGap);
    if (layout.nodes.size() != count || layout.ranks.size() != count) {
        return scene;
    }

    Router router;
    router.scale = scale;
    router.ranks = layout.ranks;
    router.boxes.resize(count);
    router.flanked.resize(count);
    std::vector<mermaid::Rect> notes(attachments.size());
    // Boxes and notes in their slots
    auto place = [&]() {
        router.slots = layout.nodes;
        for (size_t i = 0; i < count; i++) {
            const mermaid::Rect& slot = layout.nodes[i];
            float middle = (slot.top + slot.bottom) * 0.5f;
            float left = slot.left + flanks[i][0].room.w;
            mermaid::Rect& rect = router.boxes[i];
            rect = {left, middle - box[i].h * 0.5f, left + box[i].w,
                    middle + box[i].h * 0.5f};
            for (int side = 0; side < 2; side++) {
                const Flank& flank = flanks[i][side];
                router.flanked[i][side] = flank.room.w > 0.0f;
                float y = middle - flank.noteMain * 0.5f;
                for (size_t a : flank.notes) {
                    Size size = frame.in(attachments[a].size);
                    float x = side ? rect.right + kNoteGap * scale
                                   : rect.left - kNoteGap * scale - size.w;
                    notes[a] = {x, y, x + size.w, y + size.h};
                    y += size.h + kNoteStack * scale;
                }
            }
        }
        router.setup();
    };
    place();

    std::vector<Router::Forward> forwards;
    std::vector<size_t> laneEdges;
    for (size_t e = 0; e < diagram.edges.size(); e++) {
        const auto& edge = diagram.edges[e];
        if (edge.from >= count || edge.to >= count || edge.from == edge.to) {
            continue;
        }
        if (layout.ranks[edge.to] == layout.ranks[edge.from] + 1) {
            forwards.push_back({e, edge.from, edge.to, band(e)});
        } else {
            laneEdges.push_back(e);
        }
    }
    router.planForward(forwards, fan);

    // Gaps grow to hold the runs across them with their chips, and a band
    // for each back edge that may cross one directly, on the side where
    // its run nests with an edge down between the same two nodes
    router.spare.assign(router.gapCount(), {});
    for (size_t e : laneEdges) {
        const auto& edge = diagram.edges[e];
        if (layout.ranks[edge.from] == layout.ranks[edge.to] + 1) {
            bool above = router.centerX(edge.from) > router.centerX(edge.to);
            router.spare[layout.ranks[edge.to]][above ? 0 : 1].push_back(
                band(e));
        }
    }
    std::vector<float> needs = router.gapNeeds(forwards);
    std::vector<float> shift(router.rowTop.size(), 0.0f);
    float grown = 0.0f;
    for (size_t gap = 0; gap < needs.size(); gap++) {
        float have = router.rowTop[gap + 1] - router.rowBottom[gap];
        grown += std::max(0.0f, needs[gap] - have);
        shift[gap + 1] = grown;
    }
    if (grown > 0.0f) {
        for (size_t i = 0; i < count; i++) {
            layout.nodes[i].top += shift[layout.ranks[i]];
            layout.nodes[i].bottom += shift[layout.ranks[i]];
        }
        layout.height += grown;
        place();
    }
    frame.extent = layout.height;

    std::vector<RoutedEdge> routes(diagram.edges.size());
    auto forwardPaths = router.placeForward(forwards);
    for (size_t k = 0; k < forwards.size(); k++) {
        routes[forwards[k].edge].points = std::move(forwardPaths[k]);
    }

    // Self-loops in the room kept beside their box, chips beyond them
    for (size_t i = 0; i < count; i++) {
        const mermaid::Rect& rect = router.boxes[i];
        float middle = (rect.top + rect.bottom) * 0.5f;
        float main = rect.bottom - rect.top;
        for (int side = 0; side < 2; side++) {
            const Flank& flank = flanks[i][side];
            if (flank.loops.empty()) continue;
            float dir = side ? 1.0f : -1.0f;
            float x = side ? rect.right : rect.left;
            float outer = loopReach(flank.loops.size() - 1);
            float chipY = middle - flank.chipMain * 0.5f;
            for (size_t k = 0; k < flank.loops.size(); k++) {
                size_t e = flank.loops[k];
                float reach = loopReach(k);
                float half = loopHalf(k, main);
                RoutedEdge& route = routes[e];
                route.points = {{x, middle - half},
                                {x + dir * reach, middle - half},
                                {x + dir * reach, middle + half},
                                {x, middle + half}};
                Size chip = chipOf(e);
                if (chip.w > 0.0f) {
                    route.chipBeside = true;
                    route.chipAt = {
                        x + dir * (outer + kChipClear * scale + chip.w * 0.5f),
                        chipY + chip.h * 0.5f};
                    chipY += chip.h + kNoteStack * scale;
                }
                router.addDrawn(route.points);
            }
        }
        for (size_t k = 0; k < overTop[i].size(); k++) {
            float reach = loopReach(k);
            float half = loopHalf(k, main);
            float right =
                rect.right - (3.0f + 4.0f * static_cast<float>(k)) * scale;
            float left = std::max(rect.left + 2.0f * scale, right - 2.0f * half);
            RoutedEdge& route = routes[overTop[i][k]];
            route.points = {{right, rect.top},
                            {right, rect.top - reach},
                            {left, rect.top - reach},
                            {left, rect.top}};
            router.addDrawn(route.points);
        }
    }

    // Lanes, the shortest first so longer ones pass outside them
    auto span = [&](size_t e) {
        size_t a = layout.ranks[diagram.edges[e].from];
        size_t b = layout.ranks[diagram.edges[e].to];
        return a > b ? a - b : b - a;
    };
    std::stable_sort(laneEdges.begin(), laneEdges.end(),
                     [&](size_t a, size_t b) { return span(a) < span(b); });
    for (size_t e : laneEdges) {
        const auto& edge = diagram.edges[e];
        if (span(e) == 0) {  // ranks never put an edge's ends side by side
            const mermaid::Rect& f = router.boxes[edge.from];
            const mermaid::Rect& t = router.boxes[edge.to];
            float y = router.rowBottom[layout.ranks[edge.from]] + 12.0f * scale;
            routes[e].points = {{router.centerX(edge.from), f.bottom},
                                {router.centerX(edge.from), y},
                                {router.centerX(edge.to), y},
                                {router.centerX(edge.to), t.bottom}};
            router.addDrawn(routes[e].points);
            continue;
        }
        float half = chipOf(e).w * 0.5f;
        Router::Plan best = router.planLane(edge.from, edge.to, 1, half);
        Router::Plan before = router.planLane(edge.from, edge.to, 0, half);
        if (before.cost < best.cost) best = std::move(before);
        if (layout.ranks[edge.from] == layout.ranks[edge.to] + 1) {
            Router::Plan direct = router.planGap(edge.from, edge.to, band(e));
            if (direct.cost < best.cost) best = std::move(direct);
        }
        router.commit(best);
        routes[e].points = std::move(best.points);
    }

    scene.boxes.resize(count);
    for (size_t i = 0; i < count; i++) {
        scene.boxes[i] = frame.out(router.boxes[i]);
    }
    scene.attachments.resize(attachments.size());
    for (size_t a = 0; a < attachments.size(); a++) {
        scene.attachments[a] = frame.out(notes[a]);
    }
    for (auto& route : routes) {
        for (auto& point : route.points) point = frame.out(point);
        if (route.chipBeside) route.chipAt = frame.out(route.chipAt);
    }
    scene.routes = std::move(routes);
    return scene;
}

// Removes duplicate consecutive points so direction vectors are non-zero
void dedupePoints(std::vector<Point>& points) {
    for (size_t i = points.size(); i-- > 1;) {
        if (std::abs(points[i].x - points[i - 1].x) < 0.01f &&
            std::abs(points[i].y - points[i - 1].y) < 0.01f) {
            points.erase(points.begin() + i);
        }
    }
}

// Emits an end marker at points[index] (0 = start, back = end); the marker
// apex touches the node border and the line is shortened underneath solid
// markers so it does not poke through
void emitMarker(std::vector<Prim>& prims, std::vector<Point>& points,
                bool atStart, Marker marker, float scale) {
    if (marker == Marker::None || points.size() < 2) return;
    Point& tip = atStart ? points.front() : points.back();
    const Point& neighbor = atStart ? points[1] : points[points.size() - 2];
    float dx = neighbor.x - tip.x;
    float dy = neighbor.y - tip.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01f) return;
    dx /= length;  // direction AWAY from the node, along the line
    dy /= length;
    float px = -dy, py = dx;  // perpendicular
    float depth = kMarkerDepth * scale;

    auto polygon = [&](std::vector<Point> pts, Role fill, Role stroke) {
        Prim prim;
        prim.type = PrimType::Polygon;
        prim.pts = std::move(pts);
        prim.fill = fill;
        prim.stroke = stroke;
        prim.strokeWidth = 1.3f * scale;
        prims.push_back(std::move(prim));
    };
    auto lineAt = [&](Point a, Point b) {
        Prim prim;
        prim.type = PrimType::Line;
        prim.x1 = a.x;
        prim.y1 = a.y;
        prim.x2 = b.x;
        prim.y2 = b.y;
        prim.stroke = Role::Muted;
        prim.strokeWidth = 1.4f * scale;
        prims.push_back(prim);
    };
    auto shorten = [&](float amount) {
        tip.x += dx * amount;
        tip.y += dy * amount;
    };

    switch (marker) {
        case Marker::FilledArrow: {
            float wing = 4.5f * scale;
            polygon({{tip.x, tip.y},
                     {tip.x + dx * depth * 0.85f + px * wing,
                      tip.y + dy * depth * 0.85f + py * wing},
                     {tip.x + dx * depth * 0.85f - px * wing,
                      tip.y + dy * depth * 0.85f - py * wing}},
                    Role::Muted, Role::None);
            shorten(depth * 0.6f);
            break;
        }
        case Marker::OpenArrow: {
            float wing = 4.5f * scale;
            lineAt(tip, {tip.x + dx * depth * 0.85f + px * wing,
                         tip.y + dy * depth * 0.85f + py * wing});
            lineAt(tip, {tip.x + dx * depth * 0.85f - px * wing,
                         tip.y + dy * depth * 0.85f - py * wing});
            break;
        }
        case Marker::HollowTriangle: {
            float wing = 6.0f * scale;
            float triDepth = depth * 1.15f;
            polygon({{tip.x, tip.y},
                     {tip.x + dx * triDepth + px * wing,
                      tip.y + dy * triDepth + py * wing},
                     {tip.x + dx * triDepth - px * wing,
                      tip.y + dy * triDepth - py * wing}},
                    Role::Background, Role::Muted);
            shorten(depth * 1.15f);
            break;
        }
        case Marker::HollowDiamond:
        case Marker::FilledDiamond: {
            float wing = 5.0f * scale;
            float half = depth * 0.75f;
            polygon({{tip.x, tip.y},
                     {tip.x + dx * half + px * wing,
                      tip.y + dy * half + py * wing},
                     {tip.x + dx * half * 2.0f, tip.y + dy * half * 2.0f},
                     {tip.x + dx * half - px * wing,
                      tip.y + dy * half - py * wing}},
                    marker == Marker::FilledDiamond ? Role::Muted
                                                    : Role::Background,
                    Role::Muted);
            shorten(half * 2.0f);
            break;
        }
        case Marker::CrowMany:
        case Marker::ZeroMany:
        case Marker::OneMany: {
            float wing = 5.5f * scale;
            Point base = {tip.x + dx * depth, tip.y + dy * depth};
            lineAt(base, tip);
            lineAt(base, {tip.x + px * wing, tip.y + py * wing});
            lineAt(base, {tip.x - px * wing, tip.y - py * wing});
            if (marker == Marker::ZeroMany) {
                float r = 3.5f * scale;
                Prim circle;
                circle.type = PrimType::Ellipse;
                circle.x1 = base.x + dx * (r + 2.0f * scale) - r;
                circle.y1 = base.y + dy * (r + 2.0f * scale) - r;
                circle.x2 = circle.x1 + r * 2.0f;
                circle.y2 = circle.y1 + r * 2.0f;
                circle.fill = Role::Background;
                circle.stroke = Role::Muted;
                circle.strokeWidth = 1.3f * scale;
                prims.push_back(circle);
            } else if (marker == Marker::OneMany) {
                float barAt = depth + 4.0f * scale;
                float wing2 = 5.0f * scale;
                lineAt({tip.x + dx * barAt + px * wing2,
                        tip.y + dy * barAt + py * wing2},
                       {tip.x + dx * barAt - px * wing2,
                        tip.y + dy * barAt - py * wing2});
            }
            break;
        }
        case Marker::One: {
            float wing = 5.0f * scale;
            for (float at : {7.0f, 11.0f}) {
                float depth2 = at * scale;
                lineAt({tip.x + dx * depth2 + px * wing,
                        tip.y + dy * depth2 + py * wing},
                       {tip.x + dx * depth2 - px * wing,
                        tip.y + dy * depth2 - py * wing});
            }
            break;
        }
        case Marker::ZeroOne: {
            float wing = 5.0f * scale;
            float barAt = 7.0f * scale;
            lineAt({tip.x + dx * barAt + px * wing,
                    tip.y + dy * barAt + py * wing},
                   {tip.x + dx * barAt - px * wing,
                    tip.y + dy * barAt - py * wing});
            float r = 3.5f * scale;
            float circleAt = barAt + 3.0f * scale + r;
            Prim circle;
            circle.type = PrimType::Ellipse;
            circle.x1 = tip.x + dx * circleAt - r;
            circle.y1 = tip.y + dy * circleAt - r;
            circle.x2 = circle.x1 + r * 2.0f;
            circle.y2 = circle.y1 + r * 2.0f;
            circle.fill = Role::Background;
            circle.stroke = Role::Muted;
            circle.strokeWidth = 1.3f * scale;
            prims.push_back(circle);
            break;
        }
        case Marker::None:
            break;
    }
}

const std::string& edgeLabel(const mermaid::Diagram& diagram,
                             const std::vector<EdgeDecor>& decors,
                             size_t edge) {
    return edge < decors.size() && !decors[edge].label.empty()
               ? decors[edge].label
               : diagram.edges[edge].label;
}

// Label chip sizes per edge, zero for an edge without a label
std::vector<Size> measureChips(const mermaid::Diagram& diagram,
                               const std::vector<EdgeDecor>& decors,
                               const Measure& measure, float scale) {
    TextStyle labelStyle;
    labelStyle.scale = 0.9f;
    std::vector<Size> sizes(diagram.edges.size());
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        const std::string& label = edgeLabel(diagram, decors, i);
        if (label.empty()) continue;
        Size text = measure(label, labelStyle, 0.0f);
        sizes[i] = {text.w + 2.0f * kEdgeLabelPad * scale,
                    text.h + 4.0f * scale};
    }
    return sizes;
}

bool overlaps(const mermaid::Rect& a, const mermaid::Rect& b, float pad) {
    return a.left < b.right + pad && a.right > b.left - pad &&
           a.top < b.bottom + pad && a.bottom > b.top - pad;
}

// Whether an axis-aligned segment passes through a rectangle's inside
bool crossesRect(Point a, Point b, const mermaid::Rect& rect) {
    return std::min(a.x, b.x) < rect.right &&
           std::max(a.x, b.x) > rect.left &&
           std::min(a.y, b.y) < rect.bottom &&
           std::max(a.y, b.y) > rect.top;
}

struct EdgeLine {
    size_t edge;
    Point a;
    Point b;
};

// Where an edge's label chip goes: on its own line, nearest the line's
// middle, where it touches no node, note or other chip and no other line;
// failing that just beside its line; then where it covers the fewest other
// lines, sparing the room of edges whose chips are still to come; at worst
// at the nearest free spot around the middle. `lengths` holds each edge's
// route length, `pending` the edges whose chips are still to come.
mermaid::Rect placeChip(const RoutedEdge& route, size_t edge, Size size,
                        const std::vector<mermaid::Rect>& solid,
                        const std::vector<EdgeLine>& lines,
                        const std::vector<float>& lengths,
                        const std::vector<bool>& pending, float scale) {
    float clear = kChipClear * scale;
    auto centeredAt = [&](Point center) {
        return mermaid::Rect{center.x - size.w * 0.5f, center.y - size.h * 0.5f,
                             center.x + size.w * 0.5f,
                             center.y + size.h * 0.5f};
    };
    auto free = [&](const mermaid::Rect& rect) {
        for (const auto& other : solid) {
            if (overlaps(rect, other, clear)) return false;
        }
        return true;
    };
    // One per line covered, more for covering much of a line whose chip
    // is still to come
    auto covered = [&](const mermaid::Rect& rect) {
        float cost = 0.0f;
        for (const auto& line : lines) {
            if (line.edge == edge || !crossesRect(line.a, line.b, rect)) {
                continue;
            }
            cost += 1.0f;
            if (line.edge < pending.size() && pending[line.edge] &&
                lengths[line.edge] > 0.0f) {
                float along =
                    std::max(0.0f, sharedSpan(line.a.x, line.b.x, rect.left,
                                              rect.right)) +
                    std::max(0.0f, sharedSpan(line.a.y, line.b.y, rect.top,
                                              rect.bottom));
                cost += 8.0f * along / lengths[line.edge];
            }
        }
        return cost;
    };
    if (route.chipBeside && free(centeredAt(route.chipAt))) {
        return centeredAt(route.chipAt);
    }

    // Points along the line, from its middle outward
    struct Sample {
        Point at;
        bool flat;
    };
    const auto& points = route.points;
    float total = 0.0f;
    for (size_t p = 1; p < points.size(); p++) {
        total += std::hypot(points[p].x - points[p - 1].x,
                            points[p].y - points[p - 1].y);
    }
    auto sampleAt = [&](float distance) {
        for (size_t p = 1; p < points.size(); p++) {
            float dx = points[p].x - points[p - 1].x;
            float dy = points[p].y - points[p - 1].y;
            float length = std::hypot(dx, dy);
            if (distance <= length || p + 1 == points.size()) {
                float t = length > 0.0f ? std::min(1.0f, distance / length)
                                        : 0.0f;
                return Sample{{points[p - 1].x + dx * t,
                               points[p - 1].y + dy * t},
                              std::abs(dy) < std::abs(dx)};
            }
            distance -= length;
        }
        return Sample{points.front(), true};
    };
    std::vector<Sample> samples;
    float step = std::max(3.0f * scale, total / 2000.0f);
    float margin = std::min(4.0f * scale, total * 0.5f);
    for (int k = 0; k < 4002; k++) {
        float offset = static_cast<float>((k + 1) / 2) * step *
                       (k % 2 ? 1.0f : -1.0f);
        if (!(std::abs(offset) <= total * 0.5f)) break;
        float distance = total * 0.5f + offset;
        if (distance < margin || distance > total - margin) continue;
        samples.push_back(sampleAt(distance));
    }
    auto beside = [&](const Sample& sample, bool after) {
        Point center = sample.at;
        float sign = after ? 1.0f : -1.0f;
        if (sample.flat) {
            center.y += sign * (size.h * 0.5f + clear);
        } else {
            center.x += sign * (size.w * 0.5f + clear);
        }
        return centeredAt(center);
    };
    std::vector<mermaid::Rect> candidates;
    for (const Sample& sample : samples) {
        candidates.push_back(centeredAt(sample.at));
    }
    for (const Sample& sample : samples) {
        candidates.push_back(beside(sample, true));
        candidates.push_back(beside(sample, false));
    }
    bool found = false;
    float bestCost = 0.0f;
    mermaid::Rect best{};
    for (const auto& rect : candidates) {
        if (!free(rect)) continue;
        float cost = covered(rect);
        if (cost <= 0.0f) return rect;
        if (!found || cost < bestCost) {
            found = true;
            bestCost = cost;
            best = rect;
        }
    }
    if (found) return best;

    Point middle = samples.empty() ? points.front() : samples.front().at;
    float ringStep = 6.0f * scale;
    for (int ring = 1; ring <= 120; ring++) {
        float reach = static_cast<float>(ring) * ringStep;
        for (int k = 0; k <= 2 * ring; k++) {
            float along = static_cast<float>((k + 1) / 2) * ringStep *
                          (k % 2 ? 1.0f : -1.0f);
            const Point around[] = {{middle.x + along, middle.y - reach},
                                    {middle.x + along, middle.y + reach},
                                    {middle.x - reach, middle.y + along},
                                    {middle.x + reach, middle.y + along}};
            for (const Point& center : around) {
                mermaid::Rect rect = centeredAt(center);
                if (free(rect)) return rect;
            }
        }
    }
    float bottom = middle.y;
    for (const auto& other : solid) bottom = std::max(bottom, other.bottom);
    return centeredAt({middle.x, bottom + clear * 2.0f + size.h * 0.5f});
}

// Emits the routed edges: their lines and markers, then every label chip
// over the lines, each where it overlaps no node, note or other chip
void emitEdges(std::vector<Prim>& prims, const mermaid::Diagram& diagram,
               Scene& scene, const std::vector<EdgeDecor>& decors,
               const std::vector<Size>& chipSizes, const Measure& measure,
               float scale) {
    TextStyle labelStyle;
    labelStyle.scale = 0.9f;
    std::vector<mermaid::Rect> solid = scene.boxes;
    solid.insert(solid.end(), scene.attachments.begin(),
                 scene.attachments.end());
    std::vector<EdgeLine> lines;
    std::vector<Prim> labels;
    size_t edgeCount = std::min(diagram.edges.size(), scene.routes.size());
    for (size_t i = 0; i < edgeCount; i++) {
        auto& points = scene.routes[i].points;
        dedupePoints(points);
        if (points.size() < 2) continue;
        const EdgeDecor& decor =
            i < decors.size() ? decors[i] : EdgeDecor{};

        // Chips keep off the arrowheads and other end marks
        for (bool atStart : {true, false}) {
            if ((atStart ? decor.atFrom : decor.atTo) == Marker::None) continue;
            const Point& tip = atStart ? points.front() : points.back();
            float reach = (kMarkerDepth + 3.0f) * scale;
            solid.push_back(
                {tip.x - reach, tip.y - reach, tip.x + reach, tip.y + reach});
        }
        emitMarker(prims, points, true, decor.atFrom, scale);
        emitMarker(prims, points, false, decor.atTo, scale);

        for (size_t p = 1; p < points.size(); p++) {
            Prim line;
            line.type = PrimType::Line;
            line.x1 = points[p - 1].x;
            line.y1 = points[p - 1].y;
            line.x2 = points[p].x;
            line.y2 = points[p].y;
            line.stroke = Role::Muted;
            line.strokeWidth = 1.4f * scale;
            line.dashed = decor.dashed;
            prims.push_back(line);
            lines.push_back({i, points[p - 1], points[p]});
        }

        // Multiplicity labels near the ends
        auto endLabel = [&](const std::string& value, bool atStart) {
            if (value.empty()) return;
            Size size = measure(value, labelStyle, 0.0f);
            const Point& tip = atStart ? points.front() : points.back();
            const Point& neighbor =
                atStart ? points[1] : points[points.size() - 2];
            float dx = neighbor.x - tip.x;
            float dy = neighbor.y - tip.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.01f) return;
            dx /= length;
            dy /= length;
            float alongX = tip.x + dx * 16.0f * scale;
            float alongY = tip.y + dy * 16.0f * scale;
            float sideX = -dy, sideY = dx;
            Prim text;
            text.type = PrimType::Text;
            text.text = value;
            text.style = labelStyle;
            text.fill = Role::Muted;
            text.x1 = alongX + sideX * 8.0f * scale -
                      (sideX < -0.5f ? size.w : sideX > 0.5f ? 0.0f
                                                             : size.w * 0.5f);
            text.y1 = alongY + sideY * 8.0f * scale -
                      (sideY < -0.5f ? size.h : sideY > 0.5f ? 0.0f
                                                             : size.h * 0.5f);
            text.x2 = text.x1 + size.w;
            text.y2 = text.y1 + size.h;
            text.alignH = -1;
            solid.push_back({text.x1, text.y1, text.x2, text.y2});
            labels.push_back(std::move(text));
        };
        endLabel(decor.labelFrom, true);
        endLabel(decor.labelTo, false);
    }

    // Chips, the shortest edges (with the least room) first
    std::vector<size_t> labeled;
    for (size_t i = 0; i < edgeCount; i++) {
        if (i < chipSizes.size() && chipSizes[i].w > 0.0f &&
            scene.routes[i].points.size() >= 2) {
            labeled.push_back(i);
        }
    }
    std::stable_sort(labeled.begin(), labeled.end(), [&](size_t a, size_t b) {
        return pathLength(scene.routes[a].points) <
               pathLength(scene.routes[b].points);
    });
    std::vector<bool> pending(edgeCount, false);
    std::vector<float> lengths(edgeCount, 0.0f);
    for (size_t i : labeled) {
        pending[i] = true;
        lengths[i] = pathLength(scene.routes[i].points);
    }
    for (size_t i : labeled) {
        pending[i] = false;
        mermaid::Rect rect = placeChip(scene.routes[i], i, chipSizes[i],
                                       solid, lines, lengths, pending, scale);
        solid.push_back(rect);
        Prim chip;
        chip.type = PrimType::RoundRect;
        chip.radius = 4.0f * scale;
        chip.x1 = rect.left;
        chip.y1 = rect.top;
        chip.x2 = rect.right;
        chip.y2 = rect.bottom;
        chip.fill = Role::Fill;
        chip.stroke = Role::Muted;
        chip.strokeWidth = 1.0f * scale;
        labels.push_back(chip);
        Prim text;
        text.type = PrimType::Text;
        text.text = edgeLabel(diagram, decors, i);
        text.style = labelStyle;
        text.fill = Role::Text;
        text.x1 = rect.left;
        text.y1 = rect.top;
        text.x2 = rect.right;
        text.y2 = rect.bottom;
        labels.push_back(std::move(text));
    }
    prims.insert(prims.end(), std::make_move_iterator(labels.begin()),
                 std::make_move_iterator(labels.end()));
}

// The extent of a list of prims
mermaid::Rect primBounds(const std::vector<Prim>& prims) {
    mermaid::Rect bounds{kHuge, kHuge, -kHuge, -kHuge};
    auto add = [&](float x, float y) {
        bounds.left = std::min(bounds.left, x);
        bounds.top = std::min(bounds.top, y);
        bounds.right = std::max(bounds.right, x);
        bounds.bottom = std::max(bounds.bottom, y);
    };
    for (const auto& prim : prims) {
        if (prim.type == PrimType::Polygon) {
            for (const auto& point : prim.pts) add(point.x, point.y);
        } else if (prim.type == PrimType::Slice) {
            add(prim.x1 - prim.radius, prim.y1 - prim.radius);
            add(prim.x1 + prim.radius, prim.y1 + prim.radius);
        } else {
            add(prim.x1, prim.y1);
            add(prim.x2, prim.y2);
        }
    }
    if (bounds.left > bounds.right) return {};
    return bounds;
}

void shiftPrims(std::vector<Prim>& prims, float dx, float dy) {
    for (auto& prim : prims) {
        prim.x1 += dx;
        prim.x2 += dx;
        prim.y1 += dy;
        prim.y2 += dy;
        for (auto& point : prim.pts) {
            point.x += dx;
            point.y += dy;
        }
    }
}

// Moves the drawing to start at (0, 0), since lanes, chips and notes can
// lie left of or above the rank layout, and sizes it with a margin
void fitPrims(Built& built, float margin) {
    mermaid::Rect bounds = primBounds(built.prims);
    shiftPrims(built.prims, -bounds.left, -bounds.top);
    built.width = bounds.right - bounds.left + margin;
    built.height = bounds.bottom - bounds.top + margin;
}

}  // namespace

// --------------------------------------------------------------------------
// State diagrams
// --------------------------------------------------------------------------

namespace {

constexpr float kCompositePad = 14.0f;  // a composite's border to its regions
constexpr float kRegionGap = 28.0f;     // between concurrent regions

enum class StateKind { Normal, Start, End, Choice, Fork };

struct StateNote {
    size_t node = 0;
    bool rightOf = true;
    std::string text;
};

// The diagram, or one concurrent region of a composite state, laid out on
// its own with its own direction and its own start and end
struct StateRegion {
    size_t owner = SIZE_MAX;  // the composite state, SIZE_MAX at the top
    mermaid::Direction direction = mermaid::Direction::TopToBottom;
    std::vector<size_t> states;
    size_t start = SIZE_MAX;
    size_t end = SIZE_MAX;
};

struct Transition {
    size_t from = 0;
    size_t to = 0;
    std::string label;
};

// Primitives drawn from (0, 0), and their extent
struct Drawing {
    std::vector<Prim> prims;
    Size size;
};

// Regions side by side, or stacked, with where the lines between them go
struct Arranged {
    Drawing drawing;
    std::vector<float> dividers;
    bool sideBySide = true;
};

struct Composite {
    Arranged body;
    float header = 0.0f;  // height of the band with the name
    Size name;
    Size about;           // its descriptions under the name
    Size size;
};

bool isVertical(mermaid::Direction direction) {
    return direction == mermaid::Direction::TopToBottom ||
           direction == mermaid::Direction::BottomToTop;
}

}  // namespace

Built buildState(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    GraphNodes graph;
    std::vector<StateKind> kinds;
    std::vector<std::vector<std::string>> descriptions;
    std::vector<size_t> home;                // the region a state is in
    std::vector<std::vector<size_t>> inner;  // a composite state's regions
    std::vector<StateRegion> regions(1);
    std::vector<size_t> topRegions{0};       // the diagram split by --
    std::vector<size_t> open{0};             // regions being read
    std::vector<Transition> transitions;
    std::vector<StateNote> notes;

    // Whether `region` lies inside composite `state`, however deep
    auto insideOf = [&](size_t region, size_t state) {
        for (size_t owner = regions[region].owner; owner != SIZE_MAX;
             owner = regions[home[owner]].owner) {
            if (owner == state) return true;
        }
        return false;
    };
    // As in Mermaid, a state named inside a composite belongs to it, the
    // last such composite when there are several, and naming it at the
    // top again does not take it out; otherwise it belongs to the top
    // region where it is first named. A composite never goes inside itself.
    auto ensureState = [&](std::string_view id,
                           std::string_view label) -> size_t {
        size_t index = graph.ensure(id, label);
        size_t region = open.back();
        while (kinds.size() < graph.diagram.nodes.size()) {
            kinds.push_back(StateKind::Normal);
            descriptions.emplace_back();
            inner.emplace_back();
            home.push_back(region);
        }
        if (regions[region].owner != SIZE_MAX && home[index] != region &&
            !insideOf(region, index)) {
            home[index] = region;
        }
        return index;
    };
    // [*] is the start or end of the region it is written in
    auto ensureEndpoint = [&](bool isStart) -> size_t {
        size_t region = open.back();
        size_t node = isStart ? regions[region].start : regions[region].end;
        if (node == SIZE_MAX) {
            std::string id(1, '\x1f');
            id += (isStart ? "start" : "end") + std::to_string(region);
            node = ensureState(id, " ");
            kinds[node] = isStart ? StateKind::Start : StateKind::End;
            graph.diagram.nodes[node].label.clear();
            (isStart ? regions[region].start : regions[region].end) = node;
        }
        return node;
    };
    // `state X {` reads into X's first region, or its last when X was
    // opened before
    auto openComposite = [&](size_t state) {
        if (inner[state].empty()) {
            StateRegion region;
            region.owner = state;
            regions.push_back(region);
            inner[state].push_back(regions.size() - 1);
        }
        open.push_back(inner[state].back());
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "stateDiagram-v2") &&
                !startsWithWord(line, "stateDiagram")) {
                result.error = "Expected stateDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "direction", &rest)) {
            mermaid::Direction& direction = regions[open.back()].direction;
            if (rest == "LR") {
                direction = mermaid::Direction::LeftToRight;
            } else if (rest == "RL") {
                direction = mermaid::Direction::RightToLeft;
            } else if (rest == "BT") {
                direction = mermaid::Direction::BottomToTop;
            } else {
                direction = mermaid::Direction::TopToBottom;
            }
            continue;
        }
        if (line == "}") {
            if (open.size() < 2) {
                result.error = "Unexpected }";
                return result;
            }
            open.pop_back();
            continue;
        }
        if (line == "--") {
            // A concurrent region of the composite being read, or of the
            // diagram itself
            StateRegion region;
            region.owner = regions[open.back()].owner;
            regions.push_back(region);
            size_t index = regions.size() - 1;
            if (region.owner == SIZE_MAX) {
                topRegions.push_back(index);
            } else {
                inner[region.owner].push_back(index);
            }
            open.back() = index;
            continue;
        }
        if (startsWithWord(line, "note", &rest)) {
            StateNote note;
            std::string_view spec = rest;
            if (startsWithWord(spec, "right", &spec)) {
                note.rightOf = true;
            } else if (startsWithWord(spec, "left", &spec)) {
                note.rightOf = false;
            } else {
                result.error = "Unsupported note position";
                return result;
            }
            startsWithWord(spec, "of", &spec);
            size_t colon = spec.find(':');
            if (colon == std::string_view::npos) {
                result.error = "Multi-line notes are not supported";
                return result;
            }
            note.node = ensureState(trimView(spec.substr(0, colon)), {});
            note.text = cleanLabel(trimView(spec.substr(colon + 1)));
            notes.push_back(std::move(note));
            continue;
        }
        if (startsWithWord(line, "state", &rest)) {
            // state "description" as id  |  state id <<choice|fork|join>>,
            // either opening a composite with {, or {} for an empty one
            bool opens = false;
            bool empty = false;
            if (rest.size() >= 2 && rest.substr(rest.size() - 2) == "{}") {
                empty = true;
                rest = trimView(rest.substr(0, rest.size() - 2));
            } else if (!rest.empty() && rest.back() == '{') {
                opens = true;
                rest = trimView(rest.substr(0, rest.size() - 1));
            }
            size_t node = SIZE_MAX;
            if (!rest.empty() && rest.front() == '"') {
                size_t closeQuote = rest.find('"', 1);
                if (closeQuote == std::string_view::npos) {
                    result.error = "Unterminated state description";
                    return result;
                }
                std::string_view description = rest.substr(1, closeQuote - 1);
                std::string_view after = trimView(rest.substr(closeQuote + 1));
                std::string_view id;
                if (startsWithWord(after, "as", &id)) {
                    node = ensureState(trimView(id), description);
                } else {
                    node = ensureState(description, description);
                }
            } else {
                size_t marker = rest.find("<<");
                if (marker != std::string_view::npos) {
                    std::string_view id = trimView(rest.substr(0, marker));
                    std::string_view kind = rest.substr(marker + 2);
                    size_t close = kind.find(">>");
                    if (close != std::string_view::npos) {
                        kind = kind.substr(0, close);
                    }
                    node = ensureState(id, {});
                    if (kind == "choice") {
                        kinds[node] = StateKind::Choice;
                    } else if (kind == "fork" || kind == "join") {
                        kinds[node] = StateKind::Fork;
                        graph.diagram.nodes[node].label.clear();
                    }
                } else if (!rest.empty()) {
                    node = ensureState(rest, {});
                }
            }
            if (node == SIZE_MAX) {
                result.error = "State without a name";
                return result;
            }
            if (opens || empty) openComposite(node);
            if (empty) open.pop_back();
            continue;
        }

        // Transition: A --> B [: label], with [*] as start/end
        size_t arrow = line.find("-->");
        if (arrow != std::string_view::npos) {
            std::string_view fromId = trimView(line.substr(0, arrow));
            std::string_view afterArrow = line.substr(arrow + 3);
            std::string_view toId = afterArrow;
            std::string label;
            size_t colon = afterArrow.find(':');
            if (colon != std::string_view::npos) {
                toId = trimView(afterArrow.substr(0, colon));
                label = cleanLabel(trimView(afterArrow.substr(colon + 1)));
            } else {
                toId = trimView(afterArrow);
            }
            if (fromId.empty() || toId.empty()) {
                result.error = "Transition needs two states";
                return result;
            }
            Transition transition;
            transition.from = fromId == "[*]" ? ensureEndpoint(true)
                                              : ensureState(fromId, {});
            transition.to = toId == "[*]" ? ensureEndpoint(false)
                                          : ensureState(toId, {});
            transition.label = std::move(label);
            transitions.push_back(std::move(transition));
            continue;
        }

        // Description: id : text
        size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            size_t node = ensureState(trimView(line.substr(0, colon)), {});
            descriptions[node].push_back(
                cleanLabel(trimView(line.substr(colon + 1))));
            continue;
        }

        result.error = "Unsupported state statement";
        return result;
    }
    if (open.size() > 1) {
        result.error = "Unclosed composite state";
        return result;
    }
    if (graph.diagram.nodes.empty()) {
        result.error = "No states";
        return result;
    }
    for (size_t state = 0; state < home.size(); state++) {
        regions[home[state]].states.push_back(state);
    }

    // A transition belongs to the innermost region holding both its ends;
    // an end inside a composite state of that region is drawn to the
    // composite. Concurrent regions have nothing in common to draw in.
    auto parentOf = [&](size_t region) {
        size_t owner = regions[region].owner;
        return owner == SIZE_MAX ? SIZE_MAX : home[owner];
    };
    std::vector<std::vector<Transition>> regionEdges(regions.size());
    for (const Transition& transition : transitions) {
        std::vector<size_t> around;
        for (size_t r = home[transition.from]; r != SIZE_MAX; r = parentOf(r)) {
            around.push_back(r);
        }
        size_t shared = SIZE_MAX;
        for (size_t r = home[transition.to]; r != SIZE_MAX && shared == SIZE_MAX;
             r = parentOf(r)) {
            if (std::find(around.begin(), around.end(), r) != around.end()) {
                shared = r;
            }
        }
        auto lift = [&](size_t state) {
            while (home[state] != shared) state = regions[home[state]].owner;
            return state;
        };
        size_t from = shared == SIZE_MAX ? SIZE_MAX : lift(transition.from);
        size_t to = shared == SIZE_MAX ? SIZE_MAX : lift(transition.to);
        if (shared == SIZE_MAX ||
            (from == to && transition.from != from && transition.to != to)) {
            result.error = "Transitions between concurrent regions are not "
                           "supported";
            return result;
        }
        regionEdges[shared].push_back({from, to, transition.label});
    }

    TextStyle nameStyle;
    nameStyle.bold = true;
    TextStyle descStyle;
    descStyle.scale = 0.9f;
    TextStyle noteStyle;
    noteStyle.scale = 0.9f;
    bool failed = false;
    auto joinedDescriptions = [&](size_t state) {
        std::string joined;
        for (const auto& description : descriptions[state]) {
            if (!joined.empty()) joined += '\n';
            joined += description;
        }
        return joined;
    };

    // Inner diagrams first: a composite is sized from its regions, laid
    // out as one node of the region around it, and its regions' drawings
    // are moved into its box
    std::function<Drawing(size_t)> drawRegion;
    auto arrange = [&](const std::vector<size_t>& parts) {
        Arranged arranged;
        arranged.sideBySide = isVertical(regions[parts.front()].direction);
        std::vector<Drawing> drawings;
        float across = 0.0f;
        for (size_t part : parts) {
            drawings.push_back(drawRegion(part));
            const Size& size = drawings.back().size;
            across = std::max(across, arranged.sideBySide ? size.h : size.w);
        }
        float gap = kRegionGap * scale;
        float along = 0.0f;
        for (size_t i = 0; i < drawings.size(); i++) {
            Drawing& drawing = drawings[i];
            if (i > 0) {
                arranged.dividers.push_back(along + gap * 0.5f);
                along += gap;
            }
            if (arranged.sideBySide) {
                shiftPrims(drawing.prims, along,
                           (across - drawing.size.h) * 0.5f);
                along += drawing.size.w;
            } else {
                shiftPrims(drawing.prims, (across - drawing.size.w) * 0.5f,
                           along);
                along += drawing.size.h;
            }
            auto& prims = arranged.drawing.prims;
            prims.insert(prims.end(), std::make_move_iterator(drawing.prims.begin()),
                         std::make_move_iterator(drawing.prims.end()));
        }
        arranged.drawing.size = arranged.sideBySide ? Size{along, across}
                                                    : Size{across, along};
        return arranged;
    };
    auto drawComposite = [&](size_t state) {
        Composite composite;
        composite.body = arrange(inner[state]);
        composite.name = measure(graph.diagram.nodes[state].label, nameStyle, 0.0f);
        float width = composite.name.w;
        composite.header = composite.name.h + 10.0f * scale;
        if (!descriptions[state].empty()) {
            composite.about = measure(joinedDescriptions(state), descStyle, 0.0f);
            width = std::max(width, composite.about.w);
            composite.header += composite.about.h + 2.0f * scale;
        }
        float pad = kCompositePad * scale;
        const Size& body = composite.body.drawing.size;
        composite.size = {
            std::max({56.0f * scale, width + 24.0f * scale, body.w + 2.0f * pad}),
            composite.header + body.h + 2.0f * pad};
        return composite;
    };
    // The divider lines of an arrangement placed at (left, top), across
    // [from, to] of the other axis
    auto drawDividers = [&](std::vector<Prim>& prims, const Arranged& arranged,
                            float left, float top, float from, float to) {
        for (float at : arranged.dividers) {
            Prim divider;
            divider.type = PrimType::Line;
            if (arranged.sideBySide) {
                divider.x1 = divider.x2 = left + at;
                divider.y1 = from;
                divider.y2 = to;
            } else {
                divider.y1 = divider.y2 = top + at;
                divider.x1 = from;
                divider.x2 = to;
            }
            divider.stroke = Role::Muted;
            divider.strokeWidth = 1.0f * scale;
            divider.dashed = true;
            prims.push_back(divider);
        }
    };
    auto drawContainer = [&](std::vector<Prim>& prims, const mermaid::Rect& rect,
                             size_t state, Composite& composite) {
        Prim box;
        box.type = PrimType::RoundRect;
        box.radius = 9.0f * scale;
        box.x1 = rect.left;
        box.y1 = rect.top;
        box.x2 = rect.right;
        box.y2 = rect.bottom;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.5f * scale;
        prims.push_back(box);
        float headerBottom = rect.top + composite.header;
        Prim name;
        name.type = PrimType::Text;
        name.text = graph.diagram.nodes[state].label;
        name.style = nameStyle;
        name.fill = Role::Text;
        name.x1 = rect.left;
        name.y1 = rect.top + 5.0f * scale;
        name.x2 = rect.right;
        name.y2 = name.y1 + composite.name.h;
        prims.push_back(std::move(name));
        if (!descriptions[state].empty()) {
            Prim about;
            about.type = PrimType::Text;
            about.text = joinedDescriptions(state);
            about.style = descStyle;
            about.fill = Role::Text;
            about.x1 = rect.left;
            about.y1 = rect.top + 5.0f * scale + composite.name.h + 2.0f * scale;
            about.x2 = rect.right;
            about.y2 = about.y1 + composite.about.h;
            prims.push_back(std::move(about));
        }
        Prim separator;
        separator.type = PrimType::Line;
        separator.x1 = rect.left;
        separator.y1 = headerBottom;
        separator.x2 = rect.right;
        separator.y2 = headerBottom;
        separator.stroke = Role::Stroke;
        separator.strokeWidth = 1.0f * scale;
        prims.push_back(separator);
        Drawing& body = composite.body.drawing;
        float left = (rect.left + rect.right - body.size.w) * 0.5f;
        float top = headerBottom + kCompositePad * scale;
        drawDividers(prims, composite.body, left, top,
                     composite.body.sideBySide ? headerBottom : rect.left,
                     composite.body.sideBySide ? rect.bottom : rect.right);
        shiftPrims(body.prims, left, top);
        prims.insert(prims.end(), std::make_move_iterator(body.prims.begin()),
                     std::make_move_iterator(body.prims.end()));
    };

    drawRegion = [&](size_t r) {
        Drawing out;
        const StateRegion& region = regions[r];
        if (region.states.empty() || failed) return out;
        mermaid::Diagram diagram;
        diagram.direction = region.direction;
        std::vector<size_t> local(graph.diagram.nodes.size(), SIZE_MAX);
        for (size_t state : region.states) {
            local[state] = diagram.nodes.size();
            diagram.nodes.push_back(graph.diagram.nodes[state]);
        }
        std::vector<EdgeDecor> decors;
        for (const Transition& transition : regionEdges[r]) {
            mermaid::Edge edge;
            edge.from = local[transition.from];
            edge.to = local[transition.to];
            edge.label = transition.label;
            diagram.edges.push_back(std::move(edge));
            EdgeDecor decor;
            decor.atTo = Marker::FilledArrow;
            decors.push_back(std::move(decor));
        }

        // --- measure nodes ---
        bool vertical = isVertical(region.direction);
        size_t count = region.states.size();
        std::vector<mermaid::Size> sizes(count);
        std::vector<Size> nameSizes(count);
        std::vector<Composite> composites(count);
        for (size_t k = 0; k < count; k++) {
            size_t state = region.states[k];
            if (!inner[state].empty()) {
                composites[k] = drawComposite(state);
                sizes[k] = {composites[k].size.w, composites[k].size.h};
                continue;
            }
            switch (kinds[state]) {
                case StateKind::Start:
                case StateKind::End:
                    sizes[k] = {16.0f * scale, 16.0f * scale};
                    break;
                case StateKind::Choice:
                    sizes[k] = {36.0f * scale, 36.0f * scale};
                    break;
                case StateKind::Fork:
                    sizes[k] = vertical
                                   ? mermaid::Size{72.0f * scale, 9.0f * scale}
                                   : mermaid::Size{9.0f * scale, 72.0f * scale};
                    break;
                case StateKind::Normal: {
                    Size nameSize =
                        measure(graph.diagram.nodes[state].label, nameStyle, 0.0f);
                    nameSizes[k] = nameSize;
                    float width = nameSize.w;
                    float height = nameSize.h;
                    if (!descriptions[state].empty()) {
                        for (const auto& description : descriptions[state]) {
                            Size size = measure(description, descStyle, 0.0f);
                            width = std::max(width, size.w);
                            height += size.h;
                        }
                        height += 6.0f * scale;  // separator breathing room
                    }
                    sizes[k] = {std::max(56.0f * scale, width + 24.0f * scale),
                                height + 16.0f * scale};
                    break;
                }
            }
        }

        // Notes sit beside their states, in room the layout keeps for them
        std::vector<const StateNote*> regionNotes;
        std::vector<Attachment> attachments;
        for (const auto& note : notes) {
            if (home[note.node] != r) continue;
            Size size = measure(note.text, noteStyle, 220.0f * scale);
            regionNotes.push_back(&note);
            attachments.push_back({local[note.node], note.rightOf,
                                   {size.w + 16.0f * scale,
                                    size.h + 10.0f * scale}});
        }
        std::vector<Size> chips = measureChips(diagram, decors, measure, scale);
        Scene scene = layoutScene(diagram, sizes, attachments, chips,
                                  kNodeGap * scale, kRankGap * scale, scale, true);
        if (scene.boxes.size() != count) {
            failed = true;
            return out;
        }
        emitEdges(out.prims, diagram, scene, decors, chips, measure, scale);

        // --- nodes ---
        for (size_t k = 0; k < count; k++) {
            size_t state = region.states[k];
            const auto& rect = scene.boxes[k];
            if (!inner[state].empty()) {
                drawContainer(out.prims, rect, state, composites[k]);
                continue;
            }
            switch (kinds[state]) {
                case StateKind::Start: {
                    Prim dot;
                    dot.type = PrimType::Ellipse;
                    dot.x1 = rect.left;
                    dot.y1 = rect.top;
                    dot.x2 = rect.right;
                    dot.y2 = rect.bottom;
                    dot.fill = Role::Accent;
                    out.prims.push_back(dot);
                    break;
                }
                case StateKind::End: {
                    Prim ring;
                    ring.type = PrimType::Ellipse;
                    ring.x1 = rect.left;
                    ring.y1 = rect.top;
                    ring.x2 = rect.right;
                    ring.y2 = rect.bottom;
                    ring.stroke = Role::Accent;
                    ring.strokeWidth = 1.6f * scale;
                    out.prims.push_back(ring);
                    Prim dot;
                    dot.type = PrimType::Ellipse;
                    float inset = 4.0f * scale;
                    dot.x1 = rect.left + inset;
                    dot.y1 = rect.top + inset;
                    dot.x2 = rect.right - inset;
                    dot.y2 = rect.bottom - inset;
                    dot.fill = Role::Accent;
                    out.prims.push_back(dot);
                    break;
                }
                case StateKind::Choice: {
                    Prim diamond;
                    diamond.type = PrimType::Polygon;
                    float cx = (rect.left + rect.right) * 0.5f;
                    float cy = (rect.top + rect.bottom) * 0.5f;
                    diamond.pts = {{cx, rect.top},
                                   {rect.right, cy},
                                   {cx, rect.bottom},
                                   {rect.left, cy}};
                    diamond.fill = Role::Fill;
                    diamond.stroke = Role::Stroke;
                    diamond.strokeWidth = 1.5f * scale;
                    out.prims.push_back(std::move(diamond));
                    break;
                }
                case StateKind::Fork: {
                    Prim bar;
                    bar.type = PrimType::RoundRect;
                    bar.radius = 3.0f * scale;
                    bar.x1 = rect.left;
                    bar.y1 = rect.top;
                    bar.x2 = rect.right;
                    bar.y2 = rect.bottom;
                    bar.fill = Role::Accent;
                    out.prims.push_back(bar);
                    break;
                }
                case StateKind::Normal: {
                    Prim box;
                    box.type = PrimType::RoundRect;
                    box.radius = 7.0f * scale;
                    box.x1 = rect.left;
                    box.y1 = rect.top;
                    box.x2 = rect.right;
                    box.y2 = rect.bottom;
                    box.fill = Role::Fill;
                    box.stroke = Role::Stroke;
                    box.strokeWidth = 1.5f * scale;
                    out.prims.push_back(box);
                    if (descriptions[state].empty()) {
                        Prim name;
                        name.type = PrimType::Text;
                        name.text = graph.diagram.nodes[state].label;
                        name.style = nameStyle;
                        name.fill = Role::Text;
                        name.x1 = rect.left;
                        name.y1 = rect.top;
                        name.x2 = rect.right;
                        name.y2 = rect.bottom;
                        out.prims.push_back(std::move(name));
                    } else {
                        float nameHeight = nameSizes[k].h + 8.0f * scale;
                        Prim name;
                        name.type = PrimType::Text;
                        name.text = graph.diagram.nodes[state].label;
                        name.style = nameStyle;
                        name.fill = Role::Text;
                        name.x1 = rect.left;
                        name.y1 = rect.top;
                        name.x2 = rect.right;
                        name.y2 = rect.top + nameHeight;
                        out.prims.push_back(std::move(name));
                        Prim separator;
                        separator.type = PrimType::Line;
                        separator.x1 = rect.left;
                        separator.y1 = rect.top + nameHeight;
                        separator.x2 = rect.right;
                        separator.y2 = rect.top + nameHeight;
                        separator.stroke = Role::Stroke;
                        separator.strokeWidth = 1.0f * scale;
                        out.prims.push_back(separator);
                        Prim body;
                        body.type = PrimType::Text;
                        body.text = joinedDescriptions(state);
                        body.style = descStyle;
                        body.fill = Role::Text;
                        body.alignH = -1;
                        body.x1 = rect.left + 10.0f * scale;
                        body.y1 = rect.top + nameHeight + 3.0f * scale;
                        body.x2 = rect.right - 10.0f * scale;
                        body.y2 = rect.bottom - 3.0f * scale;
                        out.prims.push_back(std::move(body));
                    }
                    break;
                }
            }
        }

        // Notes in the room kept beside their nodes, on a dashed link
        for (size_t n = 0; n < regionNotes.size(); n++) {
            const auto& rect = scene.boxes[local[regionNotes[n]->node]];
            const auto& note = scene.attachments[n];
            Prim box;
            box.type = PrimType::Rect;
            box.x1 = note.left;
            box.y1 = note.top;
            box.x2 = note.right;
            box.y2 = note.bottom;
            box.fill = Role::AccentSoft;
            box.stroke = Role::Stroke;
            box.strokeWidth = 1.0f * scale;
            out.prims.push_back(box);
            Prim text;
            text.type = PrimType::Text;
            text.text = regionNotes[n]->text;
            text.style = noteStyle;
            text.fill = Role::Text;
            text.x1 = note.left + 8.0f * scale;
            text.y1 = note.top + 5.0f * scale;
            text.x2 = note.right - 8.0f * scale;
            text.y2 = note.bottom - 5.0f * scale;
            out.prims.push_back(std::move(text));
            Prim link;
            link.type = PrimType::Line;
            float nodeX = (rect.left + rect.right) * 0.5f;
            float nodeY = (rect.top + rect.bottom) * 0.5f;
            float noteX = (note.left + note.right) * 0.5f;
            float noteY = (note.top + note.bottom) * 0.5f;
            if (note.left >= rect.right || note.right <= rect.left) {
                bool right = note.left >= rect.right;
                link.x1 = right ? rect.right : rect.left;
                link.y1 = nodeY;
                link.x2 = right ? note.left : note.right;
                link.y2 = noteY;
            } else {
                bool below = note.top >= rect.bottom;
                link.x1 = nodeX;
                link.y1 = below ? rect.bottom : rect.top;
                link.x2 = noteX;
                link.y2 = below ? note.top : note.bottom;
            }
            link.stroke = Role::Muted;
            link.strokeWidth = 1.0f * scale;
            link.dashed = true;
            out.prims.push_back(link);
        }

        mermaid::Rect bounds = primBounds(out.prims);
        shiftPrims(out.prims, -bounds.left, -bounds.top);
        out.size = {bounds.right - bounds.left, bounds.bottom - bounds.top};
        return out;
    };

    Arranged top = arrange(topRegions);
    if (failed) {
        result.error = "Layout failed";
        return result;
    }
    result.prims = std::move(top.drawing.prims);
    drawDividers(result.prims, top, 0.0f, 0.0f, 0.0f,
                 top.sideBySide ? top.drawing.size.h : top.drawing.size.w);
    fitPrims(result, 8.0f * scale);
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// Class diagrams
// --------------------------------------------------------------------------

namespace {

struct ClassMembers {
    std::string stereotype;
    std::vector<std::string> attributes;
    std::vector<std::string> methods;
};

// ~T~ generics display as <T>
std::string displayGenerics(std::string value) {
    bool open = true;
    for (char& c : value) {
        if (c == '~') {
            c = open ? '<' : '>';
            open = !open;
        }
    }
    return value;
}

void addClassMember(ClassMembers& members, std::string_view raw) {
    std::string_view trimmed = trimView(raw);
    if (trimmed.empty()) return;
    if (trimmed.size() >= 4 && trimmed.substr(0, 2) == "<<") {
        size_t close = trimmed.find(">>");
        if (close != std::string_view::npos) {
            members.stereotype =
                "<<" + std::string(trimView(trimmed.substr(2, close - 2))) +
                ">>";
            return;
        }
    }
    std::string display = displayGenerics(cleanLabel(trimmed));
    if (display.find('(') != std::string::npos) {
        members.methods.push_back(std::move(display));
    } else {
        members.attributes.push_back(std::move(display));
    }
}

struct RelationToken {
    Marker marker = Marker::None;
    size_t length = 0;
};

RelationToken leftRelationMarker(std::string_view text) {
    if (text.size() >= 2 && text.substr(text.size() - 2) == "<|") {
        return {Marker::HollowTriangle, 2};
    }
    if (!text.empty()) {
        char last = text.back();
        if (last == '*') return {Marker::FilledDiamond, 1};
        if (last == 'o') return {Marker::HollowDiamond, 1};
        if (last == '<') return {Marker::OpenArrow, 1};
    }
    return {};
}

RelationToken rightRelationMarker(std::string_view text) {
    if (text.size() >= 2 && text.substr(0, 2) == "|>") {
        return {Marker::HollowTriangle, 2};
    }
    if (!text.empty()) {
        char first = text.front();
        if (first == '*') return {Marker::FilledDiamond, 1};
        if (first == 'o') return {Marker::HollowDiamond, 1};
        if (first == '>') return {Marker::OpenArrow, 1};
    }
    return {};
}

// Strips one optional trailing quoted string ("1", "many") off the end
std::string takeTrailingQuoted(std::string_view& text) {
    text = trimView(text);
    if (text.empty() || text.back() != '"') return {};
    size_t open = text.rfind('"', text.size() - 2);
    if (open == std::string_view::npos) return {};
    std::string value(text.substr(open + 1, text.size() - open - 2));
    text = trimView(text.substr(0, open));
    return value;
}

std::string takeLeadingQuoted(std::string_view& text) {
    text = trimView(text);
    if (text.empty() || text.front() != '"') return {};
    size_t close = text.find('"', 1);
    if (close == std::string_view::npos) return {};
    std::string value(text.substr(1, close - 1));
    text = trimView(text.substr(close + 1));
    return value;
}

}  // namespace

Built buildClass(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    GraphNodes graph;
    graph.diagram.direction = mermaid::Direction::TopToBottom;
    std::map<size_t, ClassMembers> members;
    std::vector<EdgeDecor> decors;
    struct ClassNote {
        std::string text;
        size_t target = SIZE_MAX;  // SIZE_MAX = floating
    };
    std::vector<ClassNote> notes;
    size_t openBlock = SIZE_MAX;  // class whose { } block is open

    auto stripCss = [](std::string_view id) {
        size_t css = id.find(":::");
        return css == std::string_view::npos ? id : trimView(id.substr(0, css));
    };
    auto ensureClass = [&](std::string_view id) {
        std::string display = displayGenerics(std::string(stripCss(id)));
        return graph.ensure(display, display);
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "classDiagram") &&
                !startsWithWord(line, "classDiagram-v2")) {
                result.error = "Expected classDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        if (openBlock != SIZE_MAX) {
            if (line == "}") {
                openBlock = SIZE_MAX;
            } else {
                addClassMember(members[openBlock], line);
            }
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "direction", &rest)) {
            if (rest == "LR") {
                graph.diagram.direction = mermaid::Direction::LeftToRight;
            } else if (rest == "RL") {
                graph.diagram.direction = mermaid::Direction::RightToLeft;
            } else if (rest == "BT") {
                graph.diagram.direction = mermaid::Direction::BottomToTop;
            } else {
                graph.diagram.direction = mermaid::Direction::TopToBottom;
            }
            continue;
        }
        if (startsWithWord(line, "click", &rest) ||
            startsWithWord(line, "callback", &rest) ||
            startsWithWord(line, "link", &rest) ||
            startsWithWord(line, "style", &rest) ||
            startsWithWord(line, "classDef", &rest) ||
            startsWithWord(line, "cssClass", &rest)) {
            continue;  // interactivity/styling metadata, nothing to draw
        }
        if (startsWithWord(line, "note", &rest)) {
            // note "text" | note for ClassName "text"
            ClassNote note;
            std::string_view spec = rest;
            std::string_view target;
            if (startsWithWord(spec, "for", &target)) {
                size_t quote = target.find('"');
                if (quote == std::string_view::npos) {
                    result.error = "Bad class note";
                    return result;
                }
                note.target = ensureClass(trimView(target.substr(0, quote)));
                spec = target.substr(quote);
            }
            std::string text = takeLeadingQuoted(spec);
            if (text.empty()) {
                result.error = "Bad class note";
                return result;
            }
            note.text = cleanLabel(text);
            notes.push_back(std::move(note));
            continue;
        }
        if (startsWithWord(line, "namespace", &rest)) {
            result.error = "Namespaces are not supported";
            return result;
        }
        if (line.substr(0, 2) == "<<") {
            size_t close = line.find(">>");
            if (close != std::string_view::npos) {
                std::string_view name = trimView(line.substr(close + 2));
                if (!name.empty()) {
                    size_t node = ensureClass(name);
                    members[node].stereotype =
                        "<<" +
                        std::string(trimView(line.substr(2, close - 2))) +
                        ">>";
                    continue;
                }
            }
            result.error = "Unsupported annotation";
            return result;
        }
        if (startsWithWord(line, "class", &rest)) {
            std::string_view spec = rest;
            bool opensBlock = false;
            if (!spec.empty() && spec.back() == '{') {
                opensBlock = true;
                spec = trimView(spec.substr(0, spec.size() - 1));
            }
            size_t node = ensureClass(spec);
            if (opensBlock) openBlock = node;
            continue;
        }

        // Relation: A [<|*o<] -- / .. [|>*o>] B, quoted multiplicities,
        // optional : label
        {
            size_t linePos = std::string_view::npos;
            bool dashed = false;
            for (size_t i = 0; i + 1 < line.size(); i++) {
                if (line[i] == '-' && line[i + 1] == '-') {
                    linePos = i;
                    dashed = false;
                    break;
                }
                if (line[i] == '.' && line[i + 1] == '.') {
                    linePos = i;
                    dashed = true;
                    break;
                }
            }
            if (linePos != std::string_view::npos) {
                std::string_view leftPart = line.substr(0, linePos);
                std::string_view rightPart = line.substr(linePos + 2);
                RelationToken leftMarker = leftRelationMarker(leftPart);
                leftPart = leftPart.substr(0, leftPart.size() -
                                                  leftMarker.length);
                RelationToken rightMarker = rightRelationMarker(rightPart);
                rightPart = rightPart.substr(rightMarker.length);

                std::string label;
                size_t colon = rightPart.find(':');
                if (colon != std::string_view::npos) {
                    label = cleanLabel(trimView(rightPart.substr(colon + 1)));
                    rightPart = rightPart.substr(0, colon);
                }
                std::string leftMult = takeTrailingQuoted(leftPart);
                std::string rightMult = takeLeadingQuoted(rightPart);
                std::string_view leftId = trimView(leftPart);
                std::string_view rightId = trimView(rightPart);
                if (leftId.empty() || rightId.empty()) {
                    result.error = "Relation needs two classes";
                    return result;
                }
                mermaid::Edge edge;
                edge.from = ensureClass(leftId);
                edge.to = ensureClass(rightId);
                edge.label = label;
                graph.diagram.edges.push_back(std::move(edge));
                EdgeDecor decor;
                decor.atFrom = leftMarker.marker;
                decor.atTo = rightMarker.marker;
                decor.dashed = dashed;
                decor.labelFrom = leftMult;
                decor.labelTo = rightMult;
                decors.push_back(std::move(decor));
                continue;
            }
        }

        // Member via colon: Name : +member
        size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            size_t node = ensureClass(trimView(line.substr(0, colon)));
            addClassMember(members[node], line.substr(colon + 1));
            continue;
        }

        result.error = "Unsupported class statement";
        return result;
    }

    if (graph.diagram.nodes.empty()) {
        result.error = "No classes";
        return result;
    }

    // --- measure ---
    TextStyle nameStyle;
    nameStyle.bold = true;
    TextStyle stereoStyle;
    stereoStyle.italic = true;
    stereoStyle.scale = 0.85f;
    TextStyle memberStyle;
    memberStyle.scale = 0.9f;
    float padX = 12.0f;
    float memberLineGap = 3.0f;

    struct MeasuredClass {
        Size name;
        Size stereotype;
        float attrHeight = 0.0f;
        float methodHeight = 0.0f;
    };
    std::vector<MeasuredClass> measured(graph.diagram.nodes.size());
    std::vector<mermaid::Size> sizes(graph.diagram.nodes.size());
    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        const ClassMembers* classMembers = nullptr;
        auto found = members.find(i);
        if (found != members.end()) classMembers = &found->second;

        MeasuredClass& node = measured[i];
        node.name = measure(graph.diagram.nodes[i].label, nameStyle, 0.0f);
        float width = node.name.w;
        float height = node.name.h + 12.0f * scale;
        if (classMembers && !classMembers->stereotype.empty()) {
            node.stereotype =
                measure(classMembers->stereotype, stereoStyle, 0.0f);
            width = std::max(width, node.stereotype.w);
            height += node.stereotype.h;
        }
        if (classMembers) {
            for (const auto& attribute : classMembers->attributes) {
                Size size = measure(attribute, memberStyle, 0.0f);
                width = std::max(width, size.w);
                node.attrHeight += size.h + memberLineGap * scale;
            }
            for (const auto& method : classMembers->methods) {
                Size size = measure(method, memberStyle, 0.0f);
                width = std::max(width, size.w);
                node.methodHeight += size.h + memberLineGap * scale;
            }
            if (!classMembers->attributes.empty() ||
                !classMembers->methods.empty()) {
                // both compartments render (possibly thin) once any exist
                height += node.attrHeight + node.methodHeight +
                          16.0f * scale;
            }
        }
        sizes[i] = {std::max(74.0f * scale, width + padX * 2.0f * scale),
                    height};
    }

    std::vector<Size> chips =
        measureChips(graph.diagram, decors, measure, scale);
    Scene scene = layoutScene(graph.diagram, sizes, {}, chips,
                              kNodeGap * scale, (kRankGap + 12.0f) * scale,
                              scale, false);
    if (scene.boxes.size() != graph.diagram.nodes.size()) {
        result.error = "Layout failed";
        return result;
    }
    emitEdges(result.prims, graph.diagram, scene, decors, chips, measure,
              scale);

    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        const auto& rect = scene.boxes[i];
        const ClassMembers* classMembers = nullptr;
        auto found = members.find(i);
        if (found != members.end()) classMembers = &found->second;
        const MeasuredClass& node = measured[i];

        Prim box;
        box.type = PrimType::RoundRect;
        box.radius = 4.0f * scale;
        box.x1 = rect.left;
        box.y1 = rect.top;
        box.x2 = rect.right;
        box.y2 = rect.bottom;
        box.fill = Role::Fill;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.5f * scale;
        result.prims.push_back(box);

        float y = rect.top + 6.0f * scale;
        if (classMembers && !classMembers->stereotype.empty()) {
            Prim stereo;
            stereo.type = PrimType::Text;
            stereo.text = classMembers->stereotype;
            stereo.style = stereoStyle;
            stereo.fill = Role::Muted;
            stereo.x1 = rect.left;
            stereo.y1 = y;
            stereo.x2 = rect.right;
            stereo.y2 = y + node.stereotype.h;
            result.prims.push_back(std::move(stereo));
            y += node.stereotype.h;
        }
        Prim name;
        name.type = PrimType::Text;
        name.text = graph.diagram.nodes[i].label;
        name.style = nameStyle;
        name.fill = Role::Text;
        name.x1 = rect.left;
        name.y1 = y;
        name.x2 = rect.right;
        name.y2 = y + node.name.h;
        result.prims.push_back(std::move(name));
        y += node.name.h + 6.0f * scale;

        bool hasMembers =
            classMembers && (!classMembers->attributes.empty() ||
                             !classMembers->methods.empty());
        if (hasMembers) {
            auto compartment = [&](const std::vector<std::string>& entries,
                                   float bodyHeight) {
                Prim separator;
                separator.type = PrimType::Line;
                separator.x1 = rect.left;
                separator.y1 = y;
                separator.x2 = rect.right;
                separator.y2 = y;
                separator.stroke = Role::Stroke;
                separator.strokeWidth = 1.0f * scale;
                result.prims.push_back(separator);
                y += 4.0f * scale;
                if (!entries.empty()) {
                    std::string joined;
                    for (const auto& entry : entries) {
                        if (!joined.empty()) joined += '\n';
                        joined += entry;
                    }
                    Prim body;
                    body.type = PrimType::Text;
                    body.text = joined;
                    body.style = memberStyle;
                    body.fill = Role::Text;
                    body.alignH = -1;
                    body.alignV = -1;
                    body.x1 = rect.left + padX * scale;
                    body.y1 = y;
                    body.x2 = rect.right - padX * scale;
                    body.y2 = y + bodyHeight;
                    result.prims.push_back(std::move(body));
                }
                y += bodyHeight + 4.0f * scale;
            };
            compartment(classMembers->attributes, node.attrHeight);
            compartment(classMembers->methods, node.methodHeight);
        }
    }

    // Notes: targeted ones hang under their class on a dashed link,
    // floating ones stack under the diagram
    if (!notes.empty()) {
        TextStyle noteStyle;
        noteStyle.scale = 0.85f;
        float notePad = 8.0f * scale;
        float noteWrap = 240.0f * scale;
        mermaid::Rect drawn = primBounds(result.prims);
        float floatCursorY = drawn.bottom + 16.0f * scale;
        for (const auto& note : notes) {
            Size size = measure(note.text, noteStyle, noteWrap);
            float width = std::min(size.w, noteWrap) + notePad * 2.0f;
            float height = size.h + notePad * 2.0f;
            float x, top;
            if (note.target != SIZE_MAX &&
                note.target < scene.boxes.size()) {
                const auto& rect = scene.boxes[note.target];
                float centerX = (rect.left + rect.right) * 0.5f;
                x = centerX - width * 0.5f;
                top = rect.bottom + 18.0f * scale;
                Prim link;
                link.type = PrimType::Line;
                link.x1 = centerX;
                link.y1 = rect.bottom;
                link.x2 = centerX;
                link.y2 = top;
                link.stroke = Role::Muted;
                link.strokeWidth = 1.2f * scale;
                link.dashed = true;
                result.prims.push_back(std::move(link));
            } else {
                x = drawn.left;
                top = floatCursorY;
                floatCursorY += height + 10.0f * scale;
            }
            Prim box;
            box.type = PrimType::RoundRect;
            box.radius = 4.0f * scale;
            box.x1 = x;
            box.y1 = top;
            box.x2 = x + width;
            box.y2 = top + height;
            box.fill = Role::AccentSoft;
            box.stroke = Role::Stroke;
            box.strokeWidth = 1.0f * scale;
            result.prims.push_back(box);
            Prim text;
            text.type = PrimType::Text;
            text.text = note.text;
            text.style = noteStyle;
            text.fill = Role::Text;
            text.alignH = -1;
            text.alignV = -1;
            text.x1 = x + notePad;
            text.y1 = top + notePad;
            text.x2 = x + width - notePad;
            text.y2 = top + height - notePad;
            result.prims.push_back(std::move(text));
        }
    }

    fitPrims(result, 8.0f * scale);
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// Entity-relationship diagrams
// --------------------------------------------------------------------------

namespace {

struct ErAttribute {
    std::string type;
    std::string name;
    std::string keys;  // PK, FK, UK joined
};

// ||, |o, o|, }|, |{, }o, o{ ... -> marker (read outward from the line)
Marker cardinalityMarker(std::string_view card) {
    bool many = card.find('{') != std::string_view::npos ||
                card.find('}') != std::string_view::npos;
    bool zero = card.find('o') != std::string_view::npos;
    size_t bars = static_cast<size_t>(
        std::count(card.begin(), card.end(), '|'));
    if (many) {
        if (zero) return Marker::ZeroMany;
        if (bars > 0) return Marker::OneMany;
        return Marker::CrowMany;
    }
    if (zero) return Marker::ZeroOne;
    if (bars >= 2) return Marker::One;
    if (bars == 1) return Marker::One;
    return Marker::None;
}

bool isCardChar(char c) {
    return c == '|' || c == 'o' || c == '{' || c == '}';
}

}  // namespace

Built buildEr(std::string_view source, const Measure& measure, float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    GraphNodes graph;
    graph.diagram.direction = mermaid::Direction::TopToBottom;
    std::map<size_t, std::vector<ErAttribute>> attributes;
    std::vector<EdgeDecor> decors;
    size_t openBlock = SIZE_MAX;

    auto entityName = [&](std::string_view id) -> std::string_view {
        // Quoted entity names and [aliases] are trimmed to the plain name
        id = trimView(id);
        if (id.size() >= 2 && id.front() == '"' && id.back() == '"') {
            id = id.substr(1, id.size() - 2);
        }
        size_t bracket = id.find('[');
        if (bracket != std::string_view::npos) {
            id = trimView(id.substr(0, bracket));
        }
        return id;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "erDiagram")) {
                result.error = "Expected erDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        if (openBlock != SIZE_MAX) {
            if (line == "}") {
                openBlock = SIZE_MAX;
                continue;
            }
            // type name [PK|FK|UK ...] ["comment"]
            ErAttribute attribute;
            std::string cleaned = cleanLabel(line);
            std::string_view rest(cleaned);
            // strip trailing comment
            size_t quote = rest.find('"');
            if (quote != std::string_view::npos) {
                rest = trimView(rest.substr(0, quote));
            }
            size_t space = rest.find(' ');
            if (space == std::string_view::npos) {
                attribute.name = std::string(rest);
            } else {
                attribute.type = std::string(rest.substr(0, space));
                std::string_view remainder = trimView(rest.substr(space + 1));
                size_t nameEnd = remainder.find(' ');
                if (nameEnd == std::string_view::npos) {
                    attribute.name = std::string(remainder);
                } else {
                    attribute.name = std::string(remainder.substr(0, nameEnd));
                    attribute.keys =
                        std::string(trimView(remainder.substr(nameEnd + 1)));
                }
            }
            attributes[openBlock].push_back(std::move(attribute));
            continue;
        }
        if (startsWithWord(line, "direction", nullptr)) continue;

        // ENTITY { ... }  |  ENTITY  |  A <card><line><card> B : label
        if (line.back() == '{') {
            std::string_view name =
                entityName(trimView(line.substr(0, line.size() - 1)));
            if (name.empty()) {
                result.error = "Entity block without a name";
                return result;
            }
            openBlock = graph.ensure(name, name);
            continue;
        }

        // Find the relationship line: -- or .. with cardinality chars around
        size_t linePos = std::string_view::npos;
        bool dashed = false;
        for (size_t i = 0; i + 1 < line.size(); i++) {
            if ((line[i] == '-' && line[i + 1] == '-') ||
                (line[i] == '.' && line[i + 1] == '.')) {
                if (i > 0 && isCardChar(line[i - 1])) {
                    linePos = i;
                    dashed = line[i] == '.';
                    break;
                }
            }
        }
        if (linePos != std::string_view::npos) {
            size_t lineEnd = linePos;
            while (lineEnd < line.size() &&
                   (line[lineEnd] == '-' || line[lineEnd] == '.')) {
                lineEnd++;
            }
            size_t leftCardStart = linePos;
            while (leftCardStart > 0 && isCardChar(line[leftCardStart - 1])) {
                leftCardStart--;
            }
            size_t rightCardEnd = lineEnd;
            while (rightCardEnd < line.size() &&
                   isCardChar(line[rightCardEnd])) {
                rightCardEnd++;
            }
            std::string_view leftCard =
                line.substr(leftCardStart, linePos - leftCardStart);
            std::string_view rightCard =
                line.substr(lineEnd, rightCardEnd - lineEnd);
            std::string_view leftId =
                entityName(line.substr(0, leftCardStart));
            std::string_view remainder = line.substr(rightCardEnd);
            std::string label;
            size_t colon = remainder.find(':');
            std::string_view rightId;
            if (colon != std::string_view::npos) {
                rightId = entityName(remainder.substr(0, colon));
                label = cleanLabel(trimView(remainder.substr(colon + 1)));
                if (label.size() >= 2 && label.front() == '"' &&
                    label.back() == '"') {
                    label = label.substr(1, label.size() - 2);
                }
            } else {
                rightId = entityName(remainder);
            }
            if (leftId.empty() || rightId.empty()) {
                result.error = "Relationship needs two entities";
                return result;
            }
            mermaid::Edge edge;
            edge.from = graph.ensure(leftId, leftId);
            edge.to = graph.ensure(rightId, rightId);
            edge.label = label;
            graph.diagram.edges.push_back(std::move(edge));
            EdgeDecor decor;
            // Cardinality glyphs read outward: the marker for the LEFT
            // entity is the left card reversed toward that end
            decor.atFrom = cardinalityMarker(leftCard);
            decor.atTo = cardinalityMarker(rightCard);
            decor.dashed = dashed;
            decors.push_back(std::move(decor));
            continue;
        }

        // Bare entity declaration
        std::string_view name = entityName(line);
        if (!name.empty() && name.find(' ') == std::string_view::npos) {
            graph.ensure(name, name);
            continue;
        }
        result.error = "Unsupported ER statement";
        return result;
    }

    if (graph.diagram.nodes.empty()) {
        result.error = "No entities";
        return result;
    }

    // --- measure ---
    TextStyle titleStyle;
    titleStyle.bold = true;
    TextStyle attrStyle;
    attrStyle.scale = 0.9f;
    TextStyle keyStyle;
    keyStyle.scale = 0.9f;
    keyStyle.italic = true;
    float padX = 10.0f;

    struct MeasuredEntity {
        Size title;
        float rowHeight = 0.0f;
        float typeWidth = 0.0f;
        float nameWidth = 0.0f;
        float keysWidth = 0.0f;
    };
    std::vector<MeasuredEntity> measured(graph.diagram.nodes.size());
    std::vector<mermaid::Size> sizes(graph.diagram.nodes.size());
    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        MeasuredEntity& entity = measured[i];
        entity.title = measure(graph.diagram.nodes[i].label, titleStyle, 0.0f);
        float width = entity.title.w;
        float height = entity.title.h + 12.0f * scale;
        auto found = attributes.find(i);
        if (found != attributes.end()) {
            for (const auto& attribute : found->second) {
                Size typeSize = measure(attribute.type, attrStyle, 0.0f);
                Size nameSize = measure(attribute.name, attrStyle, 0.0f);
                Size keysSize = measure(attribute.keys, keyStyle, 0.0f);
                entity.typeWidth = std::max(entity.typeWidth, typeSize.w);
                entity.nameWidth = std::max(entity.nameWidth, nameSize.w);
                entity.keysWidth = std::max(entity.keysWidth, keysSize.w);
                entity.rowHeight = std::max(
                    entity.rowHeight,
                    std::max(typeSize.h, std::max(nameSize.h, keysSize.h)) +
                        6.0f * scale);
            }
            float rowsWidth = entity.typeWidth + entity.nameWidth +
                              entity.keysWidth;
            if (entity.typeWidth > 0.0f) rowsWidth += padX * scale;
            if (entity.keysWidth > 0.0f) rowsWidth += padX * scale;
            width = std::max(width, rowsWidth);
            height += found->second.size() * entity.rowHeight;
        }
        sizes[i] = {std::max(90.0f * scale, width + padX * 2.0f * scale),
                    height};
    }

    std::vector<Size> chips =
        measureChips(graph.diagram, decors, measure, scale);
    Scene scene = layoutScene(graph.diagram, sizes, {}, chips,
                              (kNodeGap + 14.0f) * scale,
                              (kRankGap + 8.0f) * scale, scale, true);
    if (scene.boxes.size() != graph.diagram.nodes.size()) {
        result.error = "Layout failed";
        return result;
    }
    emitEdges(result.prims, graph.diagram, scene, decors, chips, measure,
              scale);

    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        const auto& rect = scene.boxes[i];
        const MeasuredEntity& entity = measured[i];

        Prim box;
        box.type = PrimType::Rect;
        box.x1 = rect.left;
        box.y1 = rect.top;
        box.x2 = rect.right;
        box.y2 = rect.bottom;
        box.fill = Role::Fill;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.5f * scale;
        result.prims.push_back(box);

        float titleHeight = entity.title.h + 12.0f * scale;
        Prim titleBand;
        titleBand.type = PrimType::Rect;
        titleBand.x1 = rect.left;
        titleBand.y1 = rect.top;
        titleBand.x2 = rect.right;
        titleBand.y2 = rect.top + titleHeight;
        titleBand.fill = Role::AccentSoft;
        titleBand.stroke = Role::Stroke;
        titleBand.strokeWidth = 1.0f * scale;
        result.prims.push_back(titleBand);
        Prim title;
        title.type = PrimType::Text;
        title.text = graph.diagram.nodes[i].label;
        title.style = titleStyle;
        title.fill = Role::Text;
        title.x1 = rect.left;
        title.y1 = rect.top;
        title.x2 = rect.right;
        title.y2 = rect.top + titleHeight;
        result.prims.push_back(std::move(title));

        auto found = attributes.find(i);
        if (found == attributes.end()) continue;
        float y = rect.top + titleHeight;
        float typeX = rect.left + padX * scale;
        float nameX = typeX + entity.typeWidth +
                      (entity.typeWidth > 0.0f ? padX * scale : 0.0f);
        for (size_t row = 0; row < found->second.size(); row++) {
            const auto& attribute = found->second[row];
            if (row > 0) {
                Prim separator;
                separator.type = PrimType::Line;
                separator.x1 = rect.left;
                separator.y1 = y;
                separator.x2 = rect.right;
                separator.y2 = y;
                separator.stroke = Role::Muted;
                separator.strokeWidth = 0.6f * scale;
                result.prims.push_back(separator);
            }
            auto cell = [&](const std::string& value, float x, float width,
                            const TextStyle& style, Role role) {
                if (value.empty() || width <= 0.0f) return;
                Prim text;
                text.type = PrimType::Text;
                text.text = value;
                text.style = style;
                text.fill = role;
                text.alignH = -1;
                text.x1 = x;
                text.y1 = y;
                text.x2 = x + width;
                text.y2 = y + entity.rowHeight;
                result.prims.push_back(std::move(text));
            };
            cell(attribute.type, typeX, entity.typeWidth, attrStyle,
                 Role::Muted);
            cell(attribute.name, nameX, entity.nameWidth, attrStyle,
                 Role::Text);
            cell(attribute.keys,
                 rect.right - padX * scale - entity.keysWidth,
                 entity.keysWidth, keyStyle, Role::Muted);
            y += entity.rowHeight;
        }
    }

    fitPrims(result, 8.0f * scale);
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
