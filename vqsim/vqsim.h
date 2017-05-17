
typedef enum {
	VQMSG_QUIT = 1,
	VQMSG_SYNC,		  /* set nodelist */
	VQMSG_QUORUM,	 /* quorum state of this 'node' */
	VQMSG_EXEC,		  /* message for exec_handler */
	VQMSG_QDEVICE,	/* quorum device enable/disable */
	VQMSG_QUORUMQUIT, /* quit if you don't have quorum */
} vqsim_msg_type_t;

typedef struct vq_instance *vq_object_t;

struct vqsim_msg_header {
	vqsim_msg_type_t type;
	int from_nodeid;
	int param;
};

/* This is the sync sent from the controller process */
struct vqsim_sync_msg {
	struct vqsim_msg_header header;
	struct memb_ring_id ring_id;
	size_t view_list_entries;
	unsigned int view_list[];
};

/* This is just info sent from each VQ instance */
struct vqsim_quorum_msg {
	struct vqsim_msg_header header;
	int quorate;
	struct memb_ring_id ring_id;
	size_t view_list_entries;
	unsigned int view_list[];
};

struct vqsim_exec_msg {
	struct vqsim_msg_header header;
	char execmsg[];
};

struct vqsim_lib_msg {
	struct vqsim_msg_header header;
	char libmsg[];
};

#define MAX_NODES 1024
#define MAX_PARTITIONS 16

/* In vq_object.c */
vq_object_t vq_create_instance (qb_loop_t *poll_loop, int nodeid);
void vq_quit (vq_object_t instance);
int vq_set_nodelist (vq_object_t instance, struct memb_ring_id *ring_id, int *nodeids, int nodeids_entries);
int vq_get_parent_fd (vq_object_t instance);
int vq_set_qdevice (vq_object_t instance, struct memb_ring_id *ring_id, int onoff);
int vq_quit_if_inquorate (vq_object_t instance);
pid_t vq_get_pid (vq_object_t instance);

/* in vqsim_vq_engine.c - effectively the constructor */
int fork_new_instance (int nodeid, int *vq_sock, pid_t *child_pid);

/* In parser.c */
void parse_input_command (char *cmd);

/* These are in vqmain.c */
void cmd_stop_node (int nodeid);
void cmd_stop_all_nodes (void);
void cmd_start_new_node (int nodeid, int partition);
void cmd_set_autofence (int onoff);
void cmd_move_nodes (int partition, int num_nodes, int *nodelist);
void cmd_join_partitions (int part1, int part2);
void cmd_update_all_partitions (int newring);
void cmd_qdevice_poll (int nodeid, int onoff);
void cmd_show_node_states (void);
