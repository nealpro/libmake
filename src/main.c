#include "libmake.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	const char *target = "all";
	if (argc > 1)
		target = argv[1];

	lmk_t *lmk = lmk_create();

	lmk_rule(lmk, "all",
	         (const char *[]){"libmake"}, 1,
	         NULL, 0);

	lmk_rule(lmk, "clean",
	         NULL, 0,
	         (const char *[]){"rm -f libmake *.o"}, 1);

	lmk_rule(lmk, "libmake",
	         (const char *[]){"main.o", "dag.o", "exec.o", "libmake.o"}, 4,
	         (const char *[]){"cc -o libmake main.o dag.o exec.o libmake.o"}, 1);

	lmk_rule(lmk, "main.o",
	         (const char *[]){"src/main.c", "src/libmake.h"}, 2,
	         (const char *[]){"cc -c src/main.c -o main.o"}, 1);

	lmk_rule(lmk, "dag.o",
	         (const char *[]){"src/dag.c", "src/dag.h"}, 2,
	         (const char *[]){"cc -c src/dag.c -o dag.o"}, 1);

	lmk_rule(lmk, "exec.o",
	         (const char *[]){"src/exec.c", "src/exec.h", "src/dag.h"}, 3,
	         (const char *[]){"cc -c src/exec.c -o exec.o"}, 1);

	lmk_rule(lmk, "libmake.o",
	         (const char *[]){"src/libmake.c", "src/libmake.h",
	                          "src/dag.h", "src/exec.h"}, 4,
	         (const char *[]){"cc -c src/libmake.c -o libmake.o"}, 1);

	int ret = lmk_build(lmk, target);
	lmk_free(lmk);
	return ret;
}
