// Test the VOTEQUORUM library. Requires that corosync is running and that we are root.

extern crate rust_corosync as corosync;
use corosync::votequorum;

fn quorum_fn(
    _handle: &votequorum::Handle,
    _context: u64,
    quorate: bool,
    member_list: Vec<votequorum::Node>,
) {
    println!("TEST votequorum_quorum_fn called. quorate = {quorate}");
    println!("  members: {member_list:?}");
}

fn nodelist_fn(
    _handle: &votequorum::Handle,
    _context: u64,
    ring_id: votequorum::RingId,
    member_list: Vec<corosync::NodeId>,
) {
    println!(
        "TEST nodelist_fn called for {}/{}",
        ring_id.nodeid, ring_id.seq
    );
    println!("  members: {member_list:?}");
}

fn expectedvotes_fn(_handle: &votequorum::Handle, _context: u64, expected_votes: u32) {
    println!("TEST expected_votes_fn called: value is {expected_votes}");
}

fn main() -> Result<(), corosync::CsError> {
    // Initialise the model data
    let cb = votequorum::Callbacks {
        quorum_notification_fn: Some(quorum_fn),
        nodelist_notification_fn: Some(nodelist_fn),
        expectedvotes_notification_fn: Some(expectedvotes_fn),
    };

    let handle = match votequorum::initialize(&cb) {
        Ok(h) => {
            println!("Votequorum initialized.");
            h
        }
        Err(e) => {
            println!("Error in VOTEQUORUM init: {e}");
            return Err(e);
        }
    };

    // Test context APIs
    let set_context: u64 = 0xabcdbeefcafe;
    if let Err(e) = votequorum::context_set(&handle, set_context) {
        println!("Error in VOTEQUORUM context_set: {e}");
        return Err(e);
    }

    // NOTE This will fail on 32 bit systems because void* is not u64
    match votequorum::context_get(&handle) {
        Ok(c) => {
            if c != set_context {
                println!("Error: context_get() returned {c:x}, context should be {set_context:x}");
            }
        }
        Err(e) => {
            println!("Error in VOTEQUORUM context_get: {e}");
            return Err(e);
        }
    }

    const QDEVICE_NAME: &str = "RustQdevice";

    if let Err(e) = votequorum::qdevice_register(&handle, QDEVICE_NAME) {
        println!("Error in VOTEQUORUM qdevice_register: {e}");
        return Err(e);
    }

    match votequorum::get_info(&handle, corosync::NodeId::from(1u32)) {
        Ok(i) => {
            println!("Node info for nodeid 1");
            println!("  nodeid: {}", i.node_id);
            println!("  node_state: {:?}", i.node_state);
            println!("  node_votes: {}", i.node_votes);
            println!("  node_expected: {}", i.node_expected_votes);
            println!("  highest_expected: {}", i.highest_expected);
            println!("  quorum: {}", i.quorum);
            println!("  flags: {:x}", i.flags);
            println!("  qdevice_votes: {}", i.qdevice_votes);
            println!("  qdevice_name: {}", i.qdevice_name);

            if i.qdevice_name != QDEVICE_NAME {
                println!(
                    "qdevice names do not match: s/b: \"{}\"  is: \"{}\"",
                    QDEVICE_NAME, i.qdevice_name
                );
                return Err(corosync::CsError::CsErrRustCompat);
            }
        }
        Err(e) => {
            println!("Error in VOTEQUORUM get_info: {e} (check nodeid 1 has been online)");
            return Err(e);
        }
    }

    if let Err(e) = votequorum::qdevice_unregister(&handle, QDEVICE_NAME) {
        println!("Error in VOTEQUORUM qdevice_unregister: {e}");
        return Err(e);
    }

    if let Err(e) = votequorum::trackstart(&handle, 99_u64, corosync::TrackFlags::Changes) {
        println!("Error in VOTEQUORUM trackstart: {e}");
        return Err(e);
    }

    // Quick test of dispatch
    if let Err(e) = votequorum::dispatch(&handle, corosync::DispatchFlags::OneNonblocking) {
        println!("Error in VOTEUORUM dispatch: {e}");
        return Err(e);
    }
    Ok(())
}
