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
		dag->capacity = dag->capacity == 0 ? 16 : dag->capacity * 2;
		dag->nodes = realloc(
		        dag->nodes, dag->capacity * sizeof(dag_node_t *));
	}
	dag_node_t *node = calloc(1, sizeof(dag_node_t));
	node->name = strdup(name);
	dag->nodes[dag->num_nodes++] = node;
	return node;
}

bool dag_add_edge(dag_t *dag, const char *target, const char *dependency)
{
	dag_node_t *t_node = dag_add_node(dag, target);
	dag_node_t *d_node = dag_add_node(dag, dependency);

	if (t_node->num_deps >= t_node->capacity) {
		t_node->capacity = t_node->capacity == 0 ? 4 :
		                                           t_node->capacity * 2;
		t_node->deps = realloc(
		        t_node->deps, t_node->capacity * sizeof(dag_node_t *));
	}

	t_node->deps[t_node->num_deps++] = d_node;
	return true;
}

bool dag_node_add_command(dag_node_t *node, const char *command)
{
	if (node->num_commands >= node->cmd_capacity) {
		node->cmd_capacity = node->cmd_capacity == 0 ? 4 :
		                                               node->cmd_capacity * 2;
		node->commands = realloc(
		        node->commands, node->cmd_capacity * sizeof(char *));
		if (!node->commands)
			return false;
	}
	node->commands[node->num_commands++] = strdup(command);
	return true;
}
