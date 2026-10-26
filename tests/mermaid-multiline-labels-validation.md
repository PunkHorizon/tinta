# Mermaid labels over several lines (#281)

Validated on Windows with the Release build on 2026-10-26.

## Report

A flowchart that VS Code draws showed in Tinta as its source. The attached
file has a front matter title, a `%%` comment, `graph TB`, circle nodes with
`-` in their IDs, loops, and edge labels in double quotes that run over two
to four lines.

## Cause

- The flowchart parser reads a statement per line. A double-quoted label
  that went on to the next line split its edge in two, the first half failed
  as "Unsupported edge syntax" (line 9 of the fence), and the fence fell back
  to its source.
- The quotes around `-- "text" -->` edge text stayed in the label (`"Nop"`),
  and that form skipped entity and `<br>` decoding. `-->|"text"|` kept its
  quotes too.
- Drawn, the reporter's labels showed two layout flaws. A label sized to
  exactly its measured width could wrap its last letter
  (`Push:LABORDERPANEL` / `S`), and a word longer than the 180 px label cap
  broke in the middle (`Push:LABORDERPANEL` / `RESULTS`). The HTML export had
  the same sizing.

## Change

- A line break inside a double-quoted string is kept as a one-character mark:
  the statement continues on the next line, source offsets stay where they
  were, and error lines still count the line. Decoding turns the mark into
  the label's line break, without the CR or the next line's indentation.
  Single quotes are not Mermaid strings, so an apostrophe joins nothing.
- Edge text in both forms drops its delimiting quotes and is decoded like a
  node label. Quoted `-- text -->` text may hold an arrow.
- An edge label gets a pixel of slack, and a word longer than the cap widens
  the label, up to twice the cap, instead of breaking. This holds in the
  viewer and the HTML export; Word and printing use the viewer's drawing.

## Automated checks

- `mermaid_parser` (`testMultilineQuotedLabels`): the reporter's shape, with
  CRLF and tabs, parses; `Nop` has no quotes; a two-line and a four-line
  label keep their breaks without the CR or indentation; a loop, a quoted
  `|text|` label, a quoted node label over two lines and an arrow inside
  quoted edge text all come out right; an error after a joined label names
  its own line; an apostrophe leaves the next line alone.
- `code_block_layout` (`testMermaidMultilineLabels`), on
  `tests/fixtures/mermaid-multiline-labels-281.md` (the reporter's diagram,
  a smaller one, a table, a quote and a list), in Paper and Midnight, at
  scale 1 and 1.5, at 1050 and 650 px: both fences draw as diagrams, and
  every label is laid out in exactly its own lines.

Neutering: without the parser change, the new parser test and the
reporter's file fail at the multi-line label ("Unsupported edge syntax");
without the sizing change, the long labels break (40 more failures).

## Visual check

- The reporter's file in the built app at 1050 px: it draws as a diagram,
  with `Push:ORDERS` / `Push:LABORDERS` and the four-line `Pop` labels on
  their own lines (`out/issue-281/screen-reporter`).
- The fixture through `--printpages`, `--exporthtml` and `--exportdocx`: every
  label is whole in the pages, the SVG text and the Word images
  (`out/issue-281/pages`, `export.html`, `export.docx`).

Tinta lays flowcharts out itself: ranks top to bottom, with edges to earlier
ranks in lanes at the side. The reporter's state machine is therefore taller
and narrower than Mermaid's layout in VS Code, as other Tinta flowcharts are.
