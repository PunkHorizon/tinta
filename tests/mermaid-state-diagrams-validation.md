# Mermaid state diagrams

Validated on Windows with the Release build on 2026-10-26.

## Overlapping edges, labels and notes

### Report

In a top-to-bottom state diagram, a back edge from a state with a neighbour
to its right ran across that neighbour, its label chip landed on it, and a
`note right of` the state was drawn over it. In the reproduction both
"Esc Esc" and the note sat on "Searching".

### Cause

- Back edges and edges that skip ranks left their node's right side at its
  centre, then ran in a lane just past the wider of their two ends. Nodes
  further right in the same rank, and in the ranks between, were in the way.
- A label chip went at the half-length of its route, whatever lay there.
- A note was drawn beside its node with no room kept for it.
- A transition from a state to itself kept the state out of the rank
  layout, so the state and everything after it fell into a column of
  fallback ranks.

Class and ER diagrams share the routing and had the same faults.

### Change

- Layout and routing work in a frame where ranks run downward; LR and RL
  are transposed into it and BT and RL flipped, so all four directions take
  the same path.
- Each node is laid out in a slot that holds its notes beside it, stacked
  when there are several. `note right of` sits right of its state in
  vertical diagrams and below it in horizontal ones, where Mermaid puts it;
  `note left of` sits left or above. Self-loops get room beside their state
  in the same way, one on each free side, with the label beyond the loop.
- Edges to the next rank leave and enter their states at their own points,
  ordered by where their other ends lie, and cross the gap at their own
  heights, nested so they do not cross where they can avoid it. Class
  diagrams keep the shared trunk of an inheritance tree.
- Back edges and edges that skip ranks run in a lane beyond every node and
  note of the ranks they pass, on whichever side costs least. An end with a
  neighbour or note between it and the lane leaves through the gap above or
  below it instead. A back edge to the rank just above may cross the gap
  between the two directly. Route length counts, and so does each crossing.
- Gaps between ranks grow to fit the runs crossing them with their labels.
- A label chip goes on its own line where it touches no node, note or other
  chip, and no other line if it can; failing that just beside its line, then
  where it covers the fewest lines. Chips are drawn after every line, so
  the HTML export layers them as the viewer does.
- The drawing is shifted to start at the origin, as lanes and labels can
  lie left of or above the rank layout.

### Automated checks

`tests/mermaid_layout_checks.h` reads a built diagram's primitives back into
states, notes, chips and transition segments, and reports any transition
segment that enters a state, any chip that overlaps a state, a note or
another chip, any note that overlaps a state or another note, a chip more
than a few pixels from every transition, and anything outside the
diagram's extent.

- `mermaid_parser` (`testStateOverlapRepro`): the reproduction in TB, LR, BT
  and RL at scale 1 and 1.75, plus "Esc Esc" and the note clear of
  "Searching".
- `mermaid_parser` (`testStateMachines`): a player with a self-loop and
  notes on both sides, an order with a choice, a fork, a join, a long label
  and a return over five ranks, a connection with a retry loop, a dense
  machine with two loops on one state and two notes on another, and a BT
  machine with descriptions. Each in all four directions, at scale 1 and
  1.75 with fixed-width text, and at 0.7, 0.83, 1.15 and 1.37 with
  proportional text that wraps notes.
- `mermaid_parser` (`testClassAndErLayouts`): an inheritance tree with
  multiplicities, a dependency, a self-association and a two-way
  association; an ER diagram with five relationships and attributes.
- `code_block_layout` (`testMermaidStateDiagrams`):
  `tests/fixtures/mermaid-state-diagrams.md` in the app's own layout, in
  Paper and Midnight, at scale 1 and 1.5, at 1050 and 650 px: all twelve
  diagrams draw (eight without composite states), and their primitives,
  measured with the app's DirectWrite text, pass the same checks.

Neutering: with the old routing restored, `mermaid_parser` fails 140 checks.
In the reproduction, "Esc Esc" overlaps "Searching", the note overlaps
"Searching" and the end state, and the back edge's stub crosses both; in
the other machines and directions, lanes cross the states between their
ends and chips overlap each other; in the class and ER diagrams, lanes run
through the boxes between their ends.

The proportional text found a hang the fixed-width text missed: a lane
moved past a neighbour by exactly the spacing could be found too close
after rounding, and moved to the same place again forever. The sweep at
uneven scales keeps that covered.

### Visual check

- The fixture through `--printpages` (`out/state-p1-light/pages`): the
  reproduction draws with "Esc Esc" on the left lane, "Esc" back across the
  gap above "F" and "Q", and the note in its own room; the LR, BT, player,
  order and RL diagrams have no overlaps, and the table, list, quote and
  code block lay out around them.
- `--exporthtml` in Paper and Midnight (`out/state-p1-light/export.html`,
  `out/state-p1-dark/export.html`, rasterised to PNG): eight inline SVGs,
  chips over the lines, legible in both themes. Printing always uses the
  Paper palette, so Midnight was checked in the export.
