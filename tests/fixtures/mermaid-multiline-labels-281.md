# Mermaid labels over several lines (#281)

Mermaid lets a **double-quoted** label run over lines; each line break is a
line of the label. The diagram below is the one from the report: front
matter, a `%%` comment, circle nodes with `-` in their IDs, loops, and edge
labels of up to four lines. See [Mermaid's flowchart syntax](https://mermaid.js.org/syntax/flowchart.html).

```mermaid
---
title: State Machine
---
%% stateDiagram-v2
graph TB

	START((START)) -- "Nop" --> MSH((MSH))
	MSH((MSH)) -- "Push:PERSONS" --> PID((PID))
	PID((PID)) -- "Push:ORDERS
Push:LABORDERS" --> ORC((ORC))
	PID((PID)) -- "Nop" --> PV1((PV1))
	PID((PID)) -- "Nop" --> PID-NTE((PID-NTE))
	PID-NTE((PID-NTE)) -- "Nop" --> PID-NTE((PID-NTE))
	PID-NTE((PID-NTE)) -- "Push:ORDERS
Push:LABORDERS" --> ORC((ORC))
	PID-NTE((PID-NTE)) -- "Nop" --> PV1((PV1))
	PV1((PV1)) -- "Push:ORDERS
Push:LABORDERS" --> ORC((ORC))
	ORC((ORC)) -- "Push:LABORDERPANELS" --> OBR((OBR))
	ORC((ORC)) -- "Nop" --> ORC-NTE((ORC-NTE))
	ORC-NTE((ORC-NTE)) -- "Nop" --> ORC-NTE((ORC-NTE))
	ORC-NTE((ORC-NTE)) -- "Push:LABORDERPANELS" --> OBR((OBR))
	OBR((OBR)) -- "Push:LABORDERPANELRESULTS" --> OBX((OBX))
	OBR((OBR)) -- "Nop" --> OBR-NTE((OBR-NTE))
	OBR-NTE((OBR-NTE)) -- "Nop" --> OBR-NTE((OBR-NTE))
	OBR-NTE((OBR-NTE)) -- "Push:LABORDERPANELRESULTS" --> OBX((OBX))
	OBX((OBX)) -- "Pop
Pop
Pop
Pop" --> MSH((MSH))
	OBX((OBX)) -- "Pop
Pop
Push:LABORDERPANELS" --> OBR((OBR))
	OBX((OBX)) -- "Pop
Pop
Pop
Pop" --> FINISH((FINISH))
	OBX((OBX)) -- "Nop" --> OBX-NTE((OBX-NTE))
	OBX-NTE((OBX-NTE)) -- "Nop" --> OBX-NTE((OBX-NTE))
```

| Form | Source | Label |
| --- | --- | --- |
| Edge text | `-- "Nop" -->` | Nop, without the quotes |
| Pipe text | `-->\|"Quoted"\|` | Quoted |
| Over lines | a line break inside the quotes | one line of the label per source line |

A smaller diagram with a quoted node label over two lines and an arrow
inside quoted edge text:

```mermaid
flowchart LR
    A["first line
       second line"] -->|"Quoted"| B(Rounded)
    B -- "an --> inside" --> C{Choice}
    C -- plain text --> A
```

> Single quotes are not Mermaid strings, so an apostrophe in a label such as
> `A[Don't stop]` still leaves the next line alone.

- Lines of a label are trimmed: the source's indentation is not part of it.
- Error lines after a label that ran over lines still count from the top.
