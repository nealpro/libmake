#include "libmake.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void setup_rules(lmk_t *lmk);

int main(int argc, char **argv)
{
	bool dump_graph = false;
	bool dry_run = false;
	const char *target = "all";

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--dump-graph") == 0) {
			dump_graph = true;
		} else if (strcmp(argv[i], "--dry-run") == 0) {
			dry_run = true;
		} else {
			target = argv[i];
		}
	}

	lmk_t *lmk = lmk_create();
	setup_rules(lmk);

	int ret = 0;
	if (dump_graph) {
		lmk_dump_graph_json(lmk, stdout);
	} else if (dry_run) {
		ret = lmk_explain_build(lmk, target, stdout);
	} else {
		ret = lmk_build(lmk, target);
	}

	lmk_free(lmk);
	return ret;
}

static void setup_rules(lmk_t *lmk)
{

	lmk_rule(lmk, "all", (const char *[]){"libmake"}, 1, NULL, 0);

	lmk_rule(lmk, "clean", NULL, 0, (const char *[]){"rm -f libmake *.o"},
		 1);

	lmk_rule(
		lmk, "libmake",
		(const char *[]){"main.o", "dag.o", "exec.o", "libmake.o"}, 4,
		(const char *[]){"cc -o libmake main.o dag.o exec.o libmake.o"},
		1);

	lmk_rule(lmk, "main.o", (const char *[]){"src/main.c", "src/libmake.h"},
		 2, (const char *[]){"cc -c src/main.c -o main.o"}, 1);

	lmk_rule(lmk, "dag.o", (const char *[]){"src/dag.c", "src/dag.h"}, 2,
		 (const char *[]){"cc -c src/dag.c -o dag.o"}, 1);

	lmk_rule(lmk, "exec.o",
		 (const char *[]){"src/exec.c", "src/exec.h", "src/dag.h"}, 3,
		 (const char *[]){"cc -c src/exec.c -o exec.o"}, 1);

	lmk_rule(lmk, "libmake.o",
		 (const char *[]){"src/libmake.c", "src/libmake.h", "src/dag.h",
				  "src/exec.h"},
		 4, (const char *[]){"cc -c src/libmake.c -o libmake.o"}, 1);
}
