// libcpg interface for Rust
// Copyright (c) 2020 Red Hat, Inc.
//
// All rights reserved.
//
// Author: Christine Caulfield (ccaulfi@redhat.com)
//

#![allow(clippy::single_match)]
#![allow(clippy::needless_range_loop)]
#![allow(clippy::type_complexity)]

// For the code generated by bindgen
use crate::sys::cpg as ffi;

use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::fmt;
use std::os::raw::{c_int, c_void};
use std::ptr::copy_nonoverlapping;
use std::slice;
use std::string::String;
use std::sync::Mutex;

// General corosync things
use crate::string_from_bytes;
use crate::{CsError, DispatchFlags, NodeId, Result};

const CPG_NAMELEN_MAX: usize = 128;
const CPG_MEMBERS_MAX: usize = 128;

/// RingId returned by totem_confchg_fn
#[derive(Copy, Clone)]
pub struct RingId {
    pub nodeid: NodeId,
    pub seq: u64,
}

/// Totem delivery guarantee options for [mcast_joined]
// The C enum doesn't have numbers in the code
// so don't assume we can match them
#[derive(Copy, Clone)]
pub enum Guarantee {
    TypeUnordered,
    TypeFifo,
    TypeAgreed,
    TypeSafe,
}

// Convert internal to cpg.h values.
impl Guarantee {
    pub fn to_c(&self) -> u32 {
        match self {
            Guarantee::TypeUnordered => ffi::CPG_TYPE_UNORDERED,
            Guarantee::TypeFifo => ffi::CPG_TYPE_FIFO,
            Guarantee::TypeAgreed => ffi::CPG_TYPE_AGREED,
            Guarantee::TypeSafe => ffi::CPG_TYPE_SAFE,
        }
    }
}

/// Flow control state returned from [flow_control_state_get]
#[derive(Copy, Clone)]
pub enum FlowControlState {
    Disabled,
    Enabled,
}

/// No flags current specified for model1 so leave this at None
#[derive(Copy, Clone)]
pub enum Model1Flags {
    None,
}

/// Reason for cpg item callback
#[derive(Copy, Clone)]
pub enum Reason {
    Undefined = 0,
    Join = 1,
    Leave = 2,
    NodeDown = 3,
    NodeUp = 4,
    ProcDown = 5,
}

// Convert to cpg.h values
impl Reason {
    pub fn new(r: u32) -> Reason {
        match r {
            0 => Reason::Undefined,
            1 => Reason::Join,
            2 => Reason::Leave,
            3 => Reason::NodeDown,
            4 => Reason::NodeUp,
            5 => Reason::ProcDown,
            _ => Reason::Undefined,
        }
    }
}
impl fmt::Display for Reason {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Reason::Undefined => write!(f, "Undefined"),
            Reason::Join => write!(f, "Join"),
            Reason::Leave => write!(f, "Leave"),
            Reason::NodeDown => write!(f, "NodeDown"),
            Reason::NodeUp => write!(f, "NodeUp"),
            Reason::ProcDown => write!(f, "ProcDown"),
        }
    }
}

/// A CPG address entry returned in the callbacks
pub struct Address {
    pub nodeid: NodeId,
    pub pid: u32,
    pub reason: Reason,
}
impl fmt::Debug for Address {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "[nodeid: {}, pid: {}, reason: {}]",
            self.nodeid, self.pid, self.reason
        )
    }
}

/// Data for model1 [initialize]
#[derive(Copy, Clone)]
pub struct Model1Data {
    pub flags: Model1Flags,
    pub deliver_fn: Option<
        fn(
            handle: &Handle,
            group_name: String,
            nodeid: NodeId,
            pid: u32,
            msg: &[u8],
            msg_len: usize,
        ),
    >,
    pub confchg_fn: Option<
        fn(
            handle: &Handle,
            group_name: &str,
            member_list: Vec<Address>,
            left_list: Vec<Address>,
            joined_list: Vec<Address>,
        ),
    >,
    pub totem_confchg_fn: Option<fn(handle: &Handle, ring_id: RingId, member_list: Vec<NodeId>)>,
}

/// Modeldata for [initialize], only v1 supported at the moment
#[derive(Copy, Clone)]
pub enum ModelData {
    ModelNone,
    ModelV1(Model1Data),
}

/// A handle into the cpg library. Returned from [initialize] and needed for all other calls
#[derive(Copy, Clone)]
pub struct Handle {
    cpg_handle: u64, // Corosync library handle
    model_data: ModelData,
}

// Used to convert a CPG handle into one of ours
lazy_static! {
    static ref HANDLE_HASH: Mutex<HashMap<u64, Handle>> = Mutex::new(HashMap::new());
}

