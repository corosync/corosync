// Test the CFG library. Requires that corosync is running and that we are root.

extern crate rust_corosync as corosync;
use corosync::{cfg, NodeId};

use std::thread::spawn;

fn dispatch_thread(handle: cfg::Handle) {
    loop {
        if cfg::dispatch(handle, corosync::DispatchFlags::One).is_err() {
            return;
        }
    }
}

// Test the shutdown callback
fn shutdown_check_fn(handle: &cfg::Handle, _flags: u32) {
    println!("in shutdown callback");

    // DON'T shutdown corosync - we're just testing
    if let Err(e) = cfg::reply_to_shutdown(*handle, cfg::ShutdownReply::No) {
        println!("Error in CFG replyto_shutdown: {e}");
    }
}

fn main() {
    // Initialise the callbacks data
    let cb = cfg::Callbacks {
        corosync_cfg_shutdown_callback_fn: Some(shutdown_check_fn),
    };

    let handle = match cfg::initialize(&cb) {
        Ok(h) => {
            println!("cfg initialized.");
            h
        }
        Err(e) => {
            println!("Error in CFG init: {e}");
            return;
        }
    };

    // Open two handles to CFG so that the second one can refuse shutdown
    let handle2 = match cfg::initialize(&cb) {
        Ok(h) => {
            println!("cfg2 initialized.");
            h
        }
        Err(e) => {
            println!("Error in CFG init: {e}");
            return;
        }
    };

    match cfg::track_start(handle2, cfg::TrackFlags::None) {
        Ok(_) => {
            // Run handle2 dispatch in its own thread
            spawn(move || dispatch_thread(handle2));
        }
        Err(e) => {
            println!("Error in CFG track_start: {e}");
        }
    };

    let local_nodeid = {
        match cfg::local_get(handle) {
            Ok(n) => {
                println!("Local nodeid is {n}");
                Some(n)
            }
            Err(e) => {
                println!("Error in CFG local_get: {e}");
                None
            }
        }
    };

    // Test node_status_get.
    // node status for the local node looks odd (cos it's the loopback connection), so
    // we try for a node ID one less or more than us just to get output that looks
    // sensible to the user.
    if let Some(our_nodeid) = local_nodeid {
        let us_plus1 = NodeId::from(u32::from(our_nodeid) + 1);
        let us_less1 = NodeId::from(u32::from(our_nodeid) - 1);
        let mut res = cfg::node_status_get(handle, us_plus1, cfg::NodeStatusVersion::V1);
        if let Err(e) = res {
            println!("Error from node_status_get on nodeid {us_plus1}: {e}");
            res = cfg::node_status_get(handle, us_less1, cfg::NodeStatusVersion::V1);
        };
        match res {
            Ok(ns) => {
                println!("Node Status for nodeid {}", ns.nodeid);
                println!("   reachable: {}", ns.reachable);
                println!("   remote: {}", ns.remote);
                println!("   onwire_min: {}", ns.onwire_min);
                println!("   onwire_max: {}", ns.onwire_max);
                println!("   onwire_ver: {}", ns.onwire_ver);
                for (ls_num, ls) in ns.link_status.iter().enumerate() {
                    if ls.enabled {
                        println!("   Link {ls_num}");
                        println!("      connected: {}", ls.connected);
                        println!("      mtu: {}", ls.mtu);
                        println!("      src: {}", ls.src_ipaddr);
                        println!("      dst: {}", ls.dst_ipaddr);
                    }
                }
            }
            Err(e) => {
                println!(
                    "Error in CFG node_status get: {e} (tried nodeids {us_plus1} & {us_less1})"
                );
            }
        }
    }

    // This should not shutdown corosync because the callback on handle2 will refuse it.
    match cfg::try_shutdown(handle, cfg::ShutdownFlags::Request) {
        Ok(_) => {
            println!("CFG try_shutdown suceeded, should return busy");
        }
        Err(e) => {
            if e != corosync::CsError::CsErrBusy {
                println!("Error in CFG try_shutdown: {e}");
            }
        }
    }

    // Wait for events
    loop {
        if cfg::dispatch(handle, corosync::DispatchFlags::One).is_err() {
            break;
        }
    }
    println!("ERROR: Corosync quit");
}
