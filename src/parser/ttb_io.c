#include "ttb_io.h"

#include <json-c/json.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define TTBX_MANIFEST_FILE "book.json"

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) {
        return;
    }
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int is_directory(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static void join_path(char *out, size_t out_sz, const char *dir, const char *name)
{
    if (!out || out_sz == 0) return;
    snprintf(out, out_sz, "%s/%s", dir ? dir : "", name ? name : "");
}

static char *sanitize_component(const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (len == 0) return strdup("table");

    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;

    for (size_t i = 0; i < len; ++i) {
        char ch = src[i];
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            out[i] = ch;
        } else {
            out[i] = '_';
        }
    }
    out[len] = '\0';
    return out;
}

static char *dup_json_string(struct json_object *obj, const char *fallback)
{
    const char *s = obj ? json_object_get_string(obj) : NULL;
    if (!s) s = fallback ? fallback : "";
    return strdup(s);
}

static struct json_object *serialize_table(const Table *table)
{
    if (!table) {
        return NULL;
    }

    struct json_object *obj = json_object_new_object();
    json_object_object_add(obj, "name", json_object_new_string(table->name ? table->name : ""));

    struct json_object *cols = json_object_new_array();
    for (int i = 0; i < table->column_count; ++i) {
        struct json_object *col = json_object_new_object();
        json_object_object_add(col, "name", json_object_new_string(table->columns[i].name ? table->columns[i].name : ""));
        json_object_object_add(col, "type", json_object_new_string(type_to_string(table->columns[i].type)));
        json_object_array_add(cols, col);
    }
    json_object_object_add(obj, "columns", cols);

    struct json_object *rows = json_object_new_array();
    for (int r = 0; r < table->row_count; ++r) {
        struct json_object *row = json_object_new_array();
        for (int c = 0; c < table->column_count; ++c) {
            void *val = (table->rows[r].values ? table->rows[r].values[c] : NULL);
            struct json_object *cell = NULL;
            switch (table->columns[c].type) {
            case TYPE_INT:
                if (val) cell = json_object_new_int(*(int *)val);
                break;
            case TYPE_FLOAT:
                if (val) cell = json_object_new_double((double)*(float *)val);
                break;
            case TYPE_BOOL:
                if (val) cell = json_object_new_boolean(*(int *)val ? 1 : 0);
                break;
            case TYPE_STR:
            default:
                if (val) cell = json_object_new_string((const char *)val);
                break;
            }
            if (!cell) {
                cell = json_object_new_null();
            }
            json_object_array_add(row, cell);
        }
        json_object_array_add(rows, row);
    }
    json_object_object_add(obj, "rows", rows);
    return obj;
}

