#ifndef EXEC_H
#define EXEC_H

#include "dag.h"

/*
 * Execute the recipe (command list) for a single DAG node.
 *
 * Commands are run in order via the shell. Execution stops on the first
 * command that exits with a non-zero status.
 *
 * Returns 0 on success, or the exit status of the failing command.
 */
int exec_run_node(const dag_node_t *node);

#endif /* EXEC_H */
