#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    //*Выделение памяти в куче для объекта заданного размера
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    object->next = vm.objects;
    vm.objects = object;

    return object;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    //* Создает новую строку ObjString в куче, а затем инициализирует ее поля
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    return string;
}

static uint32_t hashString(const char* key, int length) {
    //* FNV-1a
    //* Вычисление хэша строки
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; ++i) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* takeString(char* chars, int length) {
    //* тановится владельцем существующей динамически выделяемой строки
    uint32_t hash = hashString(chars, length);
    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
    //* выделение памяти из кучи для строки
    uint32_t hash = hashString(chars, length); //* Вычисляем хэш строки
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
    }
}