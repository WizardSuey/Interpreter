#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;


typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR, // or
    PREC_AND, // and
    PREC_EQUALITY, // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM, // + -
    PREC_FACTOR, // * /
    PREC_UNARY, // ! -
    PREC_CALL, // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign); // указатель на Функцию, которая может быть вызвана

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth; //* Записывает глубину области видимости блока, в котором была объявлена локальная переменная
    bool isCaptured; //* Для определения, захвачена ли данна локальная переменная замыканием
} Local; //* Структура Local используется для хранения информации о локальной переменной

//* Upvalue используется для хранения информации о внешней переменной
//* @param index хранится информация о том, какой локальный слот захватывает upvalue 
//* @param isLocal 
typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
    /*
    * Связной список
    *  Каждый Compiler указывает на Compiler для функции, которая его содержит, 
    *  вплоть до корневого Compiler для кода верхнего уровня.
    */
    struct Compiler* enclosing;
    /*
    * компилятор всегда находится внутри какого-либо тела функции, 
    * а виртуальная машина всегда выполняет код, вызывая функцию. 
    * Как будто вся программа заключена в неявную main() функцию.
    */
    ObjFunction* function;

    FunctionType type;  //*  позволяет компилятору определять, когда он компилирует код верхнего уровня, а когда — тело функции
    //* отслеживает, какие слоты стека связаны с какими локальными переменными или временными переменными
    Local locals[UINT8_COUNT];  //* массив локальных переменных
    int localCount;
    //* Upvalue — это ссылка на локальную переменную во внешней функции
    //* массив структур upvalue для отслеживания закрытых идентификаторов, которые компилятор разрешил в теле каждой функции.
    //* upvalues служат своего рода уровнем косвенности, необходимым для поиска захваченной локальной переменной даже после того, 
    //* как она будет удалена из стека.
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth; //* количество блоков, окружающих текущий фрагмент кода, который мы компилируем
} Compiler;

//* Compiler->locals связан со стеком



//*
//*********************************************************************
//* «Объявление/declaration — это когда переменная добавляется в область видимости
//* «определение/definition» — когда она становится доступной для использования.
//*********************************************************************
//*



Parser parser;

Compiler* current = NULL;

static Chunk* currentChunk() {
    //* Текущий фрагмент — это всегда фрагмент, принадлежащий функции, которую мы компилируем
    return &current->function->chunk;
}



static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {

    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    // Выводит сообщение об ошибке в текущий токен
    errorAt(&parser.current, message);
}

static void advance() {
    // Двигается вперёд по потоку токенов. Текущий токен становится предыдущим для для получения лексемы после сопоставления токеном
    parser.previous = parser.current;

    for(;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte) {
    // Добавляет байт в конец буфера
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        error("Loop body too large.");
    }

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
    //* 
    emitByte(instruction); //* Записываем операнд-заполнитель для смещения перехода
    emitByte(0xff);
    emitByte(0xff);

    return currentChunk()->count - 2; //* Возвращаем смещение выдаваемой инструкции в блоке
}

static void emitReturn() {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    current = compiler;
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }
    //*  С этого момента компилятор неявно использует нулевой слот стека для внутренних нужд виртуальной машины
    //*  Мы присваиваем ему пустое имя, чтобы пользователь не мог написать идентификатор, ссылающийся на него 
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
    //*  возвращает нас к байт-коду и заменяет операнд в заданном месте на рассчитанное смещение перехода
    //* -2 для корректировки байт-кода самого смещения перехода
    int jump = currentChunk()->count - offset - 2; //* Смещение перехода

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    //* Записываем в блок кода старший байт смещения перехода, 
    //* сдвинув его на 8 бит вправо и маскировав младшие 8 бит
    currentChunk()->code[offset] = (jump >> 8) & 0xff; 
    //* Записываем в блок кода младший байт смещения перехода, взяв младшие 8 бит
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
    #ifdef DEBUG_PRINT_CODE
        if (!parser.hadError) {
            disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
        }
    #endif
    current = current->enclosing;
    return function;
}

static void beginScope() {
    //* Увеличивает глубину области видимости при создании блока
    current->scopeDepth++;
}

