#include "csv.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *trim(char *s) {
    if (!s) return s;
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static int ends_with(const char *s, const char *suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lsf = strlen(suf);
    return (ls >= lsf && strcmp(s + ls - lsf, suf) == 0);
}

static char **split_csv_line(const char *line, int *out_count) {
    // RFC-4180-ish CSV parsing: supports quoted fields, doubled quotes, and commas inside quotes.
    // Note: Does not support multi-line quoted fields (limited by fgets line buffering).
    int cap = 8, n = 0;
    char **arr = (char**)malloc(sizeof(char*) * cap);
    const char *p = line;

    while (*p && *p != '\n' && *p != '\r') {
        char *cell = NULL;

        // Skip leading spaces only for unquoted fields; preserve spaces inside quotes
        const char *field_start = p;
        if (*p == ' '){
            // Peek ahead: if next non-space is a quote, don't trim now
            const char *q = p;
            while (*q == ' ') q++;
            if (*q != '"') p = q; // ok to trim leading spaces for unquoted fields
        }

        if (*p == '"') {
            // Quoted field
            p++; // skip opening quote
            size_t buf_cap = 64, buf_len = 0;
            char *buf = (char*)malloc(buf_cap);
            int closed = 0;
            while (*p) {
                if (*p == '"') {
                    if (*(p+1) == '"') {
                        // Escaped quote
                        if (buf_len + 1 >= buf_cap) { buf_cap *= 2; buf = (char*)realloc(buf, buf_cap); }
                        buf[buf_len++] = '"';
                        p += 2;
                    } else {
                        // Closing quote
                        p++;
                        closed = 1;
                        break;
                    }
                } else {
                    if (buf_len + 1 >= buf_cap) { buf_cap *= 2; buf = (char*)realloc(buf, buf_cap); }
                    buf[buf_len++] = *p++;
                }
            }
            // Null-terminate buffer
            if (buf_len + 1 >= buf_cap) { buf_cap += 1; buf = (char*)realloc(buf, buf_cap); }
            buf[buf_len] = '\0';
            cell = buf;

            // After closing quote, consume spaces up to delimiter or EOL
            if (closed) {
                while (*p == ' ') p++;
            }
            // Expect comma or end-of-line; if comma, consume it
            if (*p == ',') {
                p++;
            } else {
                // If CR then optionally LF, then break
                if (*p == '\r') { p++; }
                if (*p == '\n' || *p == '\0') {
                    // done with line
                } else if (*p != '\0') {
                    // There are stray characters after a quoted field before delimiter; skip until delimiter/EOL
                    while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
                    if (*p == ',') p++;
                }
            }
        } else {
            // Unquoted field
            const char *start = p;
            while (*p && *p != '\n' && *p != '\r' && *p != ',') p++;
            size_t len = (size_t)(p - start);
            cell = (char*)malloc(len + 1);
            memcpy(cell, start, len);
            cell[len] = '\0';
            char *t = trim(cell);
            if (t != cell) memmove(cell, t, strlen(t) + 1);
            if (*p == ',') p++;
        }

        if (n == cap) { cap *= 2; arr = (char**)realloc(arr, sizeof(char*) * cap); }
        arr[n++] = cell ? cell : strdup("");

        // Skip any immediate CRLF after a field; outer while checks EOL too
        if (*p == '\r') p++;
        if (*p == '\n') break;
    }

    // Handle case where line ends with a trailing comma, meaning an empty field at the end
    if (p > line) {
        const char *last = p - 1;
        if (*last == ',') {
            if (n == cap) { cap *= 2; arr = (char**)realloc(arr, sizeof(char*) * cap); }
            arr[n++] = strdup("");
        }
    }

    *out_count = n;
    return arr;
}

static int is_int_str(const char *s) {
    if (!s || !*s) return 0;
    const char *p = s;
    if (*p == '-' || *p == '+') p++;
    if (!*p) return 0;
    for (; *p; ++p) if (!isdigit((unsigned char)*p)) return 0;
    return 1;
}

static int is_float_str(const char *s) {
    if (!s || !*s) return 0;
    char *end = NULL;
    strtod(s, &end);
    return end && *end == '\0';
}

static int is_bool_str(const char *s) {
    if (!s) return 0;
    return strcmp(s, "true") == 0 || strcmp(s, "false") == 0;
}

static DataType infer_type_for_column(char **cells, int rows) {
    int all_int = 1, all_float = 1, all_bool = 1;
    for (int i = 0; i < rows; ++i) {
        const char *v = cells[i] ? cells[i] : "";
        if (v[0] == '\0') { all_int = all_float = all_bool = 0; break; }
        if (!is_int_str(v)) all_int = 0;
        if (!is_float_str(v)) all_float = 0;
        if (!is_bool_str(v)) all_bool = 0;
        if (!all_int && !all_float && !all_bool) return TYPE_STR;
    }
    if (all_bool) return TYPE_BOOL;
    if (all_int) return TYPE_INT;
    if (all_float) return TYPE_FLOAT;
    return TYPE_STR;
}

