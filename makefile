SRC = src/main.c src/chunk.c src/memory.c src/debug.c src/value.c src/vm.c src/compiler.c src/scanner.c
TARGET_LINUX = bin/clox
TARGET_WIN = bin/clox.exe

linux: $(SRC)
	gcc $(SRC) -o $(TARGET_LINUX)

win: $(SRC)
	gcc $(SRC) -o $(TARGET_WIN)

%.o: %.c
	gcc -c -o $*.o $*.c