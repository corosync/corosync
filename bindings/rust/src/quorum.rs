// libquorum interface for Rust
// Copyright (c) 2021 Red Hat, Inc.
//
// All rights reserved.
//
// Author: Christine Caulfield (ccaulfi@redhat.com)
//

#![allow(clippy::type_complexity)]
#![allow(clippy::needless_range_loop)]
#![allow(clippy::single_match)]

// For the code generated by bindgen
use crate::sys::quorum as ffi;

use crate::{CsError, DispatchFlags, NodeId, Result, TrackFlags};
use std::collections::HashMap;
use std::os::raw::{c_int, c_void};
use std::slice;
use std::sync::Mutex;

/// Data for model1 [initialize]
#[derive(Copy, Clone)]
pub enum ModelData {
    ModelNone,
    ModelV1(Model1Data),
}

/// Value returned from [initialize]. Indicates whether quorum is currently active on this cluster.
pub enum QuorumType {
    Free,
    Set,
}

/// Flags for [initialize], none currently supported
#[derive(Copy, Clone)]
pub enum Model1Flags {
    None,
}

/// RingId returned in quorum_notification_fn
pub struct RingId {
    pub nodeid: NodeId,
    pub seq: u64,
}

// Used to convert a QUORUM handle into one of ours
lazy_static! {
    static ref HANDLE_HASH: Mutex<HashMap<u64, Handle>> = Mutex::new(HashMap::new());
}

fn list_to_vec(list_entries: u32, list: *const u32) -> Vec<NodeId> {
    let mut r_member_list = Vec::<NodeId>::new();
    let temp_members: &[u32] = unsafe { slice::from_raw_parts(list, list_entries as usize) };
    for i in 0..list_entries as usize {
        r_member_list.push(NodeId::from(temp_members[i]));
    }
    r_member_list
}

// Called from quorum callback function - munge params back to Rust from C
extern "C" fn rust_quorum_notification_fn(
    handle: ffi::quorum_handle_t,
    quorate: u32,
    ring_id: ffi::quorum_ring_id,
    member_list_entries: u32,
    member_list: *const u32,
) {
    if let Some(h) = HANDLE_HASH.lock().unwrap().get(&handle) {
        let r_ring_id = RingId {
            nodeid: NodeId::from(ring_id.nodeid),
            seq: ring_id.seq,
        };
        let r_member_list = list_to_vec(member_list_entries, member_list);
        let r_quorate = match quorate {
            0 => false,
            1 => true,
            _ => false,
        };
        match &h.model_data {
            ModelData::ModelV1(md) => {
                if let Some(cb) = md.quorum_notification_fn {
                    (cb)(h, r_quorate, r_ring_id, r_member_list);
                }
            }
            _ => {}
        }
    }
}

extern "C" fn rust_nodelist_notification_fn(
    handle: ffi::quorum_handle_t,
    ring_id: ffi::quorum_ring_id,
    member_list_entries: u32,
    member_list: *const u32,
    joined_list_entries: u32,
    joined_list: *const u32,
    left_list_entries: u32,
    left_list: *const u32,
) {
    if let Some(h) = HANDLE_HASH.lock().unwrap().get(&handle) {
        let r_ring_id = RingId {
            nodeid: NodeId::from(ring_id.nodeid),
            seq: ring_id.seq,
        };

        let r_member_list = list_to_vec(member_list_entries, member_list);
        let r_joined_list = list_to_vec(joined_list_entries, joined_list);
        let r_left_list = list_to_vec(left_list_entries, left_list);

        match &h.model_data {
            ModelData::ModelV1(md) => {
                if let Some(cb) = md.nodelist_notification_fn {
                    (cb)(h, r_ring_id, r_member_list, r_joined_list, r_left_list);
                }
            }
            _ => {}
        }
    }
}

#[derive(Copy, Clone)]
/// Data for model1 [initialize]
pub struct Model1Data {
    pub flags: Model1Flags,
    pub quorum_notification_fn:
        Option<fn(hande: &Handle, quorate: bool, ring_id: RingId, member_list: Vec<NodeId>)>,
    pub nodelist_notification_fn: Option<
        fn(
            hande: &Handle,
            ring_id: RingId,
            member_list: Vec<NodeId>,
            joined_list: Vec<NodeId>,
            left_list: Vec<NodeId>,
        ),
    >,
}

/// A handle into the quorum library. Returned from [initialize] and needed for all other calls
#[derive(Copy, Clone)]
pub struct Handle {
    quorum_handle: u64,
    model_data: ModelData,
}

