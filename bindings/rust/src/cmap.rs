// libcmap interface for Rust
// Copyright (c) 2021 Red Hat, Inc.
//
// All rights reserved.
//
// Author: Christine Caulfield (ccaulfi@redhat.com)
//

#![allow(clippy::type_complexity)]

// For the code generated by bindgen
use crate::sys::cmap as ffi;

use num_enum::TryFromPrimitive;
use std::any::type_name;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::ffi::CString;
use std::fmt;
use std::os::raw::{c_char, c_int, c_void};
use std::ptr::copy_nonoverlapping;
use std::sync::Mutex;

use crate::string_from_bytes;
use crate::{CsError, DispatchFlags, Result};

// Maps:
/// "Maps" available to [initialize]
pub enum Map {
    Icmap,
    Stats,
}

bitflags! {
/// Tracker types for cmap, both passed into [track_add]
/// and returned from its callback.
    pub struct TrackType: i32
    {
    const DELETE = 1;
    const MODIFY = 2;
    const ADD = 4;
    const PREFIX = 8;
    }
}

impl fmt::Display for TrackType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.contains(TrackType::DELETE) {
            write!(f, "DELETE ")?
        }
        if self.contains(TrackType::MODIFY) {
            write!(f, "MODIFY ")?
        }
        if self.contains(TrackType::ADD) {
            write!(f, "ADD ")?
        }
        if self.contains(TrackType::PREFIX) {
            write!(f, "PREFIX ")
        } else {
            Ok(())
        }
    }
}

/// A handle returned from [initialize], needs to be passed to all other cmap API calls
pub struct Handle {
    cmap_handle: u64,
    clone: bool,
}

impl Clone for Handle {
    fn clone(&self) -> Handle {
        Handle {
            cmap_handle: self.cmap_handle,
            clone: true,
        }
    }
}

impl Drop for Handle {
    fn drop(self: &mut Handle) {
        if !self.clone {
            let _e = finalize(self);
        }
    }
}
// Clones count as equivalent
impl PartialEq for Handle {
    fn eq(&self, other: &Handle) -> bool {
        self.cmap_handle == other.cmap_handle
    }
}

#[derive(Copy, Clone)]
/// A handle for a specific CMAP tracker. returned from [track_add].
/// There may be multiple TrackHandles per [Handle]
pub struct TrackHandle {
    track_handle: u64,
    notify_callback: NotifyCallback,
}

// Used to convert CMAP handles into one of ours, for callbacks
lazy_static! {
    static ref TRACKHANDLE_HASH: Mutex<HashMap<u64, TrackHandle>> = Mutex::new(HashMap::new());
    static ref HANDLE_HASH: Mutex<HashMap<u64, Handle>> = Mutex::new(HashMap::new());
}

/// Initialize a connection to the cmap subsystem.
/// map specifies which cmap "map" to use.
/// Returns a [Handle] into the cmap library
pub fn initialize(map: Map) -> Result<Handle> {
    let mut handle: ffi::cmap_handle_t = 0;
    let c_map = match map {
        Map::Icmap => ffi::CMAP_MAP_ICMAP,
        Map::Stats => ffi::CMAP_MAP_STATS,
    };

    unsafe {
        let res = ffi::cmap_initialize_map(&mut handle, c_map);
        if res == ffi::CS_OK {
            let rhandle = Handle {
                cmap_handle: handle,
                clone: false,
            };
            HANDLE_HASH.lock().unwrap().insert(handle, rhandle.clone());
            Ok(rhandle)
        } else {
            Err(CsError::from_c(res))
        }
    }
}

