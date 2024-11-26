#ifndef clox_memmory_h
#define clox_memmory_h

#include "common.h"

// Этот макрос вычисляет новую вместимость на основе заданной текущей вместимости
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

// Этот макрос украшает вызов функции reallocate(), \
    где и происходит настоящая работа. \
    Макрос сам заботится о получении размера типа элемента массива и \
    преобразовании полученного void* обратно в указатель правильного типа.
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

// Освобождение памяти массива
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

#endif