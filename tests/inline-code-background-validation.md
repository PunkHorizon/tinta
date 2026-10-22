# Inline code without a background (#278)

Validated on Windows with the Release build on 2026-10-22.

## Request

Inline code always sat on a `codebackground` box. The reporter wanted
inline spans (`AGENTS.md`, paths, commands) without a box, while fenced
code blocks keep theirs.

## Change

- New optional `themes.ini` key `inlinecodebackground=`: a colour, or `none`
  / `transparent` for no box. Missing, inline code keeps using
  `codebackground` as before. `inlinecode=` still sets the text colour.
- The span keeps its spacing without the box, so text does not move when
  the setting changes.
- The HTML export's `code{}` rule uses the colour or `background:none`; code
  blocks keep `codebackground` through `pre`.
- The Word export shades inline code with the colour (with the same light
  paper shade as code blocks for dark themes), or leaves it unshaded.
- Printing keeps "none"; a coloured inline box takes the paper palette's
  code shade like everything else.
- Themes saved from the theme editor keep the key, and the `themes.ini`
  header and example list it.

## Automated checks

`inline_code_theme` (`inlineCodeBackground`, runs last because it rewrites
the isolated `themes.ini`) loads `tests/fixtures/inline-code-background-themes.ini`:
one theme without the key, one with `none` (the reporter's colours), and a
dark one with its own colour. For each, at 1050 and 650 px, laying out
`tests/fixtures/inline-code-background-278.md` (inline code in a heading, a
paragraph, bold, a link, a list, a quote, a table and a span long enough to
wrap, plus a fenced block):

- every inline span sits on the theme's inline box, or on no box with
  `none`;
- the fenced block keeps `codebackground`;
- the HTML export's inline `code{}` rule has the colour or `none`, and
  `pre` keeps `codebackground`;
- the Word export shades the inline run with the colour (`F2F0EE` for the
  dark theme), or not at all with `none`;
- the print palette keeps `none` and replaces a colour;
- saving the themes writes `inlinecodebackground=none` and `=45475A`, and
  `none` survives reloading.

Making the helper ignore the key fails 32 checks (the boxes and the HTML
rule); dropping the Word export's use of it failed the Word checks.

## Limits

- The theme editor has no control for it; it is a `themes.ini` setting,
  like `inlinecode=`.
