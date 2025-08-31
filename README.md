# TTB2 — Terminal Table Builder 2.0

TTB2 is the new and improved 2.0 version of the terminal table builder. It features a clean ncurses UI that displays table data live, menus have been remodeled into a list based selection system rather than a command based system, an integrated SQLite database manager, autosave, paging for wide tables, a search results modal, and a simple JSON-backed settings system.

All of the features you're used to, just improved.

## Build

Dependencies (Debian/Ubuntu packages):
- GCC/Clang (`build-essential`) 
  `sudo apt install build-essential`
- ncurses (wide-char) (`libncursesw5-dev`) 
  `sudo apt install libncursesw5-dev`
- SQLite3 (`libsqlite3-dev`) 
  `sudo apt install libsqlite3-dev`
- JSON-C (`libjson-c-dev`) 
  `sudo apt install libjson-c`
- Python 3 headers (`python3-dev`) — required for export feature 
  `sudo apt install python3-dev`

Build the native binary:
```
make
```
This produces `./build/tablecraft`.

Clean:
```
make clean
```

Run locally:
```
./build/tablecraft
```

## Highlights
- Interactive table editing (add columns/rows; edit headers and cells)
- Data types: int, float, str, bool (color-coded)
- Column paging with ←/→ and footer hints
- DB Manager: Connect/Create/Delete DB, Delete Table, Search
- Autosave to active DB (toggle via Settings)
- Settings modal (saved to `settings.json`)
- Exports via embedded Python helpers (optional)

## Screenshots

### Main UI

![main_table](assets/main_table.png)

### Table Menu

![table_menu](assets/table_menu.png)

### Database Manager

![db_manager](assets/db_manager.png)

### Searching Data

![search_results](assets/search_results.png)

## Keybindings
- `c` Add column
- `r` Add row
- `e` Edit mode (arrows to navigate, Enter to edit, Esc to exit)
- `m` Table menu (Rename, Save, Load, New Table, DB Manager, Settings)
- `q` Quit

## Exports
The app writes a temporary CSV (`tmp_export.csv`) and calls `python/export.py` to generate PDF/XLSX (ensure required Python libs are installed).

## CI
GitHub Actions installs build deps and runs a simple `make`; build logs are visible in the job output.

## Author
Luke Canada (<canadaluke888@gmail.com>)
