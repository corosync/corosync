/* Parses the interactive commands */

#include <config.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#ifdef HAVE_READLINE_HISTORY_H
#include <readline/history.h>
#endif

#include <corosync/coroapi.h>
#include "vqsim.h"

static void do_usage(void)
{
	printf("  All node IDs in the cluster are unique and belong to a numbered 'partition' (default=0)\n");
	printf("\n");
	printf("up         [<partition>:][<nodeid>[,<nodeid>] ...] [[<partition>:][<nodeid>...]] [...]\n");
	printf("           bring node(s) online in the specified partition(s)\n");
	printf("down       <nodeid>,[<nodeid>...]\n");
	printf("           send nodes offline (shut them down)\n");
	printf("move/split [<partition>:][<nodeid>[,<nodeid>] ...] [[<partition>:][<nodeid>...]] [...]\n");
	printf("           Move nodes from one partition to another (netsplit)\n");
	printf("           <partition> here is the partition to move the nodes to\n");
	printf("join       <partition> <partition> [<partition>] ... \n");
	printf("           Join partitions together (reverse of a netsplit)\n");
	printf("qdevice    on|off [<partition>:][<nodeid>[,<nodeid>] ...] [[<partition>:][<nodeid>...]] [...]\n");
	printf("           Enable quorum device in specified nodes\n");
	printf("autofence  on|off\n");
	printf("           automatically 'down' nodes on inquorate side on netsplit\n");
	printf("show       Show current nodes status\n");
	printf("exit\n\n");
}


typedef void (*cmd_routine_t)(int argc, char **argv);

static void run_up_cmd(int argc, char **argv);
static void run_down_cmd(int argc, char **argv);
static void run_join_cmd(int argc, char **argv);
static void run_move_cmd(int argc, char **argv);
static void run_exit_cmd(int argc, char **argv);
static void run_show_cmd(int argc, char **argv);
static void run_autofence_cmd(int argc, char **argv);
static void run_qdevice_cmd(int argc, char **argv);

static struct cmd_list_struct {
	const char *cmd;
	int min_args;
	cmd_routine_t cmd_runner;
} cmd_list[] = {
	{ "up", 1, run_up_cmd},
	{ "down", 1, run_down_cmd},
	{ "move", 2, run_move_cmd},
	{ "split", 2, run_move_cmd},
	{ "join", 2, run_join_cmd},
	{ "autofence", 1, run_autofence_cmd},
	{ "qdevice", 1, run_qdevice_cmd},
	{ "show", 0, run_show_cmd},
	{ "exit", 0, run_exit_cmd},
	{ "quit", 0, run_exit_cmd},
	{ "q", 0, run_exit_cmd},
};
static int num_cmds = (sizeof(cmd_list)) / sizeof(struct cmd_list_struct);
#define MAX_ARGS 1024

/* Takes a <partition>:[<node>[,<node>]...] list and return it
   as a partition and a list of nodes.
   Returns 0 if successful, -1 if not
*/
static int parse_partition_nodelist(char *string, int *partition, int *num_nodes, int **retnodes)
{
	int i;
	int nodecount;
	int len;
	int last_comma;
	char *nodeptr;
	int  *nodes;
	char *colonptr = strchr(string, ':');

	if (colonptr) {
		*colonptr = '\0';
		nodeptr = colonptr+1;
		*partition = atoi(string);
	}
	else {
		/* Default to partition 0 */
		*partition = 0;
		nodeptr = string;
	}

	/* Count the number of commas and allocate space for the nodes */
	nodecount = 0;
	for (i=0; i<strlen(nodeptr); i++) {
		if (nodeptr[i] == ',') {
			nodecount++;
		}
	}
	nodecount++; /* The one between the last comma and the trailing NUL */
	if (nodecount < 1 || nodecount > MAX_NODES) {
		return -1;
	}

	nodes = malloc(sizeof(int) * nodecount);
	if (!nodes) {
		return -1;
	}

	nodecount = 0;
	last_comma = 0;
	len = strlen(nodeptr);
	for (i=0; i<=len; i++) {
		if (nodeptr[i] == ',' || nodeptr[i] == '\0') {

			nodeptr[i] = '\0';
			nodes[nodecount++] = atoi(&nodeptr[last_comma]);
			last_comma = i+1;
		}
	}

	*num_nodes = nodecount;
	*retnodes = nodes;

	return 0;
}

