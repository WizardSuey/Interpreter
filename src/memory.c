#include <stdlib.h>

#include "memory.h"
#include "vm.h"


//  oldSize	        newSize	                Операция \
    0	            Non‑zero	            Выделить новый блок.\
    Non‑zero	    0	                    Освободить блок.\
    Non‑zero	    Меньше чем oldSize	    Сжать существующее выделение.\
    Non‑zero	    Больше чем oldSize	    Увеличение существующего выделение.
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

static void freeObject(Obj* object) {
    //* Освобождение памяти объекта
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
    }
}

void freeObjects() {
    //* Освобождение списка объектов
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}