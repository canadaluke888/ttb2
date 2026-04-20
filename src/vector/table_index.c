/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Deterministic hashed row embeddings with SQLite-friendly persistence hooks. */

#include "vector/table_index.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db/book_db.h"

typedef struct {
    char **items;
    int count;
    int capacity;
} TokenList;

typedef struct {
    TableIndexMatch match;
    int keep;
} RankedMatch;

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0) return;
    snprintf(err, err_sz, "%s", msg ? msg : "");
}

static char *dup_string(const char *src)
{
    size_t len;
    char *out;

    if (!src) src = "";
    len = strlen(src);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, src, len + 1);
    return out;
}

static uint64_t fnv1a64_seed(uint64_t hash, const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;

    while (bytes && len-- > 0) {
        hash ^= (uint64_t)(*bytes++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t fnv1a64_text(uint64_t hash, const char *text)
{
    if (!text) text = "";
    return fnv1a64_seed(hash, text, strlen(text));
}

static uint64_t fnv1a64_u32(uint64_t hash, uint32_t value)
{
    return fnv1a64_seed(hash, &value, sizeof(value));
}

static int token_list_push(TokenList *list, const char *token)
{
    char **next_items;

    if (!list || !token || token[0] == '\0') return 0;

    if (list->count == list->capacity) {
        int next_capacity = list->capacity > 0 ? list->capacity * 2 : 16;

        next_items = (char **)realloc(list->items, (size_t)next_capacity * sizeof(char *));
        if (!next_items) return -1;
        list->items = next_items;
        list->capacity = next_capacity;
    }

    list->items[list->count] = dup_string(token);
    if (!list->items[list->count]) return -1;
    list->count++;
    return 0;
}

static void token_list_free(TokenList *list)
{
    int i;

    if (!list) return;
    for (i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int append_normalized_tokens(const char *text, TokenList *tokens, int *remaining)
{
    char buf[256];
    int len = 0;

    if (!text || !tokens || !remaining || *remaining <= 0) return 0;

    while (*text && *remaining > 0) {
        unsigned char ch = (unsigned char)*text;

        if (isalnum(ch)) {
            if (len < (int)sizeof(buf) - 1) buf[len++] = (char)tolower(ch);
        } else if (len > 0) {
            buf[len] = '\0';
            if (token_list_push(tokens, buf) != 0) return -1;
            len = 0;
            (*remaining)--;
        }
        ++text;
    }

    if (len > 0 && *remaining > 0) {
        buf[len] = '\0';
        if (token_list_push(tokens, buf) != 0) return -1;
        (*remaining)--;
    }

    return 0;
}

static int token_list_contains(const TokenList *list, const char *token)
{
    int i;

    if (!list || !token || token[0] == '\0') return 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], token) == 0) return 1;
    }
    return 0;
}

static void add_hashed_feature(float *vec, int dims, const char *feature, float weight)
{
    uint64_t hash;
    int index;
    float sign;

    if (!vec || dims <= 0 || !feature || feature[0] == '\0') return;

    hash = fnv1a64_text(1469598103934665603ULL, feature);
    index = (int)(hash % (uint64_t)dims);
    sign = ((hash >> 8U) & 1U) ? 1.0f : -1.0f;
    vec[index] += weight * sign;
}

static void add_token_features(const char *token, float *vec, const TableIndexConfig *config, float weight_scale)
{
    char tri[4];
    size_t len;
    size_t i;

    if (!token || !vec || !config) return;

    add_hashed_feature(vec, config->dimensions, token, config->unigram_weight * weight_scale);

    if (!config->use_char_trigrams) return;

    len = strlen(token);
    if (len < 3) {
        add_hashed_feature(vec, config->dimensions, token, config->trigram_weight * weight_scale);
        return;
    }

    for (i = 0; i + 2 < len; ++i) {
        tri[0] = token[i];
        tri[1] = token[i + 1];
        tri[2] = token[i + 2];
        tri[3] = '\0';
        add_hashed_feature(vec, config->dimensions, tri, config->trigram_weight * weight_scale);
    }
}

static float normalize_vector(float *vec, int dims)
{
    double sum = 0.0;
    double norm;
    int i;

    if (!vec || dims <= 0) return 0.0f;

    for (i = 0; i < dims; ++i) sum += (double)vec[i] * (double)vec[i];
    norm = sqrt(sum);
    if (norm <= 0.0) return 0.0f;
    for (i = 0; i < dims; ++i) vec[i] = (float)(vec[i] / norm);
    return (float)norm;
}

static const char *cell_text(const Table *table, int row, int col, char *buf, size_t buf_sz)
{
    void *value;

    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count || !buf || buf_sz == 0) {
        return NULL;
    }
    value = table->rows[row].values ? table->rows[row].values[col] : NULL;
    if (!value) return NULL;

    switch (table->columns[col].type) {
        case TYPE_INT:
            snprintf(buf, buf_sz, "%d", *(int *)value);
            return buf;
        case TYPE_FLOAT:
            snprintf(buf, buf_sz, "%.6g", *(float *)value);
            return buf;
        case TYPE_BOOL:
            snprintf(buf, buf_sz, "%s", *(int *)value ? "true" : "false");
            return buf;
        case TYPE_STR:
        case TYPE_UNKNOWN:
        default:
            return (const char *)value;
    }
}