static void endScope() {
    //* Уменьшает глубину области видимости при завершении блока
    current->scopeDepth--;
    //* Когда мы выходим из области видимости, 
    //* мы просматриваем локальный массив в обратном порядке в поисках переменных, 
    //* объявленных на той глубине области видимости, которую мы только что покинули. 
    //* Мы удаляем их, просто уменьшая длину массива.
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

static void expression();
static void statement();
static uint8_t identifierConstant(Token* name);
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static int resolveLocal(Compiler* compiler, Token* name);
static void and_(bool canAssign);
static uint8_t argumentList();
static int resolveUpvalue(Compiler* compiler, Token* name);

static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    // Приоритет правого операнда каждого бинарного ОПЕРАТОРА на один уровень выше, чем у него самого
    parsePrecedence((Precedence)(rule->precedence));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS: emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
    default: return; 
    }
}

static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void or_(bool canAssign) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE); //* Если левая часть ложная, то прыгаем на правую
    int endJump = emitJump(OP_JUMP); //* Если левая часть истина, то прыгаем на конец 

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void string(bool canAssign) {
    // + 1 и -2 для убирания кавычек
    // +1 Так как строка начинается с кавычки
    // -2 Так как у токена длина подсчитана вместе с кавычками
    emitConstant(OBJ_VAL((Obj*)copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

/*
 * Generates code to access a named variable. If canAssign is true and the
 * next token is an =, then generates code to assign to the variable.
 * Otherwise, generates code to read the variable.
 *

 * If the variable is local, generates OP_GET_LOCAL and OP_SET_LOCALinstructions. 
 * If the variable is upvalue, generates OP_GET_UPVALUE and
 * OP_SET_UPVALUE instructions. Otherwise, generates OP_GET_GLOBAL and
 * OP_SET_GLOBAL instructions.
 */
static void namedVariable(Token name, bool canAssign) {
    //* 
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) {
    //* canAssign -
    namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    parsePrecedence(PREC_UNARY);

    switch(operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,   PREC_OR},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    // 
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    //* определяет, можно ли присвоить текущему выражению значение, основываясь на его приоритете. 
    //* Если приоритет достаточно низкий (т.е. меньше или равен PREC_ASSIGNMENT), 
    //* то выражению может быть присвоено значение, и canAssign присваивается значение true. 
    //* В противном случае ему присваивается значение false.
    bool canAssign = precedence <= PREC_ASSIGNMENT; 

    prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {    
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static uint8_t identifierConstant(Token* name) {
    //* Принимает заданный токен и добавляет его лексему в таблицу констант и возвращает индекс
    return makeConstant(OBJ_VAL((Obj*)copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    //* Проверяет, равны ли лексемы двух токенов
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    //* просматриваем массив в обратном направлении, 
    //* чтобы найти последнюю объявленную переменную с идентификатором. 
    //* Это гарантирует, что внутренние локальные переменные правильно затеняют локальные переменные 
    //* с тем же именем в окружающих областях видимости.
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            //* проверяет, осуществляется ли доступ к локальной переменной в рамках ее собственного инициализатора. 
            //* Если это так, то возникает ошибка, поскольку переменная еще не инициализирована. 
            //* Значение -1 для параметра local->depth указывает на то, что переменная находится в состоянии "неинициализирована".
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;   
        }
    }

    //* Если переменная не нашлась, возвращается -1, Значит она глобальная
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

/*
* resolveUpvalue - Разрешает локальную переменную во включенной области видимости.
 *ищет локальную переменную, объявленную в любой из окружающих её функций. Если она находит такую переменную, то возвращает «индекс восходящего значения» для этой переменной.
 * Мы вызываем этот метод после того, как не удалось разрешить локальную переменную в области видимости текущей функции, поэтому мы знаем, что переменной нет в текущем компиляторе. 
 * @param compiler: компилятор.
 * @param name: Имя локальной переменной, которую нужно разрешить.
 * @return: индекс локальной переменной во вложенной области или -1, если она не найдена.
*/
static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    /*
    *  Сначала мы ищем соответствующую локальную переменную во внешней функции
    *  Если мы её находим, то захватываем эту локальную переменную и возвращаем её
    */
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    /*
    *  В противном случае мы ищем локальную переменную за пределами непосредственно вложенной функции
    */
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    //* Когда объявляется переменная, она существует в текущей области видимости, 
    //* но находится в "неинициализированном" состоянии до тех пор, пока не будет проинициализирована.
    //* -1 для глубины области видимости переменной, чтобы указать на ее "неинициализированное" состояние. 
    //* Это позволяет компилятору определить, что переменная еще не инициализирована, 
    //* и выдать ошибку, если она будет использована до инициализации.
    local->depth = -1; 
}

static void declareVariable() {
    //* Объявление локальной переменной в текущей области видимости
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    for (int i = current->localCount - 1; i>=0; i--) {
        //* Если мы находим такую переменную в текущей области видимости, мы сообщаем об ошибке. 
        //* В противном случае, если мы доходим до начала массива или до переменной, 
        //* принадлежащей другой области видимости, мы знаем, 
        //* что проверили все существующие переменные в области видимости.
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }
        //* Проверяет, что в текущем скоупе нет переменных с таким именем
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);
    declareVariable();
    //* выходим из функции, если находимся в локальной области видимости
    //* Во время выполнения программы локальные переменные не ищутся по имени
    //* если объявление находится в локальной области видимости, мы возвращаем фиктивный индекс таблицы.
    if (current->scopeDepth > 0) return 0;
    return identifierConstant(&parser.previous);
}

static void markInitialized() {
    //* Устанавливает значение глубины области видимости переменной в текущей области видимости. 
    //* То есть, указываем, что переменная инициализирована
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
    //* Выводит инструкцию байт-кода, которая определяет новую переменную и сохраняет её начальное значение
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList() {
    /*
    *  возвращает количество скомпилированных аргументов
    */
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
   }
   consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
   return argCount;
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE); //* 

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

    block();

    ObjFunction* function = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL((Obj*)function))); 
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name."); // Индекс в таблице констант
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void varDeclaration() {
    //* Объявление переменных   
    uint8_t global = parseVariable("Ecpect variable name."); // Индекс в таблице констант

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}
    

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    //* 
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope();
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // Обработка инициализатора цикла
    if (match(TOKEN_SEMICOLON)) {
        // Нет инициализатора
    } else if (match(TOKEN_VAR)) {
        // Объявление переменной в цикле
        varDeclaration();
    } else {
        // Выражение-инициализатор
        expressionStatement();
    }

    // Запомнить начало цикла
    int loopStart = currentChunk()->count;
    int exitJump = -1; // Метка выхода из цикла
    
    // Обработка условия цикла
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Выход из цикла, если условие ложно
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        // Удалить условие из стека
        emitByte(OP_POP); 
    }
    
    // Обработка выражения-приращения
    if (!match(TOKEN_RIGHT_PAREN)) {
        //* Если присутствует приращение, нам нужно скомпилировать его сейчас, но оно пока не должно выполняться
        // Метка тела цикла
        int bodyJump = emitJump(OP_JUMP);
        // Запомнить начало выражения-инкремента
        int incrementStart = currentChunk()->count;
        expression();
        // Удалить результат выражения-инкремента из стека
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        // Перейти к началу цикла
        emitLoop(loopStart);
        // Обновить начало цикла
        loopStart = incrementStart;
        //* Установить метку тела цикла
        patchJump(bodyJump);
    }
    statement();

    //* Перейти к приращению
    emitLoop(loopStart);

    // Если есть условие, то выйти из цикла
    if (exitJump != -1) {
        // Установить метку выхода из цикла
        patchJump(exitJump);
        // Удалить условие из стека
        emitByte(OP_POP);
    }
    endScope();
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    //* метка, которая будет использована для перехода к следующему оператору, если условие не выполнено.
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    //* метка, которая будет использована для перехода к следующему оператору, если условие выполнено.
    statement();

    //* метка, которая будет использована для перехода к следующему оператору, если условие не выполнено и есть else
    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after value.");
        emitByte(OP_RETURN);
    }
}

static void whileStatement() {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    emitLoop(loopStart); //* 

    patchJump(exitJump);
    emitByte(OP_POP);
}

static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:
                ;
        }
        advance();
    }
}

static void declaration() {
    //* Правила объявления. Содержит операторы, которые объявляют имена.
    if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
        //* Объявление переменной
        varDeclaration();
    } else {
        statement();
    }
    if (parser.panicMode) synchronize(); // Режим востановления после ошибки
}

static void statement() {
    //* Правила Операторов
    if (match(TOKEN_PRINT)) {
        //* print
        printStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        //* Блок
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);
    parser.hadError = false;
    parser.panicMode = false;
    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }
    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}