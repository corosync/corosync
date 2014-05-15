/*
 * Copyright (c) 2009-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)

 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <assert.h>
#include <errno.h>

#include <corosync/sq.h>
#include <corosync/list.h>
#include <corosync/hdb.h>
#include <corosync/swab.h>

#include <qb/qbdefs.h>
#include <qb/qbloop.h>
#define LOGSYS_UTILS_ONLY 1
#include <corosync/logsys.h>
#include "totemiba.h"

#define COMPLETION_QUEUE_ENTRIES 100

#define TOTAL_READ_POSTS 100

#define MAX_MTU_SIZE 4096

#define MCAST_REJOIN_MSEC	100

struct totemiba_instance {
	struct sockaddr bind_addr;

	struct sockaddr send_token_bind_addr;

	struct sockaddr mcast_addr;

	struct sockaddr token_addr;

	struct sockaddr local_mcast_bind_addr;

	struct totem_interface *totem_interface;

	struct totem_config *totem_config;

	totemsrp_stats_t *stats;

	void (*totemiba_iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address);

	void (*totemiba_deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len);

	void (*totemiba_target_set_completed) (
		void *context);

	void *rrp_context;

	qb_loop_timer_handle timer_netif_check_timeout;

	qb_loop_t *totemiba_poll_handle;

	struct totem_ip_address my_id;

	struct rdma_event_channel *mcast_channel;

	struct rdma_cm_id *mcast_cma_id;

	struct ibv_pd *mcast_pd;

	struct sockaddr mcast_dest_addr;

	uint32_t mcast_qpn;

	uint32_t mcast_qkey;

	struct ibv_ah *mcast_ah;

	struct ibv_comp_channel *mcast_send_completion_channel;

	struct ibv_comp_channel *mcast_recv_completion_channel;

	struct ibv_cq *mcast_send_cq;

	struct ibv_cq *mcast_recv_cq;

	int recv_token_accepted;

	struct rdma_event_channel *recv_token_channel;

	struct rdma_event_channel *listen_recv_token_channel;

	struct rdma_cm_id *listen_recv_token_cma_id;

	struct rdma_cm_id *recv_token_cma_id;

	struct ibv_pd *recv_token_pd;

	struct sockaddr recv_token_dest_addr;

	struct ibv_comp_channel *recv_token_send_completion_channel;

	struct ibv_comp_channel *recv_token_recv_completion_channel;

	struct ibv_cq *recv_token_send_cq;

	struct ibv_cq *recv_token_recv_cq;

	int send_token_bound;

	struct rdma_event_channel *send_token_channel;

	struct rdma_cm_id *send_token_cma_id;

	struct ibv_pd *send_token_pd;

	struct sockaddr send_token_dest_addr;

	uint32_t send_token_qpn;

	uint32_t send_token_qkey;

	struct ibv_ah *send_token_ah;

	struct ibv_comp_channel *send_token_send_completion_channel;

	struct ibv_comp_channel *send_token_recv_completion_channel;

	struct ibv_cq *send_token_send_cq;

	struct ibv_cq *send_token_recv_cq;

        void (*totemiba_log_printf) (
		int level,
		int subsys,
		const char *function,
		const char *file,
		int line,
		const char *format,
		...)__attribute__((format(printf, 6, 7)));


	int totemiba_subsys_id;

	struct list_head mcast_send_buf_free;

	struct list_head token_send_buf_free;

	struct list_head mcast_send_buf_head;

	struct list_head token_send_buf_head;

	struct list_head recv_token_recv_buf_head;

	int mcast_seen_joined;

	qb_loop_timer_handle mcast_rejoin;
};
union u {
	uint64_t wr_id;
	void *v;
};

#define log_printf(level, format, args...)			\
do {								\
        instance->totemiba_log_printf (				\
			level,					\
			instance->totemiba_subsys_id,		\
			__FUNCTION__, __FILE__, __LINE__,	\
			(const char *)format, ##args);		\
} while (0);

struct recv_buf {
	struct list_head list_all;
	struct ibv_recv_wr recv_wr;
	struct ibv_sge sge;
	struct ibv_mr *mr;
	char buffer[MAX_MTU_SIZE + sizeof (struct ibv_grh)];
};

struct send_buf {
	struct list_head list_free;
	struct list_head list_all;
	struct ibv_mr *mr;
	char buffer[MAX_MTU_SIZE];
};

static hdb_handle_t
void2wrid (void *v) { union u u; u.v = v; return u.wr_id; }

static void *
wrid2void (uint64_t wr_id) { union u u; u.wr_id = wr_id; return u.v; }

static void totemiba_instance_initialize (struct totemiba_instance *instance)
{
	memset (instance, 0, sizeof (struct totemiba_instance));
	list_init (&instance->mcast_send_buf_free);
	list_init (&instance->token_send_buf_free);
	list_init (&instance->mcast_send_buf_head);
	list_init (&instance->token_send_buf_head);
	list_init (&instance->recv_token_recv_buf_head);
}

static inline struct send_buf *mcast_send_buf_get (
	struct totemiba_instance *instance)
{
	struct send_buf *send_buf;

	if (list_empty (&instance->mcast_send_buf_free) == 0) {
		send_buf = list_entry (instance->mcast_send_buf_free.next, struct send_buf, list_free);
		list_del (&send_buf->list_free);
		return (send_buf);
	}

	send_buf = malloc (sizeof (struct send_buf));
	if (send_buf == NULL) {
		return (NULL);
	}
	send_buf->mr = ibv_reg_mr (instance->mcast_pd,
		send_buf->buffer,
		MAX_MTU_SIZE, IBV_ACCESS_LOCAL_WRITE);
	if (send_buf->mr == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't register memory range");
		free (send_buf);
		return (NULL);
	}
	list_init (&send_buf->list_all);
	list_add_tail (&send_buf->list_all, &instance->mcast_send_buf_head);
		
	return (send_buf);
}

static inline void mcast_send_buf_put (
	struct totemiba_instance *instance,
	struct send_buf *send_buf)
{
	list_init (&send_buf->list_free);
	list_add_tail (&send_buf->list_free, &instance->mcast_send_buf_free);
}

static inline struct send_buf *token_send_buf_get (
	struct totemiba_instance *instance)
{
	struct send_buf *send_buf;

	if (list_empty (&instance->token_send_buf_free) == 0) {
		send_buf = list_entry (instance->token_send_buf_free.next, struct send_buf, list_free);
		list_del (&send_buf->list_free);
		return (send_buf);
	}

	send_buf = malloc (sizeof (struct send_buf));
	if (send_buf == NULL) {
		return (NULL);
	}
	send_buf->mr = ibv_reg_mr (instance->send_token_pd,
		send_buf->buffer,
		MAX_MTU_SIZE, IBV_ACCESS_LOCAL_WRITE);
	if (send_buf->mr == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't register memory range");
		free (send_buf);
		return (NULL);
	}
	list_init (&send_buf->list_all);
	list_add_tail (&send_buf->list_all, &instance->token_send_buf_head);
		
	return (send_buf);
}

static inline void token_send_buf_destroy (struct totemiba_instance *instance)
{
	struct list_head *list;
	struct send_buf *send_buf;

        for (list = instance->token_send_buf_head.next; list != &instance->token_send_buf_head;) {
                send_buf = list_entry (list, struct send_buf, list_all);
		list = list->next;
		ibv_dereg_mr (send_buf->mr);
		free (send_buf);
	}

	list_init (&instance->token_send_buf_free);
	list_init (&instance->token_send_buf_head);
}

static inline void token_send_buf_put (
	struct totemiba_instance *instance,
	struct send_buf *send_buf)
{
	list_init (&send_buf->list_free);
	list_add_tail (&send_buf->list_free, &instance->token_send_buf_free);
}

static inline struct recv_buf *recv_token_recv_buf_create (
	struct totemiba_instance *instance)
{
	struct recv_buf *recv_buf;

	recv_buf = malloc (sizeof (struct recv_buf));
	if (recv_buf == NULL) {
		return (NULL);
	}

	recv_buf->mr = ibv_reg_mr (instance->recv_token_pd, &recv_buf->buffer,
		MAX_MTU_SIZE + sizeof (struct ibv_grh),
		IBV_ACCESS_LOCAL_WRITE);

	recv_buf->recv_wr.next = NULL;
	recv_buf->recv_wr.sg_list = &recv_buf->sge;
	recv_buf->recv_wr.num_sge = 1;
	recv_buf->recv_wr.wr_id = (uintptr_t)recv_buf;

	recv_buf->sge.length = MAX_MTU_SIZE + sizeof (struct ibv_grh);
	recv_buf->sge.lkey = recv_buf->mr->lkey;
	recv_buf->sge.addr = (uintptr_t)recv_buf->buffer;

	list_init (&recv_buf->list_all);
	list_add (&recv_buf->list_all, &instance->recv_token_recv_buf_head);
	return (recv_buf);
}

static inline int recv_token_recv_buf_post (struct totemiba_instance *instance, struct recv_buf *recv_buf)
{
	struct ibv_recv_wr *fail_recv;
	int res;

	res = ibv_post_recv (instance->recv_token_cma_id->qp, &recv_buf->recv_wr, &fail_recv);

	return (res);
}

static inline void recv_token_recv_buf_post_initial (struct totemiba_instance *instance)
{
	struct recv_buf *recv_buf;
	unsigned int i;

	for (i = 0; i < TOTAL_READ_POSTS; i++) {
		recv_buf = recv_token_recv_buf_create (instance);

		recv_token_recv_buf_post (instance, recv_buf);
	}
}

static inline void recv_token_recv_buf_post_destroy (
	struct totemiba_instance *instance)
{
	struct recv_buf *recv_buf;
	struct list_head *list;

	for (list = instance->recv_token_recv_buf_head.next;
		list != &instance->recv_token_recv_buf_head;) {

		recv_buf = list_entry (list, struct recv_buf, list_all);
		list = list->next;
		ibv_dereg_mr (recv_buf->mr);
		free (recv_buf);
	}
	list_init (&instance->recv_token_recv_buf_head);
}

static inline struct recv_buf *mcast_recv_buf_create (struct totemiba_instance *instance)
{
	struct recv_buf *recv_buf;
	struct ibv_mr *mr;

	recv_buf = malloc (sizeof (struct recv_buf));
	if (recv_buf == NULL) {
		return (NULL);
	}

	mr = ibv_reg_mr (instance->mcast_pd, &recv_buf->buffer,
		MAX_MTU_SIZE + sizeof (struct ibv_grh),
		IBV_ACCESS_LOCAL_WRITE);

	recv_buf->recv_wr.next = NULL;
	recv_buf->recv_wr.sg_list = &recv_buf->sge;
	recv_buf->recv_wr.num_sge = 1;
	recv_buf->recv_wr.wr_id = (uintptr_t)recv_buf;

	recv_buf->sge.length = MAX_MTU_SIZE + sizeof (struct ibv_grh);
	recv_buf->sge.lkey = mr->lkey;
	recv_buf->sge.addr = (uintptr_t)recv_buf->buffer;

	return (recv_buf);
}

static inline int mcast_recv_buf_post (struct totemiba_instance *instance, struct recv_buf *recv_buf)
{
	struct ibv_recv_wr *fail_recv;
	int res;

	res = ibv_post_recv (instance->mcast_cma_id->qp, &recv_buf->recv_wr, &fail_recv);

	return (res);
}

static inline void mcast_recv_buf_post_initial (struct totemiba_instance *instance)
{
	struct recv_buf *recv_buf;
	unsigned int i;

	for (i = 0; i < TOTAL_READ_POSTS; i++) {
		recv_buf = mcast_recv_buf_create (instance);

		mcast_recv_buf_post (instance, recv_buf);
	}
}

static inline void iba_deliver_fn (struct totemiba_instance *instance, uint64_t wr_id, uint32_t bytes)
{
	const char *addr;
	const struct recv_buf *recv_buf;

	recv_buf = wrid2void(wr_id);
	addr = &recv_buf->buffer[sizeof (struct ibv_grh)];

	bytes -= sizeof (struct ibv_grh);
	instance->totemiba_deliver_fn (instance->rrp_context, addr, bytes);
}

static int mcast_cq_send_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct ibv_wc wc[32];
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int res;
	int i;

	ibv_get_cq_event (instance->mcast_send_completion_channel, &ev_cq, &ev_ctx);
	ibv_ack_cq_events (ev_cq, 1);
	res = ibv_req_notify_cq (ev_cq, 0);

	res = ibv_poll_cq (instance->mcast_send_cq, 32, wc);
	if (res > 0) {
		for (i = 0; i < res; i++) {
			mcast_send_buf_put (instance, wrid2void(wc[i].wr_id));
		}
	}

	return (0);
}

static int mcast_cq_recv_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct ibv_wc wc[64];
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int res;
	int i;

	ibv_get_cq_event (instance->mcast_recv_completion_channel, &ev_cq, &ev_ctx);
	ibv_ack_cq_events (ev_cq, 1);
	res = ibv_req_notify_cq (ev_cq, 0);

	res = ibv_poll_cq (instance->mcast_recv_cq, 64, wc);
	if (res > 0) {
		for (i = 0; i < res; i++) {
			iba_deliver_fn (instance, wc[i].wr_id, wc[i].byte_len);
			mcast_recv_buf_post (instance, wrid2void(wc[i].wr_id));
		}
	}

	return (0);
}

static void mcast_rejoin (void *data)
{
	int res;
	struct totemiba_instance *instance = (struct totemiba_instance *)data;

	res = rdma_leave_multicast (instance->mcast_cma_id, &instance->mcast_addr);
	if (instance->mcast_ah) {
		ibv_destroy_ah (instance->mcast_ah);
		instance->mcast_ah = 0;
	}

	res = rdma_join_multicast (instance->mcast_cma_id, &instance->mcast_addr, instance);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_DEBUG,
		    "rdma_join_multicast failed, errno=%d, rejoining in %u ms",
		    errno,
		    MCAST_REJOIN_MSEC);
		qb_loop_timer_add (instance->totemiba_poll_handle,
			QB_LOOP_MED,
			MCAST_REJOIN_MSEC * QB_TIME_NS_IN_MSEC,
			(void *)instance,
			mcast_rejoin,
			&instance->mcast_rejoin);
	}
}

static int mcast_rdma_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct rdma_cm_event *event;

	int res;

	res = rdma_get_cm_event (instance->mcast_channel, &event);
	if (res != 0) {
		return (0);
	}

	switch (event->event) {
	/*
	 * occurs when we resolve the multicast address
	 */
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		res = rdma_join_multicast (instance->mcast_cma_id, &instance->mcast_addr, instance);
		usleep(1000);
		if (res == 0) break;
	case RDMA_CM_EVENT_MULTICAST_ERROR:
		log_printf (LOGSYS_LEVEL_ERROR, "multicast error, trying to rejoin in %u ms", MCAST_REJOIN_MSEC);
		qb_loop_timer_add (instance->totemiba_poll_handle,
			QB_LOOP_MED,
			MCAST_REJOIN_MSEC * QB_TIME_NS_IN_MSEC,
			(void *)instance,
			mcast_rejoin,
			&instance->mcast_rejoin);
		break;
	/*
	 * occurs when the CM joins the multicast group
	 */
	case RDMA_CM_EVENT_MULTICAST_JOIN:
		instance->mcast_qpn = event->param.ud.qp_num;
		instance->mcast_qkey = event->param.ud.qkey;
		instance->mcast_ah = ibv_create_ah (instance->mcast_pd, &event->param.ud.ah_attr);

		if (instance->mcast_seen_joined == 0) {
			log_printf (LOGSYS_LEVEL_DEBUG, "joining mcast 1st time, running callbacks");
			instance->totemiba_iface_change_fn (instance->rrp_context, &instance->my_id);
			instance->mcast_seen_joined=1;
		}
		log_printf (LOGSYS_LEVEL_NOTICE, "Joined multicast!");
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		break;
	default:
		log_printf (LOGSYS_LEVEL_ERROR, "default %d", event->event);
		break;
	}

	rdma_ack_cm_event (event);
	return (0);
}

