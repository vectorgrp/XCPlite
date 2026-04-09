//-----------------------------------------------------------------------------
// Module xcp_test_executor
// Runs various tests against an XCP server

#![allow(dead_code)]
#![allow(unused_imports)]

use log::{debug, error, info, trace, warn};
use parking_lot::Mutex;
use std::net::SocketAddr;
use std::num::Wrapping;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64};
use tokio::time::{Duration, Instant};
use xcp_lite::metrics::counter;

use xcp_lite::registry::*;
use xcp_lite::*;

use xcpclient::xcp_client::xcp::*;
use xcpclient::xcp_client::*;

//-----------------------------------------------------------------------------

// Logging
pub const OPTION_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;
pub const OPTION_XCP_LOG_LEVEL: u8 = 2;

//------------------------------------------------------------------------
// Test parameters
const CAL_TEST_MAX_ITER: u32 = 10000; // Number of calibrations
const CAL_TEST_TASK_SLEEP_TIME_US: u64 = 100; // Calibration check task cycle time in us

pub const MAX_TASK_COUNT: usize = 255; // Max number of threads 

//------------------------------------------------------------------------
// Test error

pub static DAQ_ERROR: AtomicBool = AtomicBool::new(false);
pub static DAQ_PACKETS_LOST: AtomicU32 = AtomicU32::new(0);
pub static DAQ_COUNTER_ERRORS: AtomicU32 = AtomicU32::new(0);
pub static DAQ_BYTES: AtomicU64 = AtomicU64::new(0);

//------------------------------------------------------------------------
// Handle incoming SERV_TEXT data

#[derive(Debug, Clone, Copy)]
struct ServTextDecoder;

impl ServTextDecoder {
    pub fn new() -> ServTextDecoder {
        ServTextDecoder {}
    }
}

impl XcpTextDecoder for ServTextDecoder {
    // Handle incomming text data from XCP server
    fn decode(&self, data: &[u8]) {
        print!("[SERV_TEXT] ");
        let mut j = 0;
        while j < data.len() {
            if data[j] == 0 {
                break;
            }
            print!("{}", data[j] as char);
            j += 1;
        }
    }
}

//------------------------------------------------------------------------
// Handle incoming DAQ data
// Create some test diagnostic data

#[derive(Debug, Clone, Copy)]
pub struct DaqDecoder {
    pub timestamp_resolution: u64,
    pub tot_events: u32,
    pub tot_bytes: u64,
    pub packets_lost: u32,
    pub counter_errors: u32,
    pub daq_max: u16,
    pub odt_max: u8,
    pub daq_timestamp: u64,
    pub daq_events: u32,
    pub max_counter: u32,
    pub last_counter: u32,
}

impl DaqDecoder {
    pub fn new() -> DaqDecoder {
        DaqDecoder {
            timestamp_resolution: 1,
            tot_events: 0,
            tot_bytes: 0,
            packets_lost: 0,
            counter_errors: 0,
            daq_max: 0,
            odt_max: 0,
            daq_timestamp: 0,
            daq_events: 0,
            max_counter: 0,
            last_counter: 0,
        }
    }
}

impl XcpDaqDecoder for DaqDecoder {
    // Set start time and reset
    fn start(&mut self, _odt_entries: Vec<Vec<OdtEntry>>, timestamp: u64) {
        DAQ_BYTES.store(0, std::sync::atomic::Ordering::Relaxed);
        self.tot_events = 0;
        self.tot_bytes = 0;
        self.packets_lost = 0;
        self.counter_errors = 0;
        self.daq_max = 0;
        self.odt_max = 0;
        self.daq_timestamp = timestamp;
        self.daq_events = 0;
        self.max_counter = 0;
        self.last_counter = 0;
    }

    // Set timestamp resolution
    fn set_daq_properties(&mut self, timestamp_resolution: u64, daq_header_size: u8) {
        self.timestamp_resolution = timestamp_resolution;
        assert_eq!(daq_header_size, 4);
    }