static int deserialize_table(struct json_object *obj, Table **out_table, char *err, size_t err_sz)
{
    if (!obj || !out_table) {
        set_err(err, err_sz, "Invalid table data");
        return -1;
    }

    struct json_object *name = NULL;
    if (!json_object_object_get_ex(obj, "name", &name)) {
        set_err(err, err_sz, "Missing table name");
        return -1;
    }

    const char *table_name = json_object_get_string(name);
    if (!table_name) {
        table_name = "Untitled";
    }

    struct json_object *cols = NULL;
    if (!json_object_object_get_ex(obj, "columns", &cols) || !json_object_is_type(cols, json_type_array)) {
        set_err(err, err_sz, "Missing columns array");
        return -1;
    }

    Table *table = create_table(table_name);
    if (!table) {
        set_err(err, err_sz, "Failed to allocate table");
        return -1;
    }

    int col_count = json_object_array_length(cols);
    DataType *col_types = (DataType *)calloc(col_count, sizeof(DataType));
    if (!col_types) {
        free_table(table);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    for (int i = 0; i < col_count; ++i) {
        struct json_object *col = json_object_array_get_idx(cols, i);
        if (!col || !json_object_is_type(col, json_type_object)) {
            continue;
        }
        struct json_object *col_name = NULL;
        struct json_object *col_type = NULL;
        const char *col_name_str = "";
        const char *col_type_str = "str";
        if (json_object_object_get_ex(col, "name", &col_name)) {
            col_name_str = json_object_get_string(col_name);
        }
        if (json_object_object_get_ex(col, "type", &col_type)) {
            col_type_str = json_object_get_string(col_type);
        }
        DataType dt = parse_type_from_string(col_type_str);
        if (dt == TYPE_UNKNOWN) dt = TYPE_STR;
        col_types[i] = dt;
        add_column(table, col_name_str ? col_name_str : "", dt);
    }

    struct json_object *rows = NULL;
    if (json_object_object_get_ex(obj, "rows", &rows) && json_object_is_type(rows, json_type_array)) {
        int row_count = json_object_array_length(rows);
        for (int r = 0; r < row_count; ++r) {
            struct json_object *row = json_object_array_get_idx(rows, r);
            if (!row || !json_object_is_type(row, json_type_array)) {
                continue;
            }
            char **inputs = (char **)calloc(col_count, sizeof(char *));
            if (!inputs) {
                set_err(err, err_sz, "Out of memory");
                continue;
            }
            int cells = json_object_array_length(row);
            for (int c = 0; c < col_count; ++c) {
                struct json_object *cell = (c < cells) ? json_object_array_get_idx(row, c) : NULL;
                const char *value_str = "";
                char buf[64];
                if (cell && !json_object_is_type(cell, json_type_null)) {
                    switch (col_types[c]) {
                    case TYPE_INT:
                        snprintf(buf, sizeof(buf), "%d", json_object_get_int(cell));
                        value_str = buf;
                        break;
                    case TYPE_FLOAT:
                        snprintf(buf, sizeof(buf), "%g", json_object_get_double(cell));
                        value_str = buf;
                        break;
                    case TYPE_BOOL:
                        value_str = json_object_get_boolean(cell) ? "true" : "false";
                        break;
                    case TYPE_STR:
                    default:
                        value_str = json_object_get_string(cell);
                        if (!value_str) value_str = "";
                        break;
                    }
                }
                inputs[c] = strdup(value_str ? value_str : "");
            }
            add_row(table, (const char **)inputs);
            for (int c = 0; c < col_count; ++c) free(inputs[c]);
            free(inputs);
        }
    }

    free(col_types);
    *out_table = table;
    return 0;
}

static int save_json_to_file(struct json_object *root, const char *path, char *err, size_t err_sz)
{
    int rc = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY);
    if (rc != 0) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

static int load_table_file(const char *path, Table **out_table, char *err, size_t err_sz)
{
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        set_err(err, err_sz, "Failed to read table file");
        return -1;
    }

    struct json_object *table_obj = NULL;
    if (json_object_object_get_ex(root, "table", &table_obj)) {
        if (deserialize_table(table_obj, out_table, err, err_sz) != 0) {
            json_object_put(root);
            return -1;
        }
    } else {
        if (deserialize_table(root, out_table, err, err_sz) != 0) {
            json_object_put(root);
            return -1;
        }
    }

    json_object_put(root);
    return 0;
}

static int save_table_file(const Table *table, const char *path, char *err, size_t err_sz)
{
    struct json_object *root = json_object_new_object();
    struct json_object *table_obj = serialize_table(table);
    if (!table_obj) {
        json_object_put(root);
        set_err(err, err_sz, "Serialize failed");
        return -1;
    }

    json_object_object_add(root, "type", json_object_new_string("ttbl"));
    json_object_object_add(root, "version", json_object_new_int(1));
    json_object_object_add(root, "table", table_obj);
    if (save_json_to_file(root, path, err, err_sz) != 0) {
        json_object_put(root);
        return -1;
    }
    json_object_put(root);
    return 0;
}

