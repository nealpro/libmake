#ifndef DAG_H
#define DAG_H

#include <stddef.h>
#include <stdbool.h>

typedef struct dag_node {
	char *name;
	char **commands;
	size_t num_commands;
	size_t cmd_capacity;
	struct dag_node **deps;
	size_t num_deps;
	size_t capacity;
	bool has_rule;
	bool visited;
	bool resolved;
} dag_node_t;

typedef struct {
	dag_node_t **nodes;
	size_t num_nodes;
	size_t capacity;
} dag_t;

dag_t *dag_create(void);
void dag_free(dag_t *dag);

dag_node_t *dag_add_node(dag_t *dag, const char *name);
dag_node_t *dag_get_node(dag_t *dag, const char *name);
bool dag_add_edge(dag_t *dag, const char *target, const char *dependency);
bool dag_node_add_command(dag_node_t *node, const char *command);

#endif // DAG_H
