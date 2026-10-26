# Mermaid state diagrams

Tinta draws Mermaid **state diagrams** itself, without a browser. This page
mixes them with ordinary Markdown so the text around each diagram can be
checked too. The syntax is
[Mermaid's state diagram syntax](https://mermaid.js.org/syntax/stateDiagram.html).

## A viewer's keys

A back edge from a state with neighbours, and a note beside it. The label
*Esc Esc* and the note once landed on **Searching**.

```mermaid
stateDiagram-v2
    [*] --> Reading
    Reading --> Editing : press colon
    Editing --> Reading : Esc Esc
    Editing --> Saving : Ctrl+S
    Saving --> Editing
    Reading --> Searching : F
    Searching --> Reading : Esc
    Reading --> [*] : Q
    note right of Editing : Source on the left, preview on the right
```

| Key | From | To |
| --- | --- | --- |
| `:` | Reading | Editing |
| `Esc` `Esc` | Editing | Reading |
| `Ctrl+S` | Editing | Saving |
| `F` / `Esc` | Reading / Searching | Searching / Reading |

The same machine left to right. A note *right of* a state sits below it in
horizontal diagrams, where Mermaid puts it:

```mermaid
stateDiagram-v2
    direction LR
    [*] --> Reading
    Reading --> Editing : press colon
    Editing --> Reading : Esc Esc
    Editing --> Saving : Ctrl+S
    Saving --> Editing
    Reading --> Searching : F
    Searching --> Reading : Esc
    Reading --> [*] : Q
    note right of Editing : Source on the left, preview on the right
```

And bottom to top, with the note on the left:

```mermaid
stateDiagram-v2
    direction BT
    [*] --> Reading
    Reading --> Editing : press colon
    Editing --> Reading : Esc Esc
    Editing --> Saving : Ctrl+S
    Saving --> Editing
    Reading --> Searching : F
    Searching --> Reading : Esc
    Reading --> [*] : Q
    note left of Editing : Source on the left, preview on the right
```

## A player

> A transition to the same state loops beside it, and its label sits
> beyond the loop. Notes on either side keep their room.

```mermaid
stateDiagram-v2
    [*] --> Stopped
    Stopped --> Playing : play
    Playing --> Paused : pause
    Paused --> Playing : resume
    Playing --> Stopped : stop
    Paused --> Stopped : stop
    Playing --> Playing : next track
    Stopped --> [*] : eject
    note left of Paused : Position is kept
    note right of Playing : Volume keys work here
```

1. Edges to the next state down leave and enter their states at their own
   points, so each label has a line of its own.
2. Edges back up, and edges that skip states, run in a lane beside every
   state they pass.
3. A label goes where it covers no state, note or other label.

## An order

Choices, forks and joins, a long label, and a return over five states:

```mermaid
stateDiagram-v2
    state check <<choice>>
    state split <<fork>>
    state merge <<join>>
    [*] --> Received
    Received --> check : validate
    check --> Rejected : invalid
    check --> split : valid
    split --> Packing
    split --> Billing
    Packing --> merge
    Billing --> merge
    merge --> Shipped
    Shipped --> Delivered : signed for
    Shipped --> Received : returned to sender
    Rejected --> [*]
    Delivered --> [*]
    Received --> Cancelled : customer cancels before packing
    Cancelled --> [*]
```

A connection, right to left, with a retry loop and notes on both sides:

```mermaid
stateDiagram-v2
    direction RL
    [*] --> Idle
    Idle --> Connecting : dial
    Connecting --> Connected : handshake ok
    Connecting --> Idle : timeout
    Connecting --> Connecting : retry
    Connected --> Idle : hang up
    Connected --> Error : lost carrier
    Error --> Connecting : redial
    Error --> Idle : give up
    Idle --> [*]
    note right of Connecting : Three tries at most
    note left of Connected : Keep-alive every 30 s
```

## Composite states

A composite state draws as a box with its name over its own diagram, which
has its own start and end. Transitions to the composite end on its border:

```mermaid
stateDiagram-v2
    [*] --> Reading
    Reading --> Editing : press colon
    Editing --> Reading : Esc Esc
    state Editing {
        [*] --> Typing
        Typing --> Selecting : Shift+arrows
        Selecting --> Typing
    }
    Reading --> [*] : Q
```

Left to right, with a long name, its own `direction`, notes inside and on
it, a loop on it, and transitions between states **outside** and
*inside* it, which end on its border:

```mermaid
stateDiagram-v2
    direction LR
    [*] --> Idle
    state "Text editing" as Editing {
        direction LR
        [*] --> Typing
        Typing --> Selecting : Shift+arrows
        Selecting --> Typing : any key
        Typing --> [*] : Esc
        note right of Typing : Caret visible
    }
    Idle --> Typing : open file
    Selecting --> Idle : close
    Editing --> Saving : Ctrl+S
    Saving --> Editing
    Editing --> Editing : undo
    note left of Editing : Edit mode
    Saving --> [*]
```

> Regions separated by `--` run at the same time. Each is laid out on its
> own, and dashed lines part them.

```mermaid
stateDiagram-v2
    [*] --> Active
    state Active {
        [*] --> NumLockOff
        NumLockOff --> NumLockOn : EvNumLockPressed
        NumLockOn --> NumLockOff : EvNumLockPressed
        --
        [*] --> CapsLockOff
        CapsLockOff --> CapsLockOn : EvCapsLockPressed
        CapsLockOn --> CapsLockOff : EvCapsLockPressed
        --
        [*] --> ScrollLockOff
        ScrollLockOff --> ScrollLockOn : EvScrollLockPressed
        ScrollLockOn --> ScrollLockOff : EvScrollLockPressed
    }
    Active --> [*] : power off
```

Composites nest, each with its own direction:

```mermaid
stateDiagram-v2
    direction LR
    [*] --> Document
    state Document {
        [*] --> Draft
        Draft --> Review : submit
        state Review {
            direction LR
            [*] --> Reading
            Reading --> Commenting : select text
            Commenting --> Reading : post
        }
        Review --> Draft : changes requested
        Review --> [*] : approved
    }
    Document --> Published : publish
    Published --> [*]
```

| Syntax | Draws |
| --- | --- |
| `state Id { ... }` | a box named Id around its own diagram |
| `state "Long name" as Id { ... }` | the same, named Long name |
| `[*]` inside the braces | the composite's own start or end |
| `--` inside the braces | a dashed line between concurrent regions |

## Class and ER diagrams

Class and entity-relationship diagrams share the edge routing:

```mermaid
classDiagram
    Animal <|-- Duck
    Animal <|-- Fish
    Animal <|-- Zebra
    Animal : +int age
    Animal : +isMammal()
    Duck : +swim()
    Fish : +swim()
    Zebra : +run()
    Duck "1" --> "*" Pond : lives in
    Pond --> Duck : feeds
    Zebra --> Zebra : herds with
```

```mermaid
erDiagram
    CUSTOMER ||--o{ ORDER : places
    CUSTOMER ||--o{ ADDRESS : has
    ORDER ||--|{ LINE-ITEM : contains
    LINE-ITEM }o--|| PRODUCT : describes
    CUSTOMER {
        string name PK
        string email
    }
```

The source of the first diagram, as a plain code block:

```text
stateDiagram-v2
    Editing --> Reading : Esc Esc
    note right of Editing : Source on the left, preview on the right
```

- Text after the diagrams still lays out below them.
- ~~Overlapping labels~~ are gone.