static int recv_token_cq_send_event_fn (
	int fd,
	int revents,
	void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct ibv_wc wc[32];
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int res;
	int i;

	ibv_get_cq_event (instance->recv_token_send_completion_channel, &ev_cq, &ev_ctx);
	ibv_ack_cq_events (ev_cq, 1);
	res = ibv_req_notify_cq (ev_cq, 0);

	res = ibv_poll_cq (instance->recv_token_send_cq, 32, wc);
	if (res > 0) {
		for (i = 0; i < res; i++) {
			iba_deliver_fn (instance, wc[i].wr_id, wc[i].byte_len);
			ibv_dereg_mr (wrid2void(wc[i].wr_id));
		}
	}

	return (0);
}

static int recv_token_cq_recv_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct ibv_wc wc[32];
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int res;
	int i;

	ibv_get_cq_event (instance->recv_token_recv_completion_channel, &ev_cq, &ev_ctx);
	ibv_ack_cq_events (ev_cq, 1);
	res = ibv_req_notify_cq (ev_cq, 0);

	res = ibv_poll_cq (instance->recv_token_recv_cq, 32, wc);
	if (res > 0) {
		for (i = 0; i < res; i++) {
			iba_deliver_fn (instance, wc[i].wr_id, wc[i].byte_len);
			recv_token_recv_buf_post (instance, wrid2void(wc[i].wr_id));
		}
	}

	return (0);
}

