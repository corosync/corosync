/*
 * Copyright (c) 2007 Diginext/C-S
 *
 * All rights reserved.
 *
 * Author:  Lionel Tricon (lionel.tricon@diginext.fr)
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

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include "serv_main.h"
#include "serv_amf.h"

// Cluster management
SaAmfHAStateT VG_AMF_ha_state;		// State of the cluster

/*
 * =================================================================================
 *   MAIN
 * =================================================================================
 */

int
main(
	int p_argc,
	char *p_argv[]
) {
  // This function initializes AMF for the invoking process
  // and registers the various callback functions
  amf_comp_init( &VG_AMF_ha_state );

  // 
  for (;;sleep(1)) {

    switch( VG_AMF_ha_state ) {

      case SA_AMF_HA_ACTIVE:
        printf( "Active\n" );
        break;

      case SA_AMF_HA_STANDBY:
        printf( "Standby\n" );
        break;

      case SA_AMF_HA_QUIESCED:
        printf( "Quiesced\n" );
        break;

      case SA_AMF_HA_QUIESCING:
        printf( "Quiescing\n" );
        break;

      default:
        printf( "Unkwown\n" );
        break;
    }
  }

  exit( EXIT_SUCCESS );
}
