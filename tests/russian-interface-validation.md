# Russian interface

Russian uses the existing registry ID `ru` and native name `Русский`.
Built-in translations are seeded before languages.ini overrides, so user
corrections remain supported. Registry indices and existing language IDs
are unchanged. `ru-RU` and other `ru-*` aliases resolve to Russian. The
existing system UI language detector recognizes the Russian registry entry.

The 355 interface keys on the original base have Russian translations in
`src/i18n_ru.inc`, along with two compatibility strings for the separately
proposed deleted-file retention setting. English remains the fallback for future keys. Word
and character counters use neutral count labels to avoid incorrect Russian
plural forms. Names of keyboard keys, file formats, fonts and YAML property
keys retain their original spelling. Tutorial and theme sample document
content is unchanged.

Previously hardcoded frontmatter type labels, side buttons, On/Off and All,
and the default custom theme name now use translation keys. The configuration
file buttons sit below their label to prevent truncation at narrow widths.
Settings sections have enough height to keep the last card above the footer.

## Verification

- Release x64 builds of `tinta`, `input_tests`, `i18n_tests`, and the standalone
  `russian_ui_tests` target verify this branch independently of retention.
- `python tests/check_russian_translations.py` verified exact coverage of
  English keys, nonempty values, uniqueness and matching printf arguments.
- Five CTest checks passed on the standalone branch: `i18n`, `russian_interface`, `ui_font_language`,
  `input_shortcuts`, `editor_features`.
- Native mouse input selected Russian from the language dropdown. Its
  stable ID round-tripped through settings.ini. Russian uses the normal
  system font fallback; existing CJK font behavior remained covered.
- All four settings sections rendered in light and dark themes at widths
  650 and 1050. Native Direct2D PNG previews were inspected, including the
  language dropdown, metadata buttons, and appearance footer.
  Outputs: `build/russian-ui-test-Release/russian-settings-*.png`.
- `tests/fixtures/russian-interface.md` was rendered through the built
  application's native print harness with isolated `language=ru` settings.
  Exit code 0; inspected its one-page PNG, including Cyrillic, a table,
  lists, emphasis, a quote, link and code. The standalone UI test also prints
  it to `build/russian-ui-test-Release/fixture-pages/`.

System-wide Windows language settings were not changed. Automatic detection
uses the existing registry prefix matching rather than a new platform path.
