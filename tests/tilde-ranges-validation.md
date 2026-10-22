# Tilde ranges are not subscripts (#277)

Validated on Windows with the Release build on 2026-10-22.

## Problem

Chinese, Japanese and Korean text writes a range with a single tilde:
`50~100mm`, `周一~周五`. Tinta reads `~text~` as a subscript (with Typora's
rule that the span holds no whitespace), and CJK text often has no space
between two ranges, so `长度50~100mm，宽度20~30mm` drew `100mm，宽度20` as a
subscript. The reporter suggested making `~~` the subscript marker, but
`~~text~~` is GitHub's strikethrough and stays that.

## Change

A single tilde with a digit or a CJK letter (Han, kana, Hangul) on both
sides is a range: it neither opens nor closes a subscript
(`isRangeTilde`, `src/markdown.cpp`). A Latin letter sits beside each
tilde of `H~2~O`, `CO~2~`, `x~i~` and `a~n-1~`, so those stay subscripts,
also next to a range. `^sup^` is unchanged (`2^10^` stays a superscript),
and so are `~~strikethrough~~` and an escaped `\~`, which was already a
plain tilde.

## Automated checks

`tilde_ranges` (new, `tests/script_span_tests.cpp`) compares the parsed
tree for: digit ranges, CJK day ranges, chapter ranges, signed ranges with
units, subscripts alone and beside ranges (in either order), superscripts,
a strikethrough beside a range, and escaped tildes. It also parses
`tests/fixtures/tilde-ranges-277.md` (headings, a list with bold and a
link, a quote, a table, a fenced block and a struck-through range) and
counts its four subscripts and one strikethrough.

Before the change 6 checks failed (each paired range became a subscript,
and `3~5天后测H~2~O` lost its real subscript to the range before it).

## Limits

- A tilde between Latin letters still pairs (`A~B级和C~D级`); escape it
  as `\~` to keep it.
- A subscript written between two digits or two CJK letters (`10~3~`) now
  stays text; such subscripts are rare.

## Desktop check

Computer use was not available in this session; the suite checks the
parsed structure the renderer draws from.