// Convert a Rust String into a cpg_name struct for libcpg
fn string_to_cpg_name(group: &str) -> Result<ffi::cpg_name> {
    if group.len() > CPG_NAMELEN_MAX - 1 {
        return Err(CsError::CsErrInvalidParam);
    }

    let c_name = match CString::new(group) {
        Ok(n) => n,
        Err(_) => return Err(CsError::CsErrLibrary),
    };
    let mut c_group = ffi::cpg_name {
        length: group.len() as u32,
        value: [0; CPG_NAMELEN_MAX],
    };

    unsafe {
        // NOTE param order is 'wrong-way round' from C
        copy_nonoverlapping(c_name.as_ptr(), c_group.value.as_mut_ptr(), group.len());
    }

    Ok(c_group)
}

// Convert an array of cpg_addresses to a Vec<cpg::Address> - used in callbacks
fn cpg_array_to_vec(list: *const ffi::cpg_address, list_entries: usize) -> Vec<Address> {
    let temp: &[ffi::cpg_address] = unsafe { slice::from_raw_parts(list, list_entries) };
    let mut r_vec = Vec::<Address>::new();

    for i in 0..list_entries {
        let a: Address = Address {
            nodeid: NodeId::from(temp[i].nodeid),
            pid: temp[i].pid,
            reason: Reason::new(temp[i].reason),
        };
        r_vec.push(a);
    }
    r_vec
}

// Called from CPG callback function - munge params back to Rust from C
extern "C" fn rust_deliver_fn(
    handle: ffi::cpg_handle_t,
    group_name: *const ffi::cpg_name,
    nodeid: u32,
    pid: u32,
    msg: *mut ::std::os::raw::c_void,
    msg_len: usize,
) {
    if let Some(h) = HANDLE_HASH.lock().unwrap().get(&handle) {
        // Convert group_name into a Rust str.
        let r_group_name = unsafe {
            CStr::from_ptr(&(*group_name).value[0])
                .to_string_lossy()
                .into_owned()
        };

        let data: &[u8] = unsafe { std::slice::from_raw_parts(msg as *const u8, msg_len) };

        match h.model_data {
            ModelData::ModelV1(md) => {
                if let Some(cb) = md.deliver_fn {
                    (cb)(h, r_group_name, NodeId::from(nodeid), pid, data, msg_len);
                }
            }
            _ => {}
        }
    }
}

// Called from CPG callback function - munge params back to Rust from C
extern "C" fn rust_confchg_fn(
    handle: ffi::cpg_handle_t,
    group_name: *const ffi::cpg_name,
    member_list: *const ffi::cpg_address,
    member_list_entries: usize,
    left_list: *const ffi::cpg_address,
    left_list_entries: usize,
    joined_list: *const ffi::cpg_address,
    joined_list_entries: usize,
) {
    if let Some(h) = HANDLE_HASH.lock().unwrap().get(&handle) {
        let r_group_name = unsafe {
            CStr::from_ptr(&(*group_name).value[0])
                .to_string_lossy()
                .into_owned()
        };
        let r_member_list = cpg_array_to_vec(member_list, member_list_entries);
        let r_left_list = cpg_array_to_vec(left_list, left_list_entries);
        let r_joined_list = cpg_array_to_vec(joined_list, joined_list_entries);

        match h.model_data {
            ModelData::ModelV1(md) => {
                if let Some(cb) = md.confchg_fn {
                    (cb)(h, &r_group_name, r_member_list, r_left_list, r_joined_list);
                }
            }
            _ => {}
        }
    }
}

// Called from CPG callback function - munge params back to Rust from C
extern "C" fn rust_totem_confchg_fn(
    handle: ffi::cpg_handle_t,
    ring_id: ffi::cpg_ring_id,
    member_list_entries: u32,
    member_list: *const u32,
) {
    if let Some(h) = HANDLE_HASH.lock().unwrap().get(&handle) {
        let r_ring_id = RingId {
            nodeid: NodeId::from(ring_id.nodeid),
            seq: ring_id.seq,
        };
        let mut r_member_list = Vec::<NodeId>::new();
        let temp_members: &[u32] =
            unsafe { slice::from_raw_parts(member_list, member_list_entries as usize) };
        for i in 0..member_list_entries as usize {
            r_member_list.push(NodeId::from(temp_members[i]));
        }

        match h.model_data {
            ModelData::ModelV1(md) => {
                if let Some(cb) = md.totem_confchg_fn {
                    (cb)(h, r_ring_id, r_member_list);
                }
            }
            _ => {}
        }
    }
}

