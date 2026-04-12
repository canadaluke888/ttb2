#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include "data/table_view.h"

typedef struct {
    const Table *table;
    int sort_col;
    int descending;
} SortContext;

static SortContext g_sort_ctx;

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int parse_numeric_value(DataType type, const char *text, double *out)
{
    char *endptr = NULL;

    if (!text || !*text || !out) return -1;

    switch (type) {
        case TYPE_INT:
            *out = strtol(text, &endptr, 10);
            return (*endptr == '\0') ? 0 : -1;
        case TYPE_FLOAT:
            *out = strtod(text, &endptr);
            return (*endptr == '\0') ? 0 : -1;
        case TYPE_BOOL:
            if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0) {
                *out = 1.0;
                return 0;
            }
            if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0) {
                *out = 0.0;
                return 0;
            }
            return -1;
        default:
            return -1;
    }
}

static int cell_to_string(const Table *table, int row, int col, char *buf, size_t buf_sz)
{
    void *v;

    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count || !buf || buf_sz == 0) {
        return -1;
    }

    v = table->rows[row].values ? table->rows[row].values[col] : NULL;
    if (!v) {
        buf[0] = '\0';
        return 0;
    }

    switch (table->columns[col].type) {
        case TYPE_INT:
            snprintf(buf, buf_sz, "%d", *(int *)v);
            break;
        case TYPE_FLOAT:
            snprintf(buf, buf_sz, "%g", *(float *)v);
            break;
        case TYPE_BOOL:
            snprintf(buf, buf_sz, "%s", (*(int *)v) ? "true" : "false");
            break;
        case TYPE_STR:
            snprintf(buf, buf_sz, "%s", (char *)v);
            break;
        default:
            return -1;
    }
    return 0;
}

