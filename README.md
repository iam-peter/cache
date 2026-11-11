# Note Canvas

A note-taking app built with Qt 6 and QML. Each project is an infinite canvas
of Markdown notes with images and videos embedded in them. A sidebar lists
your projects and the notes on the current canvas. The look comes from
[igitt](../igitt): its Qt Quick Controls style, themes, fonts and icons.

## Features

- **Infinite canvas**: pan by dragging empty space (middle button pans
  anywhere), zoom with the wheel or a pinch, fit everything with `Ctrl+1`.
- **Markdown notes**: double-click a note to edit it, `Esc` to finish.
  Notes render headings, lists, tables, code, links, images and videos.
  Each note is a plain `.md` file.
- **Images and videos live inside notes**, written as `![name](assets/…)`:
  - drop or paste (`Ctrl+V`) on empty canvas to make a new note from them,
    onto a note (or with a note selected) to add them to it, or while
    editing to insert them at the cursor
  - **Image / video…** in the toolbar, or the image button on a note,
    picks files
  - images are scaled to the note's width, and videos play inside the note
    (expand for a large player)
- **Copy a note into Teams, mail or a document**: **Copy note** (`Ctrl+C`,
  or `Ctrl+Shift+C` while editing) puts the note on the clipboard as rich
  text, with its images embedded, plus the Markdown for plain-text editors.
  A note that is only one image is also copied as that image. Videos become
  a link to the file, as chat apps can't take an embedded local video.
- **Project sidebar**: create, rename and trash projects, and see every
  note on the canvas in reading order.
- **Full-text search** (`Ctrl+F`) over every note in every project. Type
  words or `"quoted phrases"`; a note matches when it has all of them,
  ignoring case. Results are grouped by project, the open one first, and
  each shows a snippet with the matches in bold. `↑`/`↓` and `Enter`, or
  a click, open the note, switching project if needed. While a search is
  on, matches are highlighted inside the notes, and results update as
  you edit.
- **igitt's themes**: Light, Dark, Dracula, Alucard and the four Catppuccin
  flavours, or System. The canvas, note tints and controls all follow the
  theme. Your own themes go in `~/.config/Note Canvas/themes` (format:
  `style/` and igitt's `docs/THEMES.md`).
- Note colours, duplicate, bring to front or send to back, resize handle,
  arrow-key nudging, undo and redo, and export of a whole canvas to one
  Markdown file.

Press `F1` for all shortcuts and `Ctrl+,` for settings.

## Storage

The default workspace is `~/Documents/Note Canvas`; change it from the
sidebar footer. Each project is a plain folder:

```
My Project/
├── canvas.json      position, size and colour of every note, plus the last view
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

## Layout

| Path | Purpose |
|---|---|
| `style/` | igitt's style module (`IgittStyle`), unchanged from igitt `0238e3b` |
| `fonts/`, `icons/` | Inter and JetBrains Mono (OFL) and Lucide icons (ISC), as in igitt |
| `src/boardmodel.*` | Notes: load/save, undo, media import, rendering, rich copy |
| `src/workspace.*` | Project folders in the workspace |
| `src/documentfilter.*` | Reading-order sort of the sidebar's note list |
| `src/searchmodel.*` | Full-text search over all projects (`Search` in QML) |
| `src/gridbackground.*` | Scene-graph dot grid |
| `src/Fonts.*` | Registers the bundled fonts (from igitt) |
| `qml/Main.qml` | Window, toolbar, shortcuts |
| `qml/Theme.qml` | Canvas colours made from the theme's roles, and preferences |
| `qml/CanvasView.qml` | Pan/zoom, selection, where pasted and dropped media goes |
| `qml/CanvasCard.qml` | Note frame, move, resize, floating toolbar |
| `qml/NoteBody.qml`, `VideoBlock.qml` | Rendered note with inline videos; the editor |
| `qml/Sidebar.qml`, `SettingsDialog.qml` | Projects and notes; theme, fonts, canvas settings |
| `qml/Icon*.qml`, `SplitterHandle.qml` | Helpers from igitt |

To update the style, copy igitt's `src/style` over `style/`.
