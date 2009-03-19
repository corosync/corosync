#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <corosync/engine/logsys.h>

unsigned int flt_data_size = 1000000;

unsigned int *flt_data;
#define FDHEAD_INDEX		(flt_data_size)
#define FDTAIL_INDEX		(flt_data_size + 1)

#define TOTEMIP_ADDRLEN (sizeof(struct in6_addr))

struct totem_ip_address {
        unsigned int   nodeid;
        unsigned short family;
        unsigned char  addr[TOTEMIP_ADDRLEN];
} __attribute__((packed));

struct memb_ring_id {
        struct totem_ip_address rep;
        unsigned long long seq;
} __attribute__((packed));

const char *totemip_print(struct totem_ip_address *addr)
{
	static char buf[INET6_ADDRSTRLEN];

	return inet_ntop(addr->family, addr->addr, buf, sizeof(buf));
}

char *print_string_len (unsigned char *str, unsigned int len)
{
	unsigned int i;
	static char buf[1024];
	memset (buf, 0, sizeof (buf));
	for (i = 0; i < len; i++) {
		buf[i] = str[i];
	}
	return (buf);
}

void sync_printer_confchg_set_sync (void **record)
{
	unsigned int *my_should_sync = record[0];
	printf ("Setting my_should_sync to %d\n", *my_should_sync);
}

void sync_printer_set_sync_state (void **record)
{
	unsigned int *my_sync_state = record[0];
	printf ("Setting my_sync_state to %d\n", *my_sync_state);
}

void sync_printer_process_currentstate (void **record)
{
	unsigned int *my_sync_state = record[0];
	printf ("Retrieving my_sync_state %d\n", *my_sync_state);
}

void sync_printer_process_get_shouldsync (void **record)
{
	unsigned int *my_should_sync = record[0];
	printf ("Getting my_should_sync %d\n", *my_should_sync);
}

void sync_printer_checkpoint_release (void **record)
{
	unsigned char *name = record[0];
	uint16_t *name_len = record[1];
	unsigned int *ckpt_id = record[2];
	unsigned int *from = record[3];

	printf ("Checkpoint release name=[%s] id=[%d] from=[%d] len=[%d]\n",
		print_string_len (name, *name_len),
		*ckpt_id,
		*from,
		*name_len);
}

void sync_printer_checkpoint_transmit (void **record)
{
	unsigned char *name = record[0];
	uint16_t *name_len = record[1];
	unsigned int *ckpt_id = record[2];
	unsigned int *xmit_id = record[3];

	printf ("xmit_id=[%d] Checkpoint transmit name=[%s] id=[%d]\n",
		*xmit_id, print_string_len (name, *name_len),
		*ckpt_id);
}

void sync_printer_section_transmit (void **record)
{
	unsigned char *ckpt_name = record[0];
	uint16_t *name_len = record[1];
	unsigned int *ckpt_id = record[2];
	unsigned int *xmit_id = record[3];
	unsigned char *section_name = record[4];
	uint16_t *section_name_len = record[5];
	
	printf ("xmit_id=[%d] Section transmit checkpoint name=[%s] id=[%d] ",
		*xmit_id, print_string_len (ckpt_name, *name_len),
		*ckpt_id);
	printf ("section=[%s]\n",
		print_string_len (section_name, *section_name_len));
}
void sync_printer_checkpoint_receive (void **record)
{
	unsigned char *ckpt_name = record[0];
	uint16_t *name_len = record[1];
	unsigned int *ckpt_id = record[2];
	unsigned int *xmit_id = record[3];
	
	printf ("xmit_id=[%d] Checkpoint receive checkpoint name=[%s] id=[%d]\n",
		*xmit_id, print_string_len (ckpt_name, *name_len), *ckpt_id);
}

void sync_printer_section_receive (void **record)
{
	unsigned char *ckpt_name = record[0];
	uint16_t *name_len = record[1];
	unsigned int *ckpt_id = record[2];
	unsigned int *xmit_id = record[3];
	unsigned char *section_name = record[4];
	unsigned int *section_name_len = record[5];

	printf ("xmit_id=[%d] Section receive checkpoint name=[%s] id=[%d] ",
		*xmit_id, print_string_len (ckpt_name, *name_len),
		*ckpt_id);

	printf ("section=[%s]\n",
		print_string_len (section_name, *section_name_len));
}

void sync_printer_nada (void **record)
{
printf ("nada\n");
}
void sync_printer_confchg_fn (void **record)
{
	unsigned int i;

	unsigned int *members = record[0];
	unsigned int *member_count = record[1];
	struct memb_ring_id *ring_id = record[2];
	struct in_addr addr;

	printf ("sync confchg fn ringid [ip=%s seq=%lld]\n",
		totemip_print (&ring_id->rep),
		ring_id->seq);
	printf ("members [%d]:\n", *member_count);
	for (i = 0; i < *member_count; i++) {
		addr.s_addr = members[i];
		printf ("\tmember [%s]\n", inet_ntoa (addr));
	}
}

