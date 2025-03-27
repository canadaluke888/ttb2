#ifndef TABLECRAFT_H
#define TABLECRAFT_H

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR,
    TYPE_BOOL
} DataType;

typedef struct {
    char *name;
    DataType type;
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
} Table;

Table *create_table(const char *name);
void free_table(Table *table);

int add_column(Table *table, const char *name, DataType type);
int add_row(Table *table, const char **input_strings);

const char *type_to_string(DataType type);
DataType parse_type_from_string(const char *str);

#endif