/// Finish with a connection to corosync.
/// Takes a [Handle] as returned from [initialize]
pub fn finalize(handle: &Handle) -> Result<()> {
    let res = unsafe { ffi::cmap_finalize(handle.cmap_handle) };
    if res == ffi::CS_OK {
        HANDLE_HASH.lock().unwrap().remove(&handle.cmap_handle);
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Return a file descriptor to use for poll/select on the CMAP handle.
/// Takes a [Handle] as returned from [initialize],
/// returns a C file descriptor as i32
pub fn fd_get(handle: &Handle) -> Result<i32> {
    let c_fd: *mut c_int = &mut 0 as *mut _ as *mut c_int;
    let res = unsafe { ffi::cmap_fd_get(handle.cmap_handle, c_fd) };
    if res == ffi::CS_OK {
        Ok(c_fd as i32)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Dispatch any/all active CMAP callbacks.
/// Takes a [Handle] as returned from [initialize],
/// flags [DispatchFlags] tells it how many items to dispatch before returning
pub fn dispatch(handle: &Handle, flags: DispatchFlags) -> Result<()> {
    let res = unsafe { ffi::cmap_dispatch(handle.cmap_handle, flags as u32) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Get the current 'context' value for this handle
/// The context value is an arbitrary value that is always passed
/// back to callbacks to help identify the source
pub fn context_get(handle: &Handle) -> Result<u64> {
    let (res, context) = unsafe {
        let mut context: u64 = 0;
        let c_context: *mut c_void = &mut context as *mut _ as *mut c_void;
        let r = ffi::cmap_context_get(handle.cmap_handle, c_context as *mut *const c_void);
        (r, context)
    };
    if res == ffi::CS_OK {
        Ok(context)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Set the current 'context' value for this handle
/// The context value is an arbitrary value that is always passed
/// back to callbacks to help identify the source.
/// Normally this is set in [initialize], but this allows it to be changed
pub fn context_set(handle: &Handle, context: u64) -> Result<()> {
    let res = unsafe {
        let c_context = context as *mut c_void;
        ffi::cmap_context_set(handle.cmap_handle, c_context)
    };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// The type of data returned from [get] or in a
/// tracker callback or iterator, part of the [Data] struct
#[derive(Clone, Copy, Debug, Eq, PartialEq, TryFromPrimitive)]
#[repr(u32)]
pub enum DataType {
    Int8 = ffi::CMAP_VALUETYPE_INT8,
    UInt8 = ffi::CMAP_VALUETYPE_UINT8,
    Int16 = ffi::CMAP_VALUETYPE_INT16,
    UInt16 = ffi::CMAP_VALUETYPE_UINT16,
    Int32 = ffi::CMAP_VALUETYPE_INT32,
    UInt32 = ffi::CMAP_VALUETYPE_UINT32,
    Int64 = ffi::CMAP_VALUETYPE_INT64,
    UInt64 = ffi::CMAP_VALUETYPE_UINT64,
    Float = ffi::CMAP_VALUETYPE_FLOAT,
    Double = ffi::CMAP_VALUETYPE_DOUBLE,
    String = ffi::CMAP_VALUETYPE_STRING,
    Binary = ffi::CMAP_VALUETYPE_BINARY,
    Unknown = 999,
}

fn cmap_to_enum(cmap_type: u32) -> DataType {
    match DataType::try_from(cmap_type) {
        Ok(e) => e,
        Err(_) => DataType::Unknown,
    }
}

/// Data returned from the cmap::get() call and tracker & iterators.
/// Contains the data itself and the type of that data.
pub enum Data {
    Int8(i8),
    UInt8(u8),
    Int16(i16),
    UInt16(u16),
    Int32(i32),
    UInt32(u32),
    Int64(i64),
    UInt64(u64),
    Float(f32),
    Double(f64),
    String(String),
    Binary(Vec<u8>),
    Unknown,
}

impl fmt::Display for DataType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            DataType::Int8 => write!(f, "Int8"),
            DataType::UInt8 => write!(f, "UInt8"),
            DataType::Int16 => write!(f, "Int16"),
            DataType::UInt16 => write!(f, "UInt16"),
            DataType::Int32 => write!(f, "Int32"),
            DataType::UInt32 => write!(f, "UInt32"),
            DataType::Int64 => write!(f, "Int64"),
            DataType::UInt64 => write!(f, "UInt64"),
            DataType::Float => write!(f, "Float"),
            DataType::Double => write!(f, "Double"),
            DataType::String => write!(f, "String"),
            DataType::Binary => write!(f, "Binary"),
            DataType::Unknown => write!(f, "Unknown"),
        }
    }
}

impl fmt::Display for Data {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Data::Int8(v) => write!(f, "{v} (Int8)"),
            Data::UInt8(v) => write!(f, "{v} (UInt8)"),
            Data::Int16(v) => write!(f, "{v} (Int16)"),
            Data::UInt16(v) => write!(f, "{v} (UInt16)"),
            Data::Int32(v) => write!(f, "{v} (Int32)"),
            Data::UInt32(v) => write!(f, "{v} (UInt32)"),
            Data::Int64(v) => write!(f, "{v} (Int64)"),
            Data::UInt64(v) => write!(f, "{v} (UInt64)"),
            Data::Float(v) => write!(f, "{v} (Float)"),
            Data::Double(v) => write!(f, "{v} (Double)"),
            Data::String(v) => write!(f, "{v} (String)"),
            Data::Binary(v) => write!(f, "{v:?} (Binary)"),
            Data::Unknown => write!(f, "Unknown)"),
        }
    }
}

const CMAP_KEYNAME_MAXLENGTH: usize = 255;
fn string_to_cstring_validated(key: &str, maxlen: usize) -> Result<CString> {
    if maxlen > 0 && key.chars().count() >= maxlen {
        return Err(CsError::CsErrInvalidParam);
    }

    match CString::new(key) {
        Ok(n) => Ok(n),
        Err(_) => Err(CsError::CsErrLibrary),
    }
}

fn set_value(
    handle: &Handle,
    key_name: &str,
    datatype: DataType,
    value: *mut c_void,
    length: usize,
) -> Result<()> {
    let csname = string_to_cstring_validated(key_name, CMAP_KEYNAME_MAXLENGTH)?;
    let res = unsafe {
        ffi::cmap_set(
            handle.cmap_handle,
            csname.as_ptr(),
            value,
            length,
            datatype as u32,
        )
    };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

// Returns type and size
fn generic_to_cmap<T>(_value: T) -> (DataType, usize) {
    match type_name::<T>() {
        "u8" => (DataType::UInt8, 1),
        "i8" => (DataType::Int8, 1),
        "u16" => (DataType::UInt16, 2),
        "i16" => (DataType::Int16, 2),
        "u32" => (DataType::UInt32, 4),
        "i32" => (DataType::Int32, 4),
        "u64" => (DataType::UInt64, 4),
        "f32" => (DataType::Float, 4),
        "f64" => (DataType::Double, 8),
        "&str" => (DataType::String, 0),
        // Binary not currently supported here
        _ => (DataType::Unknown, 0),
    }
}

fn is_numeric_type(dtype: DataType) -> bool {
    matches!(
        dtype,
        DataType::UInt8
            | DataType::Int8
            | DataType::UInt16
            | DataType::Int16
            | DataType::UInt32
            | DataType::Int32
            | DataType::UInt64
            | DataType::Int64
            | DataType::Float
            | DataType::Double
    )
}

/// Function to set a generic numeric value
/// This doesn't work for strings or binaries
pub fn set_number<T: Copy>(handle: &Handle, key_name: &str, value: T) -> Result<()> {
    let (c_type, c_size) = generic_to_cmap(value);

    if is_numeric_type(c_type) {
        let mut tmp = value;
        let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
        set_value(handle, key_name, c_type, c_value as *mut c_void, c_size)
    } else {
        Err(CsError::CsErrNotSupported)
    }
}

pub fn set_u8(handle: &Handle, key_name: &str, value: u8) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(handle, key_name, DataType::UInt8, c_value as *mut c_void, 1)
}

/// Sets an i8 value into cmap
pub fn set_i8(handle: &Handle, key_name: &str, value: i8) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(handle, key_name, DataType::Int8, c_value as *mut c_void, 1)
}

/// Sets a u16 value into cmap
pub fn set_u16(handle: &Handle, key_name: &str, value: u16) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(
        handle,
        key_name,
        DataType::UInt16,
        c_value as *mut c_void,
        2,
    )
}

/// Sets an i16 value into cmap
pub fn set_i16(handle: &Handle, key_name: &str, value: i16) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(handle, key_name, DataType::Int16, c_value as *mut c_void, 2)
}

/// Sets a u32 value into cmap
pub fn set_u32(handle: &Handle, key_name: &str, value: u32) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(handle, key_name, DataType::UInt32, c_value, 4)
}

