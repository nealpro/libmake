#include "libmake.h"
#include "dag.h"
#include "exec.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct lmk {
	dag_t *dag;
	char *default_target;
};

lmk_t *lmk_create(void)
{
	lmk_t *lmk = calloc(1, sizeof(lmk_t));
	if (!lmk)
		return NULL;
	lmk->dag = dag_create();
	if (!lmk->dag) {
		free(lmk);
		return NULL;
	}
	return lmk;
}

void lmk_free(lmk_t *lmk)
{
	if (!lmk)
		return;
	dag_free(lmk->dag);
	free(lmk->default_target);
	free(lmk);
}

static bool is_special_target(const char *target) { return target[0] == '.'; }

static int maybe_set_default_target(lmk_t *lmk, const char *target)
{
	if (lmk->default_target || is_special_target(target))
		return 0;

	lmk->default_target = strdup(target);
	return lmk->default_target ? 0 : 1;
}

int lmk_rule(lmk_t *lmk, const char *target, const char **deps, size_t num_deps,
	     const char **commands, size_t num_commands)
{
	dag_node_t *node = dag_add_node(lmk->dag, target);
	if (!node)
		return 1;
	node->has_rule = true;
	if (maybe_set_default_target(lmk, target) != 0)
		return 1;
	for (size_t i = 0; i < num_deps; i++)
		if (!dag_add_edge(lmk->dag, target, deps[i]))
			return 1;
	for (size_t i = 0; i < num_commands; i++)
		if (!dag_node_add_command(node, commands[i]))
			return 1;
	return 0;
}

const char *lmk_default_target(lmk_t *lmk) { return lmk->default_target; }

typedef struct {
	char **items;
	size_t count;
	size_t capacity;
} string_list_t;

static void string_list_free(string_list_t *list)
{
	for (size_t i = 0; i < list->count; i++)
		free(list->items[i]);
	free(list->items);
	list->items = NULL;
	list->count = 0;
	list->capacity = 0;
}

static int string_list_push(string_list_t *list, const char *text)
{
	if (list->count == list->capacity) {
		size_t new_capacity = list->capacity ? list->capacity * 2 : 4;
		char **new_items =
			realloc(list->items, new_capacity * sizeof(char *));
		if (!new_items)
			return 1;
		list->items = new_items;
		list->capacity = new_capacity;
	}
	list->items[list->count] = strdup(text);
	if (!list->items[list->count])
		return 1;
	list->count++;
	return 0;
}

static void string_list_move(string_list_t *dst, string_list_t *src)
{
	string_list_free(dst);
	*dst = *src;
	src->items = NULL;
	src->count = 0;
	src->capacity = 0;
}

static char *read_makefile_line(FILE *file)
{
	size_t capacity = 256;
	size_t len = 0;
	char *line = malloc(capacity);
	int ch;

	if (!line)
		return NULL;

	while ((ch = fgetc(file)) != EOF) {
		if (len + 1 == capacity) {
			size_t new_capacity = capacity * 2;
			char *new_line = realloc(line, new_capacity);
			if (!new_line) {
				free(line);
				return NULL;
			}
			line = new_line;
			capacity = new_capacity;
		}
		line[len++] = (char)ch;
		if (ch == '\n')
			break;
	}

	if (len == 0 && ch == EOF) {
		free(line);
		return NULL;
	}

	line[len] = '\0';
	return line;
}

static void chomp(char *line)
{
	size_t len = strlen(line);

	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';
}

static char *skip_blank(char *s)
{
	while (*s && isblank((unsigned char)*s))
		s++;
	return s;
}

static void trim_right(char *s)
{
	size_t len = strlen(s);

	while (len > 0 && isblank((unsigned char)s[len - 1]))
		s[--len] = '\0';
}

static char *trim(char *s)
{
	s = skip_blank(s);
	trim_right(s);
	return s;
}

static void strip_comment(char *s)
{
	for (; *s; s++) {
		if (*s == '#') {
			*s = '\0';
			return;
		}
	}
}

static int tokenize_words(char *text, string_list_t *out)
{
	char *p = text;

	while (*p) {
		char *start;

		while (*p && isblank((unsigned char)*p))
			p++;
		if (!*p)
			break;

		start = p;
		while (*p && !isblank((unsigned char)*p))
			p++;
		if (*p)
			*p++ = '\0';

		if (string_list_push(out, start) != 0)
			return 1;
	}
	return 0;
}

static int add_command_to_targets(lmk_t *lmk, const string_list_t *targets,
				  const char *command)
{
	const char *commands[] = {command};

	for (size_t i = 0; i < targets->count; i++) {
		if (lmk_rule(lmk, targets->items[i], NULL, 0, commands, 1) != 0)
			return 1;
	}
	return 0;
}

