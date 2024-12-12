#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;  //* Структура парсера

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

Parser parser;
Chunk* compilingChunk;

static Chunk* currentChunk() {
    //* Возвращает текущий кусок кода
    return compilingChunk;
}


static void errorAt(Token* token, const char* message) {
    //* Выводит сообщение об ошибке с указанием местоположения токена
    if (parser.panicMode) return;
    parser.panicMode = true;

    fpritnf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Do nothing.
    } else {
        fprintf(stderr, " at '%s.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
} 

static void error(const char* message) {
    //* Сообщение о ошибке в месте расположения токена, который мы только что использовали
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    //* Извлекаем местоположение из текущего токена, чтобы сообщить пользователю, где произошла ошибка
    errorAt(&parser.current, message);
}

static void advance() {
    //* Продвигается вперёд по потоку токенов. Запрашивает следующий токен и сохраняет его для последующего использования
    parser.previous = parser.current;

    for(;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    //* Проверяет, является ли текущий токен указанным типом и если да, продвигается вперёд по потоку токенов. Если нет, выдаёт ошибку
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static void emitByte(uint8_t byte) {
    //* Записывает заданный байт в чанк
    //* Он отправляет информацию о предыдущей строке маркера, чтобы с этой строкой были связаны ошибки времени выполнения
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    //* Для записи двух байт в чанк
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    //* Записывает маркер возврата в чанк
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    //* Сначала мы добавляем значение в таблицу констант, затем мы генерируем инструкцию OP_CONSTANT, которая помещает его в стек во время выполнения.
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    //* генерируем код для загрузки значения
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void endCompiler() {
    //* Подводим итоги компиляции
    emitReturn();
}

static void expression() {
    //* Компиляция выражения
    parsePrecedence(PREC_ASSIGNMENT);
}

static void grouping() {
    //* Для обработки выражения в скобках
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number() {
    //* Мы предполагаем, что токен для числового литерала уже был использован и хранится в previous, а затем преобразуем его в double
    double value = strtod(parser.previous.start, NULL);
    emitConstant(value);
}

static void unary() {
    //* Компиляция оператора унарного выражения
    TokenType operatorType = parser.previous.type;

    //* Компиляция операнда
    parsePrecedence(PREC_UNARY);

    //* Выдать инструкцию оператора
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return; //* 
    }
}

static void parsePrecedence(Precedence precedence) {
    //* Эта функция, как только мы ее реализуем, начинает с текущей лексемы и анализирует любое выражение на заданном уровне приоритета или выше.

}

bool compile(const char* source, Chunk* chunk) {
    // функция компиляции файла
    initScanner(source);

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    expression();
    consume(TOKEN_EOF, "Expect end of expression");
    endCompiler();
    return !parser.hadError;
}