static int recv_token_accept_destroy (struct totemiba_instance *instance)
{
	if (instance->recv_token_accepted == 0) {
		return (0);
	}

	qb_loop_poll_del (
		instance->totemiba_poll_handle,
		instance->recv_token_recv_completion_channel->fd);

	qb_loop_poll_del (
		instance->totemiba_poll_handle,
		instance->recv_token_send_completion_channel->fd);

	rdma_destroy_qp (instance->recv_token_cma_id);

	recv_token_recv_buf_post_destroy (instance);

	ibv_destroy_cq (instance->recv_token_send_cq);

	ibv_destroy_cq (instance->recv_token_recv_cq);

	ibv_destroy_comp_channel (instance->recv_token_send_completion_channel);

	ibv_destroy_comp_channel (instance->recv_token_recv_completion_channel);

	ibv_dealloc_pd (instance->recv_token_pd);

	rdma_destroy_id (instance->recv_token_cma_id);

	return (0);
}

static int recv_token_accept_setup (struct totemiba_instance *instance)
{
	struct ibv_qp_init_attr init_qp_attr;
	int res = 0;

	/*
	 * Allocate the protection domain
	 */
	instance->recv_token_pd = ibv_alloc_pd (instance->recv_token_cma_id->verbs);

	/*
	 * Create a completion channel
	 */
	instance->recv_token_recv_completion_channel = ibv_create_comp_channel (instance->recv_token_cma_id->verbs);
	if (instance->recv_token_recv_completion_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion channel");
		return (-1);
	}

	/*
	 * Create the completion queue
	 */
	instance->recv_token_recv_cq = ibv_create_cq (instance->recv_token_cma_id->verbs,
		COMPLETION_QUEUE_ENTRIES, instance,
		instance->recv_token_recv_completion_channel, 0);
	if (instance->recv_token_recv_cq == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion queue");
		return (-1);
	}
	res = ibv_req_notify_cq (instance->recv_token_recv_cq, 0);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't request notifications of the completion queue");
		return (-1);
	}

	/*
	 * Create a completion channel
	 */
	instance->recv_token_send_completion_channel = ibv_create_comp_channel (instance->recv_token_cma_id->verbs);
	if (instance->recv_token_send_completion_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion channel");
		return (-1);
	}

	/*
	 * Create the completion queue
	 */
	instance->recv_token_send_cq = ibv_create_cq (instance->recv_token_cma_id->verbs,
		COMPLETION_QUEUE_ENTRIES, instance,
		instance->recv_token_send_completion_channel, 0);
	if (instance->recv_token_send_cq == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion queue");
		return (-1);
	}
	res = ibv_req_notify_cq (instance->recv_token_send_cq, 0);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't request notifications of the completion queue");
		return (-1);
	}
	memset (&init_qp_attr, 0, sizeof (struct ibv_qp_init_attr));
	init_qp_attr.cap.max_send_wr = 50;
	init_qp_attr.cap.max_recv_wr = TOTAL_READ_POSTS;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.qp_context = instance;
	init_qp_attr.sq_sig_all = 0;
	init_qp_attr.qp_type = IBV_QPT_UD;
	init_qp_attr.send_cq = instance->recv_token_send_cq;
	init_qp_attr.recv_cq = instance->recv_token_recv_cq;
	res = rdma_create_qp (instance->recv_token_cma_id, instance->recv_token_pd,
		&init_qp_attr);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create queue pair");
		return (-1);
	}
	
	recv_token_recv_buf_post_initial (instance);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->recv_token_recv_completion_channel->fd,
		POLLIN, instance, recv_token_cq_recv_event_fn);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->recv_token_send_completion_channel->fd,
		POLLIN, instance, recv_token_cq_send_event_fn);

	instance->recv_token_accepted = 1;

	return (res);
};

