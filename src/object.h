#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

#define OBJ_TYPE(value)         (AS_OBJ(value)->type) // Извлекаем тип объекта

#define IS_STRING(value) isObjType(value, OBJ_STRING) //*

#define AS_STRING(value)    ((ObjString*)AS_OBJ(value)) //*
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars) //*

typedef enum {
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next; //* Указатель на следующий объект в цепочке
};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
}; //* Структура объекта строки

ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    //* Проверяет, является ли значение объектом
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif