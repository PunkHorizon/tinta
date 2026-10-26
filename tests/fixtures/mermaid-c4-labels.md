# C4 relationship labels

Every relation label in these **C4** diagrams should sit on its own line, in
the free space between the boxes: never on a person, system, container,
component, node or boundary, never on their text, and never on another
label. The diagram source follows the
[Mermaid C4 syntax](https://mermaid.js.org/syntax/c4.html), which is modelled
on [C4-PlantUML](https://github.com/plantuml-stdlib/C4-PlantUML).

| Diagram | Kind | What it exercises |
| --- | --- | --- |
| Tinta in context | `C4Context` | The reported layout: two labels between neighbours |
| Every relation keyword | `C4Container` | `Rel_U` to `Rel_Back`, crossing a row |
| Two-way relations | `C4Context` | `BiRel`, alone and beside a `Rel` |
| Boundaries | `C4Container` | Labels inside, outside and across frames |
| Three rows | `C4Component` | Relations that skip a row or a column |
| Busy neighbours | `C4Container` | Up to three relations between two boxes |
| Deployment | `C4Deployment` | Nested nodes |
| Opening a book | `C4Dynamic` | Long labels with technology |

## The reported diagram

> Before the fix, *Opens files in* and *Renders diagrams with* were drawn on
> top of the Tinta box and hid its description.

```mermaid
C4Context
    title Tinta in context
    Person(writer, "Writer", "Reads and edits Markdown")
    System(tinta, "Tinta", "Native Markdown viewer and editor")
    System_Ext(plantuml, "PlantUML", "Optional diagram tool")
    System_Ext(github, "GitHub Releases", "Update check for portable builds")
    Rel(writer, tinta, "Opens files in")
    Rel(tinta, plantuml, "Renders diagrams with")
    Rel(tinta, github, "Checks for updates")
```

## Direction keywords

Tinta lays C4 elements out in rows of three, in the order they are declared.
The direction in a keyword names the arrow; it does not move the boxes:

1. `Rel_U` and `Rel_Up`, `Rel_D` and `Rel_Down`
2. `Rel_L` and `Rel_Left`, `Rel_R` and `Rel_Right`
3. `Rel_Back` and `Rel_Neighbor`

```mermaid
C4Container
    title Every relation keyword
    Person(reader, "Reader", "Browses and buys books")
    Container(web, "Web shop", "TypeScript", "Catalogue, basket and checkout")
    Container(api, "Shop API", "Go", "Orders, stock and payments")
    ContainerDb(db, "Orders", "PostgreSQL", "Orders and stock levels")
    ContainerQueue(mail, "Mail queue", "RabbitMQ", "Outgoing messages")
    System_Ext(pay, "Payment provider", "Takes card payments")
    Rel_R(reader, web, "Browses")
    Rel_Right(web, api, "Places orders with", "JSON/HTTPS")
    Rel_L(api, web, "Confirms orders to")
    Rel_D(api, pay, "Charges cards with", "HTTPS")
    Rel_Down(web, mail, "Queues receipts on")
    Rel_Up(mail, reader, "Emails receipts to")
    Rel_Left(mail, db, "Reads order details from")
    Rel_Back(pay, mail, "Payment events")
    Rel_Neighbor(api, db, "Stores orders in", "SQL")
    Rel_U(db, api, "Reports stock levels to")
```

## Two-way relations

A `BiRel` has an arrow at each end. Beside another relation between the same
two boxes, each keeps its own line.

```mermaid
C4Context
    title Two-way relations
    System(shop, "Bookshop", "Sells books online")
    System_Ext(warehouse, "Warehouse", "Picks and ships orders")
    System_Ext(publisher, "Publisher feed", "New titles and prices")
    System_Ext(accounting, "Accounting", "Invoices and ledgers")
    BiRel(shop, warehouse, "Syncs stock with", "EDI")
    Rel(warehouse, shop, "Reports shipments to")
    Rel(publisher, warehouse, "Announces new titles to")
    BiRel(shop, accounting, "Exchanges invoices with")
    BiRel(accounting, warehouse, "Reconciles stock with")
```

## Boundaries

- A label may sit inside a boundary or outside it, never across its dashed
  frame or on its caption.
- Nested boundaries keep their own gaps.

```mermaid
C4Container
    title Boundaries
    Person(customer, "Customer", "Orders books online")
    System_Ext(mail, "E-mail service", "Sends receipts")
    System_Ext(pay, "Payment provider", "Takes card payments")
    System_Boundary(shop, "Bookshop") {
        Container(web, "Web shop", "TypeScript", "Catalogue and checkout")
        Container(api, "Shop API", "Go", "Orders and stock")
        Container_Boundary(data, "Data") {
            ContainerDb(db, "Orders", "PostgreSQL", "Orders and stock levels")
        }
    }
    Rel(customer, web, "Orders through", "HTTPS")
    Rel(web, api, "Calls", "JSON")
    Rel(api, db, "Stores orders in", "SQL")
    Rel(api, pay, "Charges cards with")
    Rel(api, mail, "Sends receipts through")
    Rel(mail, customer, "Emails")
```

## Three rows

```mermaid
C4Component
    title Three rows
    Container(spa, "Single-page app", "TypeScript", "Runs in the browser")
    Component(router, "Router", "Go", "Maps URLs to handlers")
    Component(auth, "Auth", "Go", "Checks sessions")
    Component(catalogue, "Catalogue", "Go", "Lists and searches books")
    Component(basket, "Basket", "Go", "Holds chosen books")
    Component(orders, "Orders", "Go", "Turns baskets into orders")
    ComponentDb(store, "Store", "PostgreSQL", "Books, baskets and orders")
    ComponentQueue(events, "Events", "NATS", "Order events")
    Rel(spa, router, "Sends requests to", "JSON/HTTPS")
    Rel(router, auth, "Checks sessions with")
    Rel(router, catalogue, "Routes searches to")
    Rel(router, basket, "Routes basket edits to")
    Rel(basket, orders, "Checks out through")
    Rel(catalogue, store, "Reads books from", "SQL")
    Rel(orders, store, "Writes orders to", "SQL")
    Rel(orders, events, "Publishes to")
    Rel(auth, events, "Logs sign-ins to")
    Rel(spa, auth, "Refreshes tokens with")
```

## Busy neighbours

**Several relations between the same two boxes** run side by side, each with
its label on its own line:

```mermaid
C4Container
    title Busy neighbours
    Person(user, "User", "Reads and writes")
    Container(spa, "Single-page app", "TypeScript", "Editor in the browser")
    Container(api, "API", "Go", "Documents and sharing")
    ContainerDb(db, "Documents", "PostgreSQL", "Text and history")
    ContainerQueue(bus, "Event bus", "Kafka", "Change events")
    Container(worker, "Indexer", "Go", "Builds search indexes")
    Rel(user, spa, "Edits in")
    Rel(spa, user, "Shows changes to")
    Rel(spa, api, "Saves through", "JSON/HTTPS")
    Rel(api, spa, "Pushes edits to", "WebSocket")
    BiRel(spa, api, "Keeps a session with")
    Rel(api, db, "Reads and writes", "SQL")
    Rel(db, api, "Notifies", "LISTEN")
    Rel(api, bus, "Publishes changes to")
    Rel(bus, worker, "Feeds")
    Rel(worker, bus, "Acknowledges")
    BiRel(worker, api, "Reports progress to")
```

## Deployment

```mermaid
C4Deployment
    title Deployment
    Deployment_Node(laptop, "Laptop", "Windows 11") {
        Deployment_Node(folder, "Portable folder", "tinta.exe") {
            Container(viewer, "Viewer", "C++17", "Draws Markdown")
            Container(settings, "Settings", "INI file", "Theme and window")
        }
        Node(java, "Java", "OpenJDK") {
            Container(plantuml, "PlantUML", "Java", "Renders diagrams")
        }
    }
    Node_R(cloud, "GitHub", "Cloud") {
        Container(releases, "Releases", "JSON API", "Latest version")
    }
    Rel(viewer, settings, "Reads", "INI")
    Rel(viewer, plantuml, "Renders through", "stdin")
    Rel(viewer, releases, "Checks for updates", "HTTPS")
```

## Opening a book

```mermaid
C4Dynamic
    title Opening a book
    ContainerDb(history, "Reading history", "SQLite", "What was read and where")
    Container(app, "Reader app", "Kotlin", "Shows the shelf")
    Container_Boundary(engine, "Book engine") {
        Component(loader, "Loader", "Kotlin", "Unpacks the file")
        Component(layout, "Layout", "Kotlin", "Pages the text")
    }
    Rel(app, loader, "Hands the chosen file to", "Intent")
    Rel(loader, layout, "Passes the unpacked chapters to")
    Rel(layout, history, "Records the last page in", "SQL")
```

The text after the diagrams still lays out:

```text
mermaid fences above: 8
```
