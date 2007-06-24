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

#include "serv_amf.h"

// Global variables
SaVersionT VG_amf_version = { 'B', 1, 1 };	// AMF version supported
SaAmfHandleT VG_amf_handle;			// AMF handle
SaNameT VG_compNameGlobal;			// name of the component

// Ip takeover
char *VG_interface=NULL;			// ip takeover interface
char *VG_ip_addr=NULL;				// floating ip address

SaAmfHAStateT VG_AMF_old_haState;		// We store the old state to ease transition detection
SaAmfHAStateT VG_AMF_haState;			// Current ha state
SaAmfHAStateT *VG_AMF_remote_haState;		// Remote ha state (from the amf_comp_init function)

// Types for State Management
char *TG_ha_state[128] = {
  "null",
  "SA_AMF_HA_ACTIVE",
  "SA_AMF_HA_STANDBY",
  "SA_AMF_HA_QUIESCED",
  "SA_AMF_HA_QUIESCING"
};

// The key of the healthcheck to be executed
SaAmfHealthcheckKeyT VG_keyAmfInvoked = {
  .key = "takeoverInvoked",
  .keyLen = 15
};

// Local function Prototypes
static void  amf_send_response( SaAmfHandleT, SaInvocationT, SaAisErrorT );
static void  amf_healthcheckCallback( SaInvocationT, const SaNameT*, SaAmfHealthcheckKeyT* );
static void  amf_ip_takeover( SaAmfCSIDescriptorT*, SaAmfHAStateT );
static void  amf_csi_setCallback( SaInvocationT, const SaNameT*, SaAmfHAStateT, SaAmfCSIDescriptorT* );
static void* amf_comp_callbacks( void* );

// Define the various callback functions that AMF may invoke on a component
SaAmfCallbacksT VG_amfCallbacks = {
  .saAmfHealthcheckCallback = amf_healthcheckCallback,
  .saAmfCSISetCallback = amf_csi_setCallback
};

//
// Response to Framework Requests to acknowledge the reception of a
// message or a callback
//
static void
amf_send_response(
	SaAmfHandleT p_handle,		// AMF handle
	SaInvocationT p_invocation,	// associate an invocation of this response function
	SaAisErrorT p_retval		// return SA_AIS_OK in case of success, else returns an appropriate error
){
  SaAisErrorT v_error;

  for (;;usleep(10000)) {
    v_error = saAmfResponse( p_handle, p_invocation, p_retval );
    if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
  }

  if (v_error != SA_AIS_OK) {
    fprintf( stderr, "saAmfResponse failed %d\n", v_error );
    exit( EXIT_FAILURE );
  }
}

//
// AMF requests the component p_compName to perform a healthcheck
// specified by p_healthcheckKey
//
void
amf_healthcheckCallback(
	SaInvocationT p_invocation,
	const SaNameT *p_compName,
	SaAmfHealthcheckKeyT *p_healthcheckKey
){
  amf_send_response( VG_amf_handle, p_invocation, SA_AIS_OK );
}

//
// IP takeover management
//
void
amf_ip_takeover(
	SaAmfCSIDescriptorT *p_csiDescriptor,	// information about the CSI targeted
	SaAmfHAStateT p_action			// SA_AMF_HA_ACTIVE=="start", sinon "stop"
){
  char v_action[64], v_exec[256];
  char *v_path = getenv( "COMP_BINARY_PATH" );
  int i;

  // CSI attributs
  if (p_csiDescriptor!=NULL && (VG_interface==NULL || VG_ip_addr==NULL)) {

    for(i=0; i<p_csiDescriptor->csiAttr.number; i++) {

      // Interface
      if (VG_interface==NULL && !strcmp((char*)p_csiDescriptor->csiAttr.attr[i].attrName,"interface")) {
        VG_interface = (char*)malloc(strlen((char*)p_csiDescriptor->csiAttr.attr[i].attrName)+1);
        strcpy( VG_interface, (char*)p_csiDescriptor->csiAttr.attr[i].attrValue );
      }

      // Floating ip address
      if (VG_ip_addr==NULL && !strcmp((char*)p_csiDescriptor->csiAttr.attr[i].attrName,"ip_addr")) {
        VG_ip_addr = (char*)malloc(strlen((char*)p_csiDescriptor->csiAttr.attr[i].attrName)+1);
        strcpy( VG_ip_addr, (char*)p_csiDescriptor->csiAttr.attr[i].attrValue );
      }
    }
  }

  // Test d'integrite
  if (v_path==NULL || VG_interface==NULL || VG_ip_addr==NULL) return;

  // Ip takeover action
  strcpy( v_action, "stop" );
  if (p_action == SA_AMF_HA_ACTIVE) strcpy( v_action, "start" );

  // We execute the takeover script
  sprintf( v_exec, "%s/takeover.sh %s %s %s", v_path, VG_interface, VG_ip_addr, v_action );
  system( v_exec );
}

