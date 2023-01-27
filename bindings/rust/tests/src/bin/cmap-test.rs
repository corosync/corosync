// Test the CMAP library. Requires that corosync is running and that we are root.

extern crate rust_corosync as corosync;
use corosync::cmap;

fn track_notify_fn(
    _handle: &cmap::Handle,
    _track_handle: &cmap::TrackHandle,
    event: cmap::TrackType,
    key_name: &str,
    old_value: &cmap::Data,
    new_value: &cmap::Data,
    user_data: u64,
) {
    println!("Track notify callback");
    println!("Key: {key_name}, event: {event}, user_data: {user_data}");
    println!("   Old value: {old_value}");
    println!("   New value: {new_value}");
}

fn main() {
    let handle = match cmap::initialize(cmap::Map::Icmap) {
        Ok(h) => {
            println!("cmap initialized.");
            h
        }
        Err(e) => {
            println!("Error in CMAP (Icmap) init: {e}");
            return;
        }
    };

    // Test some SETs
    if let Err(e) = cmap::set_u32(handle, "test.test_uint32", 456) {
        println!("Error in CMAP set_u32: {e}");
        return;
    };

    if let Err(e) = cmap::set_i16(handle, "test.test_int16", -789) {
        println!("Error in CMAP set_i16: {e}");
        return;
    };

    if let Err(e) = cmap::set_number(handle, "test.test_num_1", 6809u32) {
        println!("Error in CMAP set_number(u32): {e}");
        return;
    };

    // NOT PI (just to avoid clippy whingeing)
    if let Err(e) = cmap::set_number(handle, "test.test_num_2", 3.24159265) {
        println!("Error in CMAP set_number(f32): {e}");
        return;
    };

    if let Err(e) = cmap::set_string(handle, "test.test_string", "Hello from Rust") {
        println!("Error in CMAP set_string: {e}");
        return;
    };

    let test_d = cmap::Data::UInt64(0xdeadbeefbacecafe);
    if let Err(e) = cmap::set(handle, "test.test_data", &test_d) {
        println!("Error in CMAP set_data: {e}");
        return;
    };

    //    let test_d2 = cmap::Data::UInt32(6809);
    let test_d2 = cmap::Data::String("Test string in data 12345".to_string());
    if let Err(e) = cmap::set(handle, "test.test_again", &test_d2) {
        println!("Error in CMAP set_data2: {e}");
        return;
    };

    // get them back again
    match cmap::get(handle, "test.test_uint32") {
        Ok(v) => {
            println!("GOT uint32 {v}");
        }

        Err(e) => {
            println!("Error in CMAP get: {e}");
            return;
        }
    };
    match cmap::get(handle, "test.test_int16") {
        Ok(v) => {
            println!("GOT uint16 {v}");
        }

        Err(e) => {
            println!("Error in CMAP get: {e}");
            return;
        }
    };

    match cmap::get(handle, "test.test_num_1") {
        Ok(v) => {
            println!("GOT num {v}");
        }

        Err(e) => {
            println!("Error in CMAP get: {e}");
            return;
        }
    };
    match cmap::get(handle, "test.test_num_2") {
        Ok(v) => {
            println!("GOT num {v}");
        }

        Err(e) => {
            println!("Error in CMAP get: {e}");
            return;
        }
    };
    match cmap::get(handle, "test.test_string") {
        Ok(v) => {
            println!("GOT string {v}");
        }

        Err(e) => {
            println!("Error in CMAP get: {e}");
            return;
        }
    };

    match cmap::get(handle, "test.test_data") {
        Ok(v) => match v {
            cmap::Data::UInt64(u) => println!("GOT data value {u:x}"),
            _ => println!("ERROR type was not UInt64, got {v}"),
        },

        Err(e) => {
            println!("Error in CMAP get: {e}");
            return;
        }
    };

    // Test an iterator
    match cmap::CmapIterStart::new(handle, "totem.") {
        Ok(cmap_iter) => {
            for i in cmap_iter {
                println!("ITER: {i:?}");
            }
            println!();
        }
        Err(e) => {
            println!("Error in CMAP iter start: {e}");
        }
    }

    // Close this handle
    if let Err(e) = cmap::finalize(handle) {
        println!("Error in CMAP get: {e}");
        return;
    };

    // Test notifications on the stats map
    let handle = match cmap::initialize(cmap::Map::Stats) {
        Ok(h) => h,
        Err(e) => {
            println!("Error in CMAP (Stats) init: {e}");
            return;
        }
    };

    let cb = cmap::NotifyCallback {
        notify_fn: Some(track_notify_fn),
    };
    let _track_handle = match cmap::track_add(
        handle,
        "stats.srp.memb_merge_detect_tx",
        cmap::TrackType::MODIFY | cmap::TrackType::ADD | cmap::TrackType::DELETE,
        &cb,
        997u64,
    ) {
        Ok(th) => th,
        Err(e) => {
            println!("Error in CMAP track_add {e}");
            return;
        }
    };

    // Wait for events
    let mut event_num = 0;
    loop {
        if let Err(e) = cmap::dispatch(handle, corosync::DispatchFlags::One) {
            println!("Error from CMAP dispatch: {e}");
        }
        // Just do 5
        event_num += 1;
        if event_num > 5 {
            break;
        }
    }
}
