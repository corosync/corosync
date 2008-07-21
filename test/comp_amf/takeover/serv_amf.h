#ifndef SERV_AMF_H
#define SERV_AMF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <pthread.h>

#include "saAis.h"
#include "saAmf.h"

SaAmfHAStateT *VG_ha_state;

void amf_comp_init( SaAmfHAStateT* );

#endif	/* SERV_AMF_H */
