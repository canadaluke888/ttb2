#include "xl.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlib.h>

#define ZIP_SIG_LOCAL      0x04034b50u
#define ZIP_SIG_CENTRAL    0x02014b50u
#define ZIP_SIG_END        0x06054b50u

#define MAX_SHEET_NAME_LEN 31

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} string_list;

typedef struct {
    size_t column_index;
    char *value;
} cell_entry;

typedef struct {
    cell_entry *items;
    size_t count;
    size_t capacity;
} cell_array;

typedef struct {
    cell_array cells;
    size_t max_col;
} parsed_row;

typedef struct {
    parsed_row *rows;
    size_t count;
    size_t capacity;
    size_t max_cols;
} parsed_sheet;

struct zip_source {
    const char *name;
    const unsigned char *data;
    size_t size;
    uint32_t crc32;
    uint32_t offset;
};

struct string_builder {
    char *data;
    size_t length;
    size_t capacity;
};

static void string_list_init(string_list *list);
static void string_list_push(string_list *list, char *value);
static void string_list_free(string_list *list);

static void cell_array_init(cell_array *cells);
static void cell_array_push(cell_array *cells, size_t col, char *value);
static void cell_array_sort(cell_array *cells);
static void cell_array_free(cell_array *cells);

static void parsed_sheet_init(parsed_sheet *sheet);
static int parsed_sheet_push(parsed_sheet *sheet, cell_array *cells, size_t max_col);
static void parsed_sheet_free(parsed_sheet *sheet);

static uint16_t read_le16(const unsigned char *buf);
static uint32_t read_le32(const unsigned char *buf);

static int zip_extract_file(const char *archive_path,
                            const char *file_name,
                            unsigned char **out_data,
                            size_t *out_size);

static int zip_write_files(const char *archive_path,
                           struct zip_source *sources,
                           size_t source_count);

static char *dup_range(const char *start, const char *end);
static char *xml_unescape(const char *start, const char *end);
static char *xml_escape(const char *text);
static char *heap_strdup(const char *text);
static char *extract_tag_text(const char *block_start,
                              const char *block_end,
                              const char *tag_name);

static size_t parse_column_from_ref(const char *ref);
static size_t parse_row_index(const char *row_start, size_t *fallback_counter);

static void parse_shared_strings(const char *xml, string_list *strings);
static int parse_sheet_collect(const char *xml,
                               const string_list *shared,
                               parsed_sheet *sheet);

static uint16_t static_dos_time(void);
static uint16_t static_dos_date(void);
static void column_index_to_ref(size_t column_index, size_t row_index, char *out_buf, size_t buf_size);
static int ensure_sheet_name(char *dest, size_t dest_size, const char *source);

static int sb_append(struct string_builder *sb, const char *text);
static int sb_append_format(struct string_builder *sb, const char *fmt, ...);

static char *build_sheet_xml(const char *sheet_name,
                             const char *const *table,
                             size_t rows,
                             size_t cols);
static char *build_workbook_xml(const char *sheet_name);
static const char *content_types_xml(void);
static const char *rels_root_xml(void);
static const char *workbook_rels_xml(void);
static const char *styles_xml(void);

static bool ends_with(const char *s, const char *suf);
static char *basename_no_ext(const char *path);
static char *trim_inplace(char *s);
static void parse_header(const char *cell, char **out_name, DataType *out_type);
static int is_int_str(const char *s);
static int is_float_str(const char *s);
static int is_bool_str(const char *s);
static DataType infer_type_for_column(char **cells, int rows);

static const char *cell_value_at(const cell_array *cells, size_t column_index);

static int write_xlsx(const char *path,
                      const char *sheet_name,
                      const char *const *table,
                      size_t rows,
                      size_t cols);

