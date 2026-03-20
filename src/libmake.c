#include "libmake.h"
#include "dag.h"
#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

struct lmk {
	dag_t *dag;
};

lmk_t *lmk_create(void)
{
	lmk_t *lmk = calloc(1, sizeof(lmk_t));
	if (!lmk)
		return NULL;
	lmk->dag = dag_create();
	return lmk;
}

void lmk_free(lmk_t *lmk)
{
	if (!lmk)
		return;
	dag_free(lmk->dag);
	free(lmk);
}

void lmk_rule(lmk_t *lmk, const char *target,
              const char **deps, size_t num_deps,
              const char **commands, size_t num_commands)
{
	dag_node_t *node = dag_add_node(lmk->dag, target);
	for (size_t i = 0; i < num_deps; i++)
		dag_add_edge(lmk->dag, target, deps[i]);
	for (size_t i = 0; i < num_commands; i++)
		dag_node_add_command(node, commands[i]);
}

static time_t file_mtime(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	return st.st_mtime;
}

static bool needs_rebuild(const dag_node_t *node)
{
	if (node->num_commands == 0)
		return false;

	time_t target_time = file_mtime(node->name);
	if (target_time == 0)
		return true;

	for (size_t i = 0; i < node->num_deps; i++) {
		time_t dep_time = file_mtime(node->deps[i]->name);
		if (dep_time > target_time)
			return true;
	}
	return false;
}

static int build_node(dag_node_t *node)
{
	if (node->resolved)
		return 0;
	if (node->visited) {
		fprintf(stderr,
		        "libmake: circular dependency on '%s'\n",
		        node->name);
		return 1;
	}
	node->visited = true;

	for (size_t i = 0; i < node->num_deps; i++) {
		int ret = build_node(node->deps[i]);
		if (ret != 0)
			return ret;
	}

	if (needs_rebuild(node)) {
		int ret = exec_run_node(node);
		if (ret != 0) {
			fprintf(stderr,
			        "libmake: recipe for '%s' failed\n",
			        node->name);
			return ret;
		}
	}

	node->resolved = true;
	return 0;
}

int lmk_build(lmk_t *lmk, const char *target)
{
	dag_node_t *node = dag_get_node(lmk->dag, target);
	if (!node) {
		fprintf(stderr,
		        "libmake: no rule to make target '%s'\n",
		        target);
		return 1;
	}
	return build_node(node);
}

void lmk_reset(lmk_t *lmk)
{
	for (size_t i = 0; i < lmk->dag->num_nodes; i++) {
		lmk->dag->nodes[i]->visited = false;
		lmk->dag->nodes[i]->resolved = false;
	}
}
