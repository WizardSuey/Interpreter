#include <stdlib.h>

#include "memory.h"


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