static unsigned long long table_index_compute_fingerprint(const Table *table)
{
    uint64_t hash = 1469598103934665603ULL;
    int row;
    int col;

    if (!table) return 0ULL;

    hash = fnv1a64_text(hash, table->name ? table->name : "");
    hash = fnv1a64_u32(hash, (uint32_t)table->column_count);
    hash = fnv1a64_u32(hash, (uint32_t)table->row_count);

    for (col = 0; col < table->column_count; ++col) {
        hash = fnv1a64_text(hash, table->columns[col].name ? table->columns[col].name : "");
        hash = fnv1a64_u32(hash, (uint32_t)table->columns[col].type);
    }

    for (row = 0; row < table->row_count; ++row) {
        for (col = 0; col < table->column_count; ++col) {
            char buf[96];
            const char *text = cell_text(table, row, col, buf, sizeof(buf));

            hash = fnv1a64_u32(hash, (uint32_t)col);
            if (!text) {
                hash = fnv1a64_text(hash, "<null>");
            } else {
                hash = fnv1a64_text(hash, text);
            }
        }
    }

    return (unsigned long long)hash;
}

static int build_row_vector(const Table *table, int row, const TableIndexConfig *config, float *vec, float *norm_out)
{
    TokenList tokens = {0};
    int remaining;
    int col;
    int token_index;

    if (!table || !config || !vec || !norm_out) return -1;

    if (!config->row_vectorization_enabled) {
        *norm_out = 0.0f;
        return 0;
    }

    remaining = config->max_tokens_per_row > 0 ? config->max_tokens_per_row : 256;
    for (col = 0; col < table->column_count && remaining > 0; ++col) {
        const char *text;
        char cell_buf[96];
        TokenList local = {0};

        if (append_normalized_tokens(table->columns[col].name, &local, &remaining) != 0) {
            token_list_free(&local);
            token_list_free(&tokens);
            return -1;
        }

        for (token_index = 0; token_index < local.count; ++token_index) {
            add_token_features(local.items[token_index], vec, config, 1.20f);
            if (token_list_push(&tokens, local.items[token_index]) != 0) {
                token_list_free(&local);
                token_list_free(&tokens);
                return -1;
            }
        }
        token_list_free(&local);

        text = cell_text(table, row, col, cell_buf, sizeof(cell_buf));
        if (!text || text[0] == '\0') continue;

        if (append_normalized_tokens(text, &tokens, &remaining) != 0) {
            token_list_free(&tokens);
            return -1;
        }
    }

    for (token_index = 0; token_index < tokens.count; ++token_index) {
        add_token_features(tokens.items[token_index], vec, config, 1.0f);
    }

    *norm_out = normalize_vector(vec, config->dimensions);
    token_list_free(&tokens);
    return 0;
}

