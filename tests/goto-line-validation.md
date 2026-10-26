# Go to line with Ctrl+G (#276)

Validated on Windows with the Release build on 2026-10-22.

## Request

A way to jump to a line by its number, as Ctrl+G does in most editors.

## Change

- **Ctrl+G** opens a small prompt at the top of the pane it acts on: the
  editor's source pane in edit mode, the document in the reading view
  (including edit mode's full-window reading view). It shows the label, the
  digits typed so far with a caret, and the range `1–N`.
- While open the prompt takes every key: digits are typed (up to nine),
  Backspace removes one, other characters are ignored, **Enter** jumps and
  **Esc** closes. A click elsewhere closes it; a click on the prompt keeps
  it open (#282). Enter, Esc and Backspace are consumed on their key-down,
  so they make no character for the editor; every other key is left
  unconsumed, so the message loop translates it into the character the
  prompt reads (#282).
- In the editor the caret moves to the start of the line, carets and the
  selection are cleared, and the line is placed about 40 % down the view.
- In the reading view the page scrolls to where the line's block is drawn,
  through the scroll anchors that also drive scroll sync. A block's anchor
  points at its first text, after markers such as `## ` or `- `, so the
  line's whole extent is matched rather than its first character, as the
  preview already does for the caret's block.
- Inside edit mode both views count the editor's lines, and the reading
  view maps them through the text it was parsed from, so unsaved edits are
  followed rather than the file as last loaded.
- Numbers past the end go to the last line, 0 to the first.
- Ctrl+G does nothing on the start page or over other overlays (settings,
  help, lightbox, print preview, theme and shortcut editors, annotation
  editor, save question). It is listed in Help.
- The label is translated into all eight interface languages.

## Automated checks

`goto_line` (new, isolated portable run, `tests/goto_line_tests.cpp`, on
`tests/fixtures/goto-line-276.md`, copied into the test folder first):

- the fixture's headings each sit on the line they name;
- in Paper and Midnight, at 1050 and 650 px, with word wrap off and on:
  Ctrl+G opens the prompt, the typed number with Enter puts the caret on
  lines 120, 41, 7 and 190, the line is on screen, and the Enter character
  does not reach the text;
- 99999 goes to the last line, 0 to the first, letters are not typed,
  Backspace removes a digit, Esc closes without moving, a click closes it,
  and the document stays unmodified;
- in the reading view, for each of the 25 headings, Ctrl+G with its line
  number scrolls the heading to the top of the page (within 40 px);
- in edit mode's reading view, after ten lines are added at the top, the
  prompt counts the editor's lines and each heading's number plus ten
  scrolls to that heading;
- each run saves PNG previews of the prompt and the result into
  `build/goto-line-test-Release`.

The test also passes on a CRLF copy of the fixture.

`i18n` expects 360 Russian strings, and now scans `src` and `include` for
raw non-ASCII characters in wide literals (see below).

Neutering checks:

- letting the Enter character through fails "does not reach the text" and
  the caret checks;
- matching anchors against the line's first character instead of its
  extent fails the reading view for every heading;
- mapping edit mode's reading view through the viewer's source fails it
  for every heading;
- a raw en dash in a wide literal fails the `i18n` source scan at its line.

## Visual check

The previews were inspected in both themes and at both widths. The first
round showed the prompt's range as `1â€“198`: the en dash was a raw
character in a wide literal, and the sources build without `/utf-8`, so
MSVC read it in code page 1252. It is now `\u2013`, as elsewhere in the
code, and the `i18n` source scan guards the rest of `src` and `include`.
After the fix:

- the prompt sits centred below the tab strip, over the document or the
  source pane, its text centred vertically, in the theme's colours;
- at 650 px with the preview on, it narrows to the source pane;
- after Enter, line 120 is highlighted about 40 % down the editor, and in
  the reading view "Section at line 101" is at the top of the page;
- in edit mode's reading view with the ten added lines, the range reads
  `1–208` and line 111 brings "Section at line 101" to the top.

The real window could not be captured in this session: the remote desktop
had no visible screen, so `PrintWindow` and screen copies came back blank.
The previews draw through the app's own render target with the same
functions as the window.

One earlier failing run, when the swallowed Enter was disabled as a
neutering check, typed newlines into the fixture, and the next run's Enter
answered the save question and wrote them to disk. The fixture was
restored, and the test now works on a copy and checks that the document
stays unmodified.

# Typing into the prompt (#282)

Validated on Windows with the Release build on 2026-10-26.

## Report

In v3.9.2, Ctrl+G opened the prompt but no digit could be typed into it,
and a click on the prompt closed it.

## Cause

The main message loop translates a key-down into its `WM_CHAR` only when
no command consumed the key (#195). The prompt's key-down handler returned
"consumed" for every key, so no digit ever became a character. The tests
missed it because their `type()` handed each character to the handler
whatever the key-down returned, and the real-window check used
`SendMessage`, which skips the message loop altogether.

Closing with Enter or Esc also left that key's character waiting to be
swallowed. Since a consumed Enter makes no character, the flag stayed set
and ate the next Enter typed in the editor, as soon as the prompt could
jump at all.

## Change

- The prompt consumes only Enter, Esc and Backspace. Every other key stays
  with it (no other shortcut sees it) but is left unconsumed, so the loop
  translates digits in whatever layout types them.
- The swallowed-character flag is gone: consumed keys make no character.
- A press on the prompt keeps it open and is kept from the text below; a
  press elsewhere still closes it.

## Automated checks

`goto_line` now types through `tests/keystroke.h`, which delivers a key's
character only when the key-down leaves it unconsumed, as the loop does:

- Ctrl+G is consumed, the typed number shows in the prompt, and Enter is
  consumed, in every editor and reading-view run;
- after a jump, the next Enter starts a new line and Ctrl+Z takes it back;
- a click in the middle of the prompt keeps it open with its digits,
  without moving the caret or selecting, in the editor and the reading view.

Neutering checks:

- consuming every key again fails 304 checks: the typed numbers, the caret
  lines, the reading-view scrolls and the click checks;
- restoring the swallowed Enter fails "the Enter after a jump starts a new
  line" in all eight editor runs.

## Real window

Keys were posted to the window, so they went through `GetMessage`,
`TranslateMessage` and the loop's consumed check, with Ctrl held through
attached thread input. Each build ran as a portable copy with isolated
settings on a copy of `goto-line-276.md`: E to edit, Ctrl+G, 1 2 0, Enter,
then Ctrl+G, a click on the prompt and 5.

- v3.9.2: the prompt stayed empty; the click closed it and the 5 was typed
  into the document as a new line 2.
- With the fix: the prompt showed 120, Enter brought line 120 into view,
  and after the click the prompt stayed open showing 5. The file was left
  unmodified.

Captures are in `out/verify-282`.
