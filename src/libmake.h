#ifndef LIBMAKE_H
#define LIBMAKE_H

#include <stddef.h>
#include <stdio.h>

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
int lmk_rule_checked(lmk_t *lmk, const char *target, const char **deps,
		     size_t num_deps, const char **commands,
		     size_t num_commands);

void lmk_rule(lmk_t *lmk, const char *target, const char **deps,
	      size_t num_deps, const char **commands, size_t num_commands);

/*
 * Load target rules from a makefile subset:
 * target/prerequisite lines, semicolon commands, and tab-indented commands.
 * Returns 0 on success, non-zero on parse or allocation failure.
 */
int lmk_load_makefile(lmk_t *lmk, const char *path);

/*
 * Return the first non-special target loaded into the graph, or NULL if none.
 */
const char *lmk_default_target(lmk_t *lmk);

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

/*
 * Write the build graph as JSON to the given stream.
 */
void lmk_dump_graph_json(lmk_t *lmk, FILE *out);

/*
 * Explain what a build would do without executing anything.
 *
 * Walks the dependency graph for `target` and writes a JSON object to `out`
 * describing which nodes need rebuilding and why.  Returns 0 on success,
 * non-zero if the target is not found or a cycle is detected.
 */
int lmk_explain_build(lmk_t *lmk, const char *target, FILE *out);

#endif /* LIBMAKE_H */
