# Mermaid C4 relation labels

Validated on Windows with the Release build on 2026-10-26.

## Problem

A C4 relation's label is a chip drawn on its line. It sat on the middle of
the line, and elements stand 24 px apart in a row and 26 px between rows, so
between two neighbours the chip covered both boxes and their text. In the
reported context diagram *Opens files in* and *Renders diagrams with* hid
the Tinta box's description. Relations between the same two elements shared
one line, so their chips lay on top of each other, and a relation passing
behind a box ran on the same line as the relation ending at that box.

## Change

- Each labelled relation has a home gap, in the innermost boundary that
  holds both elements: beside the member it leaves, toward the member it
  reaches, in their row or between their rows. A gap widens to hold its
  labels in tiers: the widest label plus 16 px on each side in a row, the
  tallest plus 16 px on each side between rows, which leaves the arrowheads
  in view. Gaps without labels keep their 24 and 26 px.
- A chip sits on its line in a tier of its gap, slid along the tier while
  the line still crosses it; failing that, anywhere along its line, through
  or just beside the chip. It keeps 16 px from every box and 6 px from
  boundary frames, captions, the title and other chips, and keeps off other
  relations' lines where it can. A label that still does not fit gives its
  gap another tier and the layout runs again, up to 12 more times.
- Lines that would run along each other, between the same two elements or
  past a box that another relation ends at, take lanes side by side, far
  enough apart for their labels and inside their boxes. Unlabelled ones get
  lanes 10 px apart.
- A relation from an element to itself had no line before, and its label sat
  on the box. It now loops out of the box's right side above its middle,
  with its label beside the loop.
- Last resort: a label no gap can hold goes right of the diagram, joined to
  the middle of its line by a dashed leader. In the tests only a loop boxed
  in by its boundary's frame and a close neighbour needs it.

## Automated checks

- `tests/c4_label_checks.h` checks a built diagram's primitives: each chip
  holds its label; overlaps no element box, no other chip, no arrowhead and
  no text but its own (names, descriptions, captions, the title, other
  labels); lies wholly inside or outside each boundary frame; and is crossed
  by (or within 3 px of) a line between its relation's two elements, or
  joined to one by a leader.
- `mermaid_parser`, with character-cell measurement at scale 1 and 1.5:
  the reported diagram, with each label between its two boxes; every `Rel`
  direction keyword, `Rel_Back` and `Rel_Neighbor`; `BiRel` beside a `Rel`;
  nested enterprise, system, container and plain boundaries; three rows
  with relations skipping a row and a column; dense neighbours with three
  relations between two boxes; a fan-out; deployment nodes (`Node_L`,
  `Node_R`); loops. Also: lanes between the same two boxes run apart; a line
  passing behind a box runs beside the line ending there; a diagram without
  labels keeps its 24 and 26 px gaps; the boxed-in loop's label goes right
  of the diagram; 24 elements with 66 crossing relations; 16 relations
  between two elements; and the eight fences of the fixture.
- `code_block_layout`: the fixture laid out by the app in Paper and
  Midnight, at scale 1 and 1.5, at 1050 and 650 px. All eight fences draw as
  diagrams, the text after them lays out, and each diagram built with the
  app's DirectWrite measurement passes the same checks.

Neutering: with the old `src/mermaid_c4.cpp`, `mermaid_parser` fails 46
checks: every layout and fixture case at both scales, plus the
reported-label, lane, behind-a-box and last-resort checks. They find 219 box
overlaps, 229 text overlaps, 187 labels on labels, 265 covered arrowheads
and 23 frame crossings. `code_block_layout` fails 1176 C4 checks.

`code_block_layout`'s clipboard Copy checks fail on this machine with or
without this change (Remote Desktop clipboard sync); its other checks pass.

Layout time with character-cell measurement: about 4 ms for 24 elements and
66 relations, 56 ms for 24 elements and 129 relations.

## Visual check

`tests/fixtures/mermaid-c4-labels.md` has the eight diagrams among headings,
a table, an ordered and a bulleted list, emphasis, links, a quote and a code
block. A portable copy of the Release build (`out/c4-labels/`, ignored):

- `--printpages` in Paper before and after the change (`before/pages`,
  `after/pages`): before, every diagram had labels on boxes, text or each
  other; after, none, and the surrounding content is unchanged.
- `--exporthtml` in Paper and Midnight (`after/`, `dark/`): eight inline
  SVGs, the closing code block a `<pre>`, the table, quote and lists kept.
  The SVGs were viewed at 1:1 through headless Edge (`diagrams-*.png`).
- The running app at 1050 and 650 px in Paper (`window-1050/`,
  `window-650/`): labels as in the exports. At 650 px, and for the wider
  deployment and dynamic diagrams at 1050 px, the diagrams scroll sideways,
  as wide diagrams did before.

## Limits

- Diagrams get wider. A row of three with labels between its members is now
  about 860 to 1010 px instead of 648 px; Mermaid's own C4 layout, four
  216 px shapes 50 px apart, is about 1014 px. Nested boundaries widen more.
- Lines stay straight between box centres. A relation to a member further
  along the row, or two rows down, passes behind the boxes between (now in a
  lane of its own), and lines can cross in busy gaps.
- The direction keywords still do not move boxes. `Rel_Back` keeps its
  arrowhead at the second element, as before; Mermaid draws it at the first.
  `RelIndex` still shows its source.
- A label wider than 150 px wraps as before, and can leave one word on its
  last line (*Passes the unpacked chapters* / *to*).
- Printed pages can still break inside a diagram, between its title and its
  boxes, as before. Printing uses paper colours in every theme.
