# Changelog

All notable changes in this batch are documented here.

## [2025-09-07]

### 18:00 EST

#### Added
- Low‑RAM seek paging mode with streaming windows (seekdb) and Settings toggle.
- Row‑number gutter with centered numbers; toggle to show/hide in Settings.

#### Improved
- Smooth scrolling via non‑blocking input, key coalescing, and single‑frame redraws.
- Flicker reduction using erase + wnoutrefresh + doupdate and leaveok; width scan limited to visible rows.
- Edit‑mode focus starts at the top‑left of the current page.
- Cursor bounds enforced: prevent cursor from moving past viewport edges in search and edit modes.

#### Fixed
- Search prompt no longer immediately closes under nodelay.
- Settings menu text no longer overlaps borders; text is clipped within the window.
- Table breakage from gutter width not accounted for in visible column layout.
- Windows terminal flicker with large CSVs and wide windows.

#### Internal
- seekdb spill files moved under build/ with safe unlink/rmdir cleanup.
- Added `seekdb_get_view_columns` and a lightweight `seekdb_bench` tool.

### 21:00 EST

#### Changed
- Edit Mode interactions refined:
  - Row/Column deletion is now interactive with inline highlighting and scrolling.
  - Confirmation prompts for deletions use a list menu modal (Yes/No).
  - Footer hints updated: show "[x] Del Row" and "[^X] Del Col" in edit mode.

### Changed
- DB Manager: removed the "Search" menu option and all related DB-side search code; the existing UI search mode remains unchanged.
- Connect/Create flows now include sync prompts when a table with the same name exists in the connected DB, avoiding silent overwrites.
- Edit Mode: destructive actions are interactive with inline highlight + scrolling:
  - [x] Delete Row: highlight full row; use ↑/↓ to select; Enter confirms (list modal); Esc cancels.
  - [^X] Delete Column: highlight full column; use ←/→ to select; Enter confirms (list modal); Esc cancels. Prevent deleting last column.
  - [Backspace] Clear Cell: confirm then clear to type-appropriate empty value.
  - Footer hints updated to show [x] and [^X] bindings in edit mode.

### Fixed
- Column Add flow layering: type selection list now appears above the name prompt (prompt modal is closed before showing the list).
- Compiler warnings cleanup: removed unused functions/variables, fixed misleading indentation, and addressed truncation warning.

### API
- `db_table_exists` added to `db_manager` to support quick existence checks for sync prompts.

### Files (key updates)
- Updated: `src/ui/ui_prompt.c`, `src/ui/ui_db.c`, `src/ui/ui_file.c`, `src/db_manager.c`, `include/db_manager.h`, `src/ui/ui_draw.c`, `src/ui/ui_loop.c`

### Notes
- There is noticeable instability with large amounts of data; refinements to performance and stability are planned.

## [2025-08-31]

### Added
- Row paging for tall tables:
  - Adds `row_page`, `rows_visible`, `total_row_pages` to support paging down/up when rows exceed screen height.
  - Non-edit mode uses ↑/↓ to page rows.

### Changed
- Type selector is now a list menu (no typing):
  - Used when adding a column and when changing a column type in the header edit flow.
- Footer hint colors:
  - General actions use one color; paging hints (Cols/Rows Pg x/y) use a distinct color for clarity.
- Header styling restored:
  - Bold header cells with mixed heavy/light intersections (heavy header rules with light verticals below), matching earlier visuals.
- Column pager behavior:
  - `col_page` is a page index mapped to computed `page_starts` for accurate paging across variable column widths.
  - Exposes `col_start` for edit-mode bounds and disables paging during edit mode.

### Fixed
- Type selector cursor flicker:
  - Cursor hidden and echo disabled while list modal is active to avoid floating cursor artifacts.
- Column border breakage when paging:
  - Box-drawing characters normalized so header/body intersections render cleanly when moving between pages.
- Pager jumping across pages:
  - Fixed page index math so column pager increments/decrements by one page; total pages reflect computed page starts.

### Files (updated)
- `src/ui/ui_draw.c`, `src/ui/ui_loop.c`, `src/ui/ui_edit.c`, `src/ui/ui_prompt.c`, `include/ui.h`

### 19:02 EST

#### Added
- Open File (mini file explorer):
  - Replaces Table Menu “Load” with “Open File” to browse directories and pick a `.csv`.