Table *xl_load(const char *path, bool infer_types, char *err, size_t err_sz)
{
    if (err && err_sz) {
        err[0] = '\0';
    }
    if (!path) {
        if (err) snprintf(err, err_sz, "Invalid XLSX path");
        return NULL;
    }

    unsigned char *sheet_data = NULL;
    size_t sheet_size = 0;
    if (zip_extract_file(path, "xl/worksheets/sheet1.xml", &sheet_data, &sheet_size) != 0) {
        if (err) snprintf(err, err_sz, "Sheet1 not found in %s", path);
        return NULL;
    }

    unsigned char *shared_data = NULL;
    size_t shared_size = 0;
    zip_extract_file(path, "xl/sharedStrings.xml", &shared_data, &shared_size);

    string_list shared_strings;
    string_list_init(&shared_strings);
    if (shared_data) {
        parse_shared_strings((const char *)shared_data, &shared_strings);
    }

    parsed_sheet sheet;
    parsed_sheet_init(&sheet);
    if (parse_sheet_collect((const char *)sheet_data, &shared_strings, &sheet) != 0) {
        if (err) snprintf(err, err_sz, "Failed to parse worksheet");
        string_list_free(&shared_strings);
        free(sheet_data);
        free(shared_data);
        parsed_sheet_free(&sheet);
        return NULL;
    }

    if (sheet.count == 0 || sheet.max_cols == 0) {
        if (err) snprintf(err, err_sz, "Worksheet is empty");
        string_list_free(&shared_strings);
        free(sheet_data);
        free(shared_data);
        parsed_sheet_free(&sheet);
        return NULL;
    }

    size_t header_count = sheet.max_cols;
    size_t data_rows = (sheet.count > 1) ? sheet.count - 1 : 0;

    char **col_names = (char **)calloc(header_count, sizeof(char *));
    DataType *col_types = (DataType *)malloc(sizeof(DataType) * header_count);
    if (!col_names || !col_types) {
        if (err) snprintf(err, err_sz, "Out of memory");
        free(col_names);
        free(col_types);
        string_list_free(&shared_strings);
        free(sheet_data);
        free(shared_data);
        parsed_sheet_free(&sheet);
        return NULL;
    }

    for (size_t c = 0; c < header_count; ++c) {
        const char *cell = cell_value_at(&sheet.rows[0].cells, c);
        if (!cell) {
            cell = "";
        }
        parse_header(cell, &col_names[c], &col_types[c]);
        if (!col_names[c] || col_names[c][0] == '\0') {
            free(col_names[c]);
            char buf[32];
            snprintf(buf, sizeof(buf), "Column%zu", c + 1);
            col_names[c] = strdup(buf);
        }
        if (col_types[c] == TYPE_UNKNOWN) {
            col_types[c] = infer_types ? TYPE_UNKNOWN : TYPE_STR;
        }
    }

    if (infer_types && data_rows > 0) {
        for (size_t c = 0; c < header_count; ++c) {
            if (col_types[c] != TYPE_UNKNOWN) {
                continue;
            }
            char **column_cells = (char **)malloc(sizeof(char *) * data_rows);
            if (!column_cells) {
                if (err) snprintf(err, err_sz, "Out of memory");
                free(col_names);
                free(col_types);
                string_list_free(&shared_strings);
                free(sheet_data);
                free(shared_data);
                parsed_sheet_free(&sheet);
                return NULL;
            }
            for (size_t r = 0; r < data_rows; ++r) {
                const char *cell = cell_value_at(&sheet.rows[r + 1].cells, c);
                column_cells[r] = (cell && cell[0]) ? (char *)cell : "";
            }
            col_types[c] = infer_type_for_column(column_cells, (int)data_rows);
            free(column_cells);
        }
    }

    for (size_t c = 0; c < header_count; ++c) {
        if (col_types[c] == TYPE_UNKNOWN) {
            col_types[c] = TYPE_STR;
        }
    }

    char *table_name = basename_no_ext(path);
    if (!table_name) {
        table_name = strdup("Sheet1");
    }

    Table *table = create_table(table_name);
    free(table_name);

    if (!table) {
        if (err) snprintf(err, err_sz, "Failed to allocate table");
        for (size_t c = 0; c < header_count; ++c) free(col_names[c]);
        free(col_names);
        free(col_types);
        string_list_free(&shared_strings);
        free(sheet_data);
        free(shared_data);
        parsed_sheet_free(&sheet);
        return NULL;
    }

    for (size_t c = 0; c < header_count; ++c) {
        add_column(table, col_names[c], col_types[c]);
        free(col_names[c]);
    }
    free(col_names);
    free(col_types);

    if (data_rows > 0) {
        const char **row_inputs = (const char **)malloc(sizeof(char *) * header_count);
        if (!row_inputs) {
            if (err) snprintf(err, err_sz, "Out of memory");
            string_list_free(&shared_strings);
            parsed_sheet_free(&sheet);
            free(sheet_data);
            free(shared_data);
            free_table(table);
            return NULL;
        }
        for (size_t r = 0; r < data_rows; ++r) {
            for (size_t c = 0; c < header_count; ++c) {
                const char *cell = cell_value_at(&sheet.rows[r + 1].cells, c);
                row_inputs[c] = cell ? cell : "";
            }
            add_row(table, row_inputs);
        }
        free(row_inputs);
    }

    string_list_free(&shared_strings);
    parsed_sheet_free(&sheet);
    free(sheet_data);
    free(shared_data);
    return table;
}

int xl_save(const Table *table, const char *path, char *err, size_t err_sz)
{
    if (err && err_sz) {
        err[0] = '\0';
    }
    if (!table || !path) {
        if (err) snprintf(err, err_sz, "Invalid arguments");
        return -1;
    }

    size_t cols = (size_t)table->column_count;
    size_t rows = (size_t)table->row_count + 1; /* include header */
    if (cols == 0) {
        if (err) snprintf(err, err_sz, "Table has no columns");
        return -1;
    }

    size_t cell_count = rows * cols;
    char **grid = (char **)malloc(sizeof(char *) * cell_count);
    if (!grid) {
        if (err) snprintf(err, err_sz, "Out of memory");
        return -1;
    }

    size_t idx = 0;
    for (size_t c = 0; c < cols; ++c) {
        const char *type_name = type_to_string(table->columns[c].type);
        size_t needed = strlen(table->columns[c].name) + strlen(type_name) + 4;
        grid[idx] = (char *)malloc(needed);
        if (!grid[idx]) {
            if (err) snprintf(err, err_sz, "Out of memory");
            for (size_t k = 0; k < idx; ++k) free(grid[k]);
            free(grid);
            return -1;
        }
        snprintf(grid[idx], needed, "%s (%s)", table->columns[c].name, type_name);
        idx++;
    }

    for (int r = 0; r < table->row_count; ++r) {
        for (int c = 0; c < table->column_count; ++c) {
            const Column *col = &table->columns[c];
            void *v = table->rows[r].values[c];
            char buffer[64];
            switch (col->type) {
            case TYPE_INT:
                snprintf(buffer, sizeof(buffer), "%d", *(int *)v);
                break;
            case TYPE_FLOAT:
                snprintf(buffer, sizeof(buffer), "%g", *(float *)v);
                break;
            case TYPE_BOOL:
                snprintf(buffer, sizeof(buffer), "%s", (*(int *)v) ? "true" : "false");
                break;
            default:
                snprintf(buffer, sizeof(buffer), "%s", v ? (char *)v : "");
                break;
            }
            grid[idx] = strdup(buffer);
            if (!grid[idx]) {
                if (err) snprintf(err, err_sz, "Out of memory");
                for (size_t k = 0; k < idx; ++k) free(grid[k]);
                free(grid);
                return -1;
            }
            idx++;
        }
    }

    const char *sheet_name = table->name ? table->name : "Sheet1";
    int rc = write_xlsx(path, sheet_name, (const char *const *)grid, rows, cols);

    for (size_t i = 0; i < cell_count; ++i) {
        free(grid[i]);
    }
    free(grid);

    if (rc != 0 && err) {
        snprintf(err, err_sz, "Failed to write XLSX");
    }
    return rc;
}

