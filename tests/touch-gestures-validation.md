# Touch gestures (#286)

Validated on Windows with the Release build on 2026-10-28.

## Report

On a touchscreen, a finger dragged across the page selected text as a
mouse drag does, instead of scrolling. The request: a swipe pans, a long
press selects or opens the context menu, a double tap selects a word, the
mouse keeps its selection, and the Contents and other lists pan too.

## Change

Touch input arrives as pointer messages (`WM_POINTERDOWN`, `UPDATE`, `UP`).
Tinta used to leave them to `DefWindowProc`, which turns a touch into mouse
input. `src/touch.cpp` now takes the touches that land on what a finger
should drive:

- **The page** (the reader, or the preview beside the editor), **the
  editor's text**, and **the side panels' lists** (the Contents, the file
  browser, the search results).
- **A pan**: past 10 DIP of movement the content follows the finger, held
  to the axis the finger set out along. The page also pans sideways when it
  is wider than the window. Each move sets the position where the pan began
  less the finger's travel, so the preview's line-by-line sync with the
  editor cannot swallow a slow pan.
- **A flick**: the finger's speed over its last 100 ms carries the content
  on, slowing by 1/e every 325 ms (about speed x 325 ms in all), up to
  6 DIP/ms, until it crawls or reaches an end. A rest before lifting
  leaves no fling; a new touch, a click, the wheel, a key or another
  document stops one.
- **A tap** is a click at that point, through the mouse handlers: links,
  rows, buttons, check boxes, images and the editor's caret take it as
  before, and no hover is left behind.
- **A double tap** within the double-click time and 24 DIP of the first
  clicks where the first tap did, so the handlers count the double-click
  and select the word (a third tap, the line).
- **A press held still** for 500 ms selects the word under it, as a
  double-click does; sliding on extends the selection word by word, and the
  context menu (in the editor, the editor's) opens where the finger lifts.
  A held row in a list is a slow tap.
- **A second finger** is taken and ignored, but the first finger then
  neither taps nor holds.

Touches elsewhere keep Windows' mouse emulation as before: the tab strip
and window caption, menus, dialogs and overlays, the panels' headers and
buttons, the search bar, the editor's rail, scrollbar and seam, and every
scrollbar. A finger on a scrollbar still drags it, now along the track it
is drawn on (#287). The mouse and the pen are unchanged.

## Automated checks

`touch_gestures` (new, isolated portable run, `tests/touch_tests.cpp` on
`tests/fixtures/touch-gestures-286.md`: 42 headings, wrapped paragraphs
with emphasis, bold, code and links, a 16-column table and a long code line
for sideways pans, lists, quotes and Python). In themes 0 and 5, at 1050
and 650 px, at 1x and 1.5x:

- what a finger drives: the page in the middle; Windows for the tab strip,
  the scrollbar and an open overlay, whose touches are not taken;
- a pan: nothing moves within the slop, then the page follows the finger
  pixel for pixel with nothing selected; a rest before lifting leaves no
  fling;
- a flick at 3 px/ms goes on after the finger lifts, each step shorter than
  the one before, and stops after 0.9 to 1.01 times 975 px; it stops at the
  page's end, and a finger coming down stops it at once;
- a tap on a link follows it and leaves no hover;
- one tap on a word selects nothing; a second tap 6 and 5 px off selects
  the word, and a third the line; a tap elsewhere puts it away;
- a held press selects the word, a small tremor keeps it, sliding to a
  later word extends the selection word by word without panning, and the
  context menu opens on the lift over the same selection; a hold that loses
  its capture keeps the word without the menu, and its later lift is not
  taken;
- with a second finger down, the first finger's lift does not follow a
  link;
- a mostly sideways pan moves the page across and not down;
- the Contents list pans under the finger while the page stays, and a tap
  on a row opens its section and closes the unpinned panel;
- a mouse drag still selects text;
- in edit mode, the editor's text pans with the finger, its scrollbar and
  seam stay with the mouse, a tap places the caret, and a pan on the
  preview takes the editor along, the page settling within a line of where
  the finger took it.

Neutering checks: with `touchDown` leaving every touch to Windows (the old
behaviour), 304 checks fail; with a later tap clicking where it landed
instead of where the first did, exactly the 16 double- and triple-tap
checks fail. The full suite passes 49 of 49.

## Checked in the built app

Real touch input could not be put into the built app here. The machine is
reached over Remote Desktop with its window minimized, and Windows refuses
injected touch input in that state: `InjectTouchInput` fails, and
`InjectSyntheticPointerInput` returns access denied. The injection script
(`out/verify-286/`, ignored) checks that every contact point is on the
test window before it sends anything; it ran no gesture.

The same build ran the #287 mouse scenarios in the real app (posted mouse
messages, results read back from the saved reading position): the
scrollbar thumb lifted to the top of its track, a click there, and the
Contents bar dragged to its foot all behaved as on the build without
touch, so pointer handling leaves the mouse alone.

## Limitations

- Not yet tried with a finger on a touchscreen. The window procedure's
  pointer handling (`handlePointerMessage`: the pointer's type, position
  and time) is exercised only through the functions it calls.
- Pinch to zoom is not handled; a second finger is ignored.
- The editor pans up and down only: unwrapped lines have no right end to
  stop a sideways pan at.
- In edit mode the preview follows the editor line by line, as with the
  wheel and the preview's scrollbar, so on a narrow preview a pan there
  moves in steps of a source line.
- The pen keeps Windows' mouse emulation: a pen drag selects text.
