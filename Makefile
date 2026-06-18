CC ?= cc
CFLAGS ?=
LDFLAGS ?=

all: libmake

clean:
	rm -f libmake *.o

libmake: main.o dag.o exec.o libmake.o
	$(CC) $(LDFLAGS) -o libmake main.o dag.o exec.o libmake.o

main.o: src/main.c src/libmake.h
	$(CC) $(CFLAGS) -c src/main.c -o main.o

dag.o: src/dag.c src/dag.h
	$(CC) $(CFLAGS) -c src/dag.c -o dag.o

exec.o: src/exec.c src/exec.h src/dag.h
	$(CC) $(CFLAGS) -c src/exec.c -o exec.o

libmake.o: src/libmake.c src/libmake.h src/dag.h src/exec.h
	$(CC) $(CFLAGS) -c src/libmake.c -o libmake.o