static void string_list_init(string_list *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void string_list_push(string_list *list, char *value)
{
    if (!value) {
        return;
    }
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 8;
        char **new_items = (char **)realloc(list->items, new_cap * sizeof(char *));
        if (!new_items) {
            free(value);
            return;
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = value;
}

static void string_list_free(string_list *list)
{
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static void cell_array_init(cell_array *cells)
{
    cells->items = NULL;
    cells->count = 0;
    cells->capacity = 0;
}

static void cell_array_push(cell_array *cells, size_t col, char *value)
{
    if (!value) {
        return;
    }
    if (cells->count == cells->capacity) {
        size_t new_cap = cells->capacity ? cells->capacity * 2 : 8;
        cell_entry *new_items = (cell_entry *)realloc(cells->items, new_cap * sizeof(cell_entry));
        if (!new_items) {
            free(value);
            return;
        }
        cells->items = new_items;
        cells->capacity = new_cap;
    }
    cells->items[cells->count].column_index = col;
    cells->items[cells->count].value = value;
    cells->count++;
}

static void cell_array_sort(cell_array *cells)
{
    for (size_t i = 0; i + 1 < cells->count; ++i) {
        for (size_t j = i + 1; j < cells->count; ++j) {
            if (cells->items[j].column_index < cells->items[i].column_index) {
                cell_entry tmp = cells->items[i];
                cells->items[i] = cells->items[j];
                cells->items[j] = tmp;
            }
        }
    }
}

static void cell_array_free(cell_array *cells)
{
    for (size_t i = 0; i < cells->count; ++i) {
        free(cells->items[i].value);
    }
    free(cells->items);
    cells->items = NULL;
    cells->count = cells->capacity = 0;
}

static void parsed_sheet_init(parsed_sheet *sheet)
{
    sheet->rows = NULL;
    sheet->count = 0;
    sheet->capacity = 0;
    sheet->max_cols = 0;
}

static int parsed_sheet_push(parsed_sheet *sheet, cell_array *cells, size_t max_col)
{
    if (sheet->count == sheet->capacity) {
        size_t new_cap = sheet->capacity ? sheet->capacity * 2 : 8;
        parsed_row *rows = (parsed_row *)realloc(sheet->rows, new_cap * sizeof(parsed_row));
        if (!rows) {
            return -1;
        }
        sheet->rows = rows;
        sheet->capacity = new_cap;
    }
    sheet->rows[sheet->count].cells = *cells;
    sheet->rows[sheet->count].max_col = max_col;
    sheet->count++;
    if (max_col > sheet->max_cols) {
        sheet->max_cols = max_col;
    }
    cells->items = NULL;
    cells->count = cells->capacity = 0;
    return 0;
}

static void parsed_sheet_free(parsed_sheet *sheet)
{
    if (!sheet) {
        return;
    }
    for (size_t i = 0; i < sheet->count; ++i) {
        cell_array_free(&sheet->rows[i].cells);
    }
    free(sheet->rows);
    sheet->rows = NULL;
    sheet->count = sheet->capacity = sheet->max_cols = 0;
}

static const char *cell_value_at(const cell_array *cells, size_t column_index)
{
    if (!cells) {
        return NULL;
    }
    for (size_t i = 0; i < cells->count; ++i) {
        if (cells->items[i].column_index == column_index) {
            return cells->items[i].value;
        }
    }
    return NULL;
}

static uint16_t read_le16(const unsigned char *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_le32(const unsigned char *buf)
{
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

static int zip_extract_file(const char *archive_path,
                            const char *file_name,
                            unsigned char **out_data,
                            size_t *out_size)
{
    FILE *fp = fopen(archive_path, "rb");
    if (!fp) {
        return -1;
    }

    int found = -1;
    for (;;) {
        unsigned char header[30];
        size_t read = fread(header, 1, sizeof(header), fp);
        if (read == 0) {
            break;
        }
        if (read != sizeof(header)) {
            break;
        }

        uint32_t signature = read_le32(header);
        if (signature == ZIP_SIG_CENTRAL || signature == ZIP_SIG_END) {
            break;
        }
        if (signature != ZIP_SIG_LOCAL) {
            break;
        }

        uint16_t flags = read_le16(header + 6);
        uint16_t compression = read_le16(header + 8);
        uint32_t compressed_size = read_le32(header + 18);
        uint32_t uncompressed_size = read_le32(header + 22);
        uint16_t fname_len = read_le16(header + 26);
        uint16_t extra_len = read_le16(header + 28);

        if ((flags & 0x0008u) != 0) {
            fclose(fp);
            return -1;
        }

        char *name = (char *)malloc(fname_len + 1);
        if (!name) {
            fclose(fp);
            return -1;
        }
        if (fread(name, 1, fname_len, fp) != fname_len) {
            free(name);
            break;
        }
        name[fname_len] = '\0';

        if (fseek(fp, extra_len, SEEK_CUR) != 0) {
            free(name);
            break;
        }

        unsigned char *compressed = (unsigned char *)malloc(compressed_size);
        if (!compressed) {
            free(name);
            fclose(fp);
            return -1;
        }
        if (compressed_size > 0 && fread(compressed, 1, compressed_size, fp) != compressed_size) {
            free(name);
            free(compressed);
            break;
        }

        if (strcmp(name, file_name) == 0) {
            unsigned char *buffer = NULL;
            if (compression == 0) {
                buffer = (unsigned char *)malloc(uncompressed_size + 1);
                if (!buffer) {
                    free(name);
                    free(compressed);
                    fclose(fp);
                    return -1;
                }
                memcpy(buffer, compressed, uncompressed_size);
                buffer[uncompressed_size] = '\0';
            } else if (compression == 8) {
                buffer = (unsigned char *)malloc(uncompressed_size + 1);
                if (!buffer) {
                    free(name);
                    free(compressed);
                    fclose(fp);
                    return -1;
                }
                z_stream stream;
                memset(&stream, 0, sizeof(stream));
                stream.next_in = compressed;
                stream.avail_in = compressed_size;
                stream.next_out = buffer;
                stream.avail_out = uncompressed_size;
                int ret = inflateInit2(&stream, -MAX_WBITS);
                if (ret == Z_OK) {
                    ret = inflate(&stream, Z_FINISH);
                }
                inflateEnd(&stream);
                if (ret != Z_STREAM_END) {
                    free(buffer);
                    buffer = NULL;
                } else {
                    buffer[uncompressed_size] = '\0';
                }
            } else {
                buffer = NULL;
            }

            if (buffer) {
                *out_data = buffer;
                *out_size = uncompressed_size;
                found = 0;
                free(name);
                free(compressed);
                break;
            }
        }

        free(name);
        free(compressed);
    }

    fclose(fp);
    return found;
}

static uint16_t static_dos_time(void)
{
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_hour = 0;
    tm_val.tm_min = 0;
    tm_val.tm_sec = 0;
    return (uint16_t)(((tm_val.tm_hour & 0x1F) << 11) | ((tm_val.tm_min & 0x3F) << 5) |
                      ((tm_val.tm_sec / 2) & 0x1F));
}

static uint16_t static_dos_date(void)
{
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = 2024 - 1900;
    tm_val.tm_mon = 0;
    tm_val.tm_mday = 1;
    return (uint16_t)((((tm_val.tm_year - 80) & 0x7F) << 9) |
                      (((tm_val.tm_mon + 1) & 0x0F) << 5) | (tm_val.tm_mday & 0x1F));
}

static int zip_write_files(const char *archive_path,
                           struct zip_source *sources,
                           size_t source_count)
{
    FILE *fp = fopen(archive_path, "wb");
    if (!fp) {
        return -1;
    }

    uint32_t dos_time = static_dos_time();
    uint32_t dos_date = static_dos_date();

    for (size_t i = 0; i < source_count; ++i) {
        struct zip_source *src = &sources[i];
        src->offset = (uint32_t)ftell(fp);

        unsigned char header[30];
        memset(header, 0, sizeof(header));
        header[0] = 0x50;
        header[1] = 0x4b;
        header[2] = 0x03;
        header[3] = 0x04;
        header[4] = 0x14;
        header[5] = 0x00;
        header[10] = (unsigned char)(dos_time & 0xFF);
        header[11] = (unsigned char)((dos_time >> 8) & 0xFF);
        header[12] = (unsigned char)(dos_date & 0xFF);
        header[13] = (unsigned char)((dos_date >> 8) & 0xFF);
        header[14] = (unsigned char)(src->crc32 & 0xFF);
        header[15] = (unsigned char)((src->crc32 >> 8) & 0xFF);
        header[16] = (unsigned char)((src->crc32 >> 16) & 0xFF);
        header[17] = (unsigned char)((src->crc32 >> 24) & 0xFF);
        header[18] = (unsigned char)(src->size & 0xFF);
        header[19] = (unsigned char)((src->size >> 8) & 0xFF);
        header[20] = (unsigned char)((src->size >> 16) & 0xFF);
        header[21] = (unsigned char)((src->size >> 24) & 0xFF);
        header[22] = header[18];
        header[23] = header[19];
        header[24] = header[20];
        header[25] = header[21];
        uint16_t name_len = (uint16_t)strlen(src->name);
        header[26] = (unsigned char)(name_len & 0xFF);
        header[27] = (unsigned char)((name_len >> 8) & 0xFF);

        fwrite(header, 1, sizeof(header), fp);
        fwrite(src->name, 1, name_len, fp);
        fwrite(src->data, 1, src->size, fp);
    }

    uint32_t central_offset = (uint32_t)ftell(fp);

    for (size_t i = 0; i < source_count; ++i) {
        struct zip_source *src = &sources[i];
        unsigned char header[46];
        memset(header, 0, sizeof(header));
        header[0] = 0x50;
        header[1] = 0x4b;
        header[2] = 0x01;
        header[3] = 0x02;
        header[4] = 0x14;
        header[5] = 0x00;
        header[6] = 0x14;
        header[7] = 0x00;
        header[10] = (unsigned char)(dos_time & 0xFF);
        header[11] = (unsigned char)((dos_time >> 8) & 0xFF);
        header[12] = (unsigned char)(dos_date & 0xFF);
        header[13] = (unsigned char)((dos_date >> 8) & 0xFF);
        header[14] = (unsigned char)(src->crc32 & 0xFF);
        header[15] = (unsigned char)((src->crc32 >> 8) & 0xFF);
        header[16] = (unsigned char)((src->crc32 >> 16) & 0xFF);
        header[17] = (unsigned char)((src->crc32 >> 24) & 0xFF);
        header[18] = (unsigned char)(src->size & 0xFF);
        header[19] = (unsigned char)((src->size >> 8) & 0xFF);
        header[20] = (unsigned char)((src->size >> 16) & 0xFF);
        header[21] = (unsigned char)((src->size >> 24) & 0xFF);
        header[22] = header[18];
        header[23] = header[19];
        header[24] = header[20];
        header[25] = header[21];
        uint16_t name_len = (uint16_t)strlen(src->name);
        header[28] = (unsigned char)(name_len & 0xFF);
        header[29] = (unsigned char)((name_len >> 8) & 0xFF);
        header[42] = (unsigned char)(src->offset & 0xFF);
        header[43] = (unsigned char)((src->offset >> 8) & 0xFF);
        header[44] = (unsigned char)((src->offset >> 16) & 0xFF);
        header[45] = (unsigned char)((src->offset >> 24) & 0xFF);

        fwrite(header, 1, sizeof(header), fp);
        fwrite(src->name, 1, name_len, fp);
    }

    uint32_t central_size = (uint32_t)ftell(fp) - central_offset;

    unsigned char end_record[22];
    memset(end_record, 0, sizeof(end_record));
    end_record[0] = 0x50;
    end_record[1] = 0x4b;
    end_record[2] = 0x05;
    end_record[3] = 0x06;
    end_record[8] = (unsigned char)(source_count & 0xFF);
    end_record[9] = (unsigned char)((source_count >> 8) & 0xFF);
    end_record[10] = end_record[8];
    end_record[11] = end_record[9];
    end_record[12] = (unsigned char)(central_size & 0xFF);
    end_record[13] = (unsigned char)((central_size >> 8) & 0xFF);
    end_record[14] = (unsigned char)((central_size >> 16) & 0xFF);
    end_record[15] = (unsigned char)((central_size >> 24) & 0xFF);
    end_record[16] = (unsigned char)(central_offset & 0xFF);
    end_record[17] = (unsigned char)((central_offset >> 8) & 0xFF);
    end_record[18] = (unsigned char)((central_offset >> 16) & 0xFF);
    end_record[19] = (unsigned char)((central_offset >> 24) & 0xFF);

    fwrite(end_record, 1, sizeof(end_record), fp);
    fclose(fp);
    return 0;
}

static char *dup_range(const char *start, const char *end)
{
    if (!start || !end || end < start) {
        return NULL;
    }
    size_t len = (size_t)(end - start);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static char *xml_unescape(const char *start, const char *end)
{
    char *raw = dup_range(start, end);
    if (!raw) {
        return NULL;
    }

    size_t len = strlen(raw);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        free(raw);
        return NULL;
    }

    size_t oi = 0;
    for (size_t i = 0; i < len; ++i) {
        if (raw[i] == '&') {
            if (strncmp(&raw[i], "&amp;", 5) == 0) {
                out[oi++] = '&';
                i += 4;
            } else if (strncmp(&raw[i], "&lt;", 4) == 0) {
                out[oi++] = '<';
                i += 3;
            } else if (strncmp(&raw[i], "&gt;", 4) == 0) {
                out[oi++] = '>';
                i += 3;
            } else if (strncmp(&raw[i], "&quot;", 6) == 0) {
                out[oi++] = '"';
                i += 5;
            } else if (strncmp(&raw[i], "&apos;", 6) == 0) {
                out[oi++] = '\'';
                i += 5;
            } else {
                out[oi++] = raw[i];
            }
        } else {
            out[oi++] = raw[i];
        }
    }
    out[oi] = '\0';
    free(raw);
    return out;
}

static char *xml_escape(const char *text)
{
    size_t len = strlen(text);
    size_t extra = 0;
    for (size_t i = 0; i < len; ++i) {
        switch (text[i]) {
        case '&':
        case '<':
        case '>':
        case '"':
        case '\'':
            extra += 5;
            break;
        default:
            break;
        }
    }

    char *out = (char *)malloc(len + extra + 1);
    if (!out) {
        return NULL;
    }

    size_t oi = 0;
    for (size_t i = 0; i < len; ++i) {
        switch (text[i]) {
        case '&':
            memcpy(&out[oi], "&amp;", 5);
            oi += 5;
            break;
        case '<':
            memcpy(&out[oi], "&lt;", 4);
            oi += 4;
            break;
        case '>':
            memcpy(&out[oi], "&gt;", 4);
            oi += 4;
            break;
        case '"':
            memcpy(&out[oi], "&quot;", 6);
            oi += 6;
            break;
        case '\'':
            memcpy(&out[oi], "&apos;", 6);
            oi += 6;
            break;
        default:
            out[oi++] = text[i];
            break;
        }
    }
    out[oi] = '\0';
    return out;
}

static char *heap_strdup(const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

static char *extract_tag_text(const char *block_start,
                              const char *block_end,
                              const char *tag_name)
{
    const char *cursor = block_start;
    char open_tag[32];
    snprintf(open_tag, sizeof(open_tag), "<%s", tag_name);
    while ((cursor = strstr(cursor, open_tag)) != NULL && cursor < block_end) {
        const char *start = strchr(cursor, '>');
        if (!start || start >= block_end) {
            return NULL;
        }
        ++start;
        char close_tag[34];
        snprintf(close_tag, sizeof(close_tag), "</%s>", tag_name);
        const char *end = strstr(start, close_tag);
        if (!end || end > block_end) {
            return NULL;
        }
        return xml_unescape(start, end);
    }
    return NULL;
}

static size_t parse_row_index(const char *row_start, size_t *fallback_counter)
{
    const char *attr = strstr(row_start, " r=");
    if (!attr) {
        return ++(*fallback_counter);
    }
    const char *quote = strchr(attr, '"');
    if (!quote) {
        return ++(*fallback_counter);
    }
    long value = strtol(quote + 1, NULL, 10);
    if (value <= 0) {
        return ++(*fallback_counter);
    }
    *fallback_counter = (size_t)value;
    return (size_t)value;
}

static size_t parse_column_from_ref(const char *ref)
{
    size_t col = 0;
    for (size_t i = 0; ref[i] != '\0'; ++i) {
        if (!isalpha((unsigned char)ref[i])) {
            break;
        }
        col = col * 26 + (size_t)(toupper((unsigned char)ref[i]) - 'A' + 1);
    }
    return (col == 0) ? 0 : col - 1;
}

static void parse_shared_strings(const char *xml, string_list *strings)
{
    const char *cursor = xml;
    while ((cursor = strstr(cursor, "<si")) != NULL) {
        const char *si_end = strstr(cursor, "</si>");
        if (!si_end) {
            break;
        }
        const char *body = cursor;
        char *accum = NULL;
        const char *text_cursor = body;
        while ((text_cursor = strstr(text_cursor, "<t")) != NULL && text_cursor < si_end) {
            const char *start = strchr(text_cursor, '>');
            if (!start || start >= si_end) {
                break;
            }
            ++start;
            const char *end = strstr(start, "</t>");
            if (!end || end > si_end) {
                break;
            }
            char *part = xml_unescape(start, end);
            if (!part) {
                break;
            }
            if (!accum) {
                accum = part;
            } else {
                size_t old_len = strlen(accum);
                size_t part_len = strlen(part);
                char *merged = (char *)realloc(accum, old_len + part_len + 1);
                if (!merged) {
                    free(part);
                    break;
                }
                memcpy(merged + old_len, part, part_len + 1);
                free(part);
                accum = merged;
            }
            text_cursor = end;
        }
        if (accum) {
            string_list_push(strings, accum);
        }
        cursor = si_end + 5;
    }
}

static int parse_sheet_collect(const char *xml,
                               const string_list *shared,
                               parsed_sheet *sheet)
{
    const char *cursor = xml;
    size_t fallback_row = 0;
    while ((cursor = strstr(cursor, "<row")) != NULL) {
        const char *row_end = strstr(cursor, "</row>");
        if (!row_end) {
            break;
        }
        parse_row_index(cursor, &fallback_row);

        cell_array cells;
        cell_array_init(&cells);

        const char *cell_cursor = cursor;
        size_t implicit_col = 0;
        while ((cell_cursor = strstr(cell_cursor, "<c")) != NULL && cell_cursor < row_end) {
            const char *cell_end = strstr(cell_cursor, "</c>");
            if (!cell_end) {
                break;
            }
            const char *ref_attr = strstr(cell_cursor, " r=");
            size_t column_index = implicit_col++;
            if (ref_attr && ref_attr < cell_end) {
                const char *quote = strchr(ref_attr, '"');
                if (quote && quote < cell_end) {
                    const char *quote_end = strchr(quote + 1, '"');
                    if (quote_end && quote_end < cell_end) {
                        column_index = parse_column_from_ref(quote + 1);
                    }
                }
            }

            const char *type_attr = strstr(cell_cursor, " t=");
            char type = '\0';
            if (type_attr && type_attr < cell_end) {
                const char *quote = strchr(type_attr, '"');
                if (quote && quote < cell_end) {
                    type = quote[1];
                }
            }

            char *text = NULL;
            if (type == 's') {
                char *index_text = extract_tag_text(cell_cursor, cell_end, "v");
                if (index_text) {
                    long idx = strtol(index_text, NULL, 10);
                    free(index_text);
                    if (idx >= 0 && (size_t)idx < shared->count) {
                        text = heap_strdup(shared->items[idx]);
                    }
                }
            } else if (type == 'i') {
                const char *inline_start = strstr(cell_cursor, "<is>");
                if (inline_start && inline_start < cell_end) {
                    char *inline_text = extract_tag_text(inline_start, cell_end, "t");
                    if (inline_text) {
                        text = inline_text;
                    }
                }
            } else {
                char *value_text = extract_tag_text(cell_cursor, cell_end, "v");
                if (value_text) {
                    text = value_text;
                }
            }

            if (text) {
                cell_array_push(&cells, column_index, text);
            }
            cell_cursor = cell_end + 4;
        }

        if (cells.count > 0) {
            cell_array_sort(&cells);
            size_t max_col = cells.items[cells.count - 1].column_index + 1;
            if (parsed_sheet_push(sheet, &cells, max_col) != 0) {
                cell_array_free(&cells);
                return -1;
            }
        } else {
            cell_array_free(&cells);
        }
        cursor = row_end + 6;
    }
    return 0;
}

static void column_index_to_ref(size_t column_index, size_t row_index, char *out_buf, size_t buf_size)
{
    if (!out_buf || buf_size == 0) {
        return;
    }

    char tmp[8];
    size_t idx = 0;
    size_t value = column_index + 1;
    while (value > 0) {
        size_t remainder = (value - 1) % 26;
        tmp[idx++] = (char)('A' + remainder);
        value = (value - 1) / 26;
    }
    for (size_t i = 0; i < idx / 2; ++i) {
        char c = tmp[i];
        tmp[i] = tmp[idx - 1 - i];
        tmp[idx - 1 - i] = c;
    }
    tmp[idx] = '\0';

    int written = snprintf(out_buf, buf_size, "%s%zu", tmp, row_index);
    if (written < 0 || (size_t)written >= buf_size) {
        out_buf[buf_size - 1] = '\0';
    }
}

static int ensure_sheet_name(char *dest, size_t dest_size, const char *source)
{
    if (!dest || dest_size == 0) {
        return -1;
    }

    const char *fallback = "Sheet1";
    const char *name = (source && source[0]) ? source : fallback;
    size_t len = strlen(name);
    if (len > MAX_SHEET_NAME_LEN) {
        len = MAX_SHEET_NAME_LEN;
    }
    if (len >= dest_size) {
        len = dest_size - 1;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (c == ':' || c == '/' || c == '\\' || c == '?' || c == '*' || c == '[' || c == ']') {
            c = '_';
        }
        dest[i] = c;
    }
    dest[len] = '\0';
    return 0;
}

static int sb_append(struct string_builder *sb, const char *text)
{
    size_t len = strlen(text);
    if (sb->length + len + 1 > sb->capacity) {
        size_t new_cap = sb->capacity ? sb->capacity * 2 : 256;
        while (new_cap < sb->length + len + 1) {
            new_cap *= 2;
        }
        char *new_data = (char *)realloc(sb->data, new_cap);
        if (!new_data) {
            return -1;
        }
        sb->data = new_data;
        sb->capacity = new_cap;
    }
    memcpy(sb->data + sb->length, text, len + 1);
    sb->length += len;
    return 0;
}

static int sb_append_format(struct string_builder *sb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return -1;
    }
    size_t total_needed = sb->length + (size_t)needed + 1;
    if (total_needed > sb->capacity) {
        size_t new_cap = sb->capacity ? sb->capacity * 2 : 256;
        while (new_cap < total_needed) {
            new_cap *= 2;
        }
        char *new_data = (char *)realloc(sb->data, new_cap);
        if (!new_data) {
            va_end(args);
            return -1;
        }
        sb->data = new_data;
        sb->capacity = new_cap;
    }
    vsnprintf(sb->data + sb->length, sb->capacity - sb->length, fmt, args);
    sb->length += (size_t)needed;
    va_end(args);
    return 0;
}

static char *build_sheet_xml(const char *sheet_name,
                             const char *const *table,
                             size_t rows,
                             size_t cols)
{
    (void)sheet_name;
    struct string_builder sb = {0};
    if (sb_append(&sb,
                  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                  "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
                  "  <sheetData>\n") != 0) {
        free(sb.data);
        return NULL;
    }

    for (size_t r = 0; r < rows; ++r) {
        struct string_builder row_sb = {0};
        if (sb_append_format(&row_sb, "    <row r=\"%zu\">\n", r + 1) != 0) {
            free(row_sb.data);
            free(sb.data);
            return NULL;
        }

        for (size_t c = 0; c < cols; ++c) {
            const char *cell_text = table[r * cols + c];
            if (!cell_text || !cell_text[0]) {
                continue;
            }
            char ref[16];
            column_index_to_ref(c, r + 1, ref, sizeof(ref));
            char *escaped = xml_escape(cell_text);
            if (!escaped) {
                free(row_sb.data);
                free(sb.data);
                return NULL;
            }
            if (sb_append_format(&row_sb,
                                 "      <c r=\"%s\" t=\"inlineStr\"><is><t>%s</t></is></c>\n",
                                 ref, escaped) != 0) {
                free(escaped);
                free(row_sb.data);
                free(sb.data);
                return NULL;
            }
            free(escaped);
        }

        if (sb_append(&row_sb, "    </row>\n") != 0) {
            free(row_sb.data);
            free(sb.data);
            return NULL;
        }
        if (sb_append(&sb, row_sb.data) != 0) {
            free(row_sb.data);
            free(sb.data);
            return NULL;
        }
        free(row_sb.data);
    }

    if (sb_append(&sb, "  </sheetData>\n</worksheet>") != 0) {
        free(sb.data);
        return NULL;
    }
    return sb.data;
}

static char *build_workbook_xml(const char *sheet_name)
{
    char *escaped = xml_escape(sheet_name);
    if (!escaped) {
        return NULL;
    }

    struct string_builder sb = {0};
    if (sb_append(&sb,
                  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                  "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n"
                  "  <sheets>\n") != 0 ||
        sb_append_format(&sb,
                         "    <sheet name=\"%s\" sheetId=\"1\" r:id=\"rId1\"/>\n",
                         escaped) != 0 ||
        sb_append(&sb, "  </sheets>\n</workbook>") != 0) {
        free(sb.data);
        free(escaped);
        return NULL;
    }
    free(escaped);
    return sb.data;
}

static const char *content_types_xml(void)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
           "  <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
           "  <Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
           "  <Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\n"
           "  <Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>\n"
           "  <Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>\n"
           "</Types>";
}

static const char *rels_root_xml(void)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
           "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>\n"
           "</Relationships>";
}

static const char *workbook_rels_xml(void)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
           "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>\n"
           "  <Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>\n"
           "</Relationships>";
}

static const char *styles_xml(void)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
           "  <fonts count=\"1\"><font><sz val=\"11\"/><color theme=\"1\"/><name val=\"Calibri\"/><family val=\"2\"/><scheme val=\"minor\"/></font></fonts>\n"
           "  <fills count=\"1\"><fill><patternFill patternType=\"none\"/></fill></fills>\n"
           "  <borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>\n"
           "  <cellStyleXfs count=\"1\"><xf/></cellStyleXfs>\n"
           "  <cellXfs count=\"1\"><xf xfId=\"0\"/></cellXfs>\n"
           "  <cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>\n"
           "</styleSheet>";
}

static bool ends_with(const char *s, const char *suf)
{
    if (!s || !suf) {
        return false;
    }
    size_t ls = strlen(s);
    size_t lsf = strlen(suf);
    return (ls >= lsf) && (strcmp(s + ls - lsf, suf) == 0);
}

static char *basename_no_ext(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) {
        base = bslash + 1;
    }
#endif
    size_t len = strlen(base);
    if (len > 5 && ends_with(base, ".xlsx")) {
        len -= 5;
    } else if (len > 4 && ends_with(base, ".xls")) {
        len -= 4;
    }
    char *name = (char *)malloc(len + 1);
    if (!name) {
        return NULL;
    }
    memcpy(name, base, len);
    name[len] = '\0';
    return name;
}

