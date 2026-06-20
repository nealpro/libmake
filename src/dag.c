#include "dag.h"
#include <stdlib.h>
#include <string.h>

dag_t *dag_create(void)
{
	dag_t *dag = calloc(1, sizeof(dag_t));
	return dag;
}

void dag_free(dag_t *dag)
{
	if (!dag)
		return;
	for (size_t i = 0; i < dag->num_nodes; i++) {
		dag_node_t *node = dag->nodes[i];
		for (size_t j = 0; j < node->num_commands; j++)
			free(node->commands[j]);
		free(node->commands);
		free(node->name);
		free(node->deps);
		free(node);
	}

	free(dag->nodes);
	free(dag);
}

dag_node_t *dag_get_node(dag_t *dag, const char *name)
{
	for (size_t i = 0; i < dag->num_nodes; i++) {
		if (strcmp(dag->nodes[i]->name, name) == 0) {
			return dag->nodes[i];
		}
	}
	return NULL;
}

dag_node_t *dag_add_node(dag_t *dag, const char *name)
{
	dag_node_t *existing = dag_get_node(dag, name);
	if (existing) {
		return existing;
	}
	if (dag->num_nodes >= dag->capacity) {
		size_t new_capacity =
			dag->capacity == 0 ? 16 : dag->capacity * 2;
		dag_node_t **new_nodes = realloc(
			dag->nodes, new_capacity * sizeof(dag_node_t *));
		if (!new_nodes)
			return NULL;
		dag->nodes = new_nodes;
		dag->capacity = new_capacity;
	}
	dag_node_t *node = calloc(1, sizeof(dag_node_t));
	if (!node)
		return NULL;
	node->name = strdup(name);
	if (!node->name) {
		free(node);
		return NULL;
	}
	dag->nodes[dag->num_nodes++] = node;
	return node;
}

bool dag_add_edge(dag_t *dag, const char *target, const char *dependency)
{
	dag_node_t *t_node = dag_add_node(dag, target);
	dag_node_t *d_node = dag_add_node(dag, dependency);
	if (!t_node || !d_node)
		return false;

	if (t_node->num_deps >= t_node->capacity) {
		size_t new_capacity =
			t_node->capacity == 0 ? 4 : t_node->capacity * 2;
		dag_node_t **new_deps = realloc(
			t_node->deps, new_capacity * sizeof(dag_node_t *));
		if (!new_deps)
			return false;
		t_node->deps = new_deps;
		t_node->capacity = new_capacity;
	}

	t_node->deps[t_node->num_deps++] = d_node;
	return true;
}

bool dag_node_add_command(dag_node_t *node, const char *command)
{
	if (node->num_commands >= node->cmd_capacity) {
		size_t new_capacity =
			node->cmd_capacity == 0 ? 4 : node->cmd_capacity * 2;
		char **new_commands =
			realloc(node->commands, new_capacity * sizeof(char *));
		if (!new_commands)
			return false;
		node->commands = new_commands;
		node->cmd_capacity = new_capacity;
	}
	node->commands[node->num_commands] = strdup(command);
	if (!node->commands[node->num_commands])
		return false;
	node->num_commands++;
	return true;
}
