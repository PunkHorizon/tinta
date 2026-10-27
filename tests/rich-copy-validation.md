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
