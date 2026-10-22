# A setting to turn off notifications (#273)

Validated on Windows with the Release build on 2026-10-22.

## Request

The bottom-right notifications (signal chips) were distracting, and
dismissing one did not stop the next, for example after copying. The
reporter asked for a persistent switch, while keeping prompts that need a
decision.

## Change

- **Settings > Appearance > Show notifications** (on by default), saved to
  `settings.ini` as `showNotifications=` the moment it is switched, and left
  as on disk at exit like *Open files in tabs*, so other windows and the
  next launch follow it.
- Off, information and success messages that drain on their own do not
  show: copy confirmations (including the pill beside a code block's Copy
  button), "Saved", status messages and the edit-mode hints. Switching it
  off also removes such messages already up or parked, so the bell leaves
  with them.
- Warnings, errors, messages with an action (retry a save, restore drafts,
  open a release) and messages that wait for an answer always show, and
  dialogs are untouched.
- `signalHint` checks the setting before it pushes, since it shapes the
  chip the push appends.
- The Appearance section is 650 DIPs tall (like Frontmatter) to hold the
  sixth row.
- The strings are translated into all eight interface languages.

## Automated checks

`notification_setting` (new, isolated portable run, `tests/notification_setting_tests.cpp`):

- on by default, a confirmation and a hint show;
- in two themes at 1050 and 650 px, the Appearance section is rendered to a
  preview PNG and its new row stays clear of the footer;
- clicking the switch through the real settings overlay turns it off,
  clears the confirmations already up, and writes `showNotifications=0`;
- off: a success, an info, a hint, an anchored Copied pill and Copy file
  path's confirmation all stay away;
- off: a warning, an error, a message with an action and one that waits for
  an answer all still show;
- clicking again turns it back on, saved, and confirmations show again.

`i18n` now expects 359 Russian strings, and
`tests/check_russian_translations.py` reports complete coverage.

Removing the filter fails the "stay away" check and the counts after it.

## Desktop check

Computer use was not available in this session. The Appearance previews
(`build/notification-setting-test-Release/appearance-*.png`) were inspected:
the row matches its neighbours, sits above the footer at both widths and in
both themes, and its hint trims with an ellipsis at 650 px like the others.
