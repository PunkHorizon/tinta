# Help leaves the TOC and tab cards alone (#270)

Validated on Windows with the Release build on 2026-10-22.

## Problem

After Help was opened, the contents (TOC) entries were centred and the
folder path in a tab's hover card was out of place. Help centred its title
and footer by setting the alignment of `app.themeTitleFormat` and
`app.tocFormat`, formats it shares with the TOC, the search results panel
and the tab hover card, and never set it back. The reporter traced it to
the footer.

## Change

Help's title and footer are centred on their own text layouts
(`drawCenteredText`, `src/overlays.cpp`); the shared formats keep their
alignment.

## Automated checks

`tab_hover_card` (`helpLeavesSharedFormats`): in two themes at 100 % and
150 %, Help is painted, then the alignment of `tocFormat`, `tocFormatBold`,
`themeTitleFormat` and `folderBrowserFormat` must be what it was, and a row
laid out in the TOC format must still start at the left edge, as it did
before Help. Putting the old footer line back fails both checks in all four
configurations.

`tests/fixtures/help-alignment-270.md` is a short mixed document for
checking the TOC and a tab card by hand.

## Desktop check

Computer use was not available in this session, so the window was not
operated by hand; the check above paints Help through its real renderer in
a hidden window.