static int recv_token_rdma_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct rdma_cm_event *event;
	struct rdma_conn_param conn_param;

	int res;

	res = rdma_get_cm_event (instance->listen_recv_token_channel, &event);
	if (res != 0) {
		return (0);
	}

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		recv_token_accept_destroy (instance);

		instance->recv_token_cma_id = event->id;
		recv_token_accept_setup (instance);
		memset (&conn_param, 0, sizeof (struct rdma_conn_param));
		conn_param.qp_num = instance->recv_token_cma_id->qp->qp_num;
		res = rdma_accept (instance->recv_token_cma_id, &conn_param);
		break;
	default:
		log_printf (LOGSYS_LEVEL_ERROR, "default %d", event->event);
		break;
	}

	res = rdma_ack_cm_event (event);
	return (0);
}

static int send_token_cq_send_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct ibv_wc wc[32];
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int res;
	int i;

	ibv_get_cq_event (instance->send_token_send_completion_channel, &ev_cq, &ev_ctx);
	ibv_ack_cq_events (ev_cq, 1);
	res = ibv_req_notify_cq (ev_cq, 0);

	res = ibv_poll_cq (instance->send_token_send_cq, 32, wc);
	if (res > 0) {
		for (i = 0; i < res; i++) {
			token_send_buf_put (instance, wrid2void(wc[i].wr_id));
		}
	}

	return (0);
}

