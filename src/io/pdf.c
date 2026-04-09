#include "pdf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PDF_PAGE_WIDTH 612.0
#define PDF_PAGE_HEIGHT 792.0
#define PDF_MARGIN 36.0
#define PDF_TITLE_SIZE 14.0
#define PDF_TEXT_SIZE 8.0
#define PDF_ROW_HEIGHT 16.0
#define PDF_CELL_PADDING 3.0
#define PDF_TABLE_TOP 724.0

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PdfBuffer;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} PageList;

static void set_err(char *err, size_t err_sz, const char *fmt, ...)
{
    if (!err || err_sz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_sz, fmt, ap);
    va_end(ap);
}

static int buf_reserve(PdfBuffer *buf, size_t add)
{
    if (!buf) return -1;
    if (add > (size_t)-1 - buf->len - 1) return -1;
    size_t needed = buf->len + add + 1;
    if (needed <= buf->cap) return 0;
    size_t next = buf->cap ? buf->cap * 2 : 1024;
    while (next < needed) {
        if (next > (size_t)-1 / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    char *data = (char *)realloc(buf->data, next);
    if (!data) return -1;
    buf->data = data;
    buf->cap = next;
    return 0;
}

static int buf_append_len(PdfBuffer *buf, const char *s, size_t len)
{
    if (buf_reserve(buf, len) != 0) return -1;
    memcpy(buf->data + buf->len, s, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 0;
}

static int buf_append(PdfBuffer *buf, const char *s)
{
    return buf_append_len(buf, s, strlen(s));
}

static int buf_appendf(PdfBuffer *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(ap);
        return -1;
    }
    if (buf_reserve(buf, (size_t)needed) != 0) {
        va_end(ap);
        return -1;
    }
    vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, ap);
    buf->len += (size_t)needed;
    va_end(ap);
    return 0;
}

static char *pdf_escape_text(const char *s)
{
    PdfBuffer out = {0};
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    for (; *p; ++p) {
        unsigned char ch = *p;
        if (ch == '(' || ch == ')' || ch == '\\') {
            char esc[2] = {'\\', (char)ch};
            if (buf_append_len(&out, esc, sizeof(esc)) != 0) {
                free(out.data);
                return NULL;
            }
        } else if (ch == '\t') {
            if (buf_append(&out, " ") != 0) {
                free(out.data);
                return NULL;
            }
        } else if (ch < 32 || ch > 126) {
            if (buf_append(&out, "?") != 0) {
                free(out.data);
                return NULL;
            }
        } else {
            char raw = (char)ch;
            if (buf_append_len(&out, &raw, 1) != 0) {
                free(out.data);
                return NULL;
            }
        }
    }
    if (!out.data) {
        out.data = strdup("");
    }
    return out.data;
}

static char *truncate_for_cell(const char *s, double cell_width)
{
    const char *src = s ? s : "";
    double usable = cell_width - (PDF_CELL_PADDING * 2.0);
    if (usable <= 0.0) return strdup("");

    size_t max_chars = (size_t)(usable / (PDF_TEXT_SIZE * 0.50));
    if (max_chars == 0) return strdup("");
    size_t len = strlen(src);
    if (len <= max_chars) return strdup(src);
    if (max_chars <= 3) {
        char *out = (char *)malloc(max_chars + 1);
        if (!out) return NULL;
        memcpy(out, src, max_chars);
        out[max_chars] = '\0';
        return out;
    }
    char *out = (char *)malloc(max_chars + 1);
    if (!out) return NULL;
    memcpy(out, src, max_chars - 3);
    memcpy(out + max_chars - 3, "...", 3);
    out[max_chars] = '\0';
    return out;
}

static char *cell_to_string(const Table *table, int row, int col)
{
    if (!table || row < 0 || col < 0 || row >= table->row_count || col >= table->column_count) {
        return strdup("");
    }

    void *v = table->rows[row].values ? table->rows[row].values[col] : NULL;
    if (!v) return strdup("");

    char buffer[128];
    switch (table->columns[col].type) {
    case TYPE_INT:
        snprintf(buffer, sizeof(buffer), "%d", *(int *)v);
        return strdup(buffer);
    case TYPE_FLOAT:
        snprintf(buffer, sizeof(buffer), "%g", *(float *)v);
        return strdup(buffer);
    case TYPE_BOOL:
        return strdup((*(int *)v) ? "true" : "false");
    case TYPE_STR:
    case TYPE_UNKNOWN:
    default:
        return strdup((const char *)v);
    }
}

static int append_pdf_text(PdfBuffer *buf, double x, double y, double size, const char *text)
{
    char *escaped = pdf_escape_text(text);
    if (!escaped) return -1;
    int rc = buf_appendf(buf, "BT /F1 %.1f Tf %.2f %.2f Td (%s) Tj ET\n", size, x, y, escaped);
    free(escaped);
    return rc;
}

static int append_cell_text(PdfBuffer *buf, double x, double y, double cell_width, const char *text)
{
    char *truncated = truncate_for_cell(text, cell_width);
    if (!truncated) return -1;
    int rc = append_pdf_text(buf, x + PDF_CELL_PADDING, y + 5.0, PDF_TEXT_SIZE, truncated);
    free(truncated);
    return rc;
}

static int append_table_grid(PdfBuffer *buf, double left, double top, double width, double cell_width, int cols, int rows)
{
    double bottom = top - (PDF_ROW_HEIGHT * rows);
    if (buf_append(buf, "0.72 w\n") != 0) return -1;
    for (int r = 0; r <= rows; ++r) {
        double y = top - (PDF_ROW_HEIGHT * r);
        if (buf_appendf(buf, "%.2f %.2f m %.2f %.2f l S\n", left, y, left + width, y) != 0) return -1;
    }
    for (int c = 0; c <= cols; ++c) {
        double x = left + (cell_width * c);
        if (buf_appendf(buf, "%.2f %.2f m %.2f %.2f l S\n", x, top, x, bottom) != 0) return -1;
    }
    return 0;
}

static int render_page(const Table *table,
                       int start_row,
                       int row_count,
                       int page_number,
                       int page_total,
                       char **out)
{
    PdfBuffer buf = {0};
    double table_width = PDF_PAGE_WIDTH - (PDF_MARGIN * 2.0);
    double cell_width = table_width / (double)table->column_count;
    int visual_rows = row_count + 1;

    if (append_pdf_text(&buf, PDF_MARGIN, PDF_PAGE_HEIGHT - PDF_MARGIN, PDF_TITLE_SIZE,
                        table->name ? table->name : "Untitled Table") != 0) goto fail;
    if (page_total > 1) {
        char page_label[64];
        snprintf(page_label, sizeof(page_label), "Page %d of %d", page_number, page_total);
        if (append_pdf_text(&buf, PDF_PAGE_WIDTH - 92.0, PDF_PAGE_HEIGHT - PDF_MARGIN, PDF_TEXT_SIZE, page_label) != 0) goto fail;
    }

    if (buf_append(&buf, "0.90 0.90 0.90 rg\n") != 0) goto fail;
    if (buf_appendf(&buf, "%.2f %.2f %.2f %.2f re f\n",
                    PDF_MARGIN, PDF_TABLE_TOP - PDF_ROW_HEIGHT, table_width, PDF_ROW_HEIGHT) != 0) goto fail;
    if (buf_append(&buf, "0 0 0 rg\n") != 0) goto fail;

    if (append_table_grid(&buf, PDF_MARGIN, PDF_TABLE_TOP, table_width, cell_width,
                          table->column_count, visual_rows) != 0) goto fail;

    double y = PDF_TABLE_TOP - PDF_ROW_HEIGHT;
    for (int c = 0; c < table->column_count; ++c) {
        const char *name = table->columns[c].name ? table->columns[c].name : "";
        if (append_cell_text(&buf, PDF_MARGIN + (cell_width * c), y, cell_width, name) != 0) goto fail;
    }

    for (int r = 0; r < row_count; ++r) {
        y = PDF_TABLE_TOP - (PDF_ROW_HEIGHT * (r + 2));
        for (int c = 0; c < table->column_count; ++c) {
            char *cell = cell_to_string(table, start_row + r, c);
            if (!cell) goto fail;
            int rc = append_cell_text(&buf, PDF_MARGIN + (cell_width * c), y, cell_width, cell);
            free(cell);
            if (rc != 0) goto fail;
        }
    }

    *out = buf.data ? buf.data : strdup("");
    return *out ? 0 : -1;

fail:
    free(buf.data);
    return -1;
}

static void page_list_free(PageList *pages)
{
    if (!pages) return;
    for (size_t i = 0; i < pages->count; ++i) free(pages->items[i]);
    free(pages->items);
}

static int page_list_push(PageList *pages, char *content)
{
    if (pages->count == pages->cap) {
        size_t next = pages->cap ? pages->cap * 2 : 8;
        char **items = (char **)realloc(pages->items, sizeof(char *) * next);
        if (!items) return -1;
        pages->items = items;
        pages->cap = next;
    }
    pages->items[pages->count++] = content;
    return 0;
}

static int build_pages(const Table *table, PageList *pages)
{
    int rows_per_page = (int)((PDF_TABLE_TOP - PDF_MARGIN) / PDF_ROW_HEIGHT) - 1;
    if (rows_per_page < 1) rows_per_page = 1;
    int total_pages = table->row_count > 0 ? (table->row_count + rows_per_page - 1) / rows_per_page : 1;

    for (int p = 0; p < total_pages; ++p) {
        int start = p * rows_per_page;
        int remaining = table->row_count - start;
        int count = remaining > rows_per_page ? rows_per_page : remaining;
        if (count < 0) count = 0;
        char *content = NULL;
        if (render_page(table, start, count, p + 1, total_pages, &content) != 0) return -1;
        if (page_list_push(pages, content) != 0) {
            free(content);
            return -1;
        }
    }
    return 0;
}

static int write_obj(FILE *f, long *offsets, int obj_id, const char *fmt, ...)
{
    offsets[obj_id] = ftell(f);
    if (offsets[obj_id] < 0) return -1;
    if (fprintf(f, "%d 0 obj\n", obj_id) < 0) return -1;

    va_list ap;
    va_start(ap, fmt);
    int rc = vfprintf(f, fmt, ap);
    va_end(ap);
    if (rc < 0) return -1;
    if (fprintf(f, "\nendobj\n") < 0) return -1;
    return 0;
}

static int write_pdf_file(const char *path, const PageList *pages)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    PdfBuffer kids = {0};
    int page_count = (int)pages->count;
    int catalog_id = 1;
    int pages_id = 2;
    int font_id = 3;
    int first_page_id = 4;
    int object_count = 3 + (page_count * 2);
    long *offsets = (long *)calloc((size_t)object_count + 1, sizeof(long));
    if (!offsets) {
        fclose(f);
        return -1;
    }

    if (fprintf(f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n") < 0) goto fail;

    if (write_obj(f, offsets, catalog_id, "<< /Type /Catalog /Pages %d 0 R >>", pages_id) != 0) goto fail;

    if (buf_append(&kids, "[") != 0) goto fail;
    for (int i = 0; i < page_count; ++i) {
        if (buf_appendf(&kids, " %d 0 R", first_page_id + (i * 2)) != 0) goto fail;
    }
    if (buf_append(&kids, " ]") != 0) goto fail;
    if (write_obj(f, offsets, pages_id, "<< /Type /Pages /Kids %s /Count %d >>", kids.data, page_count) != 0) {
        goto fail;
    }
    free(kids.data);
    kids.data = NULL;

    if (write_obj(f, offsets, font_id, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>") != 0) goto fail;

    for (int i = 0; i < page_count; ++i) {
        int page_id = first_page_id + (i * 2);
        int content_id = page_id + 1;
        if (write_obj(f, offsets, page_id,
                      "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.0f %.0f] "
                      "/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>",
                      pages_id, PDF_PAGE_WIDTH, PDF_PAGE_HEIGHT, font_id, content_id) != 0) goto fail;

        offsets[content_id] = ftell(f);
        if (offsets[content_id] < 0) goto fail;
        size_t len = strlen(pages->items[i]);
        if (fprintf(f, "%d 0 obj\n<< /Length %zu >>\nstream\n", content_id, len) < 0) goto fail;
        if (fwrite(pages->items[i], 1, len, f) != len) goto fail;
        if (fprintf(f, "endstream\nendobj\n") < 0) goto fail;
    }

    long xref = ftell(f);
    if (xref < 0) goto fail;
    if (fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", object_count + 1) < 0) goto fail;
    for (int i = 1; i <= object_count; ++i) {
        if (fprintf(f, "%010ld 00000 n \n", offsets[i]) < 0) goto fail;
    }
    if (fprintf(f,
                "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
                object_count + 1, catalog_id, xref) < 0) goto fail;

    free(offsets);
    if (fclose(f) != 0) return -1;
    return 0;

fail:
    free(kids.data);
    free(offsets);
    fclose(f);
    return -1;
}

int pdf_save(const Table *table, const char *path, char *err, size_t err_sz)
{
    if (err && err_sz) err[0] = '\0';
    if (!table || !path) {
        set_err(err, err_sz, "Invalid arguments");
        return -1;
    }
    if (table->column_count <= 0) {
        set_err(err, err_sz, "Table has no columns");
        return -1;
    }

    PageList pages = {0};
    if (build_pages(table, &pages) != 0) {
        page_list_free(&pages);
        set_err(err, err_sz, "Failed to build PDF pages");
        return -1;
    }

    int rc = write_pdf_file(path, &pages);
    page_list_free(&pages);
    if (rc != 0) {
        set_err(err, err_sz, "Failed to write PDF");
        return -1;
    }
    return 0;
}
