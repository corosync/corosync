// Test the QUORUM library. Requires that corosync is running and that we are root.

extern crate rust_corosync as corosync;
use corosync::{quorum, NodeId};

fn quorum_fn(
    _handle: &quorum::Handle,
    quorate: bool,
    ring_id: quorum::RingId,
    member_list: Vec<NodeId>,
) {
    println!("TEST quorum_fn called. quorate = {quorate}");
    println!("  ring_id: {}/{}", ring_id.nodeid, ring_id.seq);
    println!("  members: {member_list:?}");
}

fn nodelist_fn(
    _handle: &quorum::Handle,
    ring_id: quorum::RingId,
    member_list: Vec<NodeId>,
    joined_list: Vec<NodeId>,
    left_list: Vec<NodeId>,
) {
    println!(
        "TEST nodelist_fn called for {}/{}",
        ring_id.nodeid, ring_id.seq
    );
    println!("  members: {member_list:?}");
    println!("  joined: {joined_list:?}");
    println!("  left: {left_list:?}");
}

fn main() -> Result<(), corosync::CsError> {
    // Initialise the model data
    let md = quorum::ModelData::ModelV1(quorum::Model1Data {
        flags: quorum::Model1Flags::None,
        quorum_notification_fn: Some(quorum_fn),
        nodelist_notification_fn: Some(nodelist_fn),
    });

    let handle = match quorum::initialize(&md, 99_u64) {
        Ok((h, t)) => {
            println!("Quorum initialized; type = {}", t as u32);
            h
        }
        Err(e) => {
            println!("Error in QUORUM init: {e}");
            return Err(e);
        }
    };

    // Test context APIs
    let set_context: u64 = 0xabcdbeefcafe;
    if let Err(e) = quorum::context_set(&handle, set_context) {
        println!("Error in QUORUM context_set: {e}");
        return Err(e);
    }

    // NOTE This will fail on 32 bit systems because void* is not u64
    match quorum::context_get(&handle) {
        Ok(c) => {
            if c != set_context {
                println!("Error: context_get() returned {c:x}, context should be {set_context:x}");
                return Err(corosync::CsError::CsErrRustCompat);
            }
        }
        Err(e) => {
            println!("Error in QUORUM context_get: {e}");
            return Err(e);
        }
    }

    if let Err(e) = quorum::trackstart(&handle, corosync::TrackFlags::Changes) {
        println!("Error in QUORUM trackstart: {e}");
        return Err(e);
    }

    // Check that fd_get returns a valid FD
    match quorum::fd_get(&handle) {
        Ok(fd) => {
            println!("FD is {fd}");
            // Arbitrary upper limit but FDs should always be low
            // and we're mainly checking addresses being returned
            if !(0..=0xFFFF).contains(&fd) {
                println!("Error - bad fd returned from fd_get: {fd}");
                return Err(corosync::CsError::CsErrRustCompat);
            }
        }
        Err(e) => {
            println!("Error in QUORUM fd_get: {e}");
            return Err(e);
        }
    }

    // Quick test of dispatch
    if let Err(e) = quorum::dispatch(&handle, corosync::DispatchFlags::OneNonblocking) {
        println!("Error in QUORUM dispatch: {e}");
        return Err(e);
    }
    Ok(())
}
