#include <config.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <qb/qblog.h>
#include <qb/qbloop.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <sys/queue.h>
#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#else
#include <unistd.h>  /* isatty */
#endif

#include "../exec/votequorum.h"
#include "../exec/service.h"
#include <corosync/logsys.h>
#include <corosync/coroapi.h>

#include "icmap.h"
#include "vqsim.h"

/* Easier than including the config file with a ton of conflicting dependencies */
extern int coroparse_configparse (icmap_map_t config_map, const char **error_string);
extern int corosync_log_config_read (const char **error_string);

/* One of these per partition */
struct vq_partition {
	TAILQ_HEAD(, vq_node) nodelist;
	struct memb_ring_id ring_id;
	int num;
};

/* One of these per node */
struct vq_node {
	vq_object_t instance;
	unsigned int nodeid;
	int fd;
	struct vq_partition *partition;
	TAILQ_ENTRY(vq_node) entries;

	/* Last status */
	int last_quorate;
	struct memb_ring_id last_ring_id;
	int last_view_list[MAX_NODES];
	int last_view_list_entries;
};

static struct vq_partition partitions[MAX_PARTITIONS];
static qb_loop_t *poll_loop;
static int autofence;
static int check_for_quorum;
static FILE *output_file;
static int nosync;
static qb_loop_timer_handle kb_timer;
static ssize_t wait_count;
static ssize_t wait_count_to_unblock;

static struct vq_node *find_by_pid(pid_t pid);
static void send_partition_to_nodes(struct vq_partition *partition, int newring);
static void start_kb_input(void);
static void start_kb_input_timeout(void *data);

#ifndef HAVE_READLINE_READLINE_H
#define INPUT_BUF_SIZE 1024
static char input_buf[INPUT_BUF_SIZE];
static size_t input_buf_term = 0;
static int is_tty;
#endif

/* Tell all non-quorate nodes to quit */
static void force_fence(void)
{
	int i;
	struct vq_node *vqn;

	for (i=0; i<MAX_PARTITIONS; i++) {
		TAILQ_FOREACH(vqn, &partitions[i].nodelist, entries) {
			vq_quit_if_inquorate(vqn->instance);
		}
	}
}

/* Save quorum state from the incoming message */
static void save_quorum_state(struct vq_node *node, struct vqsim_quorum_msg *qmsg)
{
	node->last_quorate = qmsg->quorate;
	memcpy(&node->last_ring_id, &qmsg->ring_id, sizeof(struct memb_ring_id));
	memcpy(node->last_view_list, qmsg->view_list, sizeof(int) * qmsg->view_list_entries);
	node->last_view_list_entries = qmsg->view_list_entries;

	/* If at least one node is quorate and autofence is enabled, then fence everyone who is not quorate */
	if (check_for_quorum && qmsg->quorate & autofence) {
		check_for_quorum = 0;
		force_fence();
	}
}

/* Print current node state */
static void print_quorum_state(struct vq_node *node)
{
	int i;

	if (node->last_quorate < 0) {
		fprintf(output_file, "%d:%02d: q=UNINITIALIZED\n",
			node->partition->num, node->nodeid);
		return;
	}

	fprintf(output_file, "%d:%02d: q=%d ring=[%d/%lld] ", node->partition->num, node->nodeid, node->last_quorate,
		node->last_ring_id.rep.nodeid, node->last_ring_id.seq);
	fprintf(output_file, "nodes=[");
	for (i = 0; i < node->last_view_list_entries; i++) {
		if (i) {
			fprintf(output_file, " ");
		}
		fprintf(output_file, "%d", node->last_view_list[i]);
	}
	fprintf(output_file, "]\n");

}

static void propogate_vq_message(struct vq_node *vqn, const char *msg, int len)
{
	struct vq_node *other_vqn;

	/* Send it to everyone in that node's partition (including itself) */
	TAILQ_FOREACH(other_vqn, &vqn->partition->nodelist, entries) {
		write(other_vqn->fd, msg, len);
	}
}