/// Sets an i32 value into cmap
pub fn set_i132(handle: &Handle, key_name: &str, value: i32) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(handle, key_name, DataType::Int32, c_value as *mut c_void, 4)
}

/// Sets a u64 value into cmap
pub fn set_u64(handle: &Handle, key_name: &str, value: u64) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(
        handle,
        key_name,
        DataType::UInt64,
        c_value as *mut c_void,
        8,
    )
}

/// Sets an i64 value into cmap
pub fn set_i164(handle: &Handle, key_name: &str, value: i64) -> Result<()> {
    let mut tmp = value;
    let c_value: *mut c_void = &mut tmp as *mut _ as *mut c_void;
    set_value(handle, key_name, DataType::Int64, c_value as *mut c_void, 8)
}

/// Sets a string value into cmap
pub fn set_string(handle: &Handle, key_name: &str, value: &str) -> Result<()> {
    let v_string = string_to_cstring_validated(value, 0)?;
    set_value(
        handle,
        key_name,
        DataType::String,
        v_string.as_ptr() as *mut c_void,
        value.chars().count(),
    )
}

/// Sets a binary value into cmap
pub fn set_binary(handle: &Handle, key_name: &str, value: &[u8]) -> Result<()> {
    set_value(
        handle,
        key_name,
        DataType::Binary,
        value.as_ptr() as *mut c_void,
        value.len(),
    )
}

