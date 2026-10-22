# End tabs keep their halo (#271)

Validated on Windows with the Release build on 2026-10-22.

## Problem

The first and last tabs looked cut off at their outer edges. The active tab
drops a soft halo up to 6.6 DIPs past its sides onto its neighbours, but the
tab strip's clip ended exactly at the row's first and last tab, so for an
active end tab the halo (and the anti-aliased edge of the tab's body) was
cut away on its outer side and showed only on the inner one.

## Change

The strip's clip reaches out by the halo's width plus a pixel on both sides
(`renderTabStrip`, `src/tabs.cpp`). Tab labels cannot follow it: they are
trimmed with an ellipsis inside their tab. The reach stops short of the app
icon's glyph on the left (the icon cell is 40 DIPs wide, its glyph centred)
and of the + glyph on the right, and in edit mode it stays inside the 8 DIPs
between the tool rail and the first tab.

## Automated checks

`tab_hover_card` (`endTabsKeepTheirHalo`): the strip is drawn into a bitmap
through the window target's own device context and read back, in two themes
at 100 % and 150 %, with the first and then the last of four fixture tabs
active. Averaged over the pixels 2–5 DIPs outside each edge:

- the outer side must be darker than the same pixels drawn while a middle
  tab is active (the halo is there);
- the outer side must be as dark as the inner side, within 2 levels.

With the old clip both checks fail for both tabs in all four configurations
(light theme: 230 outside against 215 inside, the same 230 as without a
halo).

## Desktop check

Computer use was not available in this session, so the window was not
inspected by eye; the check reads back the strip exactly as its renderer
draws it.
