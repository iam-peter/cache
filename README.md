# Note Canvas

A note-taking app built with Qt 6 and QML. Each project is an infinite canvas
of Markdown notes with images and videos embedded in them. A sidebar lists
your projects and the notes on the current canvas. The look comes from
[igitt](../igitt): its Qt Quick Controls style, themes, fonts and icons.

## Features

- **Infinite canvas**: zoom with the wheel or a pinch, pan with `Space`+drag,
  the middle button or the hand tool, fit everything with `Ctrl+1`. A
  minimap in the corner shows where you are.
- **Tools, as in FigJam** (along the bottom): select, pan, note, text,
  code snippet, shapes (rectangle, ellipse, diamond, triangle), section,
  connector, pen and highlighter, each with a one-letter key (`F1`).
- **Selecting and arranging**: drag on empty space to select several
  (Shift adds), `Ctrl+A` for all. Move them together with smart guides
  that snap to other items' edges and centres (Alt moves freely); align
  and space them evenly, colour, copy, duplicate or delete them at once.
- **Markdown notes**: double-click a note to edit it, `Esc` to finish.
  Notes render headings, lists, tables, links, images, videos and code,
  and grow to fit when you finish editing. Each note is a plain `.md`
  file.
- **Checklists**: `- [ ]` tasks are boxes to tick right in the note. The
  sidebar's **Open tasks** tab lists every unticked task of every project
  and ticks them there too.
