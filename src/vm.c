#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() {
    // Обнуление стека
    vm.stackTop = vm.stack;
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

static InterpretResult run() {
    // Выполнение чанка кода
    #define READ_BYTE() (*vm.ip++)  // Сначала разымёвываем инструкцию, потом увеличиваем указатель
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])   // считывает следующий байт из байт-кода
    // Простая бинарная операция с двумя операндами
    // Использование do-while для возможности содержать несколько операторов в одном блоке 
    #define BINARY_OP(op) \
        do { \
            double b = pop(); \
            double a = pop(); \
            push(a op b); \
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
            case OP_ADD:        BINARY_OP(+); break;
            case OP_SUBTRACT:   BINARY_OP(-); break;
            case OP_MULTIPLY:   BINARY_OP(*); break;
            case OP_DIVIDE:     BINARY_OP(/); break;
            case OP_NEGATE:     push(-pop()); break;
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