static char *trim_inplace(char *s)
{
    if (!s) {
        return s;
    }
    char *end;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    return s;
}

static void parse_header(const char *cell, char **out_name, DataType *out_type)
{
    *out_type = TYPE_UNKNOWN;
    const char *open = strrchr(cell, '(');
    const char *close = (open ? strrchr(cell, ')') : NULL);
    if (open && close && close > open) {
        size_t name_len = (size_t)(open - cell);
        char *name = (char *)malloc(name_len + 1);
        if (!name) {
            *out_name = NULL;
            return;
        }
        memcpy(name, cell, name_len);
        name[name_len] = '\0';
        char *tname = trim_inplace(name);
        if (tname != name) {
            memmove(name, tname, strlen(tname) + 1);
        }
        size_t type_len = (size_t)(close - open - 1);
        char *typ = (char *)malloc(type_len + 1);
        if (!typ) {
            free(name);
            *out_name = NULL;
            return;
        }
        memcpy(typ, open + 1, type_len);
        typ[type_len] = '\0';
        char *ttyp = trim_inplace(typ);
        if (ttyp != typ) {
            memmove(typ, ttyp, strlen(ttyp) + 1);
        }
        *out_name = name;
        *out_type = parse_type_from_string(typ);
        free(typ);
    } else {
        *out_name = strdup(cell);
    }
}