- **Code snippets**: fenced code blocks (`K` for a new snippet,
  `Ctrl+Shift+K` while editing) are syntax-highlighted in the theme's
  colours, with a button to copy the code. The language comes from the
  fence (```` ```python ````) or is guessed.
- **Connectors**: drag from one of a selected item's side handles (or with
  the connector tool) onto another item to connect them, or onto empty
  space for a new connected note. Connectors follow their items and can
  be curved, straight or elbowed, have arrows at either end or none, be
  dashed, coloured and labelled (double-click one).
- **Mindmaps**: with a note selected, `Tab` adds a child and `Enter` a
  sibling, ready to type; `Alt`+arrows walk the branches. Fold a branch
  into its root with the badge on its edge, and **Tidy** lays a branch out
  as a tree.
- **Wiki links**: type `[[` in a note for a list of notes of all projects;
  `[[Title]]` links one (`[[Project/Title]]` in another project).
  Hovering a link shows a preview of the note, clicking opens it, and a
  link to a note that doesn't exist yet makes it. A link on a line of its
  own shows a **preview** of the linked note, `![[Title]]`, which its
  chevron hides and shows again. Notes show how many notes link to them;
  links between notes of a canvas can be drawn as dotted lines.
- **Sections**: labelled areas (`S`) that take along what lies in them when
  moved. They are the chapters of the sidebar's note list.
- **Shapes, text and pen**: shapes with labels for flowcharts, free text in
  four sizes, pen and highlighter lines, all in the theme's colours.
- **Templates**: Kanban board, retrospective, meeting notes, pros and
  cons, mindmap, flowchart and brainstorm; save any selection as a template
  of your own (kept in the workspace's `.templates` folder).
- **Images and videos live inside notes**, written as `![name](assets/…)`:
  - drop or paste (`Ctrl+V`) on empty canvas to make a new note from them,
    onto a note (or with a note selected) to add them to it, or while
    editing to insert them at the cursor
  - **Image / video…** in the toolbar, or the image button on a note,
    picks files
  - images are scaled to the note's width, and videos play inside the note
    (expand for a large player)
- **For Teams, mail and documents**:
  - **Copy note** (`Ctrl+C`, or `Ctrl+Shift+C` while editing) puts a note on
    the clipboard as rich text with its images embedded, code as coloured
    blocks and tasks as ☐ and ☑, plus the Markdown for plain-text editors.
    A note that is only one image is also copied as that image; videos
    become a link to the file.
  - `Ctrl+Shift+C` (or the selection toolbar) copies the selection as a
    picture, at twice the screen's resolution, without selection marks.
  - **⋯ → Export canvas as PNG / PDF** saves the whole canvas (or the
    selection) as a picture or a one-page PDF; **Copy canvas as picture**
    puts it on the clipboard.
- **History**: the workspace folder becomes a git repository of its own (also
  when it lies inside another one), and a little while after changes a
  version of the project is committed. **⋯ → History** (`Ctrl+Shift+H`)
  restores any earlier version; the state before the restore stays a
  version too. Needs git; turn it off in Settings.
- **Project sidebar**: create, rename and trash projects, and see every
  note on the canvas, section by section, in reading order.
- **Full-text search** across all projects: the box at the top of the
  sidebar (`Ctrl+F`) searches every note in every project of the
  workspace, not just the open one. Type words or `"quoted phrases"`; a
  note matches when it has all of them, ignoring case and accents
  (`cafe` finds "Café"). While searching, the sidebar shows the results,
  grouped by project with the open one first, each with a snippet and
  the matches in bold. `↑`/`↓` and `Enter`, or a click, open the note,
  switching project if needed. Matches are highlighted inside the notes,
  and results update as you edit.
- **Typo-tolerant**: words also match with a typo or two, a swapped
  pair of letters counting as one (`deplyo stagign` finds "deploy" and
  "staging"), and while still half-typed (`rolbac` finds "rollback").
  Words of up to 3 letters must match exactly, 4–6 letters may have one
  typo, longer ones two; phrases match exactly. Exact matches rank above
  typo matches.
- **igitt's themes**: Light, Dark, Dracula, Alucard and the four Catppuccin
  flavours, or System. The canvas, note tints and controls all follow the
  theme. Your own themes go in `~/.config/Note Canvas/themes` (format:
  `style/` and igitt's `docs/THEMES.md`).
- Colours, duplicate, copy and paste between canvases, bring to front or
  send to back, resize handles, arrow-key nudging, undo and redo of
  everything, and export of a whole canvas to one Markdown file.

Press `F1` for all shortcuts and `Ctrl+,` for settings.

## Storage

The default workspace is `~/Documents/Note Canvas`; change it from the
sidebar footer. Each project is a plain folder:

```
My Project/
├── canvas.json      every item (notes' place, sections, shapes, text, pen
│                    lines), the connectors, and the last view
├── notes/<id>.md    one Markdown file per note
└── assets/          images and videos the notes embed (copied in on import)
```

Changes save automatically. Notes stay readable in any Markdown editor, and
`.md` files you put into `notes/` yourself show up on the canvas the next
time the project opens. Media cards from earlier versions are turned into
notes holding that media.

## Building

Needs Qt 6.9 or newer (developed against 6.12) with Qt Quick, Quick
Controls 2 and Qt Multimedia, plus CMake 3.21+ and a C++20 compiler.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/gcc_64
```

```bash
cmake --build build
```

```bash
./build/notecanvas
```

The search's matching, the Markdown and code helpers, the board model and
the history have unit tests:

```bash
ctest --test-dir build --output-on-failure
```

## Layout

| Path | Purpose |
|---|---|
| `style/` | igitt's style module (`IgittStyle`), unchanged from igitt `0238e3b` |
| `fonts/`, `icons/` | Inter and JetBrains Mono (OFL) and Lucide icons (ISC), as in igitt |
| `src/boardmodel.*` | Items and connectors: load/save, undo, media, rendering, rich copy |
| `src/boardops.cpp` | Several items at once, connectors, mindmaps, sections, templates, export |
| `src/markdown.*` | Tasks, `[[wiki links]]` and code fences in a note's Markdown |
| `src/highlight.*` | Syntax highlighting of code blocks |
| `src/history.*` | Versions of the projects, with git |
| `src/workspace.*` | Project folders in the workspace |
| `src/documentfilter.*` | Reading-order sort of the sidebar's note list |
| `src/searchmodel.*` | Search over all projects; links, backlinks and tasks across them (`Search` in QML) |
| `src/fuzzy.*` | Typo-tolerant matching: accent folding, edit distance |
| `tests/` | Tests of the matching, Markdown and code helpers, board model and history |
| `templates/` | The built-in templates |
| `src/gridbackground.*` | Scene-graph dot grid |
| `src/Fonts.*` | Registers the bundled fonts (from igitt) |
| `qml/Main.qml` | Window, toolbar, shortcuts |
| `qml/Theme.qml` | Canvas colours made from the theme's roles, and preferences |
| `qml/CanvasView.qml` | Pan/zoom, tools, selection, moving with guides, connecting, keys, export |
| `qml/CanvasCard.qml` | Any item: frame, move, resize, handles, folding, floating toolbar |
| `qml/NoteBody.qml` | A rendered note and its editor, with `[[` completion |
| `qml/*Body.qml` | Shapes, text, sections and pen lines |
| `qml/CodeBlock.qml`, `EmbedBlock.qml`, `VideoBlock.qml` | Code, link previews and videos in notes |
| `qml/Connector.qml` | A connector, its route and arrows |
| `qml/ToolPalette.qml`, `Minimap.qml`, `SelectionBar.qml`, `LinkBar.qml` | Tools, map, toolbars for selections and connectors |
| `qml/TemplateDialog.qml`, `HistoryDialog.qml` | Templates and versions |
| `qml/Sidebar.qml`, `SettingsDialog.qml` | Projects and notes; theme, fonts, canvas settings |
| `qml/Icon*.qml`, `SplitterHandle.qml` | Helpers from igitt |

To update the style, copy igitt's `src/style` over `style/`.
