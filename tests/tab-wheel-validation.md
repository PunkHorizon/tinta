# Mouse wheel over the tab strip switches tabs (#272)

Validated on Windows with the Release build on 2026-10-22.

## Request

Turning the wheel over the tab strip should switch to the previous or next
tab, wrapping at both ends, without changing the wheel anywhere else.

## Change

Over the tab row (from the app icon to the caption buttons, so over a tab or
the strip's empty stretch) with at least two tabs:

- a notch down activates the next tab and a notch up the previous one,
  wrapping from the last to the first and back (`tabCycle`, which also skips
  a tab that cannot be activated);
- a high-resolution wheel or touchpad's small deltas add up to a notch per
  tab; a 400 ms pause or a change of direction starts the count afresh, so
  stray fractions never switch a tab later;
- Ctrl+wheel keeps zooming, and with Help, the tab switcher or one tab only
  the wheel keeps its old behaviour;
- a turn that closes an open tab or context menu does only that.

## Automated checks

`tab_hover_card` (`wheelSwitchesTabs`), with the suite's four fixture tabs:
notches down step through every tab and wrap to the first, notches up wrap
to the last and step back, both over a tab and over the strip's empty
stretch; three quarter notches switch nothing and the fourth moves one tab;
half notches on either side of a pause switch nothing; Ctrl+wheel zooms
without switching; the wheel over the document leaves the tabs alone; a
turn that closes the tab menu switches nothing. The suite's existing check
that a wheel turn puts the hover card away still passes.

With the strip branch disabled, 11 checks fail.

## Desktop check

Computer use was not available in this session; the checks drive the real
wheel handler with the real tab strip's geometry in a hidden window.