static int vq_parent_read_fn(int32_t fd, int32_t revents, void *data)
{
	char msgbuf[8192];
	int msglen;
	struct vqsim_msg_header *msg;
	struct vqsim_quorum_msg *qmsg;
	struct vq_node *vqn = data;

	if (revents == POLLIN) {
		msglen = read(fd, msgbuf, sizeof(msgbuf));
		if (msglen < 0) {
			perror("read failed");
		}

		if (msglen > 0) {
			msg = (void*)msgbuf;
			switch (msg->type) {
			case VQMSG_QUORUM:
				if (!nosync && --wait_count_to_unblock <= 0)
					qb_loop_timer_del(poll_loop, kb_timer);
				qmsg = (void*)msgbuf;
				save_quorum_state(vqn, qmsg);
				print_quorum_state(vqn);
				if (!nosync && wait_count_to_unblock <= 0)
					start_kb_input();
				break;
			case VQMSG_EXEC:
				/* Message from votequorum, pass around the partition */
				propogate_vq_message(vqn, msgbuf, msglen);
				break;
			case VQMSG_QUIT:
			case VQMSG_SYNC:
			case VQMSG_QDEVICE:
			case VQMSG_QUORUMQUIT:
				/* not used here */
				break;
			}
		}
	}
	if (revents == POLLERR) {
		fprintf(stderr, "pollerr on %d\n", vqn->nodeid);
	}
	return 0;
}


static int read_corosync_conf(void)
{
	int res;
	const char *error_string;

	int err = icmap_init();
	if (!err) {
		fprintf(stderr, "icmap_init failed\n");
	}

	/* Load corosync.conf */
	logsys_format_set(NULL);
	res = coroparse_configparse(icmap_get_global_map(), &error_string);
	if (res == -1) {
		log_printf (LOGSYS_LEVEL_INFO, "Error loading corosyc.conf %s", error_string);
		return -1;
	}
	else {
		res = corosync_log_config_read (&error_string);
		if (res < 0) {
			log_printf (LOGSYS_LEVEL_INFO, "error reading log config %s", error_string);
			syslog (LOGSYS_LEVEL_INFO, "error reading log config %s", error_string);
		}
		else {
			logsys_config_apply();
		}
	}
	if (logsys_thread_start() != 0) {
	        log_printf (LOGSYS_LEVEL_ERROR, "Can't initialize log thread");
		return -1;
	}
	return 0;
}

static void remove_node(struct vq_node *node)
{
	struct vq_partition *part;
	part = node->partition;

	/* Remove from partition list */
	TAILQ_REMOVE(&part->nodelist, node, entries);
	free(node);

	wait_count--;

	/* Rebuild quorum */
	send_partition_to_nodes(part, 1);
}

static int32_t sigchld_handler(int32_t sig, void *data)
{
	pid_t pid;
	int status;
	struct vq_node *vqn;
	const char *exit_status="";
	char text[132];

	pid = wait(&status);
	if (WIFEXITED(status)) {
		vqn = find_by_pid(pid);
		if (vqn) {
			switch (WEXITSTATUS(status)) {
			case 0:
				exit_status = "(on request)";
				break;
			case 1:
				exit_status = "(autofenced)";
				break;
			default:
				sprintf(text, "(exit code %d)", WEXITSTATUS(status));
				break;
			}
			printf("%d:%02d Quit %s\n", vqn->partition->num, vqn->nodeid, exit_status);

			remove_node(vqn);
		}
		else {
			fprintf(stderr, "Unknown child %d exited with status %d\n", pid, WEXITSTATUS(status));
		}
	}
	if (WIFSIGNALED(status)) {
		vqn = find_by_pid(pid);
		if (vqn) {
			printf("%d:%02d exited on signal %d%s\n", vqn->partition->num, vqn->nodeid, WTERMSIG(status), WCOREDUMP(status)?" (core dumped)":"");
			remove_node(vqn);
		}
		else {
			fprintf(stderr, "Unknown child %d exited with status %d%s\n", pid, WTERMSIG(status), WCOREDUMP(status)?" (core dumped)":"");
		}
	}
	return 0;
}