    // Handle incomming DAQ DTOs from XCP server
    fn decode(&mut self, lost: u32, buf: &[u8]) {
        self.tot_bytes += buf.len() as u64;
        DAQ_BYTES.store(self.tot_bytes, std::sync::atomic::Ordering::Relaxed);

        if lost > 0 {
            self.packets_lost += lost;
            DAQ_PACKETS_LOST.store(self.packets_lost, std::sync::atomic::Ordering::SeqCst);
            // warn!("PACKETS_LOST = {}", lost);
        }

        let mut timestamp_raw: u32 = 0;
        let data: &[u8];

        // Decode header and raw timestamp
        let daq = (buf[2] as u16) | ((buf[3] as u16) << 8);
        let odt = buf[0];
        if odt == 0 {
            timestamp_raw = (buf[4] as u32) | ((buf[4 + 1] as u32) << 8) | ((buf[4 + 2] as u32) << 16) | ((buf[4 + 3] as u32) << 24);
            data = &buf[8..];
        } else {
            data = &buf[4..];
        }

        assert!(daq == 0);
        assert!(odt == 0);
        if daq > self.daq_max {
            self.daq_max = daq;
        }

        // Decode raw timestamp as u64
        // Check declining and stuck timestamps
        if odt == 0 {
            let t_last = self.daq_timestamp;
            let tl = (t_last & 0xFFFFFFFF) as u32;
            let mut th = (t_last >> 32) as u32;
            if timestamp_raw < tl {
                th += 1;
            }
            let t = (timestamp_raw as u64) | ((th as u64) << 32);
            if t < t_last {
                warn!("Timestamp of daq {} declining {} -> {}", daq, t_last, t);
            }
            if t == t_last {
                warn!("Timestamp of daq {} stuck at {}", daq, t);
            }
            self.daq_timestamp = t;
        }

        // Hardcoded decoding of data (only one ODT)
        assert!(odt == 0);
        if odt == 0 && data.len() >= 2 {
            let o = 0;

            // Check counter (+0)
            let counter = (data[o] as u32) | ((data[o + 1] as u32) << 8);

            // Check counter is incrementing, usually because of packets lost
            if self.daq_events != 0 && counter != self.last_counter + 1 && counter != 0 && daq != 0 {
                let count = DAQ_COUNTER_ERRORS.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                trace!("DAQ_COUNTER_ERRORS: {} daq={} {} -> {} ", count, daq, self.last_counter, counter,);
            }
            self.last_counter = counter;

            trace!("DAQ: daq = {}, odt = {} timestamp = {} counter={})", daq, odt, timestamp_raw, counter,);

            self.daq_events += 1;
            self.tot_events += 1;
        } // odt==0
    }
}

