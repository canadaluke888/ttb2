#include "ttb_io.h"

#include <json-c/json.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) {
        return;
    }
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
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

int ttbl_save(const Table *table, const char *path, char *err, size_t err_sz)
{
    if (!table || !path) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "type", json_object_new_string("ttbl"));
    json_object_object_add(root, "version", json_object_new_int(1));
    struct json_object *table_obj = serialize_table(table);
    if (!table_obj) {
        json_object_put(root);
        set_err(err, err_sz, "Serialize failed");
        return -1;
    }
    json_object_object_add(root, "table", table_obj);
    int rc = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY);
    json_object_put(root);
    if (rc != 0) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

Table *ttbl_load(const char *path, char *err, size_t err_sz)
{
    if (!path) {
        set_err(err, err_sz, "No path provided");
        return NULL;
    }
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        set_err(err, err_sz, "Failed to read file");
        return NULL;
    }
    struct json_object *table_obj = NULL;
    if (!json_object_object_get_ex(root, "table", &table_obj)) {
        set_err(err, err_sz, "Invalid .ttbl file");
        json_object_put(root);
        return NULL;
    }
    Table *table = NULL;
    if (deserialize_table(table_obj, &table, err, err_sz) != 0) {
        json_object_put(root);
        return NULL;
    }
    json_object_put(root);
    return table;
}

int ttbx_save(const Table *table, const char *path, char *err, size_t err_sz)
{
    if (!table || !path) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "type", json_object_new_string("ttbx"));
    json_object_object_add(root, "version", json_object_new_int(1));

    struct json_object *tables = json_object_new_array();
    struct json_object *table_obj = serialize_table(table);
    if (!table_obj) {
        json_object_put(root);
        set_err(err, err_sz, "Serialize failed");
        return -1;
    }
    json_object_array_add(tables, table_obj);
    json_object_object_add(root, "tables", tables);
    json_object_object_add(root, "active_table", json_object_new_int(0));

    int rc = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY);
    json_object_put(root);
    if (rc != 0) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

Table *ttbx_load(const char *path, char *err, size_t err_sz)
{
    if (!path) {
        set_err(err, err_sz, "No path provided");
        return NULL;
    }
    struct json_object *root = json_object_from_file(path);
    if (!root) {
        set_err(err, err_sz, "Failed to read file");
        return NULL;
    }
    struct json_object *tables = NULL;
    if (!json_object_object_get_ex(root, "tables", &tables) || !json_object_is_type(tables, json_type_array)) {
        set_err(err, err_sz, "Invalid .ttbx file");
        json_object_put(root);
        return NULL;
    }
    if (json_object_array_length(tables) == 0) {
        set_err(err, err_sz, "Empty project");
        json_object_put(root);
        return NULL;
    }
    struct json_object *table_obj = json_object_array_get_idx(tables, 0);
    Table *table = NULL;
    if (deserialize_table(table_obj, &table, err, err_sz) != 0) {
        json_object_put(root);
        return NULL;
    }
    json_object_put(root);
    return table;
}
