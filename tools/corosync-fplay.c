#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <corosync/engine/logsys.h>

uint32_t flt_data_size;
uint32_t old_format_file;

uint32_t *flt_data;
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

static const char *totemip_print(const struct totem_ip_address *addr)
{
	static char buf[INET6_ADDRSTRLEN];

	return inet_ntop(addr->family, addr->addr, buf, sizeof(buf));
}

static char *print_string_len (const unsigned char *str, unsigned int len)
{
	unsigned int i;
	static char buf[1024];
	memset (buf, 0, sizeof (buf));
	for (i = 0; i < len; i++) {
		buf[i] = str[i];
	}
	return (buf);
}

static void sync_printer_confchg_set_sync (const void **record)
{
	const unsigned int *my_should_sync = record[0];
	printf ("Setting my_should_sync to %d\n", *my_should_sync);
}

static void sync_printer_set_sync_state (const void **record)
{
	const unsigned int *my_sync_state = record[0];
	printf ("Setting my_sync_state to %d\n", *my_sync_state);
}

static void sync_printer_process_currentstate (const void **record)
{
	const unsigned int *my_sync_state = record[0];
	printf ("Retrieving my_sync_state %d\n", *my_sync_state);
}

static void sync_printer_process_get_shouldsync (const void **record)
{
	const unsigned int *my_should_sync = record[0];
	printf ("Getting my_should_sync %d\n", *my_should_sync);
}

static void sync_printer_checkpoint_release (const void **record)
{
	const unsigned char *name = record[0];
	const uint16_t *name_len = record[1];
	const unsigned int *ckpt_id = record[2];
	const unsigned int *from = record[3];

	printf ("Checkpoint release name=[%s] id=[%d] from=[%d] len=[%d]\n",
		print_string_len (name, *name_len),
		*ckpt_id,
		*from,
		*name_len);
}

static void sync_printer_checkpoint_transmit (const void **record)
{
	const unsigned char *name = record[0];
	const uint16_t *name_len = record[1];
	const unsigned int *ckpt_id = record[2];
	const unsigned int *xmit_id = record[3];

	printf ("xmit_id=[%d] Checkpoint transmit name=[%s] id=[%d]\n",
		*xmit_id, print_string_len (name, *name_len),
		*ckpt_id);
}

static void sync_printer_section_transmit (const void **record)
{
	const unsigned char *ckpt_name = record[0];
	const uint16_t *name_len = record[1];
	const unsigned int *ckpt_id = record[2];
	const unsigned int *xmit_id = record[3];
	const unsigned char *section_name = record[4];
	const uint16_t *section_name_len = record[5];

	printf ("xmit_id=[%d] Section transmit checkpoint name=[%s] id=[%d] ",
		*xmit_id, print_string_len (ckpt_name, *name_len),
		*ckpt_id);
	printf ("section=[%s]\n",
		print_string_len (section_name, *section_name_len));
}
static void sync_printer_checkpoint_receive (const void **record)
{
	const unsigned char *ckpt_name = record[0];
	const uint16_t *name_len = record[1];
	const unsigned int *ckpt_id = record[2];
	const unsigned int *xmit_id = record[3];

	printf ("xmit_id=[%d] Checkpoint receive checkpoint name=[%s] id=[%d]\n",
		*xmit_id, print_string_len (ckpt_name, *name_len), *ckpt_id);
}

static void sync_printer_section_receive (const void **record)
{
	const unsigned char *ckpt_name = record[0];
	const uint16_t *name_len = record[1];
	const unsigned int *ckpt_id = record[2];
	const unsigned int *xmit_id = record[3];
	const unsigned char *section_name = record[4];
	const unsigned int *section_name_len = record[5];

	printf ("xmit_id=[%d] Section receive checkpoint name=[%s] id=[%d] ",
		*xmit_id, print_string_len (ckpt_name, *name_len),
		*ckpt_id);

	printf ("section=[%s]\n",
		print_string_len (section_name, *section_name_len));
}

