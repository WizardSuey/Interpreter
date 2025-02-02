#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
    ObjString* key;
    Value value;
} Entry; // Структура Ключ/Значение

typedef struct {
    int count;
    int capacity;
    Entry* entries;
} Table; // Хэш-таблица

void initTable(Table* table);
void freeTable(Table* table);
bool tableSet(Table* table, ObjString* key, Value value);

#endif