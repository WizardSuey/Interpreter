#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);

        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }
    resetStack();
}

static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL((Obj*)copyString(name, (int)(strlen(name)))));
    push(OBJ_VAL((Obj*)newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);
    defineNative("clock", clockNative);
}

void freeVM() {
    freeObjects();
    freeTable(&vm.globals);
    freeTable(&vm.strings);
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) {
    // возвращает значение из стека, но не извлекает его. 
    // Аргумент distance указывает, насколько глубоко нужно заглянуть в стек: 
    // ноль — это верхняя часть, единица — на один слот ниже и т. д.
    return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

/*
 * Вызывает функцию или метод, представленный заданным значением.
 *
 * Эта функция пытается вызвать вызываемый объект с указанным количеством
 *аргументов argCount. Если вызываемый объект является объектом типа OBJ_FUNCTION,
 * он вызывает функцию, используя функцию `call`. Если `вызываемый объект` не является
 * вызываемым объектом, он выдает ошибку во время выполнения.
 *
 * @param callee `Value`, представляющее вызываемую функцию или метод.
 * @param argCount - количество аргументов, передаваемых функции.
 * @return значение "true", если функция была успешно вызвана, и "false" в противном случае.
 */
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

/*
 * Captures a local variable, creating a new upvalue.
 *
 * @param local the address of the local variable to capture.
 * @return a pointer to the newly created upvalue.
 */
static ObjUpvalue* captureUpvalue(Value* local) {
    /*
    * Мы начинаем с начала списка, то есть с upvalue, ближайшего к вершине стека. 
    * Мы проходим по списку, используя небольшое сравнение указателей, 
    * чтобы перебрать все upvalue, указывающие на слоты выше того, который мы ищем. 
    * При этом мы отслеживаем предыдущее upvalue в списке. 
    * Если в итоге мы вставим узел после него, нам нужно будет обновить next указатель этого узла
    */
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

/*
 * Она закрывает все открытые значения upvalue, которые могут указывать на этот слот или любой слот над ним в стеке
 * @param last указатель на слот в стеке
*/
static void closedUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL((Obj*)result));
}

static InterpretResult run() {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    #define READ_BYTE() (*frame->ip++)
    #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
    // * Объединение старшего и младшего байтов в одно 16-разрядное целое число
    //* vm.ip[-2] считывает старший байт 16-разрядного целого числа.
    //* vm.ip[-1] считывает младший байт 16-разрядного целого числа.
    //* << 8 сдвигает старший байт на 8 бит влево, эффективно умножая его на 256.
    //* | выполняет побитовую операцию OR для объединения старшего и младшего байтов в одно 16-разрядное целое число.
    #define READ_SHORT() \
        (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtimeError("Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while (false)
    
    for(;;) {
        #ifdef DEBUG_TRACE_EXECUTION
            printf(" ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
        #endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT:{
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                //* обрабатывает команду OP_DEFINE_GLOBAL. 
                //* Он определяет глобальную переменную с именем, считываемым из пула констант (READ_STRING()), 
                //* и присваивает ей значение, которое в данный момент находится на вершине стека (peek(0)). 
                //* Затем значение удаляется из стека (pop()).
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                //* обрабатывает инструкцию OP_SET_GLOBAL. 
                //* Он пытается присвоить глобальной переменной с заданным именем (READ_STRING()) значение, 
                //* находящееся в верхней части стека (peek(0)). 
                //* Если переменная еще не определена, функция tableSet() возвращает значение true, и код удаляет переменную 
                //* и сообщает об ошибке во время выполнения, указывая, что переменная не определена.
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if(IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop()))); 
                break;
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                /*
                * Нам нужно знать вызываемую функцию и количество переданных ей аргументов. 
                * Последнее мы получаем из операнда инструкции
                */
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                //* 
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL((Obj*)closure));
                //* Теперь мы должны проинициализировать upvalues
                //* нашего замыкания.
                //* читаем количество upvalues из байт-кода
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        //* Если upvalue является локальным, 
                        //* мы захватываем upvalue из текущего фрейма
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        //* Если upvalue не является локальным, 
                        //* мы просто копируем upvalue из текущего замыкания
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
            /*
            * Когда мы доходим до этой инструкции, переменная, которую мы поднимаем, оказывается в верхней части стека. 
            * Мы вызываем вспомогательную функцию, передавая ей адрес этого слота в стеке. 
            * Эта функция отвечает за закрытие upvalue и перемещение локальной переменной из стека в кучу. 
            * После этого виртуальная машина может удалить слот из стека, что она и делает, вызывая pop()
            */
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            case OP_RETURN: {
                Value result = pop();
                closedUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }  
        }
    }

    #undef BINARY_OP
    #undef READ_STRING
    #undef READ_SHORT
    #undef READ_CONSTANT
    #undef READ_BYTE
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL((Obj*)function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL((Obj*)closure));
    call(closure, 0);
    return run();
}