static void sync_printer_confchg_fn (const void **record)
{
	unsigned int i;

	const unsigned int *members = record[0];
	const unsigned int *member_count = record[1];
	const struct memb_ring_id *ring_id = record[2];
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

static void printer_totemsrp_mcast (const void **record)
{
	const unsigned int *msgid = record[0];

	printf ("totemsrp_mcast %d\n", *msgid);
}

static void printer_totemsrp_delv (const void **record)
{
	const unsigned int *msgid = record[0];

	printf ("totemsrp_delv %d\n", *msgid);
}

static void printer_totempg_mcast_fits (const void **record)
{
	const unsigned int *idx = record[0];
	const unsigned int *iov_len = record[1];
	const unsigned int *copy_len = record[2];
	const unsigned int *fragment_size = record[3];
	const unsigned int *max_packet_size = record[4];
	const unsigned int *copy_base = record[5];
	const unsigned char *next_fragment = record[6];

	printf ("totempg_mcast index=[%d] iov_len=[%d] copy_len=[%d] fragment_size=[%d] max_packet_size=[%d] copy_base=[%d] next_fragment[%d]\n",
	*idx, *iov_len, *copy_len, *fragment_size, *max_packet_size, *copy_base, *next_fragment);
}

static void sync_printer_service_process (const void **record)
{
	const struct memb_ring_id *ring_id = record[0];
	const struct memb_ring_id *sync_ring_id = record[1];

	printf ("sync service process callback ringid [ip=%s seq=%lld] ",
		totemip_print (&ring_id->rep),
		ring_id->seq);
	printf ("sync ringid [ip=%s seq=%lld]\n",
		totemip_print (&sync_ring_id->rep),
		sync_ring_id->seq);
}

struct printer_subsys_record_print {
	int ident;
	void (*print_fn)(const void **record);
	int record_length;
};

struct printer_subsys {
	const char *subsys;
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


static struct printer_subsys_record_print record_print_sync[] = {
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

static struct printer_subsys_record_print record_print_ckpt[] = {
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
static struct printer_subsys_record_print record_print_totem[] = {
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

static struct printer_subsys printer_subsystems[] = {
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

static unsigned int printer_subsys_count =
  sizeof (printer_subsystems) / sizeof (struct printer_subsys);

#define G_RECORD_SIZE	10000

/*
 * Record must have at least 4 bytes - size, indent, line and record_number
 */
#define MINIMUM_RECORD_SIZE	4

static uint32_t g_record[G_RECORD_SIZE];

static int file_rewind = 0;

/*
 * Copy record, dealing with wrapping
 */
static int logsys_rec_get (int rec_idx) {
	uint32_t rec_size;
	int firstcopy, secondcopy;

	if (rec_idx >= flt_data_size) {
		fprintf (stderr, "rec_idx too large. Input file is probably corrupted.\n");
		exit (EXIT_FAILURE);
	}

	rec_size = flt_data[rec_idx];

	firstcopy = rec_size;
	secondcopy = 0;

	if (rec_size > G_RECORD_SIZE || rec_size > flt_data_size) {
		fprintf (stderr, "rec_size too large. Input file is probably corrupted.\n");
		exit (EXIT_FAILURE);
	}

	if (rec_size < MINIMUM_RECORD_SIZE) {
		fprintf (stderr, "rec_size too small. Input file is probably corrupted.\n");
		exit (EXIT_FAILURE);
	}

	if (firstcopy + rec_idx > flt_data_size) {
		if (file_rewind) {
			fprintf (stderr, "file rewind for second time (cycle). Input file is probably corrupted.\n");
			exit (EXIT_FAILURE);
		}

		file_rewind = 1;
		firstcopy = flt_data_size - rec_idx;
		secondcopy -= firstcopy - rec_size;
	}
	memcpy (&g_record[0], &flt_data[rec_idx], firstcopy * sizeof(uint32_t));
	if (secondcopy) {
		memcpy (&g_record[firstcopy], &flt_data[0], secondcopy * sizeof(uint32_t));
	}
	return ((rec_idx + rec_size) % flt_data_size);
}

static void logsys_rec_print (const void *record)
{
	const uint32_t *buf_uint32t = record;
	uint32_t rec_size;
	uint32_t rec_ident;
	uint32_t level;
	uint32_t line;
	uint32_t arg_size_idx;
	unsigned int i;
	unsigned int j;
	unsigned int rec_idx = 0;
	uint32_t record_number;
	unsigned int words_processed;
	unsigned int found;
	const char *arguments[64];
	time_t timestamp;
	struct tm timestamp_tm;
	int arg_count = 0;
	char ts_buf[132];

	rec_size = buf_uint32t[rec_idx++];
	rec_ident = buf_uint32t[rec_idx++];
	line = buf_uint32t[rec_idx++];
	record_number = buf_uint32t[rec_idx++];

	if (!old_format_file) {
		timestamp = (time_t)(buf_uint32t[rec_idx] | (time_t)(buf_uint32t[rec_idx+1])<<32);
		rec_idx += 2;
	}

	level = LOGSYS_DECODE_LEVEL(rec_ident);

	printf ("rec=[%d] ", record_number);

	if (!old_format_file) {
		localtime_r(&timestamp, &timestamp_tm);
		strftime(ts_buf, sizeof(ts_buf), "%F %T", &timestamp_tm);
		printf ("time=[%s] ", ts_buf);
	}

	arg_size_idx = rec_idx;
	words_processed = 4;
	for (i = 0; words_processed < rec_size; i++) {
		arguments[arg_count++] =
		  (const char *)&buf_uint32t[arg_size_idx + 1];
		words_processed += buf_uint32t[arg_size_idx] + 1;
		arg_size_idx += buf_uint32t[arg_size_idx] + 1;

	}

	found = 0;
	for (i = 0; i < printer_subsys_count; i++) {
		if (strcmp (arguments[0], printer_subsystems[i].subsys) == 0) {
			for (j = 0; j < printer_subsystems[i].record_printers_count; j++) {
				if (rec_ident == printer_subsystems[i].record_printers[j].ident) {
				  printer_subsystems[i].record_printers[j].print_fn ((const void **)&arguments[3]);
					return;
				}
			}
		}
	}

	switch(LOGSYS_DECODE_RECID(rec_ident)) {
		case LOGSYS_RECID_LOG:
			printf ("Log Message=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_ENTER:
			printf ("ENTERING function [%s] line [%d]\n", arguments[2], line);
			break;
		case LOGSYS_RECID_LEAVE:
			printf ("LEAVING function [%s] line [%d]\n", arguments[2], line);
			break;
		case LOGSYS_RECID_TRACE1:
			printf ("Tracing(1) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE2:
			printf ("Tracing(2) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE3:
			printf ("Tracing(3) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE4:
			printf ("Tracing(4) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE5:
			printf ("Tracing(5) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE6:
			printf ("Tracing(6) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE7:
			printf ("Tracing(7) Messsage=%s\n", arguments[3]);
			break;
		case LOGSYS_RECID_TRACE8:
			printf ("Tracing(8) Messsage=%s\n", arguments[3]);
			break;
		default:
			printf ("Unknown record type found subsys=[%s] ident=[%d]\n",
				arguments[0], LOGSYS_DECODE_RECID(rec_ident));
			break;
	}
#ifdef COMPILE_OUT
printf ("\n");
#endif
}

int main (void)
{
	int fd;
	int rec_idx;
	int end_rec;
	int record_count = 1;
	ssize_t n_read;
	const char *data_file = LOCALSTATEDIR "/lib/corosync/fdata";
	size_t n_required;
	uint32_t flt_magic;

	if ((fd = open (data_file, O_RDONLY)) < 0) {
		fprintf (stderr, "failed to open %s: %s\n",
			 data_file, strerror (errno));
		return EXIT_FAILURE;
	}

	n_required = sizeof (uint32_t);
	n_read = read (fd, &flt_magic, n_required);
	if (n_read != n_required) {
		fprintf (stderr, "Unable to read fdata magic number\n");
		return EXIT_FAILURE;
	}

	/* If the first word is a magic number then this is a new format
	 * fdata file (with timestamps) and the next word is the length.
	 * If not, then it's an old file and we just read the length, so use it
	 */
	if (flt_magic == 0xFFFFDABB) {
		n_required = sizeof (uint32_t);
		n_read = read (fd, &flt_data_size, n_required);
		if (n_read != n_required) {
			fprintf (stderr, "Unable to read fdata header\n");
			return EXIT_FAILURE;
		}
		old_format_file = 0;
	}
	else {
		flt_data_size = flt_magic;
		old_format_file = 1;
	}

	n_required = ((flt_data_size + 2) * sizeof(uint32_t));

	if ((flt_data = malloc (n_required)) == NULL) {
		fprintf (stderr, "exhausted virtual memory\n");
		return EXIT_FAILURE;
	}
	n_read = read (fd, flt_data, n_required);
	close (fd);
	if (n_read < 0) {
		fprintf (stderr, "reading %s failed: %s\n",
			 data_file, strerror (errno));
		return EXIT_FAILURE;
	}

	if (n_read != n_required) {
		printf ("Warning: read %zd bytes, but expected %zu\n",
			n_read, n_required);
	}

	rec_idx = flt_data[FDTAIL_INDEX];
	end_rec = flt_data[FDHEAD_INDEX];

	printf ("Starting replay: head [%d] tail [%d]\n",
		flt_data[FDHEAD_INDEX],
		flt_data[FDTAIL_INDEX]);

	for (;;) {
		rec_idx = logsys_rec_get (rec_idx);
		logsys_rec_print (g_record);
		if (rec_idx == end_rec) {
			break;
		}
		record_count += 1;
	}

	printf ("Finishing replay: records found [%d]\n", record_count);
	return (0);
}
