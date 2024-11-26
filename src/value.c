#include <stdio.h>

#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
    // Инициализация пула констант (обнуление)
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    // Запись значения в пул констант
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    // Освобождение пула констант
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
    // Печать значения
    printf("%g", value);
}