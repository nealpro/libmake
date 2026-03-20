#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

int exec_run_node(const dag_node_t *node)
{
	for (size_t i = 0; i < node->num_commands; i++) {
		const char *cmd = node->commands[i];
		printf("\t%s\n", cmd);
		int status = system(cmd);
		if (status == -1)
			return -1;
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			return WEXITSTATUS(status);
		if (WIFSIGNALED(status))
			return 128 + WTERMSIG(status);
	}
	return 0;
}
