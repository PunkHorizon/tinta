# Mermaid asymmetric shape

Validated on Windows with the Release build on 2026-10-26.

## Problem

Mermaid's asymmetric node, `id>text]`, was not recognised. The flowchart
parser read a node ID up to a space or a bracket, so in `R -.-> S>Screen]`
the ID ran on through the `>`, and the node was a plain rectangle whose ID
and label were both `S>Screen]`. With a space in the text
(`Route>Route request]`) the rest of the line failed as an edge and the
whole fence showed its source.

## Change

- Parser: `>` ends a node ID and opens the label of a new
  `NodeShape::Asymmetric`, closed by `]`. Quoted text (`>`, `]` or `;`
  inside), a quoted label over several lines, `:::class`, `style`,
  `classDef` and subgraphs work as for the other shapes. Unquoted text
  keeps `;` and `|`, as a bracketed label does: the statement splitter
  skips from a `>` that follows a node ID to its `]`, unless an arrow or the
  line's end comes first.
- A `>` straight after `-` or `=` heads an arrow the parser does not read
  (`--->`, `-..->`, `->`). It fails as an unsupported edge instead of
  opening a node: Mermaid's own `B ---->|No| E[End]` would otherwise have
  drawn a node named `-` labelled `|No| E[End`. It shows its source, as
  before.
- Outline: (0,0) (w,0) (w,h) (0,h) and the notch's tip at (h/2,h/2).
  `mermaid::leftNotch` holds the depth for the viewer and the exports. The
  node is widened by the notch and its label is centred in the rectangle
  beside it.
- Viewer: the outline is built once at layout time, in the shape's own
  space, as a path that the screen and print both draw. The HTML and Word
  exports emit the same five-point polygon.
- Edges: in LR and RL layouts an edge that meets the node's left side ends
  at the notch's tip, not at the mouth of the notch where nothing is drawn.
  The top, bottom and right sides are flat; edges meet them as before.
- Flowchart labels in the viewer get a pixel of layout slack, centred. In
  the fixture, "Show screen" was laid out again at exactly its measured
  width and wrapped its last word in 2 of the 8 theme, scale and width
  runs. The HTML export already pads its text the same way.

## Checked against Mermaid

The points are those of Mermaid's `rect_left_inv_arrow` up to version 10
(`dagre-wrapper/nodes.js`): (-h/2,0) (w,0) (w,-h) (-h/2,-h) (0,-h/2), with
the label centred on the rectangle right of the tip. Mermaid 11's renderer
(`rectLeftInvArrow.ts`, and what mermaid.ink draws today) uses the same
shape with a notch half as deep, h/4. Tinta follows the h/2 points; the
depth is the one factor in `mermaid::leftNotch`.

## Automated checks

- `mermaid_parser`, `testAsymmetricShape`: `S>Screen]` is node S, labelled
  Screen, asymmetric, at the end of a dotted edge from R. Quoted labels
  with `>`, `]` and `;`, a quoted label over two lines, an unquoted label
  with `;` and `|` followed by a real `;` separator, a class, a node
  redefined as the shape, and one inside a subgraph. Fourteen one-line
  cases where a `>` belongs to an arrow or text: `A-->B`, `A --> B`,
  `A ==> B`, `A -.-> B`, edges to and from the shape with and without
  spaces, `A -- text --> B`, `A -- a > b --> B`, `A -->|a > b| B`, and
  quoted labels with `>` in this shape and others. Each has exactly nodes A
  and B, the expected asymmetric ones, and one directed edge with the
  expected label; `==>` stays thick and `-.->` dotted. A `>` after a letter
  in edge text opens no label. `A --o B`, `A --x B`, `B ---->|No| E[End]`,
  `A--->B` and `A->B` show their source as unsupported edges.
- `mermaid_parser`, `testAsymmetricFixture`: the fixture's four fences
  parse, with exactly the asymmetric nodes they show.
- `code_block_layout`, `testMermaidAsymmetricShape`, on
  `tests/fixtures/mermaid-asymmetric-shape.md` in Paper and Midnight, at
  scale 1 and 1.5, at 1050 and 650 px: the four fences draw. Each of the
  ten asymmetric nodes is a path whose fill leaves the notch out and whose
  bounds are the node's; its label keeps its lines, stays right of the tip
  and is centred beside it; every edge end at the node lies on the outline
  (within half a pixel); six edges meet a tip. The exports' polygons are
  the five points exactly, with the label centred beside the notch and
  the same six edges at the tips.

Neutering, each with the tests kept:

- Parser reverted: 21 `mermaid_parser` failures (every `>` shape case, the
  longer-arrow and `->` cases, all four fixture fences) and 56 layout
  failures (7 checks in each of the 8 runs: three fences fall back to
  source, no outlines, no tips, no export polygons).
- Drawing reverted (`render.cpp` and `export_html.cpp`): the parser tests
  pass; 280 layout failures (35 checks × 8 runs): every node a plain
  rectangle, every label off-centre, six reaching into the notch, the edges
  of six nodes ending in the notch's mouth, no tips in the viewer or the
  export.
- Label slack alone reverted: "Show screen" keeps 1 line, laid out in 2
  (2 failures).

`code_block_layout` also fails its clipboard Copy checks on this machine,
with or without the change, because of Remote Desktop's clipboard sync.

## Visual check

Portable copies of the Release `tinta.exe` in `out/asymmetric-shape/`
(ignored), with `settings.ini` beside them:

- `--printpages`, Paper (theme 0) and Midnight (theme 5): three pages each.
  The notches, centred labels, styled nodes (`style` and `classDef`), the
  node in the subgraph, edges at the tips in LR and leaving Flag's tip in
  RL, all next to the heading, table, lists, quote and code block. Print
  draws a light page in both themes; only the fonts change.
- `--exporthtml`, both themes: four inline `<svg>`, no diagram left as
  `<pre>`. Twelve five-point polygons (ten nodes; the two styled ones have
  separate fill and stroke), each `(L,T) (R,T) (R,B) (L,B) (L+h/2,T+h/2)`;
  label anchors midway between tip and right edge; the LR edges end on the
  tips; `>` escaped as `&gt;`. Midnight's SVG uses its palette (page
  `#0d1b2a`, node `#1b263b`, stroke `#38c6e4`).
- `--exportdocx`, Paper: the four diagram images show the same shapes.
- The window itself, captured after posted Home and PageDown keys: Midnight
  at 1050 px and Paper at 650 px. Zoomed in, arrowheads end at the notch's
  tip with their wings inside it. At 650 px the LR and arrows diagrams are
  wider than the page and scroll sideways, as wide diagrams do.

The capture caught two faults, both fixed: the "Show screen" wrap above,
and the fixture's `style` line, whose light fill kept Midnight's light text
until it was given a `color`.

## Limits

- Mermaid 11 draws a notch half as deep (h/4); see above.
- Edges from above and below meet the node at its centre, a quarter of the
  height left of the label's centre, so that straight runs stay straight.
- `--o`, `--x` and longer links (`--->`, `==>` with more `=`) are not read;
  such a fence shows its source, as before.
- The other approximated shapes are unchanged: `[(` (cylinder) draws as a
  rounded rectangle, `[[` (subroutine) and `[/ /]`, `[\ \]` (parallelograms)
  as plain rectangles.
- Tinta's own layout is unchanged: an edge that skips ranks still runs in
  a lane below its nodes, which can pass behind a node of the same rank.