//
// AMF invokes this callback to request the component to assume a
// particular HA state
//
static void
amf_csi_setCallback(
	SaInvocationT p_invocation,		// to acknowledge the new state
	const SaNameT *p_compName,		// name of the component 
	SaAmfHAStateT p_haState,		// new HA state to be assumed by the component for the CSI
	SaAmfCSIDescriptorT *p_csiDescriptor	// information about the CSI targeted

){
  SaAmfHAStateT v_state;
  SaAisErrorT v_error;

  // new state
  switch( p_haState ) {

    case SA_AMF_HA_ACTIVE:
    case SA_AMF_HA_STANDBY:
    case SA_AMF_HA_QUIESCED:
    case SA_AMF_HA_QUIESCING:

      // Send response to AMF
      amf_send_response( VG_amf_handle, p_invocation, SA_AIS_OK );

      // Takeover of the ip address
      amf_ip_takeover( p_csiDescriptor, p_haState );

      // We change the local state
      VG_AMF_haState = p_haState;

      fprintf( stderr, "PID %d: Component '%s' requested to enter hastate %s for \n\tCSI '%s'\n",
               (int)getpid(), p_compName->value, TG_ha_state[p_haState], p_csiDescriptor->csiName.value );

      // We check if the state is changed
      v_error = saAmfHAStateGet( VG_amf_handle, p_compName, &p_csiDescriptor->csiName, &v_state );
      if (v_error!=SA_AIS_OK || p_haState!=v_state) {
        fprintf( stderr, "saAmfHAStateGet failed: %d\n", v_error );
        exit (-1);
      }
      break;

    default:
      break;
  }

  //
  // Transition detection
  //
  if (VG_AMF_haState != VG_AMF_old_haState) {
    VG_AMF_old_haState = VG_AMF_haState;
    *VG_AMF_remote_haState = VG_AMF_haState;
  }
}

//
// Invoke pending callbacks for the handle amfHandle
//
void*
amf_comp_callbacks(
        void* p_select_fd	// file descriptor
) {
  int v_select_fd = (int)p_select_fd;
  struct timeval v_timeval;
  SaAisErrorT v_error;
  fd_set v_read_fds;
  int retval;

  // initialization
  pthread_detach( pthread_self() );
  FD_ZERO( &v_read_fds );

  // saAmfDispatch
  for (;;) {

    // related to value in amf.conf!
    v_timeval.tv_sec = 0;
    v_timeval.tv_usec = 1000;
    FD_SET( v_select_fd, &v_read_fds );

    // we check if there are data waiting
    retval = select( v_select_fd+1, &v_read_fds, 0, 0, &v_timeval );
    if (retval == -1) {
      if (errno == EINTR) continue;
      fprintf( stderr, "select failed - %s",strerror(errno) );
      exit( EXIT_FAILURE );
    }

    // We detect the availability of some date on the file descriptor
    // This avoid us to launch saAmfDispatch() too often
    if (retval > 0) {

      // invoke pending callbacks for the handle amfHandle
      for (;;usleep(10000)) {
        v_error = saAmfDispatch( VG_amf_handle,		// AMF handle
				 SA_DISPATCH_ALL );	// callback execution behavior
        if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
      }
      if (v_error != SA_AIS_OK) {
        fprintf( stderr, "saAmfDispatch failed %d", v_error );
        exit( EXIT_FAILURE );
      }
    }
  }

  pthread_exit( NULL );
  return NULL;
}

