#include "libmake.h"

#include <stdio.h>
#include <string.h>

static int setup_basic(lmk_t *lmk)
{
	return lmk_rule(lmk, "all", (const char *[]){"out.txt"}, 1, NULL, 0) ||
	       lmk_rule(lmk, "out.txt", (const char *[]){"in.txt"}, 1,
			(const char *[]){"cp in.txt out.txt"}, 1);
}

static int setup_fail(lmk_t *lmk)
{
	return lmk_rule(lmk, "all", (const char *[]){"bad"}, 1, NULL, 0) ||
	       lmk_rule(lmk, "bad", NULL, 0, (const char *[]){"sh -c 'exit 7'"},
			1);
}

static int setup_missing_dep(lmk_t *lmk)
{
	return lmk_rule(lmk, "all", (const char *[]){"out.txt"}, 1, NULL, 0) ||
	       lmk_rule(lmk, "out.txt", (const char *[]){"missing.txt"}, 1,
			(const char *[]){"cp missing.txt out.txt"}, 1);
}

static int setup_repeated(lmk_t *lmk)
{
	return lmk_rule(lmk, "all", (const char *[]){"first", "second"}, 2,
			NULL, 0) ||
	       lmk_rule(lmk, "first", (const char *[]){"input.txt"}, 1, NULL,
			0) ||
	       lmk_rule(lmk, "first", (const char *[]){"extra.txt"}, 1,
			(const char *[]){"cat input.txt extra.txt > first"},
			1) ||
	       lmk_rule(lmk, "extra.txt", NULL, 0,
			(const char *[]){"printf 'extra\\n' > extra.txt"}, 1) ||
	       lmk_rule(lmk, "second", (const char *[]){"first"}, 1,
			(const char *[]){"cp first second"}, 1);
}

static int setup_shared(lmk_t *lmk)
{
	return lmk_rule(lmk, "all", (const char *[]){"left", "right"}, 2, NULL,
			0) ||
	       lmk_rule(lmk, "left", (const char *[]){"input"}, 1,
			(const char *[]){"cp input left"}, 1) ||
	       lmk_rule(lmk, "right", (const char *[]){"input"}, 1,
			(const char *[]){"cp input right"}, 1);
}

static int setup_space_name(lmk_t *lmk)
{
	return lmk_rule(lmk, "all", (const char *[]){"out"}, 1, NULL, 0) ||
	       lmk_rule(lmk, "out", (const char *[]){"my input"}, 1,
			(const char *[]){"cp 'my input' out"}, 1);
}

static int setup_scenario(lmk_t *lmk, const char *scenario)
{
	if (strcmp(scenario, "basic") == 0)
		return setup_basic(lmk);
	if (strcmp(scenario, "fail") == 0)
		return setup_fail(lmk);
	if (strcmp(scenario, "missing-dep") == 0)
		return setup_missing_dep(lmk);
	if (strcmp(scenario, "repeated") == 0)
		return setup_repeated(lmk);
	if (strcmp(scenario, "shared") == 0)
		return setup_shared(lmk);
	if (strcmp(scenario, "space-name") == 0)
		return setup_space_name(lmk);

	fprintf(stderr, "unknown scenario: %s\n", scenario);
	return 1;
}

int main(int argc, char **argv)
{
	const char *mode;
	const char *scenario;
	const char *target;
	lmk_t *lmk;
	int ret;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s <emit|build> <scenario> [target]\n",
			argv[0]);
		return 2;
	}

	mode = argv[1];
	scenario = argv[2];

	lmk = lmk_create();
	if (!lmk) {
		fprintf(stderr, "lmk_create failed\n");
		return 2;
	}
	if (setup_scenario(lmk, scenario) != 0) {
		lmk_free(lmk);
		return 2;
	}

	if (strcmp(mode, "emit") == 0) {
		ret = lmk_dump_makefile(lmk, stdout);
	} else if (strcmp(mode, "build") == 0) {
		target = argc == 4 ? argv[3] : lmk_default_target(lmk);
		if (!target) {
			fprintf(stderr, "no default target\n");
			lmk_free(lmk);
			return 2;
		}
		ret = lmk_build(lmk, target);
	} else {
		fprintf(stderr, "unknown mode: %s\n", mode);
		ret = 2;
	}

	lmk_free(lmk);
	return ret;
}
