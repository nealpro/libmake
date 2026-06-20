#include "libmake.h"

#include <stdio.h>

int main(int argc, char **argv)
{
	const char *makefile;
	const char *target;
	lmk_t *lmk;
	int ret;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s <makefile> [target]\n", argv[0]);
		return 2;
	}

	makefile = argv[1];

	lmk = lmk_create();
	if (!lmk) {
		fprintf(stderr, "lmk_create failed\n");
		return 2;
	}

	if (lmk_load_makefile(lmk, makefile) != 0) {
		lmk_free(lmk);
		return 2;
	}

	target = argc == 3 ? argv[2] : lmk_default_target(lmk);
	if (!target) {
		fprintf(stderr, "no default target\n");
		lmk_free(lmk);
		return 2;
	}

	ret = lmk_build(lmk, target);
	lmk_free(lmk);
	return ret;
}
