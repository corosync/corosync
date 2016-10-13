/*
  This is a Votequorum object in the parent process. it's really just a conduit for the forked
  votequorum entity
*/

#include <qb/qblog.h>
#include <qb/qbloop.h>
#include <qb/qbipcc.h>
#include <netinet/in.h>

#include "../exec/votequorum.h"
#include "vqsim.h"

struct vq_instance
{
	int nodeid;
	int vq_socket;
	pid_t pid;
};

vq_object_t vq_create_instance(qb_loop_t *poll_loop, int nodeid)
{
	struct vq_instance *instance = malloc(sizeof(struct vq_instance));
	if (!instance) {
		return NULL;
	}

	instance->nodeid = nodeid;

	if (fork_new_instance(nodeid, &instance->vq_socket, &instance->pid)) {
		free(instance);
		return NULL;
	}

	return instance;
}

pid_t vq_get_pid(vq_object_t instance)
{
	struct vq_instance *vqi = instance;
	return vqi->pid;
}

void vq_quit(vq_object_t instance)
{
	struct vq_instance *vqi = instance;
	struct vqsim_msg_header msg;
	int res;

	msg.type = VQMSG_QUIT;
	msg.from_nodeid = 0;
	msg.param = 0;

	res = write(vqi->vq_socket, &msg, sizeof(msg));
	if (res <= 0) {
		perror("Quit write failed");
	}
}

int vq_quit_if_inquorate(vq_object_t instance)
{
	struct vq_instance *vqi = instance;
	struct vqsim_msg_header msg;
	int res;

	msg.type = VQMSG_QUORUMQUIT;
	msg.from_nodeid = 0;
	msg.param = 0;

	res = write(vqi->vq_socket, &msg, sizeof(msg));
	if (res <= 0) {
		perror("Quit write failed");
	}
	return 0;
}

int vq_set_nodelist(vq_object_t instance, struct memb_ring_id *ring_id, int *nodeids, int nodeids_entries)
{
	struct vq_instance *vqi = instance;
	char msgbuf[sizeof(int)*nodeids_entries + sizeof(struct vqsim_sync_msg)];
	struct vqsim_sync_msg *msg = (void*)msgbuf;
	int res;

	msg->header.type = VQMSG_SYNC;
	msg->header.from_nodeid = 0;
	msg->header.param = 0;
	msg->view_list_entries = nodeids_entries;
	memcpy(&msg->view_list, nodeids, nodeids_entries*sizeof(int));
	memcpy(&msg->ring_id, ring_id, sizeof(struct memb_ring_id));

	res = write(vqi->vq_socket, msgbuf, sizeof(msgbuf));
	if (res <= 0) {
		perror("Sync write failed");
		return -1;
	}
	return 0;
}

int vq_set_qdevice(vq_object_t instance, struct memb_ring_id *ring_id, int onoff)
{
	struct vq_instance *vqi = instance;
	struct vqsim_msg_header msg;
	int res;

	msg.type = VQMSG_QDEVICE;
	msg.from_nodeid = 0;
	msg.param = onoff;
	res = write(vqi->vq_socket, &msg, sizeof(msg));
	if (res <= 0) {
		perror("qdevice register write failed");
		return -1;
	}
	return 0;
}

int vq_get_parent_fd(vq_object_t instance)
{
	struct vq_instance *vqi = instance;

	return vqi->vq_socket;
}