static int send_token_cq_recv_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct ibv_wc wc[32];
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int res;
	int i;

	ibv_get_cq_event (instance->send_token_recv_completion_channel, &ev_cq, &ev_ctx);
	ibv_ack_cq_events (ev_cq, 1);
	res = ibv_req_notify_cq (ev_cq, 0);

	res = ibv_poll_cq (instance->send_token_recv_cq, 32, wc);
	if (res > 0) {
		for (i = 0; i < res; i++) {
			iba_deliver_fn (instance, wc[i].wr_id, wc[i].byte_len);
		}
	}

	return (0);
}

static int send_token_rdma_event_fn (int fd, int events, void *context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)context;
	struct rdma_cm_event *event;
	struct rdma_conn_param conn_param;

	int res;

	res = rdma_get_cm_event (instance->send_token_channel, &event);
	if (res != 0) {
		return (0);
	}

	switch (event->event) {
	/*
	 * occurs when we resolve the multicast address
	 */
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		res = rdma_resolve_route (instance->send_token_cma_id, 2000);
		break;
	/*
	 * occurs when the CM joins the multicast group
	 */
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		memset (&conn_param, 0, sizeof (struct rdma_conn_param));
		conn_param.private_data = NULL;
		conn_param.private_data_len = 0;
		res = rdma_connect (instance->send_token_cma_id, &conn_param);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		instance->send_token_qpn = event->param.ud.qp_num;
		instance->send_token_qkey = event->param.ud.qkey;
		instance->send_token_ah = ibv_create_ah (instance->send_token_pd, &event->param.ud.ah_attr);
		instance->totemiba_target_set_completed (instance->rrp_context);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_MULTICAST_ERROR:
		log_printf (LOGSYS_LEVEL_ERROR,
			"send_token_rdma_event_fn multicast error");
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		break;
	case RDMA_CM_EVENT_UNREACHABLE:
		log_printf (LOGSYS_LEVEL_ERROR,
			"send_token_rdma_event_fn unreachable");
		break;
	default:
		log_printf (LOGSYS_LEVEL_ERROR,
			"send_token_rdma_event_fn unknown event %d",
			event->event);
		break;
	}

	rdma_ack_cm_event (event);
	return (0);
}

static int send_token_bind (struct totemiba_instance *instance)
{
	int res;
	struct ibv_qp_init_attr init_qp_attr;

	instance->send_token_channel = rdma_create_event_channel();
	if (instance->send_token_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create rdma channel");
		return (-1);
	}

	res = rdma_create_id (instance->send_token_channel,
		&instance->send_token_cma_id, NULL, RDMA_PS_UDP);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error creating send_token_cma_id");
		return (-1);
	}

	res = rdma_bind_addr (instance->send_token_cma_id,
		&instance->send_token_bind_addr);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error doing rdma_bind_addr for send token");
		return (-1);
	}

	/*
	 * Resolve the send_token address into a GUID
	 */
	res = rdma_resolve_addr (instance->send_token_cma_id,
		&instance->bind_addr, &instance->token_addr, 2000);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error resolving send token address %d %d", res, errno);
		return (-1);
	}

	/*
	 * Allocate the protection domain
	 */
	instance->send_token_pd = ibv_alloc_pd (instance->send_token_cma_id->verbs);
	
	/*
	 * Create a completion channel
	 */
	instance->send_token_recv_completion_channel = ibv_create_comp_channel (instance->send_token_cma_id->verbs);
	if (instance->send_token_recv_completion_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion channel");
		return (-1);
	}

	/*
	 * Create the completion queue
	 */
	instance->send_token_recv_cq = ibv_create_cq (instance->send_token_cma_id->verbs,
		COMPLETION_QUEUE_ENTRIES, instance,
		instance->send_token_recv_completion_channel, 0);
	if (instance->send_token_recv_cq == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion queue");
		return (-1);
	}
	res = ibv_req_notify_cq (instance->send_token_recv_cq, 0);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"couldn't request notifications of the completion queue");
		return (-1);
	}

	/*
	 * Create a completion channel
	 */
	instance->send_token_send_completion_channel =
		ibv_create_comp_channel (instance->send_token_cma_id->verbs);

	if (instance->send_token_send_completion_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion channel");
		return (-1);
	}
	
	/*
	 * Create the completion queue
	 */
	instance->send_token_send_cq = ibv_create_cq (
		instance->send_token_cma_id->verbs,
		COMPLETION_QUEUE_ENTRIES, instance,
		instance->send_token_send_completion_channel, 0);
	if (instance->send_token_send_cq == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion queue");
		return (-1);
	}

	res = ibv_req_notify_cq (instance->send_token_send_cq, 0);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR,
			"couldn't request notifications of the completion queue");
		return (-1);
	}
	memset (&init_qp_attr, 0, sizeof (struct ibv_qp_init_attr));
	init_qp_attr.cap.max_send_wr = 50;
	init_qp_attr.cap.max_recv_wr = TOTAL_READ_POSTS;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.qp_context = instance;
	init_qp_attr.sq_sig_all = 0;
	init_qp_attr.qp_type = IBV_QPT_UD;
	init_qp_attr.send_cq = instance->send_token_send_cq;
	init_qp_attr.recv_cq = instance->send_token_recv_cq;
	res = rdma_create_qp (instance->send_token_cma_id,
		instance->send_token_pd, &init_qp_attr);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create queue pair");
		return (-1);
	}
	
	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->send_token_recv_completion_channel->fd,
		POLLIN, instance, send_token_cq_recv_event_fn);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->send_token_send_completion_channel->fd,
		POLLIN, instance, send_token_cq_send_event_fn);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->send_token_channel->fd,
		POLLIN, instance, send_token_rdma_event_fn);

	instance->send_token_bound = 1;
	return (0);
}

