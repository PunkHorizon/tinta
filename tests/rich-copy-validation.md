# Rich copy validation: bold survives a paste into Outlook

Validated on Windows on 2026-10-27.

Copying from the viewer (Ctrl+C, the context menu's Copy, and Ctrl+C on the
docked preview in edit mode) now puts two formats on the clipboard: the same
plain text as before, and an `HTML Format` (CF_HTML) fragment in which
**bold** spans are wrapped in `<b>`. Rich paste targets such as Outlook, Word
and web mail read the HTML; plain-text targets are unchanged.

When Outlook has stored a compose font (`ComposeFontComplex` under
`HKCU\Software\Microsoft\Office\16.0\Common\MailSettings`), the fragment uses
that font family and size, so the pasted text matches the mail around it
instead of falling back to Times New Roman.

## Native regression checks

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
# Explicit opt-in: overwrites the clipboard (restored afterwards).
& build/rich_copy_tests.exe --clipboard-test
```

`rich_copy` lays out `fixtures/rich-copy-bold.md` at widths 1050 and 420 and
checks bold in paragraphs, across a soft line break, in list items, a
blockquote, a table cell and bold inline code; plain, italic, linked and
heading text stay unbolded; `< & > "` are escaped; a code block's `**` stays
literal; a partial selection inside a bold span opens and closes it; the
CF_HTML header offsets match the payload; non-ASCII text is UTF-8; a relayout
does not duplicate ranges. The clipboard run drives Ctrl+A and Ctrl+C through
`tests/keystroke.h` and reads both formats back. All 48 CTest suites pass.

## Checked in the built app

The built `tinta.exe` opened the fixture; Ctrl+A, Ctrl+C put `UnicodeText` and
`HTML Format` on the clipboard. Pasting into a new classic Outlook (HTML)
draft through its Word editor gave Aptos 11 pt text with exactly the bold
phrases bold, and plain, italic and linked text not bold. The draft was
discarded.

## Limitations

- Only bold carries over; italics, links, headings and lists paste as the
  plain text they did before.
- Copy with nothing selected (Ctrl+C copies the whole document's text) stays
  plain text.
- New Outlook and web mail were not checked by hand; they read standard
  CF_HTML.

## Follow-up after merging (2026-10-27)

Tinta's copy of the fixture, taken from the real app (Release build,
isolated portable copy, Ctrl+A and Ctrl+C posted to the window), was pasted
into new documents in Word 16.0.20527 (Microsoft 365) through COM, and
compared with a paste of the plain text alone. Two differences came up:

- the lines were joined with `<br>`, so Word made the whole copy one
  paragraph with 32 manual line breaks, where the plain text gives a
  paragraph per line;
- each table cell's tab arrived as two tabs: Word counts the
  `mso-tab-count` span and the `&#9;` inside it.

Each line is now a `<div>`, an empty line `<div><br></div>`, and a tab is a
plain `&#9;`. The same paste then gives the plain text's 34 paragraphs line
for line, plus one more empty one at the end (the copy ends in a blank
line); no manual line breaks, 3 tabs, Aptos 12 pt with the Normal style's
spacing, and exactly the 13 bold phrases bold. Without the change
`rich_copy` fails 12 checks (6 at each width).

The clipboard run read the plain text with a single `OpenClipboard`; over
Remote Desktop, whose clipboard sync holds the clipboard for a moment after
each change, that read failed in 5 runs of 6. It now retries, like the HTML
read, and passed 10 runs of 10. The full suite passed 48 of 48.

Fonts, as observed on that machine:

- In Word, HTML without a font pastes in the document's own font (Aptos
  12 pt, its Normal style), like plain text. With the style the compose font
  produces (simulated as Aptos 11 pt; the registry was not changed), Word
  takes 11 pt instead. Classic Outlook has no profile there, so the Times
  New Roman fallback described above was not reproduced.
- Word also applies a face and size given only as `mso-ascii-font-family`
  and `mso-ansi-font-size`.
- In Chromium's editing pipeline (`insertHTML` into an editable element,
  the path a native paste takes), bold, lines and tabs survive. A
  `font-family` and `font-size` on the wrapper survive too, so they replace
  the editor's own font, while `mso-*` properties are dropped. Web mail
  editors that handle the paste themselves were not checked.

Evidence is in `out/verify-285/` (ignored).