//-----------------------------------------------------------------------
// Execute tests

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum TestModeDaq {
    None,
    Daq,
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum TestModeCal {
    None,
    Cal,
}

// Perform DAQ test
// Returns actual duration and bytes per event
// 0,0 on error
pub async fn test_daq(xcp_client: &mut XcpClient, _test_mode_daq: TestModeDaq, daq_test_duration_ms: u64) -> (bool, u32) {
    let mut error = false;

    xcp_client.create_measurement_object("counter").expect("Failed to create measurement object 'counter'");
    xcp_client.start_measurement().await.expect("Failed to start measurement");

    // Test for given time
    // Every 2ms check if measurement is still ok
    // Break on error
    let starttime = Instant::now();
    loop {
        if starttime.elapsed().as_millis() > daq_test_duration_ms as u128 {
            break;
        }
        if DAQ_ERROR.load(std::sync::atomic::Ordering::SeqCst) {
            warn!("DAQ error detected, aborting DAsQ test loop");
            error = true;
            break;
        }
        let packets_lost = DAQ_PACKETS_LOST.load(std::sync::atomic::Ordering::SeqCst);
        if packets_lost > 0 {
            warn!("DAQ packet loss detected, aborting DAQ test loop");
            break;
        }
        let counter_errors = DAQ_COUNTER_ERRORS.load(std::sync::atomic::Ordering::SeqCst);
        if counter_errors > 0 {
            warn!("DAQ counter error detected, aborting DAQ test loop");
            break;
        }
        tokio::time::sleep(Duration::from_micros(2000)).await;
    }
    let duration_ms = starttime.elapsed().as_millis().try_into().unwrap();

    // Stop DAQ
    let res = xcp_client.stop_measurement().await;
    match res {
        Ok(_) => {
            debug!("DAQ stopped");
        }
        Err(e) => {
            error!("DAQ stop failed: {:?}", e);
            error = true;
        }
    }

    // Wait some time to be sure the queue is emptied
    // The XCP server will not respond to STOP while the queue is not empty
    // But the queue of the client may still contain data or the control channel may need some more time
    tokio::time::sleep(Duration::from_millis(250)).await;

    // Return error state or actual est duration and bytes per event
    if error {
        error!("Error in DAQ test loop after {}ms", duration_ms);
        (false, duration_ms)
    } else {
        (true, duration_ms)
    }
}

//-----------------------------------------------------------------------

// Calibration test
async fn test_calibration(xcp_client: &mut XcpClient) -> bool {
    info!("Start calibration test");

    // Check pages on segment 1 are on RAM, if not set to RAM (segment 0 is assumed to be the EPK segment)
    let mut page: u8 = xcp_client.get_ecu_page(1).await.unwrap();
    if page != 0 {
        info!("Set ECU page to RAM");
        xcp_client.set_ecu_page(0).await.unwrap();
        page = xcp_client.get_ecu_page(0).await.unwrap();
        if !(page == 0) {
            error!("Could not set ECU page to RAM");
            return false;
        }
    }
    page = xcp_client.get_xcp_page(0).await.unwrap();
    if page != 0 {
        info!("Set XCP page to RAM");
        xcp_client.set_xcp_page(0).await.unwrap();
        page = xcp_client.get_xcp_page(0).await.unwrap();
        if !(page == 0) {
            error!("Could not set XCP page to RAM");
            return false;
        }
    }

    // Check if there is a params.delay_us
    debug!("Create calibration object params.delay_us");
    match xcp_client.create_calibration_object("params.delay_us").await {
        Ok(delay_us) => {
            let v = xcp_client.get_value_u64(delay_us);
            info!("RAM delay_us={}", v);
            if v < 100 || v > 1000000 {
                error!("params.delay_us initial value unplausible");
                return false;
            }
        }
        Err(e) => {
            warn!("Could not find calibration parameter params.delay_us: {}", e);
        }
    }

    // Calibrate params.counter_max
    debug!("Create calibration object params.counter_max");
    let counter_max = xcp_client.create_calibration_object("params.counter_max").await;
    if counter_max.is_err() {
        error!("Could not find calibration parameter params.counter_max, test not executed");
    } else {
        let counter_max = counter_max.unwrap();
        let mut v = xcp_client.get_value_u64(counter_max);
        info!("RAM counter_max={}", v);
        if v != 1024 && v != 512 {
            error!("params.counter_max initial value incorrect");
            return false;
        }
        xcp_client.set_value_u64(counter_max, 512).await.unwrap();
        xcp_client.read_value_u64(counter_max).await.unwrap();
        v = xcp_client.get_value_u64(counter_max);
        info!("RAM counter_max={}", v);
        if v != 512 {
            error!("params.counter_max value after write incorrect");
            return false;
        }

        // Set XCP page to FLASH
        xcp_client.set_xcp_page(1).await.unwrap();
        info!("Set XCP page to FLASH");
        page = xcp_client.get_xcp_page(0).await.unwrap();
        assert_eq!(page, 1);

        // Read counter_max from FLASH page
        xcp_client.read_value_u64(counter_max).await.unwrap();
        v = xcp_client.get_value_u64(counter_max);
        info!("FLASH counter_max={}", v);
        if !(v == 1024) {
            error!("params.counter_max value from FLASH not as expected, read {}", v);
            return false;
        }

        // Set ECU page to FLASH
        xcp_client.set_ecu_page(1).await.unwrap();
        page = xcp_client.get_ecu_page(0).await.unwrap();
        info!("Set ECU page to FLASH");
        assert_eq!(page, 1);

        // Set XCP page to RAM
        xcp_client.set_xcp_page(0).await.unwrap();
        info!("Set XCP page to RAM");

        // Read counter_max from RAM page
        xcp_client.read_value_u64(counter_max).await.unwrap();
        v = xcp_client.get_value_u64(counter_max);
        info!("RAM counter_max={}", v);
        if !(v == 512) {
            error!("params.counter_max value from RAM not as expected, read {}", v);
            return false;
        }

        // Reset
        xcp_client.set_ecu_page(0).await.unwrap();
        info!("Set ECU page to RAM");
        xcp_client.set_value_u64(counter_max, 1024).await.unwrap();
    } // counter_max calibration test

    // Test calibration consistency
    let sync_test1 = xcp_client.create_calibration_object("params.test_byte1").await;
    let sync_test2 = xcp_client.create_calibration_object("params.test_byte2").await;
    if sync_test1.is_err() || sync_test2.is_err() {
        error!("Could not find calibration parameters params.test_byte1 or params.test_byte2, test not executed");
    } else {
        let sync_test1 = sync_test1.unwrap();
        let sync_test2 = sync_test2.unwrap();
        let addr_sync_test1 = sync_test1.get_a2l_addr(xcp_client);
        info!(
            "Created calibration object params.test_byte1 at addr={:#X} ext={:#X}",
            addr_sync_test1.addr, addr_sync_test1.ext
        );
        let addr_sync_test2 = sync_test2.get_a2l_addr(xcp_client);
        info!(
            "Created calibration object params.test_byte2 at addr={:#X} ext={:#X}",
            addr_sync_test2.addr, addr_sync_test2.ext
        );

        let starttime = Instant::now();
        for i in 0..CAL_TEST_MAX_ITER {
            let value1: i8 = (i & 0x7F) as i8;
            let value2: i8 = value1;
            xcp_client // SHORT_DOWNLOAD cal_seg.test_u64
                .short_download(addr_sync_test1.addr, addr_sync_test1.ext, &value1.to_le_bytes())
                .await
                .unwrap();
            xcp_client // SHORT_DOWNLOAD cal_seg.test_u64
                .short_download(addr_sync_test2.addr, addr_sync_test2.ext, &value2.to_le_bytes())
                .await
                .unwrap();
        }
        let dt = starttime.elapsed().as_micros() as u64;
        info!("Average direct calibration cycle time: {}us", dt / (CAL_TEST_MAX_ITER * 2) as u64);

        let starttime = Instant::now();
        for i in 0..CAL_TEST_MAX_ITER {
            let value1: i8 = (i & 0x7F) as i8;
            let value2: i8 = -value1;
            xcp_client.modify_begin().await.unwrap();
            xcp_client // SHORT_DOWNLOAD cal_seg.test_u64
                .short_download(addr_sync_test1.addr, addr_sync_test1.ext, &value1.to_le_bytes())
                .await
                .unwrap();
            xcp_client // SHORT_DOWNLOAD cal_seg.test_u64
                .short_download(addr_sync_test2.addr, addr_sync_test2.ext, &value2.to_le_bytes())
                .await
                .unwrap();
            xcp_client.modify_end().await.unwrap();
        }
        let dt = starttime.elapsed().as_micros() as u64;
        info!("Average atomic calibration cycle time: {}us", dt / CAL_TEST_MAX_ITER as u64);
    }

    true
}

//-------------------------------------------------------------------------------------------------------------------------------------
// Setup test
// Connect, upload A2l, check EPK, check id, ...
pub async fn test_setup(
    tcp: bool,
    dest_addr: std::net::SocketAddr,
    local_addr: std::net::SocketAddr,
    load_a2l: bool,
    upload_a2l: bool,
) -> (XcpClient, Arc<parking_lot::lock_api::Mutex<parking_lot::RawMutex, DaqDecoder>>) {
    debug!("Test setup");

    //-------------------------------------------------------------------------------------------------------------------------------------
    // Create xcp_client and connect the XCP server
    info!("XCP CONNECT");

    info!("  dest_addr: {}", dest_addr);
    info!("  local_addr: {}", local_addr);
    let mut xcp_client = XcpClient::new(tcp, dest_addr, local_addr); // false = UDP
    let daq_decoder: Arc<parking_lot::lock_api::Mutex<parking_lot::RawMutex, DaqDecoder>> = Arc::new(Mutex::new(DaqDecoder::new()));
    let serv_text_decoder = ServTextDecoder::new();
    xcp_client
        .connect(0, Arc::clone(&daq_decoder), serv_text_decoder)
        .await
        .expect("Failed to connect to XCP server");

    //-------------------------------------------------------------------------------------------------------------------------------------
    // Check command timeout using a command CC_NOP (non standard) without response
    debug!("Check command timeout handling");
    let res = xcp_client.command(xcp::CC_NOP).await; // Check unknown command
    match res {
        Ok(_) => panic!("Should timeout"),
        Err(e) => {
            e.downcast_ref::<XcpError>()
                .map(|e| {
                    debug!("XCP error code ERROR_CMD_TIMEOUT as expected: {}", e);
                    assert_eq!(e.get_error_code(), xcp::ERROR_CMD_TIMEOUT);
                })
                .or_else(|| {
                    panic!("CC_NOP should return XCP error code ERROR_CMD_TIMEOUT");
                });
        }
    }

    //-------------------------------------------------------------------------------------------------------------------------------------
    // Check error responses with CC_SYNC
    debug!("Check error response handling");
    let res = xcp_client.command(xcp::CC_SYNC).await; // Check unknown command
    match res {
        Ok(_) => panic!("Should return error"),
        Err(e) => {
            e.downcast_ref::<XcpError>()
                .map(|e| {
                    assert_eq!(e.get_error_code(), xcp::CRC_CMD_SYNCH);
                    debug!("XCP error code CRC_CMD_SYNCH from SYNC as expected: {}", e);
                })
                .or_else(|| {
                    panic!("Should return XCP error from SYNC command");
                });
        }
    }

    //-------------------------------------------------------------------------------------------------------------------------------------
    // Upload A2L file and check EPK
    if load_a2l {
        // Upload A2L file from XCP server and create the xcp_client registry
        if upload_a2l {
            let mut reg = Registry::new();
            let a2l_path = std::path::Path::new("test").with_extension("a2l");
            xcp_client.upload_a2l_into_registry(&a2l_path, &mut reg).await.unwrap();
            xcp_client.set_registry(reg);
            info!("A2L file uploaded from XCP server into registry from {:?}", a2l_path);
        }
        // Load the A2L file from file
        else {
            // Send XCP GET_ID GET_ID IDT_ASAM_NAME to obtain the A2L filename
            info!("XCP GET_ID IDT_ASAM_NAME");
            let res = xcp_client.get_id(IDT_ASAM_NAME).await;
            let a2l_name = match res {
                Ok((_, Some(id))) => id,
                Err(e) => {
                    panic!("GET_ID failed, Error: {}", e);
                }
                _ => {
                    panic!("Empty string");
                }
            };
            info!("A2l file name from GET_ID IDT_ASAM_NAME = {}", a2l_name);

            // Check A2l file exists on disk
            let a2l_filename = format!("{}.a2l", a2l_name);
            let info = std::fs::metadata(&a2l_filename).unwrap();
            trace!("A2l file info: {:#?}", info);
            assert!(info.len() > 0);

            // Load A2L file from file not implemented yet
            unimplemented!("Load A2L file from file not implemented yet");
        }

        // Check EPK
        // EPK addr is always in segment 0 which is  0x80000000 and len is hardcoded to 8 ???   // @@@@ TODO
        let res = xcp_client.short_upload(0x80000000, 0, 8).await;
        let resp: Vec<u8> = match res {
            Err(e) => {
                panic!("Could not upload EPK, Error: {}", e);
            }
            Ok(r) => r,
        };
        let epk = resp[1..=8].to_vec();
        let epk_string = String::from_utf8(epk.clone()).unwrap();
        info!("Upload EPK = {} {:?}", epk_string, epk);
        debug!("A2l EPK = {}", xcp_client.get_epk().unwrap());
        //assert_eq!(epk_string.as_str(), xcp_client.a2l_epk().unwrap(), "EPK mismatch"); // @@@@ TODO
    }

    // Check the DAQ clock
    debug!("Start clock test");
    let t10 = Instant::now();
    let t1 = xcp_client.get_daq_clock().await.unwrap();
    tokio::time::sleep(Duration::from_micros(1000)).await;
    let t20 = t10.elapsed();
    let t2 = xcp_client.get_daq_clock().await.unwrap();
    let dt12 = (t2 - t1) / 1000;
    let dt120 = t20.as_micros() as u64;
    let diff = dt120 as i64 - dt12 as i64;
    if !(-100..=100).contains(&diff) {
        warn!("DAQ clock too inaccurate");
        warn!("t1 = {}ns, t2 = {}ns, dt={}us / elapsed={}us diff={}", t1, t2, dt12, dt120, diff);
    }
    //assert!(dt12 > dt120 - 400, "DAQ clock too slow");
    //assert!(dt12 < dt120 + 400, "DAQ clock too fast");

    (xcp_client, daq_decoder)
}

// Test shutdown
// Disconnect from XCP server
pub async fn test_disconnect(xcp_client: &mut XcpClient) {
    let mut error_state = false;

    // Disconnect from XCP server
    info!("Disconnect from XCP server");
    xcp_client
        .disconnect()
        .await
        .map_err(|e| {
            error_state = true;
            error!("Disconnect failed: {:?}", e);
        })
        .ok();
}

//-------------------------------------------------------------------------------------------------------------------------------------

pub async fn test_executor(
    tcp: bool,
    dest_addr: std::net::SocketAddr,
    local_addr: std::net::SocketAddr,
    test_mode_cal: TestModeCal,
    test_mode_daq: TestModeDaq,
    daq_test_duration_ms: u64,
) {
    let load_a2l = test_mode_cal != TestModeCal::None || test_mode_daq != TestModeDaq::None;
    let (mut xcp_client, daq_decoder) = test_setup(tcp, dest_addr, local_addr, load_a2l, true).await;

    //-------------------------------------------------------------------------------------------------------------------------------------
    //  Daq test

    if test_mode_daq == TestModeDaq::Daq {
        info!("Start DAQ test");
        let (test_ok, actual_duration_ms) = test_daq(&mut xcp_client, test_mode_daq, daq_test_duration_ms).await;
        let packets_lost = DAQ_PACKETS_LOST.load(std::sync::atomic::Ordering::SeqCst);
        let counter_errors = DAQ_COUNTER_ERRORS.load(std::sync::atomic::Ordering::SeqCst);
        info!(
            "DAQ test done, duration = {}ms, packet_loss = {}, counter_error = {}",
            actual_duration_ms, packets_lost, counter_errors
        );

        if test_ok {
            let d = daq_decoder.lock();
            info!("Daq test results:");
            info!("  cycles = {}", d.daq_events);
            info!("  events = {}", d.tot_events);
            info!("  bytes = {}", d.tot_bytes);
            info!("  events/s = {:.0}", d.tot_events as f64 / actual_duration_ms as f64 * 1000.0);
            info!("  datarate = {:.3} MByte/s", (d.tot_bytes as f64) / 1000.0 / actual_duration_ms as f64);
            if d.packets_lost > 0 {
                warn!("  packets lost = {}", d.packets_lost);
            }
            if d.packets_lost > 0 {
                warn!("  counter errors = {}", d.counter_errors);
            }
            let avg_cycletime_us = (actual_duration_ms as f64 * 1000.0) / d.daq_events as f64;
            info!("  average task cycle time = {:.1}us", avg_cycletime_us,);
            assert_ne!(d.tot_events, 0);
            assert!(d.daq_events > 0);
            assert_eq!(d.odt_max, 0);
            assert_eq!(d.counter_errors, 0);
            assert_eq!(d.packets_lost, 0);
        } else {
            error!("Daq test failed");
        }
    }

    //-------------------------------------------------------------------------------------------------------------------------------------
    // Calibration test

    if test_mode_cal == TestModeCal::Cal {
        //
        let error = test_calibration(&mut xcp_client).await;
        if error {
            info!("Calibration test passed");
        } else {
            error!("Calibration test failed");
        }
    }

    test_disconnect(&mut xcp_client).await;
}