/// Sets a [Data] type into cmap
pub fn set(handle: &Handle, key_name: &str, data: &Data) -> Result<()> {
    let (datatype, datalen, c_value) = match data {
        Data::Int8(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::Int8, 1, cv)
        }
        Data::UInt8(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::UInt8, 1, cv)
        }
        Data::Int16(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::Int16, 2, cv)
        }
        Data::UInt16(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::UInt8, 2, cv)
        }
        Data::Int32(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::Int32, 4, cv)
        }
        Data::UInt32(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::UInt32, 4, cv)
        }
        Data::Int64(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::Int64, 8, cv)
        }
        Data::UInt64(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::UInt64, 8, cv)
        }
        Data::Float(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::Float, 4, cv)
        }
        Data::Double(v) => {
            let mut tmp = *v;
            let cv: *mut c_void = &mut tmp as *mut _ as *mut c_void;
            (DataType::Double, 8, cv)
        }
        Data::String(v) => {
            let cv = string_to_cstring_validated(v, 0)?;
            // Can't let cv go out of scope
            return set_value(
                handle,
                key_name,
                DataType::String,
                cv.as_ptr() as *mut c_void,
                v.chars().count(),
            );
        }
        Data::Binary(v) => {
            // Vec doesn't return quite the right types.
            return set_value(
                handle,
                key_name,
                DataType::Binary,
                v.as_ptr() as *mut c_void,
                v.len(),
            );
        }
        Data::Unknown => return Err(CsError::CsErrInvalidParam),
    };

    set_value(handle, key_name, datatype, c_value, datalen)
}

// Local function to parse out values from the C mess
// Assumes the c_value is complete. So cmap::get() will need to check the size
//   and re-get before calling us with a resized buffer
fn c_to_data(value_size: usize, c_key_type: u32, c_value: *const u8) -> Result<Data> {
    unsafe {
        match cmap_to_enum(c_key_type) {
            DataType::UInt8 => {
                let mut ints = [0u8; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr(), value_size);
                Ok(Data::UInt8(ints[0]))
            }
            DataType::Int8 => {
                let mut ints = [0i8; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::Int8(ints[0]))
            }
            DataType::UInt16 => {
                let mut ints = [0u16; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::UInt16(ints[0]))
            }
            DataType::Int16 => {
                let mut ints = [0i16; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::Int16(ints[0]))
            }
            DataType::UInt32 => {
                let mut ints = [0u32; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::UInt32(ints[0]))
            }
            DataType::Int32 => {
                let mut ints = [0i32; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::Int32(ints[0]))
            }
            DataType::UInt64 => {
                let mut ints = [0u64; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::UInt64(ints[0]))
            }
            DataType::Int64 => {
                let mut ints = [0i64; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::Int64(ints[0]))
            }
            DataType::Float => {
                let mut ints = [0f32; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::Float(ints[0]))
            }
            DataType::Double => {
                let mut ints = [0f64; 1];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr() as *mut u8, value_size);
                Ok(Data::Double(ints[0]))
            }
            DataType::String => {
                let mut ints = vec![0u8; value_size];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr(), value_size);
                // -1 here so CString doesn't see the NUL
                let cs = match CString::new(&ints[0..value_size - 1_usize]) {
                    Ok(c1) => c1,
                    Err(_) => return Err(CsError::CsErrLibrary),
                };
                match cs.into_string() {
                    Ok(s) => Ok(Data::String(s)),
                    Err(_) => Err(CsError::CsErrLibrary),
                }
            }
            DataType::Binary => {
                let mut ints = vec![0u8; value_size];
                copy_nonoverlapping(c_value as *mut u8, ints.as_mut_ptr(), value_size);
                Ok(Data::Binary(ints))
            }
            DataType::Unknown => Ok(Data::Unknown),
        }
    }
}

const INITIAL_SIZE: usize = 256;