static void send_partition_to_nodes(struct vq_partition *partition, int newring)
{
	struct vq_node *vqn;
	int nodelist[MAX_NODES];
	int nodes = 0;
	int first = 1;

	if (newring) {
		/* Simulate corosync incrementing the seq by 4 for added authenticity */
		partition->ring_id.seq += 4;
	}

	/* Build the node list */
	TAILQ_FOREACH(vqn, &partition->nodelist, entries) {
		nodelist[nodes++] = vqn->nodeid;
		if (first) {
			partition->ring_id.rep.nodeid = vqn->nodeid;
			first = 0;
		}
	}

	TAILQ_FOREACH(vqn, &partition->nodelist, entries) {
		vq_set_nodelist(vqn->instance, &partition->ring_id, nodelist, nodes);
	}
}

static void init_partitions(void)
{
	int i;

	for (i=0; i<MAX_PARTITIONS; i++) {
		TAILQ_INIT(&partitions[i].nodelist);
		partitions[i].ring_id.rep.nodeid = 1000+i;
		partitions[i].ring_id.seq = 0;
		partitions[i].num = i;
	}
}

static pid_t create_node(int nodeid, int partno)
{
	struct vq_node *newvq;

	newvq = malloc(sizeof(struct vq_node));
	if (newvq) {
		if (!nosync) {
			/* Number of expected "quorum" vq messages is a square
			   of the total nodes count, so increment the node
			   counter and set new square of this value as
			   a "to observe" counter */
			wait_count++;
			wait_count_to_unblock = wait_count * wait_count;
		}
		newvq->last_quorate = -1;  /* mark "uninitialized" */
		newvq->instance = vq_create_instance(poll_loop, nodeid);
		if (!newvq->instance) {
			fprintf(stderr,
			        "ERR: could not create vq instance nodeid %d\n",
				nodeid);
			return (pid_t) -1;
		}
		newvq->partition = &partitions[partno];
		newvq->nodeid = nodeid;
		newvq->fd = vq_get_parent_fd(newvq->instance);
		TAILQ_INSERT_TAIL(&partitions[partno].nodelist, newvq, entries);

		if (qb_loop_poll_add(poll_loop,
				     QB_LOOP_MED,
				     newvq->fd,
				     POLLIN | POLLERR,
				     newvq,
				     vq_parent_read_fn)) {
			perror("qb_loop_poll_add returned error");
			return (pid_t) -1;
		}

		/* Send sync with all the nodes so far in it. */
		send_partition_to_nodes(&partitions[partno], 1);
		return vq_get_pid(newvq->instance);
	}
	return (pid_t) -1;
}

static size_t create_nodes_from_config(void)
{
	icmap_iter_t iter;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	uint32_t node_pos;
	uint32_t nodeid;
	const char *iter_key;
	int res;
	pid_t pid;
	size_t ret = 0;

	init_partitions();

	iter = icmap_iter_init("nodelist.node.");
	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {
		res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.nodeid", node_pos);
		if (icmap_get_uint32(tmp_key, &nodeid) == CS_OK) {
			pid = create_node(nodeid, 0);
			if (pid == (pid_t) -1) {
				fprintf(stderr,
					"ERR: nodeid %d could not be spawned\n",
					nodeid);
				exit(1);
			}
			ret++;
		}

	}
	icmap_iter_finalize(iter);

	return ret;
}

static struct vq_node *find_node(int nodeid)
{
	int i;
	struct vq_node *vqn;

	for (i=0; i<MAX_PARTITIONS; i++) {
		TAILQ_FOREACH(vqn, &partitions[i].nodelist, entries) {
			if (vqn->nodeid == nodeid) {
				return vqn;
			}
		}
	}
	return NULL;
}

static struct vq_node *find_by_pid(pid_t pid)
{
	int i;
	struct vq_node *vqn;

	for (i=0; i<MAX_PARTITIONS; i++) {
		TAILQ_FOREACH(vqn, &partitions[i].nodelist, entries) {
			if (vq_get_pid(vqn->instance) == pid) {
				return vqn;
			}
		}
	}
	return NULL;
}

