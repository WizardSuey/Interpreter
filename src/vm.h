#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
    Chunk* chunk;   // Кусок кода, который бует выполняться
    // всегда указывает на следующую инструкцию
    uint8_t* ip;    // Указатель инструкций. Указывает на инструкцию, которая будет выполнена
    Value stack[STACK_MAX]; // Стек
    Value* stackTop; // Указатель на вершину стека. всегда указывает сразу за последним элементом
    Obj* objects; //* Список объектов
} VM;   // Структура виртуальной машины

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;  // Результат работы интерпретации чанка кода

extern VM vm;


void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif