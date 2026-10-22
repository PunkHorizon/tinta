# Edit-mode preview follows the editor through long documents (#267)

Validated on Windows with the Release build on 2026-10-21, with the
reporter's redacted document and settings (1,049 lines, 23 images, kept in
the ignored `out/issue-267/`) and a fixture shaped like it.

## Problem

In edit mode the preview stopped following the editor at about line 275 of
the reporter's document and only moved again once the editor scrolled back
up past it.

The preview follows through scroll anchors: each laid-out block records its
source offset and rendered height, and an anchor is kept only when its
offset is larger than the last one's. Line 274 of the document opens a YAML
code block whose first line is indented. md4c reports that indentation from
its own static string of spaces, and the parser took the block's offset
from the first text it was handed: the distance between that string and
the document, 138,064,145,913,312 bytes. Every later anchor was smaller and
dropped, so from that block on the editor's position mapped to its own
y, a few pixels on.

md4c reports the line breaks inside code blocks and paragraphs the same
way, so any element whose first text came from one was exposed too.

## Change

- `textCallback` (`src/markdown.cpp`) takes an element's offset only from
  text that lies inside the source; the next piece of text, from the
  document, sets it.
- The preview sync moved unchanged from `main_d2d.cpp` into
  `syncPreviewToEditor` (`src/editor.cpp`) so tests can run it.

## Automated checks

`edit_preview_scroll_sync` (new, `tests/scroll_sync_tests.cpp`) runs on
`tests/fixtures/scroll-sync-267/docs/scroll-sync.md`: 20 sections with
headings, emoji callouts, tables, C#, JSON and PowerShell blocks, images in
`../assets`, a contents list of links, and YAML blocks indented past their
fence in sections 8 and 17. At the reporter's 1979×1251 and at 1050×900,
with word wrap off and on, it checks that:

- every element's source offset lies inside the document, in order;
- the anchors climb and reach the last tenth of the document;
- with the editor scrolled so each heading is its top line, the preview
  shows that heading at its top, within 80 px;
- wheeling the source down from the top, and on past its end, never moves
  the preview up and takes it to its end;
- the other direction, reported on the issue later: with the reading view
  scrolled to each heading, E opens the editor on that heading's line. It
  maps the reading position through the same anchors, and past the bad one
  opened the editor at the last line (365 lines away for the heading after
  the fixture's first YAML block).

The same suite run on the reporter's document passes. With the old offset
code both fail: headings after the YAML block are off by up to 10,019 px in
the fixture and 21,430 px in the reporter's document.

## CRLF files (follow-up, 2026-10-22)

The contributor of PR #275 found the E check failing on a Windows checkout,
where git turns the fixture's LF into CRLF. That is a product bug: the
viewer reads documents in text mode, so its anchors count a CRLF as one
byte, while `enterEditMode` read the file raw and counted newlines up to the
anchor's offset. Every line above the reading position moved the target up
by one byte, so the editor opened too far up: 22 lines at the end of the
fixture. `toggleTaskOnDisk` already maps these offsets by skipping CRs; the
E mapping now walks the raw content the same way.

The suite now runs every check on an LF copy and a CRLF copy of the document
(each with its `../assets`), whatever the checkout produced. With the old
mapping only the CRLF copy fails, at 22 lines; with the change both pass,
and the reporter's document passes too.

## Limits

- The reporter's document is not committed; the fixture reproduces the
  same block.

## Desktop check

Computer use was not available in this session, so the window was not
operated by hand. The checks above drive the real input handlers, layout
and painting in a hidden window instead. The full suite passed 39 of 40 on
the Release build; the one failure is the known first-run crash of
`editor_context_selection`, which passed on three reruns.