static int ensure_directory(const char *path, char *err, size_t err_sz)
{
    if (mkdir(path, 0755) != 0) {
        if (errno == EEXIST && is_directory(path)) {
            return 0;
        }
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

static int remove_path_if_exists(const char *path, char *err, size_t err_sz)
{
    DIR *dir;
    struct dirent *de;
    char child[PATH_MAX];
    struct stat st;

    if (!path) return 0;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path) != 0) {
            set_err(err, err_sz, strerror(errno));
            return -1;
        }
        return 0;
    }

    dir = opendir(path);
    if (!dir) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        join_path(child, sizeof(child), path, de->d_name);
        if (remove_path_if_exists(child, err, err_sz) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    if (rmdir(path) != 0) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst, char *err, size_t err_sz)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[8192];
    size_t nread;

    if (!in) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    while ((nread = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, nread, out) != nread) {
            fclose(in);
            fclose(out);
            set_err(err, err_sz, strerror(errno));
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int copy_book_dir(const char *src_path, const char *dst_path, char *err, size_t err_sz)
{
    DIR *dir;
    struct dirent *de;
    char src_file[PATH_MAX];
    char dst_file[PATH_MAX];
    struct stat st;

    if (!ttbx_is_book_dir(src_path)) {
        set_err(err, err_sz, "Source is not a valid book");
        return -1;
    }
    if (remove_path_if_exists(dst_path, err, err_sz) != 0) return -1;
    if (ensure_directory(dst_path, err, err_sz) != 0) return -1;

    dir = opendir(src_path);
    if (!dir) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        join_path(src_file, sizeof(src_file), src_path, de->d_name);
        if (stat(src_file, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        join_path(dst_file, sizeof(dst_file), dst_path, de->d_name);
        if (copy_file(src_file, dst_file, err, err_sz) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static int manifest_find_index(const TtbxManifest *manifest, const char *table_id)
{
    if (!manifest || !table_id) return -1;
    for (int i = 0; i < manifest->table_count; ++i) {
        if (manifest->tables[i].id && strcmp(manifest->tables[i].id, table_id) == 0) return i;
    }
    return -1;
}

static int entry_identity_conflicts(const TtbxManifest *manifest,
                                    int skip_idx,
                                    const char *id_value,
                                    const char *file_value)
{
    if (!manifest) return 0;
    for (int i = 0; i < manifest->table_count; ++i) {
        if (i == skip_idx) continue;
        if (id_value &&
            manifest->tables[i].id &&
            strcmp(manifest->tables[i].id, id_value) == 0) {
            return 1;
        }
        if (file_value &&
            manifest->tables[i].file &&
            strcmp(manifest->tables[i].file, file_value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int build_unique_identity(const TtbxManifest *manifest,
                                 int skip_idx,
                                 const char *table_name,
                                 char *out_id,
                                 size_t out_id_sz,
                                 char *out_file,
                                 size_t out_file_sz,
                                 char *err,
                                 size_t err_sz)
{
    char *san;
    int suffix = 1;

    if (!out_id || !out_file || out_id_sz == 0 || out_file_sz == 0) {
        set_err(err, err_sz, "Invalid identity buffer");
        return -1;
    }

    san = sanitize_component(table_name && *table_name ? table_name : "Untitled Table");
    if (!san) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    while (1) {
        if (suffix == 1) {
            snprintf(out_id, out_id_sz, "%s", san);
        } else {
            snprintf(out_id, out_id_sz, "%s_%d", san, suffix);
        }
        snprintf(out_file, out_file_sz, "%s.ttbl", out_id);
        if (!entry_identity_conflicts(manifest, skip_idx, out_id, out_file)) {
            break;
        }
        suffix++;
    }

    free(san);
    return 0;
}

static int update_entry_identity(TtbxManifest *manifest,
                                 int idx,
                                 const char *table_name,
                                 const char *dir_path,
                                 char *err,
                                 size_t err_sz)
{
    char id_buf[64];
    char file_buf[96];
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];
    char *new_id;
    char *new_name;
    char *new_file;

    if (!manifest || idx < 0 || idx >= manifest->table_count) {
        set_err(err, err_sz, "Invalid table entry");
        return -1;
    }

    if (build_unique_identity(manifest, idx, table_name, id_buf, sizeof(id_buf), file_buf, sizeof(file_buf), err, err_sz) != 0) {
        return -1;
    }

    if (manifest->tables[idx].id &&
        manifest->tables[idx].file &&
        strcmp(manifest->tables[idx].id, id_buf) == 0 &&
        strcmp(manifest->tables[idx].file, file_buf) == 0 &&
        manifest->tables[idx].name &&
        strcmp(manifest->tables[idx].name, table_name ? table_name : "Untitled Table") == 0) {
        return 0;
    }

    new_id = strdup(id_buf);
    new_name = strdup(table_name ? table_name : "Untitled Table");
    new_file = strdup(file_buf);
    if (!new_id || !new_name || !new_file) {
        free(new_id);
        free(new_name);
        free(new_file);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    if (dir_path &&
        manifest->tables[idx].file &&
        strcmp(manifest->tables[idx].file, new_file) != 0) {
        join_path(old_path, sizeof(old_path), dir_path, manifest->tables[idx].file);
        join_path(new_path, sizeof(new_path), dir_path, new_file);
        if (rename(old_path, new_path) != 0 && errno != ENOENT) {
            free(new_id);
            free(new_name);
            free(new_file);
            set_err(err, err_sz, strerror(errno));
            return -1;
        }
    }

    if (manifest->active_table_id &&
        manifest->tables[idx].id &&
        strcmp(manifest->active_table_id, manifest->tables[idx].id) == 0) {
        free(manifest->active_table_id);
        manifest->active_table_id = strdup(new_id);
        if (!manifest->active_table_id) {
            free(new_id);
            free(new_name);
            free(new_file);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    free(manifest->tables[idx].id);
    free(manifest->tables[idx].name);
    free(manifest->tables[idx].file);
    manifest->tables[idx].id = new_id;
    manifest->tables[idx].name = new_name;
    manifest->tables[idx].file = new_file;
    return 0;
}

static int manifest_add_table(TtbxManifest *manifest, const char *table_name, char *err, size_t err_sz)
{
    int idx;
    char id_buf[64];
    char file_buf[96];

    idx = manifest->table_count;
    TtbxTableEntry *next = (TtbxTableEntry *)realloc(manifest->tables, sizeof(TtbxTableEntry) * (idx + 1));
    if (!next) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    manifest->tables = next;

    if (build_unique_identity(manifest, -1, table_name, id_buf, sizeof(id_buf), file_buf, sizeof(file_buf), err, err_sz) != 0) {
        return -1;
    }

    manifest->tables[idx].id = strdup(id_buf);
    manifest->tables[idx].name = strdup(table_name ? table_name : "Untitled Table");
    manifest->tables[idx].file = strdup(file_buf);
    if (!manifest->tables[idx].id || !manifest->tables[idx].name || !manifest->tables[idx].file) {
        free(manifest->tables[idx].id);
        free(manifest->tables[idx].name);
        free(manifest->tables[idx].file);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    manifest->table_count++;
    if (!manifest->active_table_id) manifest->active_table_id = strdup(manifest->tables[idx].id);
    if (!manifest->book_name) manifest->book_name = strdup("Workbook");
    if (!manifest->active_table_id || !manifest->book_name) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    return idx;
}

void ttbx_manifest_free(TtbxManifest *manifest)
{
    if (!manifest) return;
    free(manifest->book_name);
    free(manifest->active_table_id);
    for (int i = 0; i < manifest->table_count; ++i) {
        free(manifest->tables[i].id);
        free(manifest->tables[i].name);
        free(manifest->tables[i].file);
    }
    free(manifest->tables);
    memset(manifest, 0, sizeof(*manifest));
}

int ttbx_is_book_dir(const char *path)
{
    char manifest_path[PATH_MAX];
    struct stat st;

    if (!is_directory(path)) return 0;
    join_path(manifest_path, sizeof(manifest_path), path, TTBX_MANIFEST_FILE);
    if (stat(manifest_path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int ttbl_save(const Table *table, const char *path, char *err, size_t err_sz)
{
    if (!table || !path) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    return save_table_file(table, path, err, err_sz);
}

Table *ttbl_load(const char *path, char *err, size_t err_sz)
{
    Table *table = NULL;
    if (!path) {
        set_err(err, err_sz, "No path provided");
        return NULL;
    }
    if (load_table_file(path, &table, err, err_sz) != 0) return NULL;
    return table;
}

int ttbx_manifest_load(const char *path, TtbxManifest *manifest, char *err, size_t err_sz)
{
    char manifest_path[PATH_MAX];
    struct json_object *root = NULL;
    struct json_object *tables = NULL;

    if (!path || !manifest) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    memset(manifest, 0, sizeof(*manifest));
    if (!is_directory(path)) {
        set_err(err, err_sz, "Book path is not a directory");
        return -1;
    }

    join_path(manifest_path, sizeof(manifest_path), path, TTBX_MANIFEST_FILE);
    root = json_object_from_file(manifest_path);
    if (!root) {
        set_err(err, err_sz, "Failed to read book header");
        return -1;
    }
    if (!json_object_object_get_ex(root, "tables", &tables) || !json_object_is_type(tables, json_type_array)) {
        json_object_put(root);
        set_err(err, err_sz, "Invalid book header");
        return -1;
    }

    struct json_object *book_name = NULL;
    struct json_object *active_id = NULL;
    if (json_object_object_get_ex(root, "book_name", &book_name)) {
        manifest->book_name = dup_json_string(book_name, "Workbook");
    }
    if (json_object_object_get_ex(root, "active_table_id", &active_id)) {
        manifest->active_table_id = dup_json_string(active_id, "");
    }

    int count = json_object_array_length(tables);
    manifest->tables = (TtbxTableEntry *)calloc(count > 0 ? count : 1, sizeof(TtbxTableEntry));
    if (!manifest->tables) {
        json_object_put(root);
        ttbx_manifest_free(manifest);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    manifest->table_count = count;

    for (int i = 0; i < count; ++i) {
        struct json_object *item = json_object_array_get_idx(tables, i);
        struct json_object *id = NULL, *name = NULL, *file = NULL;
        if (!item || !json_object_is_type(item, json_type_object)) continue;
        if (json_object_object_get_ex(item, "id", &id)) {
            manifest->tables[i].id = dup_json_string(id, "");
        }
        if (json_object_object_get_ex(item, "name", &name)) {
            manifest->tables[i].name = dup_json_string(name, "");
        }
        if (json_object_object_get_ex(item, "file", &file)) {
            manifest->tables[i].file = dup_json_string(file, "");
        }
        if (!manifest->tables[i].id || !manifest->tables[i].file) {
            json_object_put(root);
            ttbx_manifest_free(manifest);
            set_err(err, err_sz, "Invalid table entry in book header");
            return -1;
        }
        if (!manifest->tables[i].name) {
            manifest->tables[i].name = strdup(manifest->tables[i].id);
        }
    }

    if (manifest->table_count == 0) {
        json_object_put(root);
        ttbx_manifest_free(manifest);
        set_err(err, err_sz, "Book has no tables");
        return -1;
    }
    if (!manifest->active_table_id || manifest_find_index(manifest, manifest->active_table_id) < 0) {
        free(manifest->active_table_id);
        manifest->active_table_id = strdup(manifest->tables[0].id);
    }

    json_object_put(root);
    return 0;
}

int ttbx_manifest_save(const char *path, const TtbxManifest *manifest, char *err, size_t err_sz)
{
    char manifest_path[PATH_MAX];
    struct json_object *root;
    struct json_object *tables;

    if (!path || !manifest) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    if (ensure_directory(path, err, err_sz) != 0) return -1;

    root = json_object_new_object();
    tables = json_object_new_array();
    json_object_object_add(root, "type", json_object_new_string("ttbx"));
    json_object_object_add(root, "version", json_object_new_int(2));
    json_object_object_add(root, "book_name", json_object_new_string(manifest->book_name ? manifest->book_name : "Workbook"));
    json_object_object_add(root, "active_table_id", json_object_new_string(manifest->active_table_id ? manifest->active_table_id : ""));

    for (int i = 0; i < manifest->table_count; ++i) {
        struct json_object *item = json_object_new_object();
        json_object_object_add(item, "id", json_object_new_string(manifest->tables[i].id ? manifest->tables[i].id : ""));
        json_object_object_add(item, "name", json_object_new_string(manifest->tables[i].name ? manifest->tables[i].name : ""));
        json_object_object_add(item, "file", json_object_new_string(manifest->tables[i].file ? manifest->tables[i].file : ""));
        json_object_array_add(tables, item);
    }
    json_object_object_add(root, "tables", tables);

    join_path(manifest_path, sizeof(manifest_path), path, TTBX_MANIFEST_FILE);
    if (save_json_to_file(root, manifest_path, err, err_sz) != 0) {
        json_object_put(root);
        return -1;
    }
    json_object_put(root);
    return 0;
}

int ttbx_copy_book(const char *src_path, const char *dst_path, char *err, size_t err_sz)
{
    if (!src_path || !dst_path) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    return copy_book_dir(src_path, dst_path, err, err_sz);
}

int ttbx_remove_book(const char *path, char *err, size_t err_sz)
{
    return remove_path_if_exists(path, err, err_sz);
}

Table *ttbx_load_table(const char *path, const char *table_id, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    Table *table = NULL;
    int idx;
    char table_path[PATH_MAX];

    if (ttbx_manifest_load(path, &manifest, err, err_sz) != 0) return NULL;

    idx = manifest_find_index(&manifest, table_id ? table_id : manifest.active_table_id);
    if (idx < 0) {
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Book table not found");
        return NULL;
    }

    join_path(table_path, sizeof(table_path), path, manifest.tables[idx].file);
    if (load_table_file(table_path, &table, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return NULL;
    }

    ttbx_manifest_free(&manifest);
    return table;
}

Table *ttbx_load(const char *path, char *err, size_t err_sz)
{
    return ttbx_load_table(path, NULL, err, err_sz);
}

int ttbx_rename_table(const char *path, const char *table_id, const char *name, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    Table *table = NULL;
    int idx;
    char old_table_path[PATH_MAX];
    char new_table_path[PATH_MAX];
    char *new_name;

    if (!path || !table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }

    if (ttbx_manifest_load(path, &manifest, err, err_sz) != 0) return -1;
    idx = manifest_find_index(&manifest, table_id);
    if (idx < 0) {
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Book table not found");
        return -1;
    }

    join_path(old_table_path, sizeof(old_table_path), path, manifest.tables[idx].file);
    if (load_table_file(old_table_path, &table, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }

    new_name = strdup((name && *name) ? name : "Untitled Table");
    if (!new_name) {
        free_table(table);
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    free(table->name);
    table->name = new_name;

    if (update_entry_identity(&manifest, idx, table->name, path, err, err_sz) != 0) {
        free_table(table);
        ttbx_manifest_free(&manifest);
        return -1;
    }

    join_path(new_table_path, sizeof(new_table_path), path, manifest.tables[idx].file);
    if (save_table_file(table, new_table_path, err, err_sz) != 0) {
        free_table(table);
        ttbx_manifest_free(&manifest);
        return -1;
    }
    free_table(table);

    if (ttbx_manifest_save(path, &manifest, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }

    ttbx_manifest_free(&manifest);
    return 0;
}

int ttbx_delete_table(const char *path, const char *table_id, char *next_active_id, size_t next_active_id_sz, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    int idx;
    int was_active;
    char deleted_path[PATH_MAX];
    char *next_id = NULL;

    if (next_active_id && next_active_id_sz > 0) next_active_id[0] = '\0';
    if (!path || !table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }

    if (ttbx_manifest_load(path, &manifest, err, err_sz) != 0) return -1;
    idx = manifest_find_index(&manifest, table_id);
    if (idx < 0) {
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Book table not found");
        return -1;
    }
    if (manifest.table_count <= 1) {
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Cannot delete the last table");
        return -1;
    }

    was_active = manifest.active_table_id && strcmp(manifest.active_table_id, table_id) == 0;
    if (was_active) {
        int next_idx = (idx < manifest.table_count - 1) ? idx + 1 : idx - 1;
        next_id = strdup(manifest.tables[next_idx].id ? manifest.tables[next_idx].id : "");
        if (!next_id) {
            ttbx_manifest_free(&manifest);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    } else {
        next_id = strdup(manifest.active_table_id ? manifest.active_table_id : "");
        if (!next_id) {
            ttbx_manifest_free(&manifest);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    join_path(deleted_path, sizeof(deleted_path), path, manifest.tables[idx].file);
    if (unlink(deleted_path) != 0 && errno != ENOENT) {
        free(next_id);
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, strerror(errno));
        return -1;
    }

    free(manifest.tables[idx].id);
    free(manifest.tables[idx].name);
    free(manifest.tables[idx].file);
    if (idx < manifest.table_count - 1) {
        memmove(&manifest.tables[idx], &manifest.tables[idx + 1],
                sizeof(TtbxTableEntry) * (manifest.table_count - idx - 1));
    }
    manifest.table_count--;

    free(manifest.active_table_id);
    manifest.active_table_id = strdup(next_id);
    if (!manifest.active_table_id) {
        free(next_id);
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    if (ttbx_manifest_save(path, &manifest, err, err_sz) != 0) {
        free(next_id);
        ttbx_manifest_free(&manifest);
        return -1;
    }

    if (next_active_id && next_active_id_sz > 0) {
        strncpy(next_active_id, next_id, next_active_id_sz - 1);
        next_active_id[next_active_id_sz - 1] = '\0';
    }
    free(next_id);
    ttbx_manifest_free(&manifest);
    return 0;
}

int ttbx_save_table(const Table *table, const char *path, const char *table_id, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    int idx;
    char table_path[PATH_MAX];

    if (!table || !path) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }

    if (ttbx_is_book_dir(path)) {
        if (ttbx_manifest_load(path, &manifest, err, err_sz) != 0) return -1;
    } else {
        memset(&manifest, 0, sizeof(manifest));
        manifest.book_name = strdup(table->name ? table->name : "Workbook");
    }

    idx = manifest_find_index(&manifest, table_id ? table_id : manifest.active_table_id);
    if (idx < 0) {
        idx = manifest_add_table(&manifest, table->name, err, err_sz);
        if (idx < 0) {
            ttbx_manifest_free(&manifest);
            return -1;
        }
    }

    if (update_entry_identity(&manifest, idx, table->name, path, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }

    free(manifest.active_table_id);
    manifest.active_table_id = strdup(manifest.tables[idx].id);
    if (!manifest.active_table_id) {
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    if (ensure_directory(path, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }

    join_path(table_path, sizeof(table_path), path, manifest.tables[idx].file);
    if (save_table_file(table, table_path, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }
    if (ttbx_manifest_save(path, &manifest, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }

    ttbx_manifest_free(&manifest);
    return 0;
}

int ttbx_save(const Table *table, const char *path, char *err, size_t err_sz)
{
    return ttbx_save_table(table, path, NULL, err, err_sz);
}
