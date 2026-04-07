# TTB2 — Terminal Table Builder 2.0

![logo](assets/ttb2_img.png)

TTB2 is the new and improved 2.0 version of the terminal table builder. It features a clean ncurses UI that displays table data live, list-based menus, autosave, paging for wide and tall tables, an inline search mode, and interactive row/column deletion. CSV and XLSX import/export support is built in, with simple PDF export for sharing table snapshots.

All of the features you're used to, just improved.

## Install

### Debian/Ubuntu package

Go to the [latest TTB2 release](https://github.com/canadaluke888/ttb2/releases/latest) and download the `.deb` file.

Install the downloaded package:
```bash
sudo apt install ./ttb2_VERSION_ARCH.deb
```

Replace `ttb2_VERSION_ARCH.deb` with the name of the file you downloaded.

### Build from source

Install dependencies (Debian/Ubuntu packages):
- GCC/Clang (`build-essential`) 
  `sudo apt install build-essential`
- ncurses (wide-char) (`libncursesw5-dev`) 
  `sudo apt install libncursesw5-dev`
- SQLite3 (`libsqlite3-dev`) 
  `sudo apt install libsqlite3-dev`
- JSON-C (`libjson-c-dev`) 
  `sudo apt install libjson-c-dev`
- zlib (`zlib1g-dev`)
  `sudo apt install zlib1g-dev`

Build the native binary:
```
make
```
This produces `./build/ttb2`.

Install to your `PATH`:
```
sudo make install
```

Override the install prefix if needed:
```
make install PREFIX=/usr
make install DESTDIR=/tmp/package-root
```

Clean:
```
make clean
```

## Usage

```bash
ttb2
ttb2 path/to/data.csv
ttb2 path/to/data.xlsx
ttb2 path/to/table.ttbl
ttb2 path/to/book.ttbx
```

Supported startup inputs:
- `.csv`
- `.xlsx`
- `.ttbl`
- `.ttbx` directory books

## Highlights
- Interactive table editing (add columns/rows; rename columns; change types; edit cells)
- Fast load and render times; smooth scrolling even on large tables
- Low‑RAM seek paging (SQLite-backed, streaming windows; no OFFSET) — toggle in Settings
- Row‑number gutter (centered, toggle in Settings)
- Data types: int, float, str, bool (color‑coded)
- Column paging with ←/→ and footer hints
- Row paging with ↑/↓
- Search mode: press F to search; navigate matches with ←/→/↑/↓; Esc exits; exact substring highlight inside the selected cell
- Edit mode tools: [x] Delete Row, [Shift+X] Delete Column (guarded), [Backspace] Clear Cell
- Workspace auto-save to `.ttbx` projects (toggle via Settings, manual save with `S`)
- Table Manager: Easily switch between tables within a book, rename, and delete tables.
- Settings modal (saved to `settings/settings.json`; includes core toggles and editor color options)
- Exports: native CSV, XLSX, and PDF save options (no external runtime required)

## Screenshots

### Main UI

![main_table](assets/main_ui.png)

### Table Menu

![table_menu](assets/menu.png)


## Editor Keybindings
- `c` Add column
- `r` Add row
- `e` Edit mode (arrows to navigate, Enter to edit, Esc to exit)
- `f` Search mode (arrows to jump matches, Esc to exit)
- `S` Save workspace project
- `Ctrl+H` Jump to top‑left (Home)
- `m` Table menu (Rename, Save, Load, New Table, DB Manager, Settings)
- `q` Quit

- In Edit mode:
  - `Enter` Edit cell
  - `F` Search mode
  - `x` Delete row (interactive; Enter confirms)
  - `Shift+X` Delete column (interactive; Enter confirms)
  - `Backspace` Clear cell (with confirmation)
  - `Ctrl+H` Jump to top‑left (Home)
  - `Esc` Back

## Performance / Low‑RAM Mode
- Enable “Low‑RAM seek paging” in Settings to browse large datasets without loading everything into memory.
- Rows are fetched in small windows and rendered incrementally to keep memory and latency stable.
- On Windows terminals, flicker is minimized by double‑buffered updates and scanning only visible rows for column widths.

## Workspace & Exports
- A project workspace lives in `workspace/session.ttbx` by default. The file is created automatically and updated whenever autosave triggers or you press `S`.
- Use the Export menu to write the current data as:
  - `.ttbl` – a single-table snapshot
  - `.ttbx` – a project/workbook bundle
  - `.csv`, `.xlsx`, or `.pdf`
- Export now lets you browse to a destination directory in-app before entering the output filename.

## Runtime Files
- Settings are stored in `settings/settings.json`.
- The active workspace session book is stored in `workspace/session.ttbx` while the app is running.

## CI
GitHub Actions installs build deps and runs a simple `make`; build logs are visible in the job output.

## Author
Luke Canada (<canadaluke888@gmail.com>)
