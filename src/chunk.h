#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_RETURN,
} OpCode;   // Инструкция

typedef struct {
    int count;
    int capacity;
    uint8_t* code;  // Указатель на массив инструкций
    int* lines;     // массив строк
    ValueArray constants; // пул констант
} Chunk;    // Динамический массив инструкций

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