/// Initialize a connection to the quorum library. You must call this before doing anything
/// else and use the passed back [Handle].
/// Remember to free the handle using [finalize] when finished.
pub fn initialize(model_data: &ModelData, context: u64) -> Result<(Handle, QuorumType)> {
    let mut handle: ffi::quorum_handle_t = 0;
    let mut quorum_type: u32 = 0;

    let mut m = match model_data {
        ModelData::ModelV1(_v1) => ffi::quorum_model_v1_data_t {
            model: ffi::QUORUM_MODEL_V1,
            quorum_notify_fn: Some(rust_quorum_notification_fn),
            nodelist_notify_fn: Some(rust_nodelist_notification_fn),
        },
        // Only V1 supported. No point in doing legacy stuff in a new binding
        _ => return Err(CsError::CsErrInvalidParam),
    };

    handle = unsafe {
        let c_context: *mut c_void = &mut &context as *mut _ as *mut c_void;
        let c_model: *mut ffi::quorum_model_data_t =
            &mut m as *mut _ as *mut ffi::quorum_model_data_t;
        let res = ffi::quorum_model_initialize(
            &mut handle,
            m.model,
            c_model,
            &mut quorum_type,
            c_context,
        );

        if res == ffi::CS_OK {
            handle
        } else {
            return Err(CsError::from_c(res));
        }
    };

    let quorum_type = match quorum_type {
        0 => QuorumType::Free,
        1 => QuorumType::Set,
        _ => QuorumType::Set,
    };
    let rhandle = Handle {
        quorum_handle: handle,
        model_data: *model_data,
    };
    HANDLE_HASH.lock().unwrap().insert(handle, rhandle);
    Ok((rhandle, quorum_type))
}

/// Finish with a connection to corosync
pub fn finalize(handle: Handle) -> Result<()> {
    let res = unsafe { ffi::quorum_finalize(handle.quorum_handle) };
    if res == ffi::CS_OK {
        HANDLE_HASH.lock().unwrap().remove(&handle.quorum_handle);
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

// Not sure if an FD is the right thing to return here, but it will do for now.
/// Return a file descriptor to use for poll/select on the QUORUM handle
pub fn fd_get(handle: Handle) -> Result<i32> {
    let c_fd: *mut c_int = &mut 0 as *mut _ as *mut c_int;
    let res = unsafe { ffi::quorum_fd_get(handle.quorum_handle, c_fd) };
    if res == ffi::CS_OK {
        Ok(c_fd as i32)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Display any/all active QUORUM callbacks for this [Handle], see [DispatchFlags] for details
pub fn dispatch(handle: Handle, flags: DispatchFlags) -> Result<()> {
    let res = unsafe { ffi::quorum_dispatch(handle.quorum_handle, flags as u32) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Return the quorate status of the cluster
pub fn getquorate(handle: Handle) -> Result<bool> {
    let c_quorate: *mut c_int = &mut 0 as *mut _ as *mut c_int;
    let (res, r_quorate) = unsafe {
        let res = ffi::quorum_getquorate(handle.quorum_handle, c_quorate);
        let r_quorate: i32 = *c_quorate;
        (res, r_quorate)
    };
    if res == ffi::CS_OK {
        match r_quorate {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(CsError::CsErrLibrary),
        }
    } else {
        Err(CsError::from_c(res))
    }
}

/// Track node and quorum changes
pub fn trackstart(handle: Handle, flags: TrackFlags) -> Result<()> {
    let res = unsafe { ffi::quorum_trackstart(handle.quorum_handle, flags as u32) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Stop tracking node and quorum changes
pub fn trackstop(handle: Handle) -> Result<()> {
    let res = unsafe { ffi::quorum_trackstop(handle.quorum_handle) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get the current 'context' value for this handle.
/// The context value is an arbitrary value that is always passed
/// back to callbacks to help identify the source
pub fn context_get(handle: Handle) -> Result<u64> {
    let (res, context) = unsafe {
        let mut context: u64 = 0;
        let c_context: *mut c_void = &mut context as *mut _ as *mut c_void;
        let r = ffi::quorum_context_get(handle.quorum_handle, c_context as *mut *const c_void);
        (r, context)
    };
    if res == ffi::CS_OK {
        Ok(context)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Set the current 'context' value for this handle.
/// The context value is an arbitrary value that is always passed
/// back to callbacks to help identify the source.
/// Normally this is set in [initialize], but this allows it to be changed
pub fn context_set(handle: Handle, context: u64) -> Result<()> {
    let res = unsafe {
        let c_context = context as *mut c_void;
        ffi::quorum_context_set(handle.quorum_handle, c_context)
    };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}
