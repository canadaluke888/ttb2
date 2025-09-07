#ifndef SEEKDB_H
#define SEEKDB_H

#include <sqlite3.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SEEKDB_MODE_AUTO = 0,
    SEEKDB_MODE_LOW_RAM,
    SEEKDB_MODE_NORMAL
} seekdb_mode_t;

/* Opaque handle */
typedef struct seekdb seekdb;

/* Row callback: called once per row in a window.
   Return false to stop early (e.g., user scrolled fast). */
typedef bool (*seekdb_row_cb)(void *user, sqlite3_stmt *row);

/* Open existing DB (connected mode) if path is SQLite; otherwise create a temp spill DB.
   - mode: AUTO picks low-ram settings on small machines; override with LOW_RAM or NORMAL.
   - err/errlen: optional error buffer (can be NULL). */
seekdb*  seekdb_open(const char *path_or_null, seekdb_mode_t mode, char *err, size_t errlen);

/* Create a table if needed; columns is raw SQL like: "id INTEGER, name TEXT, ...".
   Returns 0 on success, <0 on error. */
int      seekdb_ensure_table(seekdb *s, const char *table, const char *columns_sql, char *err, size_t errlen);

/* Ensure stable integer key for keyset pagination (adds column + index if missing).
   Returns 0 on success, <0 on error. */
int      seekdb_ensure_stable_key(seekdb *s, const char *table, const char *key, char *err, size_t errlen);

/* Define/refresh the working VIEW for filter/sort (no data copy).
   order_sql should be your ORDER BY list (without the keyword). The key is auto-appended if missing.
   Returns 0 on success, <0 on error. */
int      seekdb_set_view(seekdb *s, const char *base_table, const char *where_sql, const char *order_sql,
                         const char *key, char *err, size_t errlen);

/* Windowed reads (seek-only): deliver rows to callback.
   - limit: maximum rows to deliver
   - last_id/first_id: boundary IDs for after/before
   Returns number of rows delivered, or <0 on error. */
int      seekdb_seek_first (seekdb *s, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen);
int      seekdb_seek_after (seekdb *s, long long last_id, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen);
int      seekdb_seek_before(seekdb *s, long long first_id, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen);
int      seekdb_seek_by_id (seekdb *s, long long target_id, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen);

/* Utility: count rows in current view (may be slow on very large tables). */
long long seekdb_count(seekdb *s, char *err, size_t errlen);

/* Save ephemeral DB to a permanent file. */
int      seekdb_save_as(seekdb *s, const char *dest_path, char *err, size_t errlen);

/* Close and cleanup. */
void     seekdb_close(seekdb *s);

/* View metadata: returns a heap-allocated array of column names for current view.
   Caller must free each name and the array itself. Returns count (>0) or <0 on error. */
int      seekdb_get_view_columns(seekdb *s, char ***names_out, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
#endif