static int parse_rule_line(lmk_t *lmk, char *line, string_list_t *current,
			   const char *path, size_t lineno)
{
	string_list_t targets = {0};
	string_list_t deps = {0};
	char *semicolon = strchr(line, ';');
	char *command = NULL;
	char *colon;
	int ret = 1;

	if (semicolon) {
		*semicolon = '\0';
		command = trim(semicolon + 1);
	}

	strip_comment(line);
	colon = strchr(line, ':');
	if (!colon) {
		char *content = trim(line);
		if (*content == '\0')
			return 0;
		fprintf(stderr, "libmake: %s:%zu: expected ':' in rule\n", path,
			lineno);
		return 1;
	}

	*colon = '\0';
	if (tokenize_words(trim(line), &targets) != 0 ||
	    tokenize_words(trim(colon + 1), &deps) != 0) {
		fprintf(stderr, "libmake: %s:%zu: out of memory\n", path,
			lineno);
		goto out;
	}

	if (targets.count == 0) {
		fprintf(stderr, "libmake: %s:%zu: rule has no target\n", path,
			lineno);
		goto out;
	}

	for (size_t i = 0; i < targets.count; i++) {
		if (lmk_rule(lmk, targets.items[i], (const char **)deps.items,
			     deps.count, NULL, 0) != 0) {
			fprintf(stderr, "libmake: %s:%zu: out of memory\n",
				path, lineno);
			goto out;
		}
	}

	if (command && *command) {
		if (add_command_to_targets(lmk, &targets, command) != 0) {
			fprintf(stderr, "libmake: %s:%zu: out of memory\n",
				path, lineno);
			goto out;
		}
	}

	string_list_move(current, &targets);
	ret = 0;

out:
	string_list_free(&targets);
	string_list_free(&deps);
	return ret;
}

int lmk_load_makefile(lmk_t *lmk, const char *path)
{
	FILE *file = fopen(path, "r");
	string_list_t current_targets = {0};
	char *line;
	size_t lineno = 0;
	int ret = 0;

	if (!file) {
		fprintf(stderr, "libmake: cannot open makefile '%s'\n", path);
		return 1;
	}

	while ((line = read_makefile_line(file)) != NULL) {
		lineno++;
		chomp(line);

		if (line[0] == '\t') {
			if (current_targets.count == 0) {
				fprintf(stderr,
					"libmake: %s:%zu: recipe without "
					"rule\n",
					path, lineno);
				ret = 1;
			} else if (add_command_to_targets(lmk, &current_targets,
							  line + 1) != 0) {
				fprintf(stderr,
					"libmake: %s:%zu: out of memory\n",
					path, lineno);
				ret = 1;
			}
		} else {
			ret = parse_rule_line(lmk, line, &current_targets, path,
					      lineno);
		}

		free(line);
		if (ret != 0)
			break;
	}

	string_list_free(&current_targets);
	fclose(file);
	return ret;
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

	if (!node->has_rule && file_mtime(node->name) == 0) {
		fprintf(stderr, "libmake: no rule to make target '%s'\n",
			node->name);
		return 1;
	}

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

static bool makefile_name_safe(const char *s)
{
	if (!s || !*s)
		return false;

	for (; *s; s++) {
		if (isspace((unsigned char)*s))
			return false;
		switch (*s) {
		case ':':
		case '#':
			return false;
		default:
			break;
		}
	}
	return true;
}

static bool makefile_command_safe(const char *s)
{
	if (!s)
		return false;

	for (; *s; s++) {
		if (*s == '\n' || *s == '\r')
			return false;
	}
	return true;
}

int lmk_dump_makefile(lmk_t *lmk, FILE *out)
{
	dag_t *dag = lmk->dag;
	bool wrote_rule = false;

	for (size_t i = 0; i < dag->num_nodes; i++) {
		dag_node_t *n = dag->nodes[i];

		if (!n->has_rule)
			continue;
		if (!makefile_name_safe(n->name)) {
			fprintf(stderr,
				"libmake: cannot emit unsupported target "
				"name '%s'\n",
				n->name);
			return 1;
		}
		for (size_t d = 0; d < n->num_deps; d++) {
			if (!makefile_name_safe(n->deps[d]->name)) {
				fprintf(stderr,
					"libmake: cannot emit unsupported "
					"dependency name '%s'\n",
					n->deps[d]->name);
				return 1;
			}
		}
		for (size_t c = 0; c < n->num_commands; c++) {
			if (!makefile_command_safe(n->commands[c])) {
				fprintf(stderr,
					"libmake: cannot emit unsupported "
					"command for target '%s'\n",
					n->name);
				return 1;
			}
		}
	}

	for (size_t i = 0; i < dag->num_nodes; i++) {
		dag_node_t *n = dag->nodes[i];

		if (!n->has_rule)
			continue;

		if (wrote_rule)
			fputc('\n', out);
		wrote_rule = true;

		fputs(n->name, out);
		fputc(':', out);
		for (size_t d = 0; d < n->num_deps; d++) {
			fputc(' ', out);
			fputs(n->deps[d]->name, out);
		}
		fputc('\n', out);

		for (size_t c = 0; c < n->num_commands; c++) {
			fputc('\t', out);
			fputs(n->commands[c], out);
			fputc('\n', out);
		}
	}

	return 0;
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
	if (node->visited) {
		fprintf(stderr, "libmake: circular dependency on '%s'\n",
			node->name);
		return 1;
	}
	node->visited = true;

	if (!node->has_rule && file_mtime(node->name) == 0) {
		fprintf(stderr, "libmake: no rule to make target '%s'\n",
			node->name);
		return 1;
	}

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
