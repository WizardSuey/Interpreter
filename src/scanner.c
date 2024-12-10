#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;      //* Началао текущей сканируемой лексемы
    const char* current;    //* Текущий символ
    int line;               //* Для отлеживания того, на какой строке находится текущая лексема 
} Scanner;  //* Струкутура сканера

Scanner scanner;

void initScanner(const char* source) {
    //* Обнуление сканера
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool isDigit(char c) {
    //* Проверка на цифру
    return c >= '0' && c <= '9';
}

static bool isAtEnd() {
    //* Проверка на то, заканчивается ли строка нулём
    return *scanner.current == '\0';
}

static char advance() {
    //* Потребляет текущий символ и возвращает его
    scanner.current++;
    return scanner.current[-1];
}

static char peek() {
    //* Возвращает текущий символ, но не потребляет его
    return *scanner.current;
}

static char peekNext() {
    //* Возвращает символ дальше текущего
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool match(char expected) {
    //* Проверка на совпадение символа
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    //* Конструктор Токена
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char* message) {
    //* Конструктор токена ошибки
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhitespace() {
    //* Пропуск начальных пробелов
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    //* Комментарий до конца строки 
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    //* Проверяет, соответствует ли текущий идентификатор заданному ключевому слову.
    //* @param start Начальное смещение ключевого слова в идентификаторе.
    //* @param length Длина ключевого слова.
    //* @param rest Символы ключевого слова.
    //* @param type Тип TokenType, который будет возвращен, если ключевое слово соответствует.
    //* @return TokenType ключевого слова, если оно соответствует,
    //* или TOKEN_IDENTIFIER, если это не так.
   if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0) {
       return type;
   }

   return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    //** Индетификация ключевого слова или обычного идентификатора
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'v': return checkKeyword(2, 4, "ar", TOKEN_VAR);
                }
            }
            break;
        case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
  }
    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    //* Создание токена идентификатора
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number() {
    //* Создание токена числа

    //* Искать дробную часть
    if (peek() == '.' && isDigit(peekNext())) {
        //* Потреблять точку
        advance();

        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

static Token string() {
    //* Создание токена строки
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    //* Закрывающая кавычка
    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken() {
    //* Сканирование токена
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch(c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        case '!':
            return makeToken(
                match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(
                match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(
                match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(
                match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}