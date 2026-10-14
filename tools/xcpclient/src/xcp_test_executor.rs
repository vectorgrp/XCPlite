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
use xcp_registry::*;

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

const DAQ_TEST_DURATION_MS: u64 = 10000; // Duration of the DAQ test
const DAQ_CMD_CYCLE_TIME_MS: u64 = 50; // GET_DAQ_CLOCK cycle time during the DAQ test in mode DaqCmd
const DAQ_CLOCK_WINDOWS: usize = 10; // Number of time windows for the clock offset and drift estimation

//------------------------------------------------------------------------
// Test statistics

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
    pub daq0_events: u32,
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
            daq0_events: 0,
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
        self.daq0_events = 0;
        self.max_counter = 0;
        self.last_counter = 0;
    }

    // Set timestamp resolution
    fn set_daq_properties(&mut self, timestamp_resolution: u64, daq_header_size: u8) {
        self.timestamp_resolution = timestamp_resolution;
        assert_eq!(daq_header_size, 4);
    }

    // Handle incoming DAQ DTOs from XCP server
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

        // Hardcoded decoding of daq 0 counter data
        if daq == 0 && odt == 0 && data.len() >= 2 {
            let o = 0;

            // Check counter (+0)
            let counter = (data[o] as u32) | ((data[o + 1] as u32) << 8);

            // Check counter is incrementing
            if self.daq0_events != 0 && counter != self.last_counter + 1 && counter != 0 && daq != 0 {
                let count = DAQ_COUNTER_ERRORS.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                trace!("DAQ_COUNTER_ERRORS: {} daq={} {} -> {} ", count, daq, self.last_counter, counter,);
            }
            self.last_counter = counter;

            trace!("DAQ: daq = {}, odt = {} timestamp = {} counter={})", daq, odt, timestamp_raw, counter,);
            self.daq0_events += 1;
        }

        self.tot_events += 1;
    }
}