- Native CSV support (no Python bridge):
  - `csv_load` reads headers as `name` or `name (type)`, loads rows, and names the table from the file basename.
  - Optional type inference (int/float/bool/str) when loading; falls back to `str` when disabled.
  - `csv_save` writes headers with types and all rows.
- Settings toggle: Type inference
  - New `type_infer_enabled` in `settings.json` and Settings UI; defaults ON.
- Edit-mode position indicator:
  - Displays at top-left: `R current/total  C current/total` (Row 0 indicates header focus).

#### Changed
- Save menu:
  - Added “CSV” option for native save; PDF/XLSX continue via Python exporter.
- DB Manager:
  - “Load Table” moved from Table Menu into DB Manager.
- Table Menu labels:
  - “Load” → “Open File”.

#### Notes
- CSV parser is simple (no quoted-field or escaped-comma handling yet).

#### Files (key additions/updates)
- Added: `include/csv.h`, `src/csv.c`, `src/ui/ui_file.c`
- Updated: `src/ui/ui_prompt.c`, `src/ui/ui_db.c`, `src/ui/ui_draw.c`, `src/ui/ui_settings.c`, `include/settings.h`, `src/settings.c`

## [2025-08-30]

### Added
- Database manager (native sqlite3) and TUI integration:
  - `include/db_manager.h` redesigned to a clean, opaque API.
  - `src/db_manager.c` implementing open/close, list databases/tables, delete database/table, search, save table, load table.
  - New DB Manager UI (`src/ui/ui_db.c`) with arrow-key menu: Connect, Create, Delete DB, Delete Table, Close, Search, Back.
  - Autosave-on-by-default with a global active DB; changes persist automatically when connected.
- Settings support backed by JSON-C:
  - `include/settings.h`, `src/settings.c` for load/save (`settings.json`).
  - `src/ui/ui_settings.c` settings modal (toggle Autosave; Save & Close; Cancel).
  - `src/main.c` loads settings on startup and saves on exit.
- Column paging for wide tables:
  - Compute visible columns to fit terminal width; navigate with ←/→.
  - Footer hint shows current page and available paging keys.
- Search results modal:
  - Collects results and displays them in a styled mini-window with pager (x/y), navigable via ←/→.
- SQL table loading:
  - Table Menu → Load: pick a table from active DB and replace in-memory table.
- New Table option:
  - Table Menu → New Table: saves current table (if connected), optionally confirms if risky, then clears to a new "Untitled Table".

### Changed
- Table Menu updated: Rename, Save, Load, New Table, DB Manager, Settings, Cancel.
- DB label displayed at the top-right:
  - Shows `DB: <basename>` when connected; otherwise "No Database Connected".
  - Truncates with ellipsis as needed to avoid overlapping the centered title.
- UI drawing & loop:
  - `src/ui/ui_draw.c` now draws only the visible set of columns and the DB label.
  - `src/ui/ui_loop.c` tracks `col_page`, `cols_visible`, and `total_pages`; handles paging with arrow keys.
- Autosave hooks added across flows:
  - After Add Column, Add Row, rename column/table, type changes, and body cell edit.

### Fixed
- Invalid input warning box interfering with input state:
  - `show_error_message` no longer toggles echo and preserves cursor visibility.
- DB search UI polish:
  - Results window uses clearer colors and layout.
  - Query input artifacts cleared before showing list/results.
- DB path/title overlap:
  - DB label is shortened and truncated to prevent overwriting table title.
- Memory safety when starting a New Table:
  - Free logic adjusted to avoid referencing updated counts.

### Build
- Makefile updated to link sqlite3 and json-c:
  - `-lsqlite3` and `-ljson-c` added to `LDFLAGS`.

### Files (key additions/updates)
- Added:
  - `src/db_manager.c`, `include/db_manager.h`
  - `src/ui/ui_db.c`
  - `include/settings.h`, `src/settings.c`, `src/ui/ui_settings.c`
  - `CHANGELOG.md`
- Updated (not exhaustive):
  - `src/ui/ui_draw.c`, `src/ui/ui_loop.c`, `src/ui/ui_edit.c`, `src/ui/ui_prompt.c`, `src/main.c`, `src/errors.c`, `Makefile`

### Notes
- Default database directory: `./databases/`.
- Autosave is ON by default; can be toggled in Settings.