void printer_totemsrp_mcast (void **record)
{
	unsigned int *msgid = record[0];

	printf ("totemsrp_mcast %d\n", *msgid);
}

void printer_totemsrp_delv (void **record)
{
	unsigned int *msgid = record[0];

	printf ("totemsrp_delv %d\n", *msgid);
}

void printer_totempg_mcast_fits (void **record)
{
	unsigned int *index = record[0];
	unsigned int *iov_len = record[1];
	unsigned int *copy_len = record[2];
	unsigned int *fragment_size = record[3];
	unsigned int *max_packet_size = record[4];
	unsigned int *copy_base = record[5];
	unsigned char *next_fragment = record[6];

	printf ("totempg_mcast index=[%d] iov_len=[%d] copy_len=[%d] fragment_size=[%d] max_packet_size=[%d] copy_base=[%d] next_fragment[%d]\n",
	*index, *iov_len, *copy_len, *fragment_size, *max_packet_size, *copy_base, *next_fragment);
}

void sync_printer_service_process (void **record)
{
	struct memb_ring_id *ring_id = record[0];
	struct memb_ring_id *sync_ring_id = record[1];

	printf ("sync service process callback ringid [ip=%s seq=%lld] ",
		totemip_print (&ring_id->rep),
		ring_id->seq);
	printf ("sync ringid [ip=%s seq=%lld]\n",
		totemip_print (&sync_ring_id->rep),
		sync_ring_id->seq);
}

struct printer_subsys_record_print {
	int ident;
	void (*print_fn)(void **record);
	int record_length;
};

struct printer_subsys {
	char *subsys;
	struct printer_subsys_record_print *record_printers;
	int record_printers_count;
};

#define LOGREC_ID_SYNC_CONFCHG_FN 0
#define LOGREC_ID_SYNC_SERVICE_PROCESS 1

/*
 * CKPT subsystem
 */
#define LOGREC_ID_CONFCHG_SETSYNC 0
#define LOGREC_ID_SETSYNCSTATE 1
#define LOGREC_ID_SYNC_PROCESS_CURRENTSTATE 2
#define LOGREC_ID_SYNC_PROCESS_GETSHOULDSYNC 3
#define LOGREC_ID_SYNC_CHECKPOINT_TRANSMIT 4
#define LOGREC_ID_SYNC_SECTION_TRANSMIT 5
#define LOGREC_ID_SYNC_CHECKPOINT_RECEIVE 6
#define LOGREC_ID_SYNC_SECTION_RECEIVE 7
#define LOGREC_ID_SYNC_CHECKPOINT_RELEASE 8

#define LOGREC_ID_TOTEMSRP_MCAST 0
#define LOGREC_ID_TOTEMSRP_DELV 1
#define LOGREC_ID_TOTEMPG_MCAST_FITS 2


struct printer_subsys_record_print record_print_sync[] = {
	{
		.ident				= LOGREC_ID_SYNC_CONFCHG_FN,
		.print_fn			= sync_printer_confchg_fn,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_SERVICE_PROCESS,
		.print_fn			= sync_printer_service_process,
		.record_length			= 28
	}
};

struct printer_subsys_record_print record_print_ckpt[] = {
	{
		.ident				= LOGREC_ID_CONFCHG_SETSYNC,
		.print_fn			= sync_printer_confchg_set_sync,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SETSYNCSTATE,
		.print_fn			= sync_printer_set_sync_state,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_PROCESS_CURRENTSTATE,
		.print_fn			= sync_printer_process_currentstate,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_PROCESS_GETSHOULDSYNC,
		.print_fn			= sync_printer_process_get_shouldsync,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_CHECKPOINT_TRANSMIT,
		.print_fn			= sync_printer_checkpoint_transmit,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_SECTION_TRANSMIT,
		.print_fn			= sync_printer_section_transmit,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_CHECKPOINT_RECEIVE,
		.print_fn			= sync_printer_checkpoint_receive,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_SECTION_RECEIVE,
		.print_fn			= sync_printer_section_receive,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_SYNC_CHECKPOINT_RELEASE,
		.print_fn			= sync_printer_checkpoint_release,
		.record_length			= 28
	}

};
struct printer_subsys_record_print record_print_totem[] = {
	{
		.ident				= LOGREC_ID_TOTEMSRP_MCAST,
		.print_fn			= printer_totemsrp_mcast,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_TOTEMSRP_DELV,
		.print_fn			= printer_totemsrp_delv,
		.record_length			= 28
	},
	{
		.ident				= LOGREC_ID_TOTEMPG_MCAST_FITS,
		.print_fn			= printer_totempg_mcast_fits,
		.record_length			= 28
	}
};

struct printer_subsys printer_subsystems[] = {
	{
		.subsys =			"SYNC",
		.record_printers = 		record_print_sync,
		.record_printers_count = 	sizeof (record_print_sync) / sizeof (struct printer_subsys_record_print)
	},
	{
		.subsys =			"CKPT",
		.record_printers = 		record_print_ckpt,
		.record_printers_count = 	sizeof (record_print_ckpt) / sizeof (struct printer_subsys_record_print)
	},
	{
		.subsys =			"TOTEM",
		.record_printers = 		record_print_totem,
		.record_printers_count = 	sizeof (record_print_totem) / sizeof (struct printer_subsys_record_print)
	}
};