/* Routines called from the parser */
void cmd_start_new_node(int nodeid, int partition)
{
	struct vq_node *node;

	node = find_node(nodeid);
	if (node) {
		fprintf(stderr, "ERR: nodeid %d already exists in partition %d\n", nodeid, node->partition->num);
		return;
	}
	qb_loop_poll_del(poll_loop, STDIN_FILENO);
	create_node(nodeid, partition);
	if (!nosync) {
		/* Delay kb input handling by 0.25 second when we've just
		   added a node; expect that the delay will be cancelled
		   substantially earlier once it has reported its quorum info
		   (the delay is in fact a failsafe input enabler here) */
		qb_loop_timer_add(poll_loop,
				  QB_LOOP_MED,
				  250000000,
				  NULL,
				  start_kb_input_timeout,
				  &kb_timer);
	}
}

void cmd_stop_all_nodes()
{
	int i;
	struct vq_node *vqn;

	for (i=0; i<MAX_PARTITIONS; i++) {
		TAILQ_FOREACH(vqn, &partitions[i].nodelist, entries) {
			vq_quit(vqn->instance);
		}
	}
}

void cmd_show_node_states()
{
	int i;
	struct vq_node *vqn;

	for (i=0; i<MAX_PARTITIONS; i++) {
		TAILQ_FOREACH(vqn, &partitions[i].nodelist, entries) {
			print_quorum_state(vqn);
		}
	}
	fprintf(output_file, "#autofence: %s\n", autofence?"on":"off");
}

void cmd_stop_node(int nodeid)
{
	struct vq_node *node;

	node = find_node(nodeid);
	if (!node) {
		fprintf(stderr, "ERR: nodeid %d is not up\n", nodeid);
		return;
	}

	/* Remove processor */
	vq_quit(node->instance);

	/* Node will be removed when the child process exits */
}

/* Move all nodes in 'nodelist' into partition 'partition' */
void cmd_move_nodes(int partition, int num_nodes, int *nodelist)
{
	int i;
	struct vq_node *node;

	for (i=0; i<num_nodes; i++) {
		node = find_node(nodelist[i]);
		if (node) {

			/* Remove it from the current partition */
			TAILQ_REMOVE(&node->partition->nodelist, node, entries);

			/* Add it to the new partition */
			TAILQ_INSERT_TAIL(&partitions[partition].nodelist, node, entries);
			node->partition = &partitions[partition];
		}
		else {
			printf("ERR: node %d does not exist\n", nodelist[i]);
		}
	}
}

/* Take all the nodes in part2 and join them to part1 */
void cmd_join_partitions(int part1, int part2)
{
	struct vq_node *vqn;

	/* TAILQ_FOREACH is not delete safe *sigh* */
retry:
	TAILQ_FOREACH(vqn, &partitions[part2].nodelist, entries) {

		TAILQ_REMOVE(&vqn->partition->nodelist, vqn, entries);
		TAILQ_INSERT_TAIL(&partitions[part1].nodelist, vqn, entries);
		vqn->partition = &partitions[part1];

		goto retry;
	}
}

void cmd_set_autofence(int onoff)
{
	autofence = onoff;
	fprintf(output_file, "#autofence: %s\n", onoff?"on":"off");
}

void cmd_update_all_partitions(int newring)
{
	int i;

	check_for_quorum = 1;
	for (i=0; i<MAX_PARTITIONS; i++) {
		send_partition_to_nodes(&partitions[i], newring);
	}
}

void cmd_qdevice_poll(int nodeid, int onoff)
{
	struct vq_node *node;

	node = find_node(nodeid);
	if (node) {
		vq_set_qdevice(node->instance, &node->partition->ring_id, onoff);
	}
}

/* ---------------------------------- */

#ifndef HAVE_READLINE_READLINE_H
static void dummy_read_char(void);