//-----------------------------------------------------------------------
// Execute tests

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum TestModeDaq {
    None,
    Daq,
    DaqCmd,
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum TestModeCal {
    None,
    Cal,
}

// Print latency statistics (min, median, avg, p90, p99, max) of samples in ns
fn print_latency_statistics(name: &str, samples: &mut [u64]) {
    if samples.is_empty() {
        warn!("{name}: no samples");
        return;
    }
    samples.sort_unstable();
    let n = samples.len();
    let percentile = |p: f64| samples[((p * n as f64) as usize).min(n - 1)] as f64 / 1000.0;
    let avg = samples.iter().sum::<u64>() as f64 / n as f64 / 1000.0;
    info!(
        "{name} ({n} samples): min = {:.1}us, median = {:.1}us, avg = {:.1}us, p90 = {:.1}us, p99 = {:.1}us, max = {:.1}us",
        samples[0] as f64 / 1000.0,
        percentile(0.5),
        avg,
        percentile(0.9),
        percentile(0.99),
        samples[n - 1] as f64 / 1000.0
    );
}

// GET_DAQ_CLOCK sample
// Local times in ns since test start (client monotonic clock), daq in ns (server DAQ clock)
#[derive(Debug, Copy, Clone)]
struct DaqClockSample {
    t_send: i64, // Local time before the command is sent
    t_recv: i64, // Local time after the response is received
    daq: i64,    // Server DAQ clock value returned by GET_DAQ_CLOCK
}

// Result of the GET_DAQ_CLOCK sample analysis
struct DaqClockAnalysis {
    drift: f64,            // Drift of the server clock relative to the client clock (ns/ns)
    windows: usize,        // Number of time windows used for the line fit
    constant_max: f64,     // Upper bound for the constant part of the delay in ns
    delays: Vec<u64>,      // Drift compensated delay send to clock sampling relative to the best case sample in ns
    round_trips: Vec<u64>, // Round trip times in ns
}

// Analyze the GET_DAQ_CLOCK samples
//
// d = daq - t_send = offset(t) + delay, where delay is the time from sending the command until the server samples its clock.
// This is what a client experiences, which pairs the server clock value with its local send time for clock synchronization.
// offset(t) = offset + drift*t is estimated by a least squares line fit through the minima of d in DAQ_CLOCK_WINDOWS time windows,
// then shifted down to the lower envelope of all d. The drift of the two clocks can not be neglected at us resolution: 10ppm are 100us in 10s.
// The constant part of the delay can not be separated from the offset with one way measurements, so the compensated delay is relative to
// the best case sample (jitter). Its upper bound is given by the response: daq - t_recv <= offset(t) - (constant delay).
fn analyze_daq_clock(samples: &[DaqClockSample]) -> Option<DaqClockAnalysis> {
    if samples.len() < 2 {
        return None;
    }

    // Use values relative to the first sample, to keep the f64 calculations precise
    let t_ref = samples[0].t_send;
    let d_ref = samples[0].daq - samples[0].t_send;
    let t = |s: &DaqClockSample| (s.t_send - t_ref) as f64;
    let d = |s: &DaqClockSample| (s.daq - s.t_send - d_ref) as f64;

    // Minimum of d in each time window
    let span = t(&samples[samples.len() - 1]);
    let mut minima: Vec<Option<(f64, f64)>> = vec![None; DAQ_CLOCK_WINDOWS];
    for s in samples {
        let w = if span > 0.0 {
            ((t(s) / span * DAQ_CLOCK_WINDOWS as f64) as usize).min(DAQ_CLOCK_WINDOWS - 1)
        } else {
            0
        };
        if minima[w].is_none_or(|m| d(s) < m.1) {
            minima[w] = Some((t(s), d(s)));
        }
    }
    let points: Vec<(f64, f64)> = minima.into_iter().flatten().collect();

    // Least squares line fit through the window minima
    let n = points.len() as f64;
    let mean_t = points.iter().map(|p| p.0).sum::<f64>() / n;
    let mean_d = points.iter().map(|p| p.1).sum::<f64>() / n;
    let var_t = points.iter().map(|p| (p.0 - mean_t) * (p.0 - mean_t)).sum::<f64>();
    let cov_td = points.iter().map(|p| (p.0 - mean_t) * (p.1 - mean_d)).sum::<f64>();
    let drift = if points.len() >= 2 && var_t > 0.0 { cov_td / var_t } else { 0.0 };
    let line = |t: f64| mean_d + drift * (t - mean_t);

    // Shift the line down to the lower envelope of d
    let shift = samples.iter().map(|s| d(s) - line(t(s))).fold(f64::INFINITY, f64::min);
    let offset = |s: &DaqClockSample| line(t(s)) + shift;

    // Drift compensated delay relative to the best case sample
    let delays: Vec<u64> = samples.iter().map(|s| (d(s) - offset(s)).max(0.0).round() as u64).collect();

    // Upper bound for the constant part of the delay: min(offset(t) - (daq - t_recv))
    let constant_max = samples.iter().map(|s| offset(s) - (s.daq - s.t_recv - d_ref) as f64).fold(f64::INFINITY, f64::min);

    // Round trip times
    let round_trips: Vec<u64> = samples.iter().map(|s| (s.t_recv - s.t_send) as u64).collect();

    Some(DaqClockAnalysis {
        drift,
        windows: points.len(),
        constant_max,
        delays,
        round_trips,
    })
}

// Analyze the GET_DAQ_CLOCK samples and print the statistics
fn print_daq_clock_statistics(samples: &[DaqClockSample]) {
    let Some(mut a) = analyze_daq_clock(samples) else {
        warn!("GET_DAQ_CLOCK: not enough samples ({})", samples.len());
        return;
    };
    info!(
        "GET_DAQ_CLOCK: clock drift server to client = {:.2}ppm (fit through {} window minima), constant delay send to clock sampling <= {:.1}us",
        a.drift * 1e6,
        a.windows,
        a.constant_max / 1000.0
    );
    print_latency_statistics("GET_DAQ_CLOCK round trip", &mut a.round_trips);
    print_latency_statistics("GET_DAQ_CLOCK send to clock sampling, drift compensated, relative to best case", &mut a.delays);
}

// Perform DAQ test
// In mode DaqCmd, a GET_DAQ_CLOCK command is sent every DAQ_CMD_CYCLE_TIME_MS during the measurement,
// the round trip and the delay from sending the command until the server clock is sampled are analyzed
// Returns actual duration and bytes per event
// 0,0 on error
pub async fn test_daq(xcp_client: &mut XcpClient, daq_test_mode: TestModeDaq) -> (bool, u32) {
    info!("Start daq test");

    let mut error = false;

    xcp_client.create_measurement_object("counter").expect("Failed to create measurement object 'counter'");
    for i in 1..8 {
        let name = format!("task_{i}.counter");
        let res = xcp_client.create_measurement_object(&name);
        if res.is_none() {
            warn!("Failed to create measurement object '{name}'");
        }
    }

    xcp_client.start_measurement().await.expect("Failed to start measurement");

    // Test for given time
    // Every 2ms check if measurement is still ok
    // Break on error
    let starttime = Instant::now();
    let mut cmd_last_time = starttime;
    let mut daq_clock_samples: Vec<DaqClockSample> = Vec::new();
    loop {
        if starttime.elapsed().as_millis() > DAQ_TEST_DURATION_MS as u128 {
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

        if daq_test_mode == TestModeDaq::DaqCmd && cmd_last_time.elapsed() >= Duration::from_millis(DAQ_CMD_CYCLE_TIME_MS) {
            // Send a GET_DAQ_CLOCK command, record the local send and receive time and the server clock value
            // The send time is taken in send_command directly before the socket send
            cmd_last_time = Instant::now();
            match xcp_client.get_daq_clock().await {
                Ok(daq) => {
                    let t_recv = Instant::now();
                    let t_send = xcp_client.last_command_send_time().unwrap_or(cmd_last_time);
                    daq_clock_samples.push(DaqClockSample {
                        t_send: t_send.duration_since(starttime).as_nanos() as i64,
                        t_recv: t_recv.duration_since(starttime).as_nanos() as i64,
                        daq: daq as i64,
                    });
                }
                Err(e) => {
                    error!("GET_DAQ_CLOCK during DAQ failed: {}", e);
                    error = true;
                    break;
                }
            }
        }

        tokio::time::sleep(Duration::from_micros(2000)).await;
    }
    let duration_ms = starttime.elapsed().as_millis().try_into().unwrap();
    if daq_test_mode == TestModeDaq::DaqCmd {
        print_daq_clock_statistics(&daq_clock_samples);
    }

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
        if v != 2048 && v != 1024 && v != 512 {
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
        if v != 2048 && v != 1024 && v != 512 {
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
        if v != 2048 && v != 1024 && v != 512 {
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
// Setup test (XCP on ETH)
// Connect, upload A2l, check EPK, check id, ...
pub async fn test_setup(
    protocol: &'static str,
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
    let mut xcp_client = XcpClient::new(protocol, dest_addr, local_addr, 0);
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

    let t0 = Instant::now();
    let daq_t1 = xcp_client.get_daq_clock().await.unwrap();
    let t1 = Instant::now();

    tokio::time::sleep(Duration::from_micros(10000)).await;

    let t2 = Instant::now();
    let daq_t2 = xcp_client.get_daq_clock().await.unwrap();
    let t3 = Instant::now();

    // Third command immediately after the second, to distinguish wake-up latency after idle from steady state latency
    let daq_t3 = xcp_client.get_daq_clock().await.unwrap();
    let t4 = Instant::now();

    // Command round trip latencies
    let lat1 = t1 - t0;
    let lat2 = t3 - t2;
    let lat3 = t4 - t3;

    // The server samples its clock somewhere within each round trip, use the round trip midpoints as host reference
    let host_dt = ((t2 - t0) + (t3 - t1)).as_nanos() as i64 / 2;
    let daq_dt = daq_t2 as i64 - daq_t1 as i64;
    let diff = daq_dt - host_dt;
    info!(
        "GET_DAQ_CLOCK: latency1 = {}us, latency2 = {}us, latency3 = {}us, daq_dt = {daq_dt}ns, host_dt = {host_dt}ns, diff = {diff}ns",
        lat1.as_micros(),
        lat2.as_micros(),
        lat3.as_micros()
    );

    // latency2 is excluded, it includes the wake-up latency after the idle sleep
    if lat1.as_micros() > 500 || lat3.as_micros() > 500 {
        warn!("GET_DAQ_CLOCK high latency (>500us)!");
    }
    if daq_t3 <= daq_t2 {
        warn!("GET_DAQ_CLOCK not monotonic: daq_t2 = {daq_t2}, daq_t3 = {daq_t3}");
    }
    if diff.abs() > 500_000 {
        warn!("GET_DAQ_CLOCK inaccurate (>500us deviation)!");
    }

    (xcp_client, daq_decoder)
}

// Test shutdown
// Disconnect from XCP server, returns false if the disconnect failed
pub async fn test_disconnect(xcp_client: &mut XcpClient) -> bool {
    info!("Disconnect from XCP server");
    match xcp_client.disconnect().await {
        Ok(_) => true,
        Err(e) => {
            error!("Disconnect failed: {:?}", e);
            false
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------------------------

// Returns true if all tests passed
pub async fn test_executor(
    protocol: &'static str,
    dest_addr: std::net::SocketAddr,
    local_addr: std::net::SocketAddr,
    test_mode_cal: TestModeCal,
    test_mode_daq: TestModeDaq,
) -> bool {
    let mut all_ok = true;
    let load_a2l = test_mode_cal != TestModeCal::None || test_mode_daq != TestModeDaq::None;
    let (mut xcp_client, daq_decoder) = test_setup(protocol, dest_addr, local_addr, load_a2l, true).await;

    //-------------------------------------------------------------------------------------------------------------------------------------
    //  Daq test

    if test_mode_daq == TestModeDaq::Daq || test_mode_daq == TestModeDaq::DaqCmd {
        info!("Start DAQ test");
        let (test_ok, actual_duration_ms) = test_daq(&mut xcp_client, test_mode_daq).await;
        let packets_lost = DAQ_PACKETS_LOST.load(std::sync::atomic::Ordering::SeqCst);
        let counter_errors = DAQ_COUNTER_ERRORS.load(std::sync::atomic::Ordering::SeqCst);
        info!(
            "DAQ test done, duration = {}ms, packet_loss = {}, counter_error = {}",
            actual_duration_ms, packets_lost, counter_errors
        );

        if test_ok {
            let d = daq_decoder.lock();
            info!("Daq test results:");
            info!("  cycles = {}", d.daq0_events);
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
            let avg_cycletime_us = (actual_duration_ms as f64 * 1000.0) / d.daq0_events as f64;
            info!("  average task cycle time = {:.1}us", avg_cycletime_us,);
            assert_ne!(d.tot_events, 0);
            assert!(d.daq0_events > 0);
            assert_eq!(d.odt_max, 0);
            assert_eq!(d.counter_errors, 0);
            assert_eq!(d.packets_lost, 0);
        } else {
            error!("Daq test failed");
            all_ok = false;
        }
    }

    //-------------------------------------------------------------------------------------------------------------------------------------
    // Calibration test

    if test_mode_cal == TestModeCal::Cal {
        //
        let cal_ok = test_calibration(&mut xcp_client).await;
        if cal_ok {
            info!("Calibration test passed");
        } else {
            error!("Calibration test failed");
            all_ok = false;
        }
    }

    if !test_disconnect(&mut xcp_client).await {
        all_ok = false;
    }
    all_ok
}

//-----------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    // Synthetic samples: server clock = offset + (1 + drift) * client clock, known delays send to sampling and sampling to receive
    fn synthetic_samples(drift_ppm: f64, delay_ns: impl Fn(usize) -> i64, back_ns: i64) -> Vec<DaqClockSample> {
        let offset: i64 = 123_456_789_000_000; // Arbitrary server epoch
        (0..200)
            .map(|i| {
                let t_send = i as i64 * 50_000_000; // 50ms cycle, 10s
                let t_sample = t_send + delay_ns(i);
                let daq = offset + (t_sample as f64 * (1.0 + drift_ppm * 1e-6)).round() as i64;
                DaqClockSample {
                    t_send,
                    t_recv: t_sample + back_ns,
                    daq,
                }
            })
            .collect()
    }

    #[test]
    fn test_analyze_daq_clock_drift_and_jitter() {
        // 20ppm drift, constant delay 30us, every 7th sample has 100us additional delay
        let samples = synthetic_samples(20.0, |i| 30_000 + if i % 7 == 0 { 100_000 } else { 0 }, 25_000);
        let a = analyze_daq_clock(&samples).unwrap();
        assert!((a.drift * 1e6 - 20.0).abs() < 0.01, "drift {}ppm", a.drift * 1e6);
        assert_eq!(a.windows, DAQ_CLOCK_WINDOWS);
        // Delays relative to the best case: 0 or 100us, despite 200us drift over 10s
        for (i, d) in a.delays.iter().enumerate() {
            let expected = if i % 7 == 0 { 100_000 } else { 0 };
            assert!((*d as i64 - expected).abs() <= 2, "sample {i}: delay {d}ns, expected {expected}ns");
        }
        // Constant delay bound: constant delay 30us + min back delay 25us
        assert!((a.constant_max - 55_000.0).abs() < 10.0, "constant_max {}ns", a.constant_max);
        assert!(a.round_trips.iter().all(|r| *r >= 55_000));
    }

    #[test]
    fn test_analyze_daq_clock_too_few_samples() {
        let samples = synthetic_samples(0.0, |_| 0, 0);
        assert!(analyze_daq_clock(&samples[..1]).is_none());
    }
}
