/*
 * Copyright (c) 2004 Mark Haverkamp
 * Copyright (c) 2004 Open Source Development Lab
 *
 * All rights reserved.
 *
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
 * - Neither the name of the Open Source Developent Lab nor the names of its
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

#ifndef EVT_H
#define EVT_H
#include "hdb.h"
#include "../include/list.h"
#include "../include/ais_types.h"

extern struct service_handler evt_service_handler;

/*
 * event instance structure. Contains information about the
 * active connection to the API library.
 *
 * esi_version:				Version that the library is running.
 * esi_open_chans:			list of open channels associated with this
 * 							instance.  Used to clean up any data left
 * 							allocated when the finalize is done.
 * 							(event_svr_channel_open.eco_instance_entry)
 * esi_events:				list of pending events to be delivered on this 
 *  						instance (struct chan_event_list.cel_entry)
 * esi_queue_blocked:		non-zero if the delivery queue got too full
 * 							and we're blocking new messages until we
 * 							drain some of the queued messages.
 * esi_nevents:				Number of events in events lists to be sent.
 * esi_hdb:					Handle data base for open channels on this
 * 							instance.  Used for a quick lookup of
 * 							open channel data from a lib api message.
 */
struct libevt_ci {
	SaVersionT				esi_version;
	struct list_head		esi_open_chans;
	struct list_head		esi_events[SA_EVT_LOWEST_PRIORITY+1];
	int						esi_nevents;
	int						esi_queue_blocked;
	struct saHandleDatabase	esi_hdb;
};

#endif
