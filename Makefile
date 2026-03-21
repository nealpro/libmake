all: libmake

clean:
	rm -f libmake *.o

libmake: main.o dag.o exec.o libmake.o
	cc -o libmake main.o dag.o exec.o libmake.o

main.o: src/main.c src/libmake.h
	cc -c src/main.c -o main.o

dag.o: src/dag.c src/dag.h
	cc -c src/dag.c -o dag.o

exec.o: src/exec.c src/exec.h src/dag.h
	cc -c src/exec.c -o exec.o

libmake.o: src/libmake.c src/libmake.h src/dag.h src/exec.h
	cc -c src/libmake.c -o libmake.o
