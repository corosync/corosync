/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#ifndef PRINT_H_DEFINED
#define PRINT_H_DEFINED

#include "../include/ais_types.h"


#define LOG_LEVEL_SECURITY	1
#define LOG_LEVEL_ERROR		2
#define LOG_LEVEL_WARNING	3
#define LOG_LEVEL_NOTICE	4
#define LOG_LEVEL_DEBUG		5

extern void internal_log_printf (int level, char *string, ...);

/*
 * The optimizer will remove DEBUG logging messages in production builds
 */
#ifdef DEBUG
#define log_printf(level,format,args...) { internal_log_printf (level,format,##args); }
#else
#define log_printf(level,format,args...) { if (level != LOG_LEVEL_DEBUG) internal_log_printf (level,format,##args); }
#endif

extern char *getSaNameT (SaNameT *name);

extern char *getSaClmNodeAddressT (SaClmNodeAddressT *nodeAddress);

extern void printSaClmClusterNodeT (char *description, SaClmClusterNodeT *clusterNode);

extern void saAmfPrintGroups (void);

#endif /* PRINT_H_DEFINED */