static void init_default_stats(TableIndexColumnStats *stats, int col, DataType type)
{
    if (!stats) return;
    stats->column_index = col;
    stats->type = type;
    stats->value_count = 0;
    stats->null_count = 0;
    stats->min_value = 0.0;
    stats->max_value = 0.0;
    stats->mean_value = 0.0;
    stats->has_values = 0;
}

static void compute_numeric_stats(const Table *table, TableIndex *index)
{
    int col;

    if (!table || !index || !index->column_stats) return;

    for (col = 0; col < table->column_count; ++col) {
        TableIndexColumnStats *stats = &index->column_stats[col];
        double sum = 0.0;
        int row;

        init_default_stats(stats, col, table->columns[col].type);
        if (table->columns[col].type != TYPE_INT && table->columns[col].type != TYPE_FLOAT) continue;

        for (row = 0; row < table->row_count; ++row) {
            void *value = table->rows[row].values ? table->rows[row].values[col] : NULL;
            double numeric_value;

            if (!value) {
                stats->null_count++;
                continue;
            }

            numeric_value = (table->columns[col].type == TYPE_INT)
                ? (double)(*(int *)value)
                : (double)(*(float *)value);

            if (!stats->has_values) {
                stats->min_value = numeric_value;
                stats->max_value = numeric_value;
                stats->has_values = 1;
            } else {
                if (numeric_value < stats->min_value) stats->min_value = numeric_value;
                if (numeric_value > stats->max_value) stats->max_value = numeric_value;
            }

            sum += numeric_value;
            stats->value_count++;
        }

        if (stats->value_count > 0) {
            stats->mean_value = sum / (double)stats->value_count;
        }
    }
}

static int ci_find(const char *hay, const char *need)
{
    size_t nlen;
    int pos;

    if (!hay || !need) return -1;
    nlen = strlen(need);
    if (nlen == 0) return 0;

    for (pos = 0; hay[pos]; ++pos) {
        size_t i = 0;

        while (hay[pos + (int)i] && i < nlen) {
            char a = hay[pos + (int)i];
            char b = need[i];

            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            ++i;
        }
        if (i == nlen) return pos;
    }

    return -1;
}

static float compute_lexical_score(const Table *table,
                                   int row,
                                   const char *query,
                                   const TokenList *query_tokens,
                                   int *best_col_out,
                                   int *match_start_out,
                                   int *match_len_out)
{
    float best_score = 0.0f;
    int best_col = 0;
    int best_start = -1;
    int best_len = 0;
    int col;

    if (!table || row < 0 || row >= table->row_count || !query || query[0] == '\0') {
        if (best_col_out) *best_col_out = 0;
        if (match_start_out) *match_start_out = -1;
        if (match_len_out) *match_len_out = 0;
        return 0.0f;
    }

    for (col = 0; col < table->column_count; ++col) {
        char cell_buf[128];
        const char *text = cell_text(table, row, col, cell_buf, sizeof(cell_buf));
        TokenList cell_tokens = {0};
        int remaining = 64;
        int query_matches = 0;
        int start = -1;
        float score = 0.0f;
        int q;

        if (text && text[0] != '\0') {
            start = ci_find(text, query);
            if (start >= 0) {
                score += 1.25f;
            }
            if (append_normalized_tokens(text, &cell_tokens, &remaining) == 0 && query_tokens && query_tokens->count > 0) {
                for (q = 0; q < query_tokens->count; ++q) {
                    if (token_list_contains(&cell_tokens, query_tokens->items[q])) query_matches++;
                }
            }
        }

        if (query_tokens && query_tokens->count > 0) {
            score += 0.70f * ((float)query_matches / (float)query_tokens->count);
        }
        if (table->columns[col].name && ci_find(table->columns[col].name, query) >= 0) {
            score += 0.25f;
        }

        token_list_free(&cell_tokens);

        if (score > best_score || (fabsf(score - best_score) < 0.0001f && col < best_col)) {
            best_score = score;
            best_col = col;
            best_start = start;
            best_len = (start >= 0) ? (int)strlen(query) : 0;
        }
    }

    if (best_col_out) *best_col_out = best_col;
    if (match_start_out) *match_start_out = best_start;
    if (match_len_out) *match_len_out = best_len;
    return best_score;
}

