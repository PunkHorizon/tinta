# Inline code without a box: `inline_heading` (#278)

With `inlinecodebackground=none` in a theme, inline spans such as
`inline_one` and `inline_two` keep their colour and spacing but lose the box;
fenced code below keeps `codebackground`. **Bold `inline_bold`** and a
[`linked_code`](https://example.com) span follow the same rule.

- A list item with `inline_list` in it.
- A long span that wraps in a narrow window: `inline_wrap_starts_here_and_keeps_going_past_the_right_edge_of_a_narrow_window_on_purpose`.

> A quote with `inline_quote` and $a+b=c$.

| Setting | Effect |
| --- | --- |
| `inline_table` | inline spans only |

```text
block_plain
```

Use the accompanying `inline-code-background-themes.ini` in an isolated
portable test folder: one theme inherits `codebackground`, one sets `none`
and one sets its own colour.