static int ci_contains(const char *hay, const char *need)
{
    size_t nlen;

    if (!hay || !need) return 0;
    nlen = strlen(need);
    if (nlen == 0) return 1;

    for (int pos = 0; hay[pos]; ++pos) {
        size_t i = 0;
        while (hay[pos + i] && i < nlen) {
            char a = hay[pos + i];
            char b = need[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static int row_matches_filter(const Table *table, int row, const FilterRule *rule)
{
    char cell_buf[128];
    DataType type;

    if (!table || !rule || rule->col < 0 || rule->col >= table->column_count) return 0;
    if (cell_to_string(table, row, rule->col, cell_buf, sizeof(cell_buf)) != 0) return 0;

    type = table->columns[rule->col].type;
    if (rule->op == FILTER_CONTAINS) {
        return ci_contains(cell_buf, rule->value);
    }

    if (type == TYPE_STR) {
        int cmp = strcmp(cell_buf, rule->value);
        switch (rule->op) {
            case FILTER_EQUALS: return strcasecmp(cell_buf, rule->value) == 0;
            case FILTER_GT: return cmp > 0;
            case FILTER_LT: return cmp < 0;
            case FILTER_GTE: return cmp >= 0;
            case FILTER_LTE: return cmp <= 0;
            default: return 0;
        }
    } else {
        double left = 0.0;
        double right = 0.0;
        if (parse_numeric_value(type, cell_buf, &left) != 0 || parse_numeric_value(type, rule->value, &right) != 0) {
            return 0;
        }
        switch (rule->op) {
            case FILTER_EQUALS: return left == right;
            case FILTER_GT: return left > right;
            case FILTER_LT: return left < right;
            case FILTER_GTE: return left >= right;
            case FILTER_LTE: return left <= right;
            default: return 0;
        }
    }
}

static int compare_rows(const void *a, const void *b)
{
    int row_a = *(const int *)a;
    int row_b = *(const int *)b;
    const Table *table = g_sort_ctx.table;
    int col = g_sort_ctx.sort_col;
    int desc = g_sort_ctx.descending;
    void *va;
    void *vb;
    int cmp = 0;

    va = table->rows[row_a].values ? table->rows[row_a].values[col] : NULL;
    vb = table->rows[row_b].values ? table->rows[row_b].values[col] : NULL;

    if (!va && !vb) cmp = 0;
    else if (!va) cmp = -1;
    else if (!vb) cmp = 1;
    else {
        switch (table->columns[col].type) {
            case TYPE_INT: {
                int ia = *(int *)va;
                int ib = *(int *)vb;
                cmp = (ia > ib) - (ia < ib);
                break;
            }
            case TYPE_FLOAT: {
                float fa = *(float *)va;
                float fb = *(float *)vb;
                cmp = (fa > fb) - (fa < fb);
                break;
            }
            case TYPE_BOOL: {
                int ba = *(int *)va;
                int bb = *(int *)vb;
                cmp = (ba > bb) - (ba < bb);
                break;
            }
            case TYPE_STR:
                cmp = strcasecmp((char *)va, (char *)vb);
                break;
            default:
                cmp = 0;
                break;
        }
    }

    if (cmp == 0) cmp = (row_a > row_b) - (row_a < row_b);
    return desc ? -cmp : cmp;
}

void tableview_init(TableView *view)
{
    if (!view) return;
    memset(view, 0, sizeof(*view));
    view->sort_col = -1;
    view->filter_rule.col = -1;
}

void tableview_free(TableView *view)
{
    if (!view) return;
    free(view->row_map);
    tableview_init(view);
}

int tableview_rebuild(Table *table, TableView *view, char *err, size_t err_sz)
{
    int *map = NULL;
    int count = 0;

    if (!table || !view) {
        set_err(err, err_sz, "No table view");
        return -1;
    }

    if (!view->filter_active && !view->sort_active) {
        free(view->row_map);
        view->row_map = NULL;
        view->row_map_count = 0;
        return 0;
    }

    if (table->row_count > 0) {
        map = malloc((size_t)table->row_count * sizeof(int));
        if (!map) {
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    for (int row = 0; row < table->row_count; ++row) {
        if (!view->filter_active || row_matches_filter(table, row, &view->filter_rule)) {
            map[count++] = row;
        }
    }

    if (view->sort_active && count > 1) {
        g_sort_ctx.table = table;
        g_sort_ctx.sort_col = view->sort_col;
        g_sort_ctx.descending = view->sort_desc;
        qsort(map, (size_t)count, sizeof(int), compare_rows);
    }

    free(view->row_map);
    view->row_map = map;
    view->row_map_count = count;
    return 0;
}

int tableview_sort(Table *table, TableView *view, int col, int descending, char *err, size_t err_sz)
{
    if (!table || !view || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid sort column");
        return -1;
    }
    view->sort_active = 1;
    view->sort_col = col;
    view->sort_desc = descending ? 1 : 0;
    return tableview_rebuild(table, view, err, err_sz);
}

void tableview_clear_sort(TableView *view)
{
    if (!view) return;
    view->sort_active = 0;
    view->sort_col = -1;
    view->sort_desc = 0;
}

int tableview_apply_filter(Table *table, TableView *view, const FilterRule *rule, char *err, size_t err_sz)
{
    DataType type;
    double numeric_value;

    if (!table || !view || !rule || rule->col < 0 || rule->col >= table->column_count) {
        set_err(err, err_sz, "Invalid filter");
        return -1;
    }
    if (rule->value[0] == '\0') {
        set_err(err, err_sz, "Filter value is required");
        return -1;
    }

    type = table->columns[rule->col].type;
    if (rule->op != FILTER_CONTAINS && type != TYPE_STR) {
        if (parse_numeric_value(type, rule->value, &numeric_value) != 0) {
            set_err(err, err_sz, "Filter value does not match column type");
            return -1;
        }
    }

    view->filter_active = 1;
    view->filter_rule = *rule;
    return tableview_rebuild(table, view, err, err_sz);
}

void tableview_clear_filter(TableView *view)
{
    if (!view) return;
    view->filter_active = 0;
    view->filter_rule.col = -1;
    view->filter_rule.op = FILTER_CONTAINS;
    view->filter_rule.value[0] = '\0';
}

int tableview_row_to_actual(const Table *table, const TableView *view, int visible_row)
{
    if (!table || visible_row < 0) return -1;
    if (!view || (!view->filter_active && !view->sort_active)) {
        return (visible_row < table->row_count) ? visible_row : -1;
    }
    if (visible_row >= view->row_map_count) return -1;
    return view->row_map[visible_row];
}

int tableview_visible_row_count(const Table *table, const TableView *view)
{
    if (!table) return 0;
    if (!view || (!view->filter_active && !view->sort_active)) return table->row_count;
    return view->row_map_count;
}

const char *tableview_filter_op_label(FilterOp op)
{
    switch (op) {
        case FILTER_CONTAINS: return "contains";
        case FILTER_EQUALS: return "=";
        case FILTER_GT: return ">";
        case FILTER_LT: return "<";
        case FILTER_GTE: return ">=";
        case FILTER_LTE: return "<=";
        default: return "?";
    }
}

int tableview_describe(const Table *table, const TableView *view, char *buf, size_t buf_sz)
{
    int visible_rows;
    int written = 0;

    if (!buf || buf_sz == 0 || !table) return -1;
    visible_rows = tableview_visible_row_count(table, view);
    written = snprintf(buf, buf_sz, "View: %d/%d rows", visible_rows, table->row_count);
    if (written < 0 || (size_t)written >= buf_sz) return -1;

    if (!view || (!view->filter_active && !view->sort_active)) return 0;

    if (view->filter_active &&
        view->filter_rule.col >= 0 &&
        view->filter_rule.col < table->column_count) {
        written += snprintf(buf + written,
                            buf_sz - (size_t)written,
                            " | Filter %s %s %s",
                            table->columns[view->filter_rule.col].name,
                            tableview_filter_op_label(view->filter_rule.op),
                            view->filter_rule.value);
        if ((size_t)written >= buf_sz) return -1;
    }

    if (view->sort_active &&
        view->sort_col >= 0 &&
        view->sort_col < table->column_count) {
        written += snprintf(buf + written,
                            buf_sz - (size_t)written,
                            " | Sort %s %s",
                            table->columns[view->sort_col].name,
                            view->sort_desc ? "desc" : "asc");
        if ((size_t)written >= buf_sz) return -1;
    }

    return 0;
}