unsigned int printer_subsys_count = sizeof (printer_subsystems) / sizeof (struct printer_subsys);
	
unsigned int records_printed = 1;

unsigned int record[10000];

/*
 * Copy record, dealing with wrapping
 */
int logsys_rec_get (int rec_idx) {
	unsigned int rec_size;
	int firstcopy, secondcopy;

	rec_size = flt_data[rec_idx];

	firstcopy = rec_size;
	secondcopy = 0;
	if (firstcopy + rec_idx > flt_data_size) {
		firstcopy = flt_data_size - rec_idx; 
		secondcopy -= firstcopy - rec_size;
	}
	memcpy (&record[0], &flt_data[rec_idx], firstcopy<<2);
	if (secondcopy) {
		memcpy (&record[firstcopy], &flt_data[0], secondcopy<<2);
	}
	return ((rec_idx + rec_size) % flt_data_size);
}

void logsys_rec_print (void *record)
{
	unsigned int *buf_uint32t = (unsigned int *)record;
	unsigned int rec_size;
	unsigned int rec_ident;
	unsigned int line;
	unsigned int arg_size_idx;
	unsigned int i;
	unsigned int j;
	unsigned int rec_idx = 0;
	unsigned int record_number;
	unsigned int words_processed;
	unsigned int found;
	void *arguments[64];
	int arg_count = 0;

	rec_size = buf_uint32t[rec_idx];
	rec_ident = buf_uint32t[rec_idx+1];
	line = buf_uint32t[rec_idx+2];
	record_number = buf_uint32t[rec_idx+3];

printf ("rec=[%d] ", record_number);
	arg_size_idx = rec_idx + 4;
	words_processed = 4;
	for (i = 0; words_processed < rec_size; i++) {
		arguments[arg_count++] = &buf_uint32t[arg_size_idx + 1];
		words_processed += buf_uint32t[arg_size_idx] + 1;
		arg_size_idx += buf_uint32t[arg_size_idx] + 1;
		
	}

	found = 0;
	for (i = 0; i < printer_subsys_count; i++) {
		if (strcmp ((char *)arguments[0], printer_subsystems[i].subsys) == 0) {
			for (j = 0; j < printer_subsystems[i].record_printers_count; j++) {
				if (rec_ident == printer_subsystems[i].record_printers[j].ident) {
					printer_subsystems[i].record_printers[j].print_fn (&arguments[3]);
					found = 1;
				}
			}
		}	
	}
	if (rec_ident & LOGSYS_TAG_LOG) {
		printf ("Log Message=%s\n", (char *)arguments[3]);
		found = 1;
	}
	if (rec_ident & LOGSYS_TAG_ENTER) {
		printf ("ENTERING function [%s] line [%d]\n", (char *)arguments[2], line);
		found = 1;
	}
	if (rec_ident & LOGSYS_TAG_LEAVE) {
		printf ("LEAVING function [%s] line [%d]\n", (char *)arguments[2], line);
		found = 1;
	}
	if (found == 0) {
		printf ("Unknown record type found subsys=[%s] ident=[%d]\n",
		(char *)arguments[0], rec_ident);
	}


	if (rec_ident == 999) {
		printf ("ENTERING function [%s] line [%d]\n", (char *)arguments[2], line);
		found = 1;
	}
	if (rec_ident == 1000) {
		printf ("LEAVING function [%s] line [%d]\n", (char *)arguments[2], line);
		found = 1;
	}
	if (found == 0) {
		printf ("Unknown record type found subsys=[%s] ident=[%d]\n",
			(char *)arguments[0], rec_ident);
	}
		

#ifdef COMPILE_OUT
printf ("\n");
#endif
}

int main (void)
{
	unsigned int fd;
	int rec_idx;
	int end_rec;
	int record_count = 1;
	int size_read;

	flt_data = malloc ((flt_data_size + 2) * sizeof (unsigned int));
	fd = open (LOCALSTATEDIR "/lib/corosync/fdata", O_RDONLY);
	size_read = (int)read (fd, flt_data, (flt_data_size + 2) * sizeof (unsigned int));

	if (size_read != (flt_data_size + 2) * sizeof (unsigned int)) {
		printf ("Warning: read %d bytes, but expected %d\n",
				size_read, (flt_data_size + 2) * sizeof (unsigned int));
	}

	rec_idx = flt_data[FDTAIL_INDEX];
	end_rec = flt_data[FDHEAD_INDEX];

	printf ("Starting replay: head [%d] tail [%d]\n",
		flt_data[FDHEAD_INDEX],
		flt_data[FDTAIL_INDEX]);

	for (;;) {
		rec_idx = logsys_rec_get (rec_idx);
		logsys_rec_print (record);
		if (rec_idx == end_rec) {
			break;
		}
		record_count += 1;
	}

	printf ("Finishing replay: records found [%d]\n", record_count);
	return (0);
}