static int send_token_unbind (struct totemiba_instance *instance)
{
	if (instance->send_token_bound == 0) {
		return (0);
	}

	qb_loop_poll_del (
		instance->totemiba_poll_handle,
		instance->send_token_recv_completion_channel->fd);
	qb_loop_poll_del (
		instance->totemiba_poll_handle,
		instance->send_token_send_completion_channel->fd);
	qb_loop_poll_del (
		instance->totemiba_poll_handle,
		instance->send_token_channel->fd);

	if(instance->send_token_ah)
	{
		ibv_destroy_ah(instance->send_token_ah);
		instance->send_token_ah = 0;
	}

	rdma_destroy_qp (instance->send_token_cma_id);
	ibv_destroy_cq (instance->send_token_send_cq);
	ibv_destroy_cq (instance->send_token_recv_cq);
	ibv_destroy_comp_channel (instance->send_token_send_completion_channel);
	ibv_destroy_comp_channel (instance->send_token_recv_completion_channel);
	token_send_buf_destroy (instance);
	ibv_dealloc_pd (instance->send_token_pd);
	rdma_destroy_id (instance->send_token_cma_id);
	rdma_destroy_event_channel (instance->send_token_channel);
	return (0);
}

static int recv_token_bind (struct totemiba_instance *instance)
{
	int res;
	struct ibv_port_attr port_attr;

	instance->listen_recv_token_channel = rdma_create_event_channel();
	if (instance->listen_recv_token_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create rdma channel");
		return (-1);
	}

	res = rdma_create_id (instance->listen_recv_token_channel,
		&instance->listen_recv_token_cma_id, NULL, RDMA_PS_UDP);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error creating recv_token_cma_id");
		return (-1);
	}

	res = rdma_bind_addr (instance->listen_recv_token_cma_id,
		&instance->bind_addr);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error doing rdma_bind_addr for recv token");
		return (-1);
	}

	/*
	 * Determine active_mtu of port and compare it with the configured one (160 is aproximation of all totem
	 * structures.
	 *
	 * TODO: Implement MTU discovery also for IP and handle MTU correctly for all structures inside totemsrp,
	 *       crypto, ...
	 */
	res = ibv_query_port (instance->listen_recv_token_cma_id->verbs, instance->listen_recv_token_cma_id->port_num, &port_attr);
	if ( (1 << (port_attr.active_mtu + 7)) < instance->totem_config->net_mtu + 160) {
		log_printf (LOGSYS_LEVEL_ERROR, "requested net_mtu is %d and is larger than the active port mtu %d\n",\
				instance->totem_config->net_mtu + 160, (1 << (port_attr.active_mtu + 7)));
		return (-1);
	}

	/*
	 * Resolve the recv_token address into a GUID
	 */
	res = rdma_listen (instance->listen_recv_token_cma_id, 10);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error listening %d %d", res, errno);
		return (-1);
	}

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->listen_recv_token_channel->fd,
		POLLIN, instance, recv_token_rdma_event_fn);

	return (0);
}