- The app window at 650 px (`out/state-p1-narrow`): the TB diagram fits;
  the LR one is wider than the column and scrolls sideways, as wide
  diagrams always have.
- The fixture's class and ER diagrams (`out/state-p1-light/pages/page-5.png`):
  the inheritance tree keeps its trunk, the self-association loops beside
  Zebra with its label, multiplicities sit at their ends, and each ER
  relationship has its own cardinality marks at the shared entity.

### Limitations

- Edges still cross where the rank order forces it, and a label may cover
  another edge's line where no free spot is left on or beside its own.
- Several labelled self-loops on one side of a state nest, and their
  labels stack beside the outermost loop.
- Ranks and the order within them come from the existing layout: the
  router does not reorder states to remove crossings.

## Composite states

### Report

A state diagram with `state Id { ... }` showed its source: any line with a
`{`, and any `--` or `}` line, failed with "Composite states are not
supported".

### Change

- The parser reads a tree of regions: the diagram, and the regions of each
  composite, split by `--`. `state Id {` and `state "Long name" as Id {`
  open a composite (or open it again to add to it), `state Id {}` makes an
  empty one, and `}` closes it. `[*]` inside the braces is that region's
  own start or end. `direction` applies to the region it is written in; a
  composite's regions run top to bottom unless they say otherwise, as in
  Mermaid.
- As in Mermaid, a state named inside a composite belongs to it, to the
  last such composite when there are several, and naming it at the top
  again does not take it out. A composite never goes inside itself.
- A transition is laid out in the innermost region holding both its ends;
  an end inside a composite of that region is drawn to the composite's box.
  A transition from a state to its own composite loops on the box. One
  between concurrent regions has no region holding both, and falls back to
  the source.
- Regions are laid out from the inside out: each is a diagram of its own,
  with the notes, loops, lanes and labels above, and a composite is one
  node of its parent, sized to its header and regions. Its box has its name
  (and any description) in a band above a rule, and below it its regions,
  side by side, or stacked when they run left to right, parted by dashed
  lines. The diagram itself may be split by `--` the same way.
- Gaps between ranks leave room for a label between arrowheads at both of
  their faces. In a left-to-right composite example, the label of an edge
  beside its parallel back edge found no room in the gap.

### Automated checks

- `mermaid_parser` (`testCompositeStates`): the example from the report in
  all four directions at scale 1 and 1.75. Editing is a box around Typing
  and Selecting with its own start, apart from the diagram's start and end;
  the transitions to and from Editing end on its border; the inner label is
  inside it. Then, in all four directions at both scales: three nested
  composites, each inside the last with its own start and end; three
  concurrent regions parted by two dashed lines, each with its own start
  and no state across a line; a composite named with `as`, with its own
  direction, notes inside it and on it, a loop on it, and transitions
  between states outside and inside it ending on its border; a diagram
  split at the top; a transition into a state two composites deep, named
  before its composite; and an empty composite. The layout checks also
  report a transition crossing a composite's border or running through its
  header, and a chip, note or state that straddles a composite instead of
  lying in its body or clear of it.
- `mermaid_parser` (`testExtFallbacks`): a composite now draws; an
  unclosed composite, a stray `}` and a transition between concurrent
  regions fall back.
- `code_block_layout` (`testMermaidStateDiagrams`): the fixture's four
  composite diagrams pass the same checks with the app's text.

Neutering: with composite lines rejected again, `mermaid_parser` fails 153
checks. Every composite diagram falls back ("Composite states are not
supported"), so no container, inner start, divider or nested box is found,
and no transition ends on a composite's border.

### Visual check

- The fixture through `--printpages` (`out/state-p2-light/pages`, pages 5
  and 6): Editing is a box with its name over its own start, Typing and
  Selecting, and "press colon" and "Esc Esc" end on its top. "Text
  editing" holds its own left-to-right diagram, with "Caret visible" below
  Typing, "Edit mode" above the box, the "undo" loop below it, and "open
  file", "close" and "Ctrl+S" on its sides. Active has three regions parted
  by dashed lines; Review sits inside Document, each in its own direction.
  The surrounding text, quote and table lay out as before.
- `--exporthtml` in Paper and Midnight (`out/state-p2-light/export.html`,
  `out/state-p2-dark/export.html`, rasterised): twelve inline SVGs; the
  boxes are drawn in the theme's accent in both.
- The app window at 650 px (`out/state-p2-narrow`): the composite example
  draws as in print, between its heading and text.

### Limitations

- A transition between a state outside a composite and one inside it is
  drawn to the composite's box, not into the box to the inner state.
- A transition between concurrent regions falls back to the source.
- A composite's regions run top to bottom unless they set a direction,
  whatever the diagram's own direction, as Mermaid lays them out.
- Notes over several lines (`note ... end note`) still fall back, as
  before.