/// Get a value from cmap, returned as a [Data] struct, so could be anything
pub fn get(handle: &Handle, key_name: &str) -> Result<Data> {
    let csname = string_to_cstring_validated(key_name, CMAP_KEYNAME_MAXLENGTH)?;
    let mut value_size: usize = 16;
    let mut c_key_type: u32 = 0;

    // First guess at a size for Strings and Binaries. Expand if needed
    let mut c_value = vec![0u8; INITIAL_SIZE];

    unsafe {
        let res = ffi::cmap_get(
            handle.cmap_handle,
            csname.as_ptr(),
            c_value.as_mut_ptr() as *mut c_void,
            &mut value_size,
            &mut c_key_type,
        );
        if res == ffi::CS_OK {
            if value_size > INITIAL_SIZE {
                // Need to try again with a bigger buffer
                c_value.resize(value_size, 0u8);
                let res2 = ffi::cmap_get(
                    handle.cmap_handle,
                    csname.as_ptr(),
                    c_value.as_mut_ptr() as *mut c_void,
                    &mut value_size,
                    &mut c_key_type,
                );
                if res2 != ffi::CS_OK {
                    return Err(CsError::from_c(res2));
                }
            }

            // Convert to Rust type and return as a Data enum
            c_to_data(value_size, c_key_type, c_value.as_ptr())
        } else {
            Err(CsError::from_c(res))
        }
    }
}

