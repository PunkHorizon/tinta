# Reading position after a new note (#279)

Validated on Windows with the Release build on 2026-10-24.

## Report

On Windows 10: reading file A, creating a new file without saving it, then
switching back to A put the page back at an older place: where it was the
last time another file was switched to, or when A was last closed.

## Cause

Reading positions (#77) are saved when another file replaces the current
one (`openDocumentSourceInViewer`) and when the window closes. Leaving a
file for something that is not a file saved nothing:

- a new note: Ctrl+N, the + button, the context menu's quick note;
- the start page in a new tab: Ctrl+T;
- an untitled tab that is already open;
- closing the last tab, which shows the start page.

Coming back to the file then restored the last place that had been saved.

A second way to lose the place: a file left before the layout reached its
saved place, as in a quick run through the tabs with the wheel (#272) or
Ctrl+Tab, saved the top of the page the view showed meanwhile over it.

## Change

- `persistLeavingPosition` saves the current document's place as the view
  leaves it. The file-to-file switch uses it, and now also the four paths
  above.
- `readingPlace` is the place a document is read at: its saved place while
  that is still waiting for the layout, otherwise the scroll position. The
  save on leaving and the save at window close both use it.

## Automated checks

`reading_position` (new, isolated portable run,
`tests/reading_position_tests.cpp`, on
`tests/fixtures/reading-position-279.md`: headings, wrapped paragraphs with
inline styles and links, nested lists, tables, code, quotes and Chinese
text). At 1050 and 650 px, with the fixture and a second file in tabs:

- switching to the other file and back resumes the place, which also leaves
  a saved place behind for the next steps to go stale against;
- after Ctrl+N (through the key handler), after Ctrl+T, and after switching
  to the open note's tab, coming back resumes where the page was left;
- leaving at once for the other file, or for a new note, while the saved
  place is still waiting for the layout keeps that place;
- closing every other tab and then the document's own, so the start page
  shows, and opening the file again resumes the place.

Each run saves `left-*.png` and `returned-*.png` around the Ctrl+N step.

Neutering checks, one at a time, each failing only its own case at both
widths: the save in `tabOpenQuickNote` (Ctrl+N), in `tabOpenStartPage`
(Ctrl+T), in `tabActivate`'s untitled branch (the note's tab), in
`tabBecomeStartPage` (closing the last tab), and `readingPlace` falling back
to the scroll position (both "leaving at once" cases). Before the fix the
test fails 12 checks, with the document coming back at the stale place or
at the top.

## Desktop check

The built app ran in portable mode from a scratch folder, in Paper at
1050 x 820, once as built before the fix and once with it. Input went in as
window messages (Ctrl held through the window thread's key state) and the
window was captured from the screen:

- six Page Down presses reached Part 10;
- Ctrl+N opened an untitled note in a new tab, in the editor;
- Ctrl+1 went back to the fixture. Before the fix it showed the top of the
  document (no place had been saved yet in that run); with the fix, Part 10
  exactly as it was left.

The frames are in `out/reading-position-279/{unfixed,fixed}`.

The save at window close is covered by reading only: `main_d2d.cpp` is not
part of the test executables.