void parse_input_command(char *rl_cmd)
{
	int i;
	int argc = 0;
	int valid_cmd = 0;
	char *argv[MAX_ARGS];
	int last_arg_start = 0;
	int last_was_space = 0;
	int len;
	char *cmd;

	/* ^D quits */
	if (rl_cmd == NULL) {
		run_exit_cmd(0, NULL);
	}

	cmd = strdup(rl_cmd);

	/* Split cmd up into args
	 * destroying the original string mwahahahaha
	 */

	len = strlen(cmd);

	/* Span leading spaces */
	for (i=0; cmd[i] == ' '; i++)
		;
	last_arg_start = i;

	for (; i<=len; i++) {
		if (cmd[i] == ' ' || cmd[i] == '\0') {

			/* Allow multiple spaces */
			if (last_was_space) {
				continue;
			}

			cmd[i] = '\0';
			last_was_space = 1;

			argv[argc] = &cmd[last_arg_start];
			argc++;
		}
		else {
			if (last_was_space) {
				last_arg_start = i;
			}
			last_was_space = 0;
		}
	}

	/* Ignore null commands */
	if (strlen(argv[0]) == 0) {
		free(cmd);
	    return;
	}
#ifdef HAVE_READLINE_HISTORY_H
	add_history(rl_cmd);
#endif

	/* Dispatch command */
	for (i=0; i<num_cmds; i++) {
		if (strcasecmp(argv[0], cmd_list[i].cmd) == 0) {

			if (argc < cmd_list[i].min_args) {
				break;
			}
			cmd_list[i].cmd_runner(argc, argv);
			valid_cmd = 1;
		}
	}
	if (!valid_cmd) {
		do_usage();
	}
	free(cmd);
}



static void run_up_cmd(int argc, char **argv)
{
	int partition;
	int num_nodes;
	int *nodelist;
	int i,j;

	if (argc <= 1) {
		return;
	}

	for (i=1; i<argc; i++) {
		if (parse_partition_nodelist(argv[i], &partition, &num_nodes, &nodelist) == 0) {
			for (j=0; j<num_nodes; j++) {
				cmd_start_new_node(nodelist[j], partition);
			}
			free(nodelist);
		}
	}
}

static void run_down_cmd(int argc, char **argv)
{
	int nodeid;
	int i;

	for (i=1; i<argc; i++) {
		nodeid = atoi(argv[1]);
		cmd_stop_node(nodeid);
	}
}

static void run_join_cmd(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		printf("join needs at least two partition numbers\n");
		return;
	}

	for (i=2; i<argc; i++) {
		cmd_join_partitions(atoi(argv[1]), atoi(argv[i]));
	}
	cmd_update_all_partitions(1);
}

static void run_move_cmd(int argc, char **argv)
{
	int i;
	int partition;
	int num_nodes;
	int *nodelist;

	for (i=1; i<argc; i++) {
		if (parse_partition_nodelist(argv[i], &partition, &num_nodes, &nodelist) == 0) {
			cmd_move_nodes(partition, num_nodes, nodelist);
			free(nodelist);
		}
	}
	cmd_update_all_partitions(1);
}

static void run_autofence_cmd(int argc, char **argv)
{
	int onoff = -1;

	if (strcasecmp(argv[1], "on") == 0) {
		onoff = 1;
	}
	if (strcasecmp(argv[1], "off") == 0) {
		onoff = 0;
	}
	if (onoff == -1) {
		fprintf(stderr, "ERR: autofence value must be 'on' or 'off'\n");
	}
	else {
		cmd_set_autofence(onoff);
	}
}

static void run_qdevice_cmd(int argc, char **argv)
{
	int i,j;
	int partition;
	int num_nodes;
	int *nodelist;
	int onoff = -1;

	if (strcasecmp(argv[1], "on") == 0) {
		onoff = 1;
	}
	if (strcasecmp(argv[1], "off") == 0) {
		onoff = 0;
	}

	if (onoff == -1) {
		fprintf(stderr, "ERR: qdevice should be 'on' or 'off'\n");
		return;
	}

	for (i=2; i<argc; i++) {
		if (parse_partition_nodelist(argv[i], &partition, &num_nodes, &nodelist) == 0) {
			for (j=0; j<num_nodes; j++) {
				cmd_qdevice_poll(nodelist[j], onoff);
			}
			free(nodelist);
		}
	}
	cmd_update_all_partitions(0);
}

static void run_show_cmd(int argc, char **argv)
{
	cmd_show_node_states();
}

static void run_exit_cmd(int argc, char **argv)
{
	cmd_stop_all_nodes();
	exit(0);
}