/// increment the value in a cmap key (must be a numeric type)
pub fn inc(handle: &Handle, key_name: &str) -> Result<()> {
    let csname = string_to_cstring_validated(key_name, CMAP_KEYNAME_MAXLENGTH)?;
    let res = unsafe { ffi::cmap_inc(handle.cmap_handle, csname.as_ptr()) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// decrement the value in a cmap key (must be a numeric type)
pub fn dec(handle: &Handle, key_name: &str) -> Result<()> {
    let csname = string_to_cstring_validated(key_name, CMAP_KEYNAME_MAXLENGTH)?;
    let res = unsafe { ffi::cmap_dec(handle.cmap_handle, csname.as_ptr()) };
    if res == ffi::CS_OK {
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

// Callback for CMAP notify events from corosync, convert params to Rust and pass on.
extern "C" fn rust_notify_fn(
    cmap_handle: ffi::cmap_handle_t,
    cmap_track_handle: ffi::cmap_track_handle_t,
    event: i32,
    key_name: *const ::std::os::raw::c_char,
    new_value: ffi::cmap_notify_value,
    old_value: ffi::cmap_notify_value,
    user_data: *mut ::std::os::raw::c_void,
) {
    // If cmap_handle doesn't match then throw away the callback.
    if let Some(r_cmap_handle) = HANDLE_HASH.lock().unwrap().get(&cmap_handle) {
        if let Some(h) = TRACKHANDLE_HASH.lock().unwrap().get(&cmap_track_handle) {
            let r_keyname = match string_from_bytes(key_name, CMAP_KEYNAME_MAXLENGTH) {
                Ok(s) => s,
                Err(_) => return,
            };

            let r_old = match c_to_data(old_value.len, old_value.type_, old_value.data as *const u8)
            {
                Ok(v) => v,
                Err(_) => return,
            };
            let r_new = match c_to_data(new_value.len, new_value.type_, new_value.data as *const u8)
            {
                Ok(v) => v,
                Err(_) => return,
            };

            if let Some(cb) = h.notify_callback.notify_fn {
                (cb)(
                    r_cmap_handle,
                    h,
                    TrackType { bits: event },
                    &r_keyname,
                    &r_old,
                    &r_new,
                    user_data as u64,
                );
            }
        }
    }
}

/// Callback function called every time a tracker reports a change in a tracked value
#[derive(Copy, Clone)]
pub struct NotifyCallback {
    pub notify_fn: Option<
        fn(
            handle: &Handle,
            track_handle: &TrackHandle,
            event: TrackType,
            key_name: &str,
            new_value: &Data,
            old_value: &Data,
            user_data: u64,
        ),
    >,
}

/// Track changes in cmap values, multiple [TrackHandle]s per [Handle] are allowed
pub fn track_add(
    handle: &Handle,
    key_name: &str,
    track_type: TrackType,
    notify_callback: &NotifyCallback,
    user_data: u64,
) -> Result<TrackHandle> {
    let c_name = string_to_cstring_validated(key_name, CMAP_KEYNAME_MAXLENGTH)?;
    let mut c_trackhandle = 0u64;
    let res = unsafe {
        ffi::cmap_track_add(
            handle.cmap_handle,
            c_name.as_ptr(),
            track_type.bits,
            Some(rust_notify_fn),
            user_data as *mut c_void,
            &mut c_trackhandle,
        )
    };
    if res == ffi::CS_OK {
        let rhandle = TrackHandle {
            track_handle: c_trackhandle,
            notify_callback: *notify_callback,
        };
        TRACKHANDLE_HASH
            .lock()
            .unwrap()
            .insert(c_trackhandle, rhandle);
        Ok(rhandle)
    } else {
        Err(CsError::from_c(res))
    }
}

/// Remove a tracker frm this [Handle]
pub fn track_delete(handle: &Handle, track_handle: TrackHandle) -> Result<()> {
    let res = unsafe { ffi::cmap_track_delete(handle.cmap_handle, track_handle.track_handle) };
    if res == ffi::CS_OK {
        TRACKHANDLE_HASH
            .lock()
            .unwrap()
            .remove(&track_handle.track_handle);
        Ok(())
    } else {
        Err(CsError::from_c(res))
    }
}

/// Create one of these to start iterating over cmap values.
pub struct CmapIterStart {
    iter_handle: u64,
    cmap_handle: u64,
}

pub struct CmapIntoIter {
    cmap_handle: u64,
    iter_handle: u64,
}

/// Value returned from the iterator. contains the key name and the [Data]
pub struct CmapIter {
    key_name: String,
    data: Data,
}

impl CmapIter {
    pub fn key_name(&self) -> &str {
        &self.key_name
    }
    pub fn data(&self) -> &Data {
        &self.data
    }
}

impl fmt::Debug for CmapIter {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}: {}", self.key_name, self.data)
    }
}

impl Iterator for CmapIntoIter {
    type Item = CmapIter;

    fn next(&mut self) -> Option<CmapIter> {
        let mut c_key_name = [0u8; CMAP_KEYNAME_MAXLENGTH + 1];
        let mut c_value_len = 0usize;
        let mut c_value_type = 0u32;
        let res = unsafe {
            ffi::cmap_iter_next(
                self.cmap_handle,
                self.iter_handle,
                c_key_name.as_mut_ptr() as *mut c_char,
                &mut c_value_len,
                &mut c_value_type,
            )
        };
        if res == ffi::CS_OK {
            // Return the Data for this iteration
            let mut c_value = vec![0u8; c_value_len];
            let res = unsafe {
                ffi::cmap_get(
                    self.cmap_handle,
                    c_key_name.as_ptr() as *mut c_char,
                    c_value.as_mut_ptr() as *mut c_void,
                    &mut c_value_len,
                    &mut c_value_type,
                )
            };
            if res == ffi::CS_OK {
                match c_to_data(c_value_len, c_value_type, c_value.as_ptr()) {
                    Ok(d) => {
                        let r_keyname = match string_from_bytes(
                            c_key_name.as_ptr() as *mut c_char,
                            CMAP_KEYNAME_MAXLENGTH,
                        ) {
                            Ok(s) => s,
                            Err(_) => return None,
                        };
                        Some(CmapIter {
                            key_name: r_keyname,
                            data: d,
                        })
                    }
                    Err(_) => None,
                }
            } else {
                // cmap_get returned error
                None
            }
        } else if res == ffi::CS_ERR_NO_SECTIONS {
            // End of list
            unsafe {
                // Yeah, we don't check this return code. There's nowhere to report it.
                ffi::cmap_iter_finalize(self.cmap_handle, self.iter_handle)
            };
            None
        } else {
            None
        }
    }
}

impl CmapIterStart {
    /// Create a new [CmapIterStart] object for iterating over a list of cmap keys
    pub fn new(cmap_handle: &Handle, prefix: &str) -> Result<CmapIterStart> {
        let mut iter_handle: u64 = 0;
        let res = unsafe {
            let c_prefix = string_to_cstring_validated(prefix, CMAP_KEYNAME_MAXLENGTH)?;
            ffi::cmap_iter_init(cmap_handle.cmap_handle, c_prefix.as_ptr(), &mut iter_handle)
        };
        if res == ffi::CS_OK {
            Ok(CmapIterStart {
                cmap_handle: cmap_handle.cmap_handle,
                iter_handle,
            })
        } else {
            Err(CsError::from_c(res))
        }
    }
}

impl IntoIterator for CmapIterStart {
    type Item = CmapIter;
    type IntoIter = CmapIntoIter;

    fn into_iter(self) -> Self::IntoIter {
        CmapIntoIter {
            iter_handle: self.iter_handle,
            cmap_handle: self.cmap_handle,
        }
    }
}
