# Scrollbars beside the preview (#280)

Validated on Windows with the Release build on 2026-10-26.

## Report

With the editor and its preview side by side:

1. dragging the editor's scrollbar showed the text cursor;
2. the preview's scrollbar could not be dragged.

## Cause

1. The editor pane set the text beam for any point left of the seam, its
   scrollbar included, and the scrollbar drag set no cursor, so the beam
   stayed.
2. Beside the editor the preview follows the editor every frame, through the
   scroll-anchor sync (#77). A drag on its scrollbar moved the page, and the
   next frame put it back where the editor was. The wheel over the preview
   already scrolls the editor for this reason.

## Change

- The pointer is an arrow over the editor's scrollbar and for the whole drag,
  also when it strays over the text. The hover and the press use one
  helper for the scrollbar's band.
- `editorFollowPreview` does the sync the other way: the editor scrolls to the
  source line at the preview's top. A drag on the preview's scrollbar, or a
  click on its track, calls it, so both panes move and the next sync keeps the
  preview where the thumb was taken. At the end the preview may still scroll
  past the editor's end (#85).

## Automated checks

`split_scrollbars` (new, isolated portable run,
`tests/split_scrollbar_tests.cpp`, on `tests/fixtures/split-scrollbars-280.md`:
headings, wrapped paragraphs with inline styles and links, nested lists,
tables, code, quotes and Chinese text), in Paper and Midnight, at 1050 and
650 px, with word wrap off and on:

- the beam over the editor's text, an arrow over its scrollbar, while
  dragging it and when the drag strays over the text; the drag scrolls the
  editor and the release ends it;
- an arrow over the preview's scrollbar and while dragging it; dragged 35 %
  down its track, after the panes sync, the preview stays within a few lines
  of where the thumb was taken and the editor follows; dragged to the end,
  the preview shows its end; dragged back up, both panes are at the top;
- a click on the preview's track jumps both panes there, while the button is
  still held as well.

Before the fix 48 checks fail, six in each configuration. Without the call in
the track click, that check fails in all eight.

## Desktop check

The built app ran in portable mode in Paper at 1050 x 820, once as built
before the fix and once with it. E opened the editor, and window messages
dragged the preview's thumb 300 px down. Before the fix the preview stayed at
the top while the thumb was dragged; with it, both panes showed Section 6
(`out/issue-280/{unfixed,fixed}`).

The pointer's shape is checked in the test only (`GetCursor` after each
move): screen captures do not include the pointer.
