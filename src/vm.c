#include <stdio.h>
#include <stdarg.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() {
    // Обнуление стека
    vm.stackTop = vm.stack;
}

static void runtimeError(const char* format, ...) {
    //* Сообщение об ошибке времени выполнения
    va_list args;   //* Произвольное число аргументов
    va_start(args, format); //* указание на последний обязательный аргумент
    vfprintf(stderr, format, args);
    va_end(args);   //* Выход из функции с переменным списком аргументов
    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk->code - 1;    //* Текущая инструкция, которая вызвала ошибку
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void initVM() {
    // Инициализация виртуальной машины
    resetStack();
}

void freeVM() {
    // Освобождение памяти виртуаьной машины
}

void push(Value value) {
    // Добавление значения в стек
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    // Извлечение значения из стека
    vm.stackTop--;
    return *vm.stackTop;
}

static Value peek(int distance) {
    //* Возвращает значение из стека, но не извлекает его
    return vm.stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
    //* nil и false ложны, а любое другое значение ведет себя как true
    return IS_NIL(value) || (IS_BOOL(value) && !IS_BOOL(value));
}

static InterpretResult run() {
    // Выполнение чанка кода
    #define READ_BYTE() (*vm.ip++)  // Сначала разымёвываем инструкцию, потом увеличиваем указатель
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])   // считывает следующий байт из байт-кода
    // Простая бинарная операция с двумя операндами
    // Использование do-while для возможности содержать несколько операторов в одном блоке 
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtimeError("OPerands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while (false); 
    for(;;) {
        #ifdef DEBUG_TRACE_EXECUTION
            printf("           ");
            // Печать значений из стека
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            // (int)(vm.ip - vm.chunk->code) - вычисление смещения байта
            disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
        #endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL:        push(NIL_VAL); break;
            case OP_TRUE:       push(BOOL_VAL(true)); break;
            case OP_FALSE:      push(BOOL_VAL(false)); break;
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:    BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:       BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD:        BINARY_OP(NUMBER_VAL, +); break;
            case OP_SUBTRACT:   BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY:   BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:     BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_RETURN: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }
    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    // Интерпретация чанка кода
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}

