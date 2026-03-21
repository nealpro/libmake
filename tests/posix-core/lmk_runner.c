#include "libmake.h"

#include <stdio.h>
#include <string.h>

static int setup_basic(lmk_t *lmk)
{
lmk_rule(lmk, "all", (const char *[]){"out.txt"}, 1, NULL, 0);
lmk_rule(lmk, "out.txt", (const char *[]){"in.txt"}, 1,
         (const char *[]){"cp in.txt out.txt"}, 1);
lmk_rule(lmk, "in.txt", NULL, 0, NULL, 0);
return 0;
}

static int setup_fail(lmk_t *lmk)
{
lmk_rule(lmk, "all", (const char *[]){"bad"}, 1, NULL, 0);
lmk_rule(lmk, "bad", NULL, 0, (const char *[]){"sh -c 'exit 7'"}, 1);
return 0;
}

int main(int argc, char **argv)
{
const char *scenario;
const char *target;
lmk_t *lmk;
int ret;

if (argc < 3) {
fprintf(stderr, "usage: %s <scenario> <target>\\n", argv[0]);
return 2;
}

scenario = argv[1];
target = argv[2];

lmk = lmk_create();
if (!lmk) {
fprintf(stderr, "lmk_create failed\\n");
return 2;
}

if (strcmp(scenario, "basic") == 0) {
setup_basic(lmk);
} else if (strcmp(scenario, "fail") == 0) {
setup_fail(lmk);
} else if (strcmp(scenario, "missing") == 0) {
/* Intentionally leave empty: missing target path. */
} else {
fprintf(stderr, "unknown scenario: %s\\n", scenario);
lmk_free(lmk);
return 2;
}

ret = lmk_build(lmk, target);
lmk_free(lmk);
return ret;
}