static int mcast_bind (struct totemiba_instance *instance)
{
	int res;
	struct ibv_qp_init_attr init_qp_attr;

	instance->mcast_channel = rdma_create_event_channel();
	if (instance->mcast_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create rdma channel");
		return (-1);
	}

	res = rdma_create_id (instance->mcast_channel, &instance->mcast_cma_id, NULL, RDMA_PS_UDP);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error creating mcast_cma_id");
		return (-1);
	}

	res = rdma_bind_addr (instance->mcast_cma_id, &instance->local_mcast_bind_addr);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error doing rdma_bind_addr for mcast");
		return (-1);
	}

	/*
	 * Resolve the multicast address into a GUID
	 */
	res = rdma_resolve_addr (instance->mcast_cma_id, &instance->local_mcast_bind_addr,
		&instance->mcast_addr, 5000);
	if (res) {
		log_printf (LOGSYS_LEVEL_ERROR, "error resolving multicast address %d %d", res, errno);
		return (-1);
	}

	/*
	 * Allocate the protection domain
	 */
	instance->mcast_pd = ibv_alloc_pd (instance->mcast_cma_id->verbs);
	
	/*
	 * Create a completion channel
	 */
	instance->mcast_recv_completion_channel = ibv_create_comp_channel (instance->mcast_cma_id->verbs);
	if (instance->mcast_recv_completion_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion channel");
		return (-1);
	}

	/*
	 * Create the completion queue
	 */
	instance->mcast_recv_cq = ibv_create_cq (instance->mcast_cma_id->verbs,
		COMPLETION_QUEUE_ENTRIES, instance,
		instance->mcast_recv_completion_channel, 0);
	if (instance->mcast_recv_cq == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion queue");
		return (-1);
	}
	res = ibv_req_notify_cq (instance->mcast_recv_cq, 0);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't request notifications of the completion queue");
		return (-1);
	}

	/*
	 * Create a completion channel
	 */
	instance->mcast_send_completion_channel = ibv_create_comp_channel (instance->mcast_cma_id->verbs);
	if (instance->mcast_send_completion_channel == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion channel");
		return (-1);
	}

	/*
	 * Create the completion queue
	 */
	instance->mcast_send_cq = ibv_create_cq (instance->mcast_cma_id->verbs,
		COMPLETION_QUEUE_ENTRIES, instance,
		instance->mcast_send_completion_channel, 0);
	if (instance->mcast_send_cq == NULL) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create completion queue");
		return (-1);
	}
	res = ibv_req_notify_cq (instance->mcast_send_cq, 0);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't request notifications of the completion queue");
		return (-1);
	}
	memset (&init_qp_attr, 0, sizeof (struct ibv_qp_init_attr));
	init_qp_attr.cap.max_send_wr = 50;
	init_qp_attr.cap.max_recv_wr = TOTAL_READ_POSTS;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.qp_context = instance;
	init_qp_attr.sq_sig_all = 0;
	init_qp_attr.qp_type = IBV_QPT_UD;
	init_qp_attr.send_cq = instance->mcast_send_cq;
	init_qp_attr.recv_cq = instance->mcast_recv_cq;
	res = rdma_create_qp (instance->mcast_cma_id, instance->mcast_pd,
		&init_qp_attr);
	if (res != 0) {
		log_printf (LOGSYS_LEVEL_ERROR, "couldn't create queue pair");
		return (-1);
	}
	
	mcast_recv_buf_post_initial (instance);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->mcast_recv_completion_channel->fd,
		POLLIN, instance, mcast_cq_recv_event_fn);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->mcast_send_completion_channel->fd,
		POLLIN, instance, mcast_cq_send_event_fn);

	qb_loop_poll_add (
		instance->totemiba_poll_handle,
		QB_LOOP_MED,
		instance->mcast_channel->fd,
		POLLIN, instance, mcast_rdma_event_fn);

	return (0);
}

static void timer_function_netif_check_timeout (
      void *data)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)data;
	int res;
	int interface_up;
	int interface_num;
	int addr_len;

	totemip_iface_check (&instance->totem_interface->bindnet,
		&instance->totem_interface->boundto, &interface_up, &interface_num, instance->totem_config->clear_node_high_bit);

	totemip_totemip_to_sockaddr_convert(&instance->totem_interface->boundto,
		instance->totem_interface->ip_port, (struct sockaddr_storage *)&instance->bind_addr,
		&addr_len);

	totemip_totemip_to_sockaddr_convert(&instance->totem_interface->boundto,
		0, (struct sockaddr_storage *)&instance->send_token_bind_addr,
		&addr_len);

	totemip_totemip_to_sockaddr_convert(&instance->totem_interface->boundto,
		0, (struct sockaddr_storage *)&instance->local_mcast_bind_addr,
		&addr_len);

	totemip_totemip_to_sockaddr_convert(&instance->totem_interface->boundto,
		instance->totem_interface->ip_port, (struct sockaddr_storage *)&instance->my_id,
		&addr_len);

	totemip_sockaddr_to_totemip_convert(
		(const struct sockaddr_storage *)&instance->bind_addr,
		&instance->my_id);

	memcpy (&instance->my_id, &instance->totem_interface->boundto,
		sizeof (struct totem_ip_address));

	totemip_totemip_to_sockaddr_convert(&instance->totem_interface->mcast_addr,
		instance->totem_interface->ip_port,
		(struct sockaddr_storage *)&instance->mcast_addr, &addr_len);

	res = recv_token_bind (instance);

	res = mcast_bind (instance);
}

int totemiba_crypto_set (
	void *iba_context,
	const char *cipher_type,
	const char *hash_type)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

