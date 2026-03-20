#ifndef LIBMAKE_H
#define LIBMAKE_H

#include <stddef.h>

typedef struct lmk lmk_t;

lmk_t *lmk_create(void);
void lmk_free(lmk_t *lmk);

/*
 * Add a build rule.
 *
 * target     — name of the target (typically a file path)
 * deps       — array of dependency names (may be NULL if num_deps == 0)
 * num_deps   — number of dependencies
 * commands   — array of shell commands (may be NULL if num_commands == 0)
 * num_commands — number of commands
 */
void lmk_rule(lmk_t *lmk, const char *target,
              const char **deps, size_t num_deps,
              const char **commands, size_t num_commands);

/*
 * Build a target and all of its dependencies.
 *
 * Walks the dependency graph depth-first, executing recipes only for
 * targets whose file either does not exist or is older than at least one
 * dependency.  Returns 0 on success, non-zero on failure.
 */
int lmk_build(lmk_t *lmk, const char *target);

/*
 * Reset traversal state so the same lmk_t can be used for another build.
 */
void lmk_reset(lmk_t *lmk);

#endif /* LIBMAKE_H */