static void parse_header(const char *cell, char **out_name, DataType *out_type) {
    // Accept either "name" or "name (type)"
    *out_type = TYPE_UNKNOWN;
    const char *open = strrchr(cell, '(');
    const char *close = (open ? strrchr(cell, ')') : NULL);
    if (open && close && close > open) {
        size_t name_len = (size_t)(open - cell);
        char *name = (char*)malloc(name_len + 1);
        memcpy(name, cell, name_len);
        name[name_len] = '\0';
        char *tname = trim(name);
        if (tname != name) memmove(name, tname, strlen(tname) + 1);
        size_t type_len = (size_t)(close - open - 1);
        char *typ = (char*)malloc(type_len + 1);
        memcpy(typ, open + 1, type_len);
        typ[type_len] = '\0';
        char *ttyp = trim(typ);
        if (ttyp != typ) memmove(typ, ttyp, strlen(ttyp) + 1);
        *out_name = name;
        *out_type = parse_type_from_string(typ);
        free(typ);
    } else {
        *out_name = strdup(cell);
    }
}

static char *basename_no_ext(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    size_t len = strlen(base);
    if (len > 4 && ends_with(base, ".csv")) len -= 4;
    char *name = (char*)malloc(len + 1);
    memcpy(name, base, len);
    name[len] = '\0';
    return name;
}

Table *csv_load(const char *path, bool infer_types, char *err, size_t err_sz) {
    if (err && err_sz) err[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) snprintf(err, err_sz, "Failed to open: %s", path);
        return NULL;
    }

    char line[4096];
    if (!fgets(line, sizeof(line), f)) {
        if (err) snprintf(err, err_sz, "Empty file or read error");
        fclose(f);
        return NULL;
    }
    int header_count = 0;
    char **header_cells = split_csv_line(line, &header_count);
    if (header_count <= 0) {
        if (err) snprintf(err, err_sz, "No headers found");
        fclose(f);
        for (int i = 0; i < header_count; ++i) free(header_cells[i]);
        free(header_cells);
        return NULL;
    }

    // Read data rows as strings first
    int rows_cap = 32, rows = 0;
    char ***data = (char***)malloc(sizeof(char**) * rows_cap);
    while (fgets(line, sizeof(line), f)) {
        int c = 0; char **cells = split_csv_line(line, &c);
        // normalize to header_count columns
        if (c < header_count) {
            cells = (char**)realloc(cells, sizeof(char*) * header_count);
            for (int i = c; i < header_count; ++i) cells[i] = strdup("");
            c = header_count;
        }
        if (rows == rows_cap) { rows_cap *= 2; data = (char***)realloc(data, sizeof(char**) * rows_cap); }
        data[rows++] = cells;
    }
    fclose(f);

    // Prepare headers
    char **col_names = (char**)malloc(sizeof(char*) * header_count);
    DataType *col_types = (DataType*)malloc(sizeof(DataType) * header_count);
    for (int i = 0; i < header_count; ++i) {
        parse_header(header_cells[i], &col_names[i], &col_types[i]);
    }

    // Infer types optionally (only when unknown)
    if (infer_types) {
        for (int c = 0; c < header_count; ++c) {
            if (col_types[c] == TYPE_UNKNOWN) {
                // build column cells array
                char **cells = (char**)malloc(sizeof(char*) * rows);
                for (int r = 0; r < rows; ++r) cells[r] = data[r][c];
                col_types[c] = infer_type_for_column(cells, rows);
                free(cells);
            }
        }
    } else {
        for (int c = 0; c < header_count; ++c) if (col_types[c] == TYPE_UNKNOWN) col_types[c] = TYPE_STR;
    }

    // Build table
    char *tname = basename_no_ext(path);
    Table *t = create_table(tname);
    free(tname);
    for (int c = 0; c < header_count; ++c) add_column(t, col_names[c], col_types[c]);
    for (int r = 0; r < rows; ++r) {
        const char **row_inputs = (const char **)data[r];
        add_row(t, row_inputs);
    }

    // Cleanup temporaries
    for (int i = 0; i < header_count; ++i) { free(header_cells[i]); free(col_names[i]); }
    free(header_cells); free(col_names); free(col_types);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < header_count; ++c) free(data[r][c]);
        free(data[r]);
    }
    free(data);
    return t;
}

int csv_save(const Table *table, const char *path, char *err, size_t err_sz) {
    if (err && err_sz) err[0] = '\0';
    if (!table || !path) { if (err) snprintf(err, err_sz, "Invalid args"); return -1; }
    FILE *f = fopen(path, "w");
    if (!f) { if (err) snprintf(err, err_sz, "Failed to open for write: %s", path); return -1; }
    // Header with types to preserve info
    for (int j = 0; j < table->column_count; ++j) {
        fprintf(f, "%s (%s)%s", table->columns[j].name, type_to_string(table->columns[j].type),
                (j < table->column_count - 1) ? "," : "\n");
    }
    // Rows
    for (int i = 0; i < table->row_count; ++i) {
        for (int j = 0; j < table->column_count; ++j) {
            void *v = table->rows[i].values[j];
            switch (table->columns[j].type) {
                case TYPE_INT:   fprintf(f, "%d", *(int*)v); break;
                case TYPE_FLOAT: fprintf(f, "%g", *(float*)v); break;
                case TYPE_BOOL:  fprintf(f, "%s", (*(int*)v) ? "true" : "false"); break;
                default:         fprintf(f, "%s", v ? (char*)v : ""); break;
            }
            if (j < table->column_count - 1) fprintf(f, ",");
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return 0;
}