int totemiba_finalize (
	void *iba_context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

/*
 * Create an instance
 */
int totemiba_initialize (
	qb_loop_t *qb_poll_handle,
	void **iba_context,
	struct totem_config *totem_config,
	totemsrp_stats_t *stats,
	int interface_no,
	void *context,

	void (*deliver_fn) (
		void *context,
		const void *msg,
		unsigned int msg_len),

	void (*iface_change_fn) (
		void *context,
		const struct totem_ip_address *iface_address),

	void (*target_set_completed) (
		void *context))
{
	struct totemiba_instance *instance;
	int res = 0;

	instance = malloc (sizeof (struct totemiba_instance));
	if (instance == NULL) {
		return (-1);
	}

	totemiba_instance_initialize (instance);

	instance->totem_interface = &totem_config->interfaces[interface_no];

	instance->totemiba_poll_handle = qb_poll_handle;

	instance->totem_interface->bindnet.nodeid = totem_config->node_id;

	instance->totemiba_deliver_fn = deliver_fn;

	instance->totemiba_target_set_completed = target_set_completed;

	instance->totemiba_iface_change_fn = iface_change_fn;

	instance->totem_config = totem_config;
	instance->stats = stats;

	instance->rrp_context = context;

	qb_loop_timer_add (instance->totemiba_poll_handle,
		QB_LOOP_MED,
		100*QB_TIME_NS_IN_MSEC,
		(void *)instance,
		timer_function_netif_check_timeout,
		&instance->timer_netif_check_timeout);

	instance->totemiba_subsys_id = totem_config->totem_logging_configuration.log_subsys_id;
	instance->totemiba_log_printf = totem_config->totem_logging_configuration.log_printf;

	*iba_context = instance;
	return (res);
}

void *totemiba_buffer_alloc (void)
{
	return malloc (MAX_MTU_SIZE);
}

void totemiba_buffer_release (void *ptr)
{
	return free (ptr);
}

int totemiba_processor_count_set (
	void *iba_context,
	int processor_count)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

int totemiba_recv_flush (void *iba_context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

int totemiba_send_flush (void *iba_context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

int totemiba_token_send (
	void *iba_context,
	const void *ms,
	unsigned int msg_len)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;
	struct ibv_send_wr send_wr, *failed_send_wr;
	struct ibv_sge sge;
	void *msg;
	struct send_buf *send_buf;

	send_buf = token_send_buf_get (instance);
	if (send_buf == NULL) {
		return (-1);
	}
	msg = send_buf->buffer;
	memcpy (msg, ms, msg_len);

	send_wr.next = NULL;
	send_wr.sg_list = &sge;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = IBV_SEND_SIGNALED;
	send_wr.wr_id = void2wrid(send_buf);
	send_wr.imm_data = 0;
	send_wr.wr.ud.ah = instance->send_token_ah;
	send_wr.wr.ud.remote_qpn = instance->send_token_qpn;
	send_wr.wr.ud.remote_qkey = instance->send_token_qkey;

	sge.length = msg_len;
	sge.lkey = send_buf->mr->lkey;
	sge.addr = (uintptr_t)msg;

	if(instance->send_token_ah != 0 && instance->send_token_bound)
		res = ibv_post_send (instance->send_token_cma_id->qp, &send_wr, &failed_send_wr);

	return (res);
}

int totemiba_mcast_flush_send (
	void *iba_context,
	const void *ms,
	unsigned int msg_len)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;
	struct ibv_send_wr send_wr, *failed_send_wr;
	struct ibv_sge sge;
	void *msg;
	struct send_buf *send_buf;

	send_buf = mcast_send_buf_get (instance);
	if (send_buf == NULL) {
		return (-1);
	}

	msg = send_buf->buffer;
	memcpy (msg, ms, msg_len);
	send_wr.next = NULL;
	send_wr.sg_list = &sge;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = IBV_SEND_SIGNALED;
	send_wr.wr_id = void2wrid(send_buf);
	send_wr.imm_data = 0;
	send_wr.wr.ud.ah = instance->mcast_ah;
	send_wr.wr.ud.remote_qpn = instance->mcast_qpn;
	send_wr.wr.ud.remote_qkey = instance->mcast_qkey;

	sge.length = msg_len;
	sge.lkey = send_buf->mr->lkey;
	sge.addr = (uintptr_t)msg;

	if (instance->mcast_ah != 0) {
		res = ibv_post_send (instance->mcast_cma_id->qp, &send_wr, &failed_send_wr);
	}

	return (res);
}

int totemiba_mcast_noflush_send (
	void *iba_context,
	const void *ms,
	unsigned int msg_len)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;
	struct ibv_send_wr send_wr, *failed_send_wr;
	struct ibv_sge sge;
	void *msg;
	struct send_buf *send_buf;

	send_buf = mcast_send_buf_get (instance);
	if (send_buf == NULL) {
		return (-1);
	}

	msg = send_buf->buffer;
	memcpy (msg, ms, msg_len);
	send_wr.next = NULL;
	send_wr.sg_list = &sge;
	send_wr.num_sge = 1;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = IBV_SEND_SIGNALED;
	send_wr.wr_id = void2wrid(send_buf);
	send_wr.imm_data = 0;
	send_wr.wr.ud.ah = instance->mcast_ah;
	send_wr.wr.ud.remote_qpn = instance->mcast_qpn;
	send_wr.wr.ud.remote_qkey = instance->mcast_qkey;

	sge.length = msg_len;
	sge.lkey = send_buf->mr->lkey;
	sge.addr = (uintptr_t)msg;

	if (instance->mcast_ah != 0) {
		res = ibv_post_send (instance->mcast_cma_id->qp, &send_wr, &failed_send_wr);
	}

	return (res);
}

extern int totemiba_iface_check (void *iba_context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

extern void totemiba_net_mtu_adjust (void *iba_context, struct totem_config *totem_config)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	instance = NULL;
}

const char *totemiba_iface_print (void *iba_context)  {
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;

        const char *ret_char;

        ret_char = totemip_print (&instance->my_id);

        return (ret_char);
}

int totemiba_iface_get (
	void *iba_context,
	struct totem_ip_address *addr)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	memcpy (addr, &instance->my_id, sizeof (struct totem_ip_address));

	return (res);
}

int totemiba_token_target_set (
	void *iba_context,
	const struct totem_ip_address *token_target)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;
	int addr_len = 16;

	totemip_totemip_to_sockaddr_convert((struct totem_ip_address *)token_target,
		instance->totem_interface->ip_port, (struct sockaddr_storage *)&instance->token_addr,
		&addr_len);

	res = send_token_unbind (instance);

	res = send_token_bind (instance);

	return (res);
}

extern int totemiba_recv_mcast_empty (
	void *iba_context)
{
	struct totemiba_instance *instance = (struct totemiba_instance *)iba_context;
	int res = 0;

	instance = NULL;

	return (res);
}

