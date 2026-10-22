# Temporary document: восстановление

This **saved copy** remains readable after its original is deleted.

Enable **Settings > General > Keep deleted files in memory** first.
This option increases RAM usage and keeps copies only until Tinta closes.

| Item | Result |
| --- | --- |
| Temporary Markdown | Available in its tab |

- Switch to another tab.
- Delete this file, then return here.
- Edit the recovered text, then press Ctrl+S: Save As must open with the old name suggested.
- Cancel: the original stays deleted and the edited text stays available.
- Press Ctrl+S again and confirm a destination to recover the document.

> Keep the surrounding content as well as the heading.

[Local link](deleted-tab.md) and `inline code`.

```text
Last readable version
```

## Closing beside a deleted tab with retention disabled

Turn **Keep deleted files in memory** off. Open copies of this document in
two tabs, delete the inactive copy from disk, then close the active tab.
Tinta must show its start page with a valid active tab; it must not show the
closed document under the missing tab's name. Repeat with the tab order
reversed. If a third readable tab exists, it must be selected instead.

## Other actions after deletion

With retention off, try history navigation and clicking/dragging the missing
tab: neither must act on the live document under the missing file's identity.
Recover a draft for the missing path: the live tab must remain unchanged.
Use Close Others on a missing tab: the sweep must stop safely, without hanging.
With retention on, save a missing document before dragging it to another window;
path-only transfers must never discard its only in-memory copy.

Ctrl+Tab and reverse cycling must skip unavailable tabs when another readable tab exists.
