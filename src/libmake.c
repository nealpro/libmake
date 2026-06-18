#include "libmake.h"
#include "dag.h"
#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void lmk_rule(lmk_t *lmk, const char *target, const char **deps,
	      size_t num_deps, const char **commands, size_t num_commands)
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
		fprintf(stderr, "libmake: circular dependency on '%s'\n",
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
			fprintf(stderr, "libmake: recipe for '%s' failed\n",
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
		fprintf(stderr, "libmake: no rule to make target '%s'\n",
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

/* Write a JSON-escaped string to out. */
static void json_puts(FILE *out, const char *s)
{
	fputc('"', out);
	for (; *s; s++) {
		switch (*s) {
		case '"':
			fputs("\\\"", out);
			break;
		case '\\':
			fputs("\\\\", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		default:
			fputc(*s, out);
		}
	}
	fputc('"', out);
}

void lmk_dump_graph_json(lmk_t *lmk, FILE *out)
{
	dag_t *dag = lmk->dag;

	fputs("{\n  \"nodes\": [\n", out);
	for (size_t i = 0; i < dag->num_nodes; i++) {
		dag_node_t *n = dag->nodes[i];
		fputs("    {\"name\": ", out);
		json_puts(out, n->name);

		fputs(", \"commands\": [", out);
		for (size_t c = 0; c < n->num_commands; c++) {
			if (c > 0)
				fputs(", ", out);
			json_puts(out, n->commands[c]);
		}

		fputs("], \"deps\": [", out);
		for (size_t d = 0; d < n->num_deps; d++) {
			if (d > 0)
				fputs(", ", out);
			json_puts(out, n->deps[d]->name);
		}

		fputc(']', out);
		fputc('}', out);
		if (i + 1 < dag->num_nodes)
			fputc(',', out);
		fputc('\n', out);
	}
	fputs("  ]\n}\n", out);
}

typedef struct {
	const char *node_name;
	const char
		*reason; /* "file_missing", "dependency_newer", "up_to_date" */
	const char *dep; /* triggering dep name, or NULL */
} explain_entry_t;

typedef struct {
	explain_entry_t *entries;
	size_t count;
	size_t capacity;
} explain_list_t;

static void explain_list_init(explain_list_t *list)
{
	list->entries = NULL;
	list->count = 0;
	list->capacity = 0;
}

static void explain_list_push(explain_list_t *list, const char *node_name,
			      const char *reason, const char *dep)
{
	if (list->count == list->capacity) {
		size_t newcap = list->capacity ? list->capacity * 2 : 8;
		list->entries = realloc(list->entries,
					newcap * sizeof(explain_entry_t));
		list->capacity = newcap;
	}
	list->entries[list->count++] =
		(explain_entry_t){node_name, reason, dep};
}

static void explain_list_free(explain_list_t *list) { free(list->entries); }

static int explain_node(dag_node_t *node, explain_list_t *plan,
			explain_list_t *skipped)
{
	if (node->resolved)
		return 0;
	if (node->visited)
		return 1;
	node->visited = true;

	for (size_t i = 0; i < node->num_deps; i++) {
		int ret = explain_node(node->deps[i], plan, skipped);
		if (ret != 0)
			return ret;
	}

	if (node->num_commands == 0) {
		/* phony / no-recipe node */
	} else if (needs_rebuild(node)) {
		time_t target_time = file_mtime(node->name);
		const char *dep = NULL;
		const char *reason;

		if (target_time == 0) {
			reason = "file_missing";
		} else {
			reason = "dependency_newer";
			for (size_t i = 0; i < node->num_deps; i++) {
				if (file_mtime(node->deps[i]->name) >
				    target_time) {
					dep = node->deps[i]->name;
					break;
				}
			}
		}
		explain_list_push(plan, node->name, reason, dep);
	} else {
		explain_list_push(skipped, node->name, "up_to_date", NULL);
	}

	node->resolved = true;
	return 0;
}

static void write_explain_list(FILE *out, const explain_list_t *list,
			       bool include_action)
{
	for (size_t i = 0; i < list->count; i++) {
		const explain_entry_t *e = &list->entries[i];
		fputs("    {\"node\": ", out);
		json_puts(out, e->node_name);
		if (include_action)
			fputs(", \"action\": \"rebuild\"", out);
		fputs(", \"reason\": ", out);
		json_puts(out, e->reason);
		if (e->dep) {
			fputs(", \"dep\": ", out);
			json_puts(out, e->dep);
		}
		fputc('}', out);
		if (i + 1 < list->count)
			fputc(',', out);
		fputc('\n', out);
	}
}

int lmk_explain_build(lmk_t *lmk, const char *target, FILE *out)
{
	dag_node_t *node = dag_get_node(lmk->dag, target);
	if (!node) {
		fprintf(stderr, "libmake: no rule to make target '%s'\n",
			target);
		return 1;
	}

	explain_list_t plan, skipped;
	explain_list_init(&plan);
	explain_list_init(&skipped);

	int ret = explain_node(node, &plan, &skipped);
	lmk_reset(lmk);

	if (ret != 0) {
		fprintf(stderr, "libmake: circular dependency on '%s'\n",
			target);
		explain_list_free(&plan);
		explain_list_free(&skipped);
		return ret;
	}

	fputs("{\n  \"target\": ", out);
	json_puts(out, target);
	fputs(",\n  \"plan\": [\n", out);
	write_explain_list(out, &plan, true);
	fputs("  ],\n  \"skipped\": [\n", out);
	write_explain_list(out, &skipped, false);
	fputs("  ]\n}\n", out);

	explain_list_free(&plan);
	explain_list_free(&skipped);
	return 0;
}