/// Initialize a connection to the cpg library. You must call this before doing anything
/// else and use the passed back [Handle].
/// Remember to free the handle using [finalize] when finished.
pub fn initialize(model_data: &ModelData, context: u64) -> Result<Handle> {
    let mut handle: ffi::cpg_handle_t = 0;
    let mut m = match model_data {
        ModelData::ModelV1(_v1) => {
            ffi::cpg_model_v1_data_t {
                model: ffi::CPG_MODEL_V1,
                cpg_deliver_fn: Some(rust_deliver_fn),
                cpg_confchg_fn: Some(rust_confchg_fn),
                cpg_totem_confchg_fn: Some(rust_totem_confchg_fn),
                flags: 0, // No supported flags (yet)
            }
        }
        _ => return Err(CsError::CsErrInvalidParam),
    };

    unsafe {
        let c_context: *mut c_void = &mut &context as *mut _ as *mut c_void;
        let c_model: *mut ffi::cpg_model_data_t = &mut m as *mut _ as *mut ffi::cpg_model_data_t;
        let res = ffi::cpg_model_initialize(&mut handle, m.model, c_model, c_context);

        if res == ffi::CS_OK {
            let rhandle = Handle {
                cpg_handle: handle,
                model_data: *model_data,
            };
            HANDLE_HASH.lock().unwrap().insert(handle, rhandle);
            Ok(rhandle)
        } else {
            Err(CsError::from_c(res))
        }
    }
}