static int compare_ranked_match(const void *left, const void *right)
{
    const RankedMatch *a = (const RankedMatch *)left;
    const RankedMatch *b = (const RankedMatch *)right;

    if (a->keep != b->keep) return b->keep - a->keep;
    if (a->match.score < b->match.score) return 1;
    if (a->match.score > b->match.score) return -1;
    if (a->match.lexical_score < b->match.lexical_score) return 1;
    if (a->match.lexical_score > b->match.lexical_score) return -1;
    return a->match.actual_row - b->match.actual_row;
}

static void progress_update(const ProgressReporter *progress, double value, const char *message)
{
    if (progress && progress->update) {
        progress->update(progress->ctx, value, message);
    }
}

TableIndexConfig table_index_default_config(void)
{
    TableIndexConfig config;

    config.dimensions = 256;
    config.max_tokens_per_row = 128;
    config.use_char_trigrams = 1;
    config.include_numeric_stats = 1;
    config.row_vectorization_enabled = 1;
    config.unigram_weight = 1.0f;
    config.trigram_weight = 0.35f;
    config.lexical_weight = 0.85f;
    config.semantic_weight = 0.65f;
    config.config_version = 2U;
    return config;
}

int table_index_build_for_table_with_progress(const Table *table,
                                              const TableIndexConfig *config,
                                              const ProgressReporter *progress,
                                              TableIndex **out_index,
                                              char *err,
                                              size_t err_sz)
{
    TableIndexConfig effective;
    TableIndex *index = NULL;
    int row;

    if (out_index) *out_index = NULL;
    if (!table || !out_index) {
        set_err(err, err_sz, "Invalid table index build request");
        return -1;
    }

    effective = config ? *config : table_index_default_config();
    if (effective.dimensions <= 0) effective = table_index_default_config();

    index = (TableIndex *)calloc(1, sizeof(*index));
    if (!index) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    index->config = effective;
    index->fingerprint = table_index_compute_fingerprint(table);
    index->row_count = table->row_count;
    index->column_count = table->column_count;
    index->dimensions = effective.dimensions;

    if (effective.row_vectorization_enabled && index->row_count > 0) {
        index->row_ids = (int *)calloc((size_t)index->row_count, sizeof(int));
        index->embeddings = (float *)calloc((size_t)index->row_count * (size_t)index->dimensions, sizeof(float));
        index->norms = (float *)calloc((size_t)index->row_count, sizeof(float));
        if (!index->row_ids || !index->embeddings || !index->norms) {
            table_index_free(index);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    if (effective.include_numeric_stats && table->column_count > 0) {
        index->column_stats = (TableIndexColumnStats *)calloc((size_t)table->column_count, sizeof(*index->column_stats));
        if (!index->column_stats) {
            table_index_free(index);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    if (effective.row_vectorization_enabled) {
        progress_update(progress, 0.02, "Preparing row vectors...");
        for (row = 0; row < table->row_count; ++row) {
            float *vec = &index->embeddings[(size_t)row * (size_t)index->dimensions];

            index->row_ids[row] = row;
            if (build_row_vector(table, row, &effective, vec, &index->norms[row]) != 0) {
                table_index_free(index);
                set_err(err, err_sz, "Failed to build row vector");
                return -1;
            }
            if (table->row_count > 0 && ((row + 1) % 32 == 0 || row + 1 == table->row_count)) {
                char message[96];
                double value = 0.05 + (0.85 * ((double)(row + 1) / (double)table->row_count));

                snprintf(message, sizeof(message), "Encoding rows %d/%d...", row + 1, table->row_count);
                progress_update(progress, value, message);
            }
        }
    } else {
        progress_update(progress, 0.90, "Row vectorization disabled.");
    }

    if (index->column_stats) {
        progress_update(progress, 0.93, "Computing numeric summaries...");
        compute_numeric_stats(table, index);
    }

    progress_update(progress, 1.0, effective.row_vectorization_enabled ? "Row vectors ready." : "Search index ready.");

    *out_index = index;
    return 0;
}

int table_index_build_for_table(const Table *table,
                                const TableIndexConfig *config,
                                TableIndex **out_index,
                                char *err,
                                size_t err_sz)
{
    return table_index_build_for_table_with_progress(table, config, NULL, out_index, err, err_sz);
}

int table_index_query_with_progress(const TableIndex *index,
                                    const Table *table,
                                    const int *actual_rows,
                                    int actual_row_count,
                                    const char *query,
                                    TableIndexMatch *out,
                                    size_t max_out,
                                    const ProgressReporter *progress,
                                    char *err,
                                    size_t err_sz)
{
    RankedMatch *ranked = NULL;
    int row_i;
    size_t kept = 0;

    if (!index || !table || !out || max_out == 0 || !query || query[0] == '\0') {
        set_err(err, err_sz, "Invalid table index query");
        return -1;
    }
    if (actual_row_count <= 0) return 0;

    ranked = (RankedMatch *)calloc((size_t)actual_row_count, sizeof(*ranked));
    if (!ranked) {
        free(ranked);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    progress_update(progress, 0.02, "Searching visible rows...");

    for (row_i = 0; row_i < actual_row_count; ++row_i) {
        int actual_row = actual_rows[row_i];
        int best_col = 0;
        int match_start = -1;
        int match_len = 0;
        float lexical_score;

        if (actual_row < 0 || actual_row >= index->row_count) continue;

        lexical_score = compute_lexical_score(table,
                                              actual_row,
                                              query,
                                              NULL,
                                              &best_col,
                                              &match_start,
                                              &match_len);
        if (match_start >= 0) {
            ranked[row_i].keep = 1;
            ranked[row_i].match.actual_row = actual_row;
            ranked[row_i].match.best_col = best_col;
            ranked[row_i].match.match_start = match_start;
            ranked[row_i].match.match_len = match_len;
            ranked[row_i].match.score = lexical_score;
            ranked[row_i].match.lexical_score = lexical_score;
            ranked[row_i].match.semantic_score = 0.0f;
        }

        if (actual_row_count > 0 && (((row_i + 1) % 32) == 0 || row_i + 1 == actual_row_count)) {
            char message[96];
            double value = 0.08 + (0.84 * ((double)(row_i + 1) / (double)actual_row_count));

            snprintf(message, sizeof(message), "Checking rows %d/%d...", row_i + 1, actual_row_count);
            progress_update(progress, value, message);
        }
    }

    progress_update(progress, 0.95, "Sorting matches...");
    qsort(ranked, (size_t)actual_row_count, sizeof(*ranked), compare_ranked_match);
    for (row_i = 0; row_i < actual_row_count && kept < max_out; ++row_i) {
        if (!ranked[row_i].keep) break;
        out[kept++] = ranked[row_i].match;
    }

    free(ranked);
    progress_update(progress, 1.0, "Search complete.");
    return (int)kept;
}

int table_index_query(const TableIndex *index,
                      const Table *table,
                      const int *actual_rows,
                      int actual_row_count,
                      const char *query,
                      TableIndexMatch *out,
                      size_t max_out,
                      char *err,
                      size_t err_sz)
{
    return table_index_query_with_progress(index,
                                           table,
                                           actual_rows,
                                           actual_row_count,
                                           query,
                                           out,
                                           max_out,
                                           NULL,
                                           err,
                                           err_sz);
}

void table_index_invalidate(TableIndex **index_ptr)
{
    if (!index_ptr || !*index_ptr) return;
    table_index_free(*index_ptr);
    *index_ptr = NULL;
}

void table_index_free(TableIndex *index)
{
    if (!index) return;
    free(index->row_ids);
    free(index->embeddings);
    free(index->norms);
    free(index->column_stats);
    free(index);
}

static int config_matches(const TableIndex *index, const TableIndexConfig *config)
{
    if (!index || !config) return 0;
    return index->config.dimensions == config->dimensions &&
           index->config.max_tokens_per_row == config->max_tokens_per_row &&
           index->config.use_char_trigrams == config->use_char_trigrams &&
           index->config.include_numeric_stats == config->include_numeric_stats &&
           index->config.row_vectorization_enabled == config->row_vectorization_enabled &&
           fabsf(index->config.unigram_weight - config->unigram_weight) < 0.0001f &&
           fabsf(index->config.trigram_weight - config->trigram_weight) < 0.0001f &&
           fabsf(index->config.lexical_weight - config->lexical_weight) < 0.0001f &&
           fabsf(index->config.semantic_weight - config->semantic_weight) < 0.0001f &&
           index->config.config_version == config->config_version;
}

int table_index_sync_bookdb_with_progress(BookDB *db,
                                          const char *table_id,
                                          const Table *table,
                                          const TableIndexConfig *config,
                                          const ProgressReporter *progress,
                                          TableIndex **index_in_out,
                                          char *err,
                                          size_t err_sz)
{
    TableIndexConfig effective;
    TableIndex *loaded = NULL;
    TableIndex *built = NULL;
    unsigned long long fingerprint;

    if (!table || !index_in_out) {
        set_err(err, err_sz, "Invalid table index sync request");
        return -1;
    }

    effective = config ? *config : table_index_default_config();
    fingerprint = table_index_compute_fingerprint(table);

    if (*index_in_out &&
        !(*index_in_out)->stale &&
        (*index_in_out)->fingerprint == fingerprint &&
        (*index_in_out)->row_count == table->row_count &&
        (*index_in_out)->column_count == table->column_count &&
        config_matches(*index_in_out, &effective)) {
        progress_update(progress, 1.0, "Semantic index already up to date.");
        return 0;
    }

    if (effective.row_vectorization_enabled && db && table_id && *table_id) {
        progress_update(progress, 0.02, "Checking stored row vectors...");
        loaded = bookdb_load_semantic_index(db, table_id, err, err_sz);
        if (loaded &&
            !loaded->stale &&
            loaded->fingerprint == fingerprint &&
            loaded->row_count == table->row_count &&
            loaded->column_count == table->column_count &&
            config_matches(loaded, &effective)) {
            table_index_invalidate(index_in_out);
            *index_in_out = loaded;
            progress_update(progress, 1.0, "Loaded row vectors from SQLite cache.");
            return 0;
        }
        table_index_free(loaded);
    }

    if (table_index_build_for_table_with_progress(table, &effective, progress, &built, err, err_sz) != 0) return -1;

    if (effective.row_vectorization_enabled && db && table_id && *table_id) {
        char save_err[256] = {0};

        progress_update(progress, 0.97, "Saving row vectors to SQLite...");
        (void)bookdb_save_semantic_index(db, table_id, built, save_err, sizeof(save_err));
    }

    table_index_invalidate(index_in_out);
    *index_in_out = built;
    progress_update(progress, 1.0, effective.row_vectorization_enabled ? "Row vectors saved." : "Search index ready.");
    return 0;
}

int table_index_sync_bookdb(BookDB *db,
                            const char *table_id,
                            const Table *table,
                            const TableIndexConfig *config,
                            TableIndex **index_in_out,
                            char *err,
                            size_t err_sz)
{
    return table_index_sync_bookdb_with_progress(db, table_id, table, config, NULL, index_in_out, err, err_sz);
}