static void dummy_read_char()
{
	int c, flush = 0;

	while (!flush) {
		c = getchar();
		if (++input_buf_term >= INPUT_BUF_SIZE) {
			if (c != '\n' && c != EOF)
				fprintf(stderr, "User input overflows the limit: %zu\n",
						(size_t) INPUT_BUF_SIZE);
			input_buf[INPUT_BUF_SIZE - 1] = '\0';
			flush = 1;
		} else if (c == '\n' || c == EOF) {
			input_buf[input_buf_term - 1] = '\0';
			flush = 1;
		} else {
			input_buf[input_buf_term - 1] = c;
		}
	}

	parse_input_command((c == EOF) ? NULL : input_buf);
	input_buf_term = 0;

	if (is_tty) {
		printf("vqsim> ");
		fflush(stdout);
	}
}
#endif

static int stdin_read_fn(int32_t fd, int32_t revents, void *data)
{
#ifdef HAVE_READLINE_READLINE_H
	/* Send it to readline */
	rl_callback_read_char();
#else
	dummy_read_char();
#endif
	return 0;
}

static void start_kb_input(void)
{
	wait_count_to_unblock = 0;

#ifdef HAVE_READLINE_READLINE_H
	/* Readline will deal with completed lines when they arrive */
	rl_callback_handler_install("vqsim> ", parse_input_command);
#else
	if (is_tty) {
		printf("vqsim> ");
		fflush(stdout);
	}
#endif

	/* Send stdin to readline */
	if (qb_loop_poll_add(poll_loop,
			     QB_LOOP_MED,
			     STDIN_FILENO,
			     POLLIN | POLLERR,
			     NULL,
			     stdin_read_fn)) {
		if (errno != EEXIST) {
			perror("qb_loop_poll_add1 returned error");
		}
	}
}

static void start_kb_input_timeout(void *data)
{
//	fprintf(stderr, "Waiting for nodes to report status timed out\n");
	start_kb_input();
}

static void usage(char *program)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [-f <config-file>] [-o <output-file>]\n", program);
	printf("\n");
	printf("    -f     config file. defaults to /etc/corosync/corosync.conf\n");
	printf("    -o     output file. defaults to stdout\n");
	printf("    -n     no synchronization (on adding a node)\n");
	printf("    -h     display this help text\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	qb_loop_signal_handle sigchld_qb_handle;
	int ch;
	char *config_file_name = NULL;
	char *output_file_name = NULL;
	char envstring[PATH_MAX];

	while ((ch = getopt (argc, argv, "f:o:nh")) != EOF) {
		switch (ch) {
		case 'f':
			config_file_name = optarg;
			break;
		case 'o':
			output_file_name = optarg;
			break;
		case 'n':
			nosync = 1;
			break;
		default:
			usage(argv[0]);
			exit(0);
		}
	}

	if (config_file_name) {
		sprintf(envstring, "COROSYNC_MAIN_CONFIG_FILE=%s", config_file_name);
		putenv(envstring);
	}
	if (output_file_name) {
		output_file = fopen(output_file_name, "w");
		if (!output_file) {
			fprintf(stderr, "Unable to open %s for output: %s\n", output_file_name, strerror(errno));
			exit(-1);
		}
	}
	else {
		output_file = stdout;
	}
#ifndef HAVE_READLINE_READLINE_H
	is_tty = isatty(STDIN_FILENO);
#endif

	qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION, "*", LOG_DEBUG);

	qb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);
	qb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FUNCTION, "*", LOG_DEBUG);

	poll_loop = qb_loop_create();

	/* SIGCHLD handler to reap sub-processes and reconfigure the cluster */
	qb_loop_signal_add(poll_loop,
			   QB_LOOP_MED,
			   SIGCHLD,
			   NULL,
			   sigchld_handler,
			   &sigchld_qb_handle);

	/* Create a full cluster of nodes from corosync.conf */
	read_corosync_conf();
	if (create_nodes_from_config() && !nosync) {
		/* Delay kb input handling by 1 second when we've just
		   added the nodes from corosync.conf; expect that
		   the delay will be cancelled substantially earlier
		   once they all have reported their quorum info
		   (the delay is in fact a failsafe input enabler here) */
		qb_loop_timer_add(poll_loop,
				  QB_LOOP_MED,
				  1000000000,
				  NULL,
				  start_kb_input_timeout,
				  &kb_timer);
	} else {
		start_kb_input();
	}

	qb_loop_run(poll_loop);
	return 0;
}