/// Finish with a connection to corosync
pub fn finalize(handle: Handle) -> Result<()> {
    let res = unsafe { ffi::cpg_finalize(handle.cpg_handle) };
    if res == ffi::CS_OK {
        HANDLE_HASH.lock().unwrap().remove(&handle.cpg_handle);
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

// Not sure if an FD is the right thing to return here, but it will do for now.
/// Returns a file descriptor to use for poll/select on the CPG handle
pub fn fd_get(handle: Handle) -> Result<i32> {
    let c_fd: *mut c_int = &mut 0 as *mut _ as *mut c_int;
    let res = unsafe { ffi::cpg_fd_get(handle.cpg_handle, c_fd) };
    if res == ffi::CS_OK {
        Ok(c_fd as i32)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Call any/all active CPG callbacks for this [Handle] see [DispatchFlags] for details
pub fn dispatch(handle: Handle, flags: DispatchFlags) -> Result<()> {
    let res = unsafe { ffi::cpg_dispatch(handle.cpg_handle, flags as u32) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Joins a CPG group for sending and receiving messages
pub fn join(handle: Handle, group: &str) -> Result<()> {
    let res = unsafe {
        let c_group = string_to_cpg_name(group)?;
        ffi::cpg_join(handle.cpg_handle, &c_group)
    };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Leave the currently joined CPG group, another group can now be joined on
/// the same [Handle] or [finalize] can be called to finish using CPG
pub fn leave(handle: Handle, group: &str) -> Result<()> {
    let res = unsafe {
        let c_group = string_to_cpg_name(group)?;
        ffi::cpg_leave(handle.cpg_handle, &c_group)
    };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get the local node ID
pub fn local_get(handle: Handle) -> Result<NodeId> {
    let mut nodeid: u32 = 0;
    let res = unsafe { ffi::cpg_local_get(handle.cpg_handle, &mut nodeid) };
    if res == ffi::CS_OK {
        Ok(NodeId::from(nodeid))
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get a list of members of a CPG group as a vector of [Address] structs
pub fn membership_get(handle: Handle, group: &str) -> Result<Vec<Address>> {
    let mut member_list_entries: i32 = 0;
    let member_list = [ffi::cpg_address {
        nodeid: 0,
        pid: 0,
        reason: 0,
    }; CPG_MEMBERS_MAX];
    let res = unsafe {
        let mut c_group = string_to_cpg_name(group)?;
        let c_memlist = member_list.as_ptr() as *mut ffi::cpg_address;
        ffi::cpg_membership_get(
            handle.cpg_handle,
            &mut c_group,
            &mut *c_memlist,
            &mut member_list_entries,
        )
    };
    if res == ffi::CS_OK {
        Ok(cpg_array_to_vec(
            member_list.as_ptr(),
            member_list_entries as usize,
        ))
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get the maximum size that CPG can send in one corosync message,
/// any messages sent via [mcast_joined] that are larger than this
/// will be fragmented
pub fn max_atomic_msgsize_get(handle: Handle) -> Result<u32> {
    let mut asize: u32 = 0;
    let res = unsafe { ffi::cpg_max_atomic_msgsize_get(handle.cpg_handle, &mut asize) };
    if res == ffi::CS_OK {
        Ok(asize)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get the current 'context' value for this handle.
/// The context value is an arbitrary value that is always passed
/// back to callbacks to help identify the source
pub fn context_get(handle: Handle) -> Result<u64> {
    let mut c_context: *mut c_void = &mut 0u64 as *mut _ as *mut c_void;
    let (res, context) = unsafe {
        let r = ffi::cpg_context_get(handle.cpg_handle, &mut c_context);
        let context: u64 = c_context as u64;
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
        ffi::cpg_context_set(handle.cpg_handle, c_context)
    };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get the flow control state of corosync CPG
pub fn flow_control_state_get(handle: Handle) -> Result<bool> {
    let mut fc_state: u32 = 0;
    let res = unsafe { ffi::cpg_flow_control_state_get(handle.cpg_handle, &mut fc_state) };
    if res == ffi::CS_OK {
        if fc_state == 1 {
            Ok(true)
        } else {
            Ok(false)
        }
    } else {
        Err(CsError::from_c(res))
    }
}

/// Send a message to the currently joined CPG group
pub fn mcast_joined(handle: Handle, guarantee: Guarantee, msg: &[u8]) -> Result<()> {
    let c_iovec = ffi::iovec {
        iov_base: msg.as_ptr() as *mut c_void,
        iov_len: msg.len(),
    };
    let res = unsafe { ffi::cpg_mcast_joined(handle.cpg_handle, guarantee.to_c(), &c_iovec, 1) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Type of iteration for [CpgIterStart]
#[derive(Copy, Clone)]
pub enum CpgIterType {
    NameOnly = 1,
    OneGroup = 2,
    All = 3,
}

// Iterator based on information on this page. thank you!
// https://stackoverflow.com/questions/30218886/how-to-implement-iterator-and-intoiterator-for-a-simple-struct
// Object to iterate over
/// An object to iterate over a list of CPG groups, create one of these and then use 'for' over it
pub struct CpgIterStart {
    iter_handle: u64,
}

/// struct returned from iterating over a [CpgIterStart]
pub struct CpgIter {
    pub group: String,
    pub nodeid: NodeId,
    pub pid: u32,
}

pub struct CpgIntoIter {
    iter_handle: u64,
}

impl fmt::Debug for CpgIter {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "[group: {}, nodeid: {}, pid: {}]",
            self.group, self.nodeid, self.pid
        )
    }
}

impl Iterator for CpgIntoIter {
    type Item = CpgIter;

    fn next(&mut self) -> Option<CpgIter> {
        let mut c_iter_description = ffi::cpg_iteration_description_t {
            nodeid: 0,
            pid: 0,
            group: ffi::cpg_name {
                length: 0_u32,
                value: [0; CPG_NAMELEN_MAX],
            },
        };
        let res = unsafe { ffi::cpg_iteration_next(self.iter_handle, &mut c_iter_description) };

        if res == ffi::CS_OK {
            let r_group =
                match string_from_bytes(c_iter_description.group.value.as_ptr(), CPG_NAMELEN_MAX) {
                    Ok(groupname) => groupname,
                    Err(_) => return None,
                };
            Some(CpgIter {
                group: r_group,
                nodeid: NodeId::from(c_iter_description.nodeid),
                pid: c_iter_description.pid,
            })
        } else if res == ffi::CS_ERR_NO_SECTIONS {
            // End of list
            unsafe {
                // Yeah, we don't check this return code. There's nowhere to report it.
                ffi::cpg_iteration_finalize(self.iter_handle)
            };
            None
        } else {
            None
        }
    }
}

impl CpgIterStart {
    /// Create a new [CpgIterStart] object for iterating over a list of active CPG groups
    pub fn new(cpg_handle: Handle, group: &str, iter_type: CpgIterType) -> Result<CpgIterStart> {
        let mut iter_handle: u64 = 0;
        let res = unsafe {
            let mut c_group = string_to_cpg_name(group)?;
            let c_itertype = iter_type as u32;
            // IterType 'All' requires that the group pointer is passed in as NULL
            let c_group_ptr = {
                match iter_type {
                    CpgIterType::All => std::ptr::null_mut(),
                    _ => &mut c_group,
                }
            };
            ffi::cpg_iteration_initialize(
                cpg_handle.cpg_handle,
                c_itertype,
                c_group_ptr,
                &mut iter_handle,
            )
        };
        if res == ffi::CS_OK {
            Ok(CpgIterStart { iter_handle })
        } else {
            Err(CsError::from_c(res))
        }
    }
}

impl IntoIterator for CpgIterStart {
    type Item = CpgIter;
    type IntoIter = CpgIntoIter;

    fn into_iter(self) -> Self::IntoIter {
        CpgIntoIter {
            iter_handle: self.iter_handle,
        }
    }
}