static int is_int_str(const char *s)
{
    if (!s || !*s) {
        return 0;
    }
    const char *p = s;
    if (*p == '-' || *p == '+') {
        p++;
    }
    if (!*p) {
        return 0;
    }
    for (; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

static int is_float_str(const char *s)
{
    if (!s || !*s) {
        return 0;
    }
    char *end = NULL;
    strtod(s, &end);
    return end && *end == '\0';
}

static int is_bool_str(const char *s)
{
    if (!s) {
        return 0;
    }
    return strcmp(s, "true") == 0 || strcmp(s, "false") == 0;
}

static DataType infer_type_for_column(char **cells, int rows)
{
    int all_int = 1, all_float = 1, all_bool = 1;
    for (int i = 0; i < rows; ++i) {
        const char *v = cells[i] ? cells[i] : "";
        if (v[0] == '\0') {
            all_int = all_float = all_bool = 0;
            break;
        }
        if (!is_int_str(v)) {
            all_int = 0;
        }
        if (!is_float_str(v)) {
            all_float = 0;
        }
        if (!is_bool_str(v)) {
            all_bool = 0;
        }
        if (!all_int && !all_float && !all_bool) {
            return TYPE_STR;
        }
    }
    if (all_bool) {
        return TYPE_BOOL;
    }
    if (all_int) {
        return TYPE_INT;
    }
    if (all_float) {
        return TYPE_FLOAT;
    }
    return TYPE_STR;
}

static int write_xlsx(const char *path,
                      const char *sheet_name,
                      const char *const *table,
                      size_t rows,
                      size_t cols)
{
    if (!path || !table) {
        return -1;
    }

    char safe_sheet_name[MAX_SHEET_NAME_LEN + 1];
    if (ensure_sheet_name(safe_sheet_name, sizeof(safe_sheet_name), sheet_name) != 0) {
        return -1;
    }

    char *sheet_xml = build_sheet_xml(safe_sheet_name, table, rows, cols);
    if (!sheet_xml) {
        return -1;
    }

    char *workbook_xml = build_workbook_xml(safe_sheet_name);
    if (!workbook_xml) {
        free(sheet_xml);
        return -1;
    }

    struct zip_source sources[] = {
        {.name = "[Content_Types].xml", .data = (const unsigned char *)content_types_xml()},
        {.name = "_rels/.rels", .data = (const unsigned char *)rels_root_xml()},
        {.name = "xl/workbook.xml", .data = (const unsigned char *)workbook_xml},
        {.name = "xl/_rels/workbook.xml.rels", .data = (const unsigned char *)workbook_rels_xml()},
        {.name = "xl/worksheets/sheet1.xml", .data = (const unsigned char *)sheet_xml},
        {.name = "xl/styles.xml", .data = (const unsigned char *)styles_xml()},
    };

    size_t source_count = sizeof(sources) / sizeof(sources[0]);
    for (size_t i = 0; i < source_count; ++i) {
        sources[i].size = strlen((const char *)sources[i].data);
        sources[i].crc32 = crc32(0L, sources[i].data, (uInt)sources[i].size);
    }

    int rc = zip_write_files(path, sources, source_count);

    free(sheet_xml);
    free(workbook_xml);
    return rc;
}
