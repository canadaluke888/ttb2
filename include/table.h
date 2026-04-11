#ifndef TTB2_TABLE_H
#define TTB2_TABLE_H

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR,
    TYPE_BOOL,
    TYPE_UNKNOWN
} DataType;

typedef struct {
    char *name;
    DataType type;
    int color_pair_id;
} Column;

typedef struct {
    void **values;
} Row;

typedef struct {
    char *name;
    Column *columns;
    int column_count;

    Row *rows;
    int row_count;

    int capacity_columns;
    int capacity_rows;
    int dirty;
} Table;

Table *create_table(const char *name);
void free_table(Table *table);
void clear_table(Table *table, const char *name);
int replace_table_contents(Table *dest, Table *src);

int add_column(Table *table, const char *name, DataType type);
int add_row(Table *table, const char **input_strings);

const char *type_to_string(DataType type);
DataType parse_type_from_string(const char *str);

#endif