//
// Initialize AMF for the invoking process
//
void
amf_comp_init(
	SaAmfHAStateT *p_amf_state		// current HA state of the component
){
  char *v_name;				// to get the environment variables
  SaAisErrorT v_error;			// result of the AMF functions
  SaSelectionObjectT v_select_fd;	// file descriptor to check if there is pending callbacks
  pthread_t v_thread;			// to create a thread to manage the healthcheck
  int retval;

  // We get the environment variable SA_AMF_COMPONENT_NAME
  v_name = getenv( "SA_AMF_COMPONENT_NAME" );
  if (v_name == NULL) {
    fprintf( stderr, "SA_AMF_COMPONENT_NAME missing\n" );
    exit( EXIT_FAILURE );
  }

  fprintf( stderr, "** %d: Hello world from [%s]\n", (int)getpid(), v_name );

  // the real application need to know about ha state
  VG_AMF_remote_haState = p_amf_state;

  // Initialization
  VG_AMF_haState = SA_AMF_HA_STANDBY;
  VG_AMF_old_haState = SA_AMF_HA_STANDBY;
  *VG_AMF_remote_haState = SA_AMF_HA_STANDBY;

  // Initialize AMF for the invoking process and registers the various callback functions
  for (;;usleep(10000)) {
    v_error = saAmfInitialize( &VG_amf_handle,		// handle of the Availability Management Framework
			       &VG_amfCallbacks,	// callback functions that AMF may invoke
			       &VG_amf_version );	// version that should support AMF
    if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
  } 
  if (v_error != SA_AIS_OK) {
    fprintf( stderr, "saAmfInitialize result is %d\n", v_error );
    exit( EXIT_FAILURE );
  }

  // Handle to detect pending callbacks,
  // instead of repeatedly invoking saAmfDispatch()
  for (;;usleep(10000)) {
    v_error = saAmfSelectionObjectGet( VG_amf_handle,		// AMF handle
				       &v_select_fd );		// descriptor to detect pending callbacks
    if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
  }
  if (v_error != SA_AIS_OK) {
    fprintf( stderr, "saAmfSelectionObjectGet failed %d\n", v_error );
    exit( EXIT_FAILURE );
  }

  // Get the name of the component the calling process belongs to
  for (;;usleep(10000)) {
    v_error = saAmfComponentNameGet( VG_amf_handle,		// AMF handle
				     &VG_compNameGlobal );	// name of the component 
    if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
  }
  if (v_error != SA_AIS_OK) {
    fprintf( stderr, "saAmfComponentNameGet failed %d\n", v_error );
    exit( EXIT_FAILURE );
  }

  // The healthchecks are invoked by the Availability Management Framework
  for (;;usleep(10000)) {
    v_error = saAmfHealthcheckStart( VG_amf_handle,                     // AMF handle
                                     &VG_compNameGlobal,                // name of the component to be healthchecked
                                     &VG_keyAmfInvoked,                 // the key of the healthcheck to be executed
                                     SA_AMF_HEALTHCHECK_AMF_INVOKED,    // component Healthcheck Monitoring
                                     SA_AMF_NO_RECOMMENDATION );        // What to do if the component fails a healthcheck
    if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
  }
  if (v_error != SA_AIS_OK) {
    fprintf( stderr, "saAmfHealthcheckStart failed %d\n", v_error );
    exit( EXIT_FAILURE );
  }

  // Registration of the componant
  for (;;usleep(10000)) {
    v_error = saAmfComponentRegister( VG_amf_handle,		// AMF handle
				      &VG_compNameGlobal,	// name of the component to be registered
				      NULL );			// unused (proxy component)
    if (v_error != SA_AIS_ERR_TRY_AGAIN) break;
  }
  if (v_error != SA_AIS_OK) {
    fprintf( stderr, "saAmfComponentRegister failed %d\n", v_error );
    exit( EXIT_FAILURE );
  }

  // We start the healthcheck processing
  retval = pthread_create( &v_thread, NULL, (void*(*)(void*))amf_comp_callbacks, (void*)(int)v_select_fd );
  if (retval != 0) {
    fprintf( stderr, "saAmfComponentRegister failed %d\n", retval );
    exit( EXIT_FAILURE );
    }
}
