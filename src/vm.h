#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

//* один текущий вызов функции
/*
* для каждого вызова функции в реальном времени — каждого вызова, 
* который ещё не завершился — нам нужно отслеживать, 
* где в стеке начинаются локальные переменные этой функции и где должен возобновиться вызов
*/
typedef struct {
    ObjClosure* closure; //* указатель на замыкание (функцию и состояние переменных во время выполнения)
    //* Когда мы возвращаемся из функции, виртуальная машина переходит к ip фрейма CallFrame вызывающего объекта и продолжает работу оттуда
    uint8_t* ip; //*  вызывающий объект сохраняет свой собственный ip. 
    Value* slots; //* указывает на стек значений виртуальной машины в первом слоте, который может использовать эта функция
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals; // Таблица глобальных переменных
    Table strings; // Таблица строк для выполнения Интернирования строк
    ObjUpvalue* openUpvalues; // Список открытых upvalue
    Obj* objects; // Указатель на первый объект интрузивного списка. Сборщик мусора
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif