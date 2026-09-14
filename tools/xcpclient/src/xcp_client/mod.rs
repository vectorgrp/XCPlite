//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module xcp_client
// Simplified, quick and dirty implementation of an UDP XCP client for integration testing

#![allow(dead_code)] // because of all the unused XCP definitions

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use parking_lot::Mutex;
use std::net::SocketAddr;
use std::sync::Arc;

use tokio::net::{TcpStream, UdpSocket};
use tokio::sync::mpsc;
use tokio::time::Duration;

pub mod xcp;
use xcp::*;

mod types;
pub use types::*;

mod decoder;
pub use decoder::*;

mod a2l;
mod cal;
mod daq;
mod protocol;
mod transport;

//--------------------------------------------------------------------------------------------------------------------------------------------------
// XCP Parameters

pub const CMD_TIMEOUT: Duration = Duration::from_secs(3);

//--------------------------------------------------------------------------------------------------------------------------------------------------
// Type to control the receive task sent over the receive task control channel

#[derive(Debug, Copy, Clone)]
pub struct XcpTaskControl {
    running: bool,
    connected: bool,
}

impl XcpTaskControl {
    #[allow(clippy::new_without_default)]
    pub fn new() -> XcpTaskControl {
        XcpTaskControl { running: false, connected: false }
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// Socket abstraction for UDP and TCP

#[derive(Debug)]
enum XcpSocket {
    Udp(Arc<UdpSocket>),
    Tcp(Arc<TcpStream>),
}

impl XcpSocket {
    async fn send_to(&self, buf: &[u8], addr: SocketAddr) -> Result<usize, std::io::Error> {
        match self {
            XcpSocket::Udp(udp_socket) => {
                // On macOS, sendto() on a UDP socket returns EHOSTUNREACH immediately when
                // the ARP cache is empty, instead of queuing the packet like Linux does.
                // Retry with backoff to allow ARP resolution to complete.
                let mut delay_ms = 50u64;
                loop {
                    match udp_socket.send_to(buf, addr).await {
                        Ok(n) => return Ok(n),
                        Err(e) if e.kind() == std::io::ErrorKind::HostUnreachable && delay_ms <= 400 => {
                            debug!("send_to: EHOSTUNREACH, retrying after {}ms (ARP not yet resolved)", delay_ms);
                            tokio::time::sleep(Duration::from_millis(delay_ms)).await;
                            delay_ms *= 2;
                        }
                        Err(e) => return Err(e),
                    }
                }
            }
            XcpSocket::Tcp(tcp_stream) => {
                // But for now, let's revert to the working approach:
                let mut pos = 0;
                while pos < buf.len() {
                    match tcp_stream.try_write(&buf[pos..]) {
                        Ok(0) => return Err(std::io::Error::new(std::io::ErrorKind::WriteZero, "write zero bytes")),
                        Ok(n) => pos += n,
                        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            tcp_stream.writable().await?;
                        }
                        Err(e) => return Err(e),
                    }
                }
                Ok(buf.len())
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------
// XcpClient

/// XCP client
pub struct XcpClient {
    protocol: &'static str,
    baud_rate: u32,
    bind_addr: SocketAddr,
    dest_addr: SocketAddr,

    // Information from the CONNECT, GET_COMM_MODE_INFO, GET_DAQ_PROCESSOR_INFO and GET_PAG_PROCESSOR_INFO commands
    resources: u8,
    comm_mode_basic: u8,
    max_cto_size: u8,
    max_dto_size: u16,
    protocol_version: u16,
    transport_layer_version: u16,
    comm_mode_optional: u8,
    driver_version: u8,
    max_segments: u8,
    freeze_supported: bool,
    max_events: u16,

    registry: Option<xcp_registry::Registry>,

    timestamp_resolution_ns: u64,
    daq_header_size: u8,

    socket: Option<XcpSocket>,
    receive_task: Option<tokio::task::JoinHandle<()>>,
    rx_cmd_resp: Option<mpsc::Receiver<Vec<u8>>>,
    tx_task_control: Option<mpsc::Sender<XcpTaskControl>>,
    task_control: XcpTaskControl,
    daq_decoder: Option<Arc<Mutex<dyn XcpDaqDecoder>>>,
    ctr: u16,

    calibration_object_list: Vec<XcpClientCalibrationObject>,
    measurement_object_list: Vec<XcpClientMeasurementObject>,

    // Event id used for the DAQ measurement of objects without a fixed event (global variables), None: such objects can not be measured
    default_event: Option<u16>,
}

impl XcpClient {
    //------------------------------------------------------------------------
    // new
    //
    #[allow(clippy::type_complexity)]
    pub fn new(protocol: &'static str, dest_addr: SocketAddr, bind_addr: SocketAddr, baud_rate: u32) -> XcpClient {
        XcpClient {
            protocol,
            bind_addr,
            dest_addr,
            baud_rate,
            socket: None,
            receive_task: None,
            rx_cmd_resp: None,
            tx_task_control: None,
            task_control: XcpTaskControl::new(),
            daq_decoder: None,
            ctr: 0,
            resources: 0,
            comm_mode_basic: 0,
            comm_mode_optional: 0,
            driver_version: 0,
            max_cto_size: 0,
            max_dto_size: 0,
            max_segments: 0,
            max_events: 0,
            freeze_supported: false,
            protocol_version: 0,
            transport_layer_version: 0,
            timestamp_resolution_ns: 1,
            daq_header_size: 4,
            registry: None,
            calibration_object_list: Vec::new(),
            measurement_object_list: Vec::new(),
            default_event: None,
        }
    }

    /// Set the event id used for the DAQ measurement of objects without a fixed event (global variables)
    pub fn set_default_event(&mut self, event: Option<u16>) {
        self.default_event = event;
    }

    pub fn set_registry(&mut self, registry: xcp_registry::Registry) {
        self.registry = Some(registry);
    }

    //------------------------------------------------------------------------
    // Information from the XCP server, valid after connect

    /// COMM_MODE_BASIC from CONNECT (byte order, address granularity, block modes)
    pub fn comm_mode_basic(&self) -> u8 {
        self.comm_mode_basic
    }

    /// Number of events from GET_DAQ_PROCESSOR_INFO, 0 if the server does not support event information
    pub fn max_events(&self) -> u16 {
        self.max_events
    }

    /// Number of calibration segments from GET_PAG_PROCESSOR_INFO, 0 if the server does not support segment information
    pub fn max_segments(&self) -> u8 {
        self.max_segments
    }

    /// Log the XCP protocol information obtained from the server
    pub fn log_connect_info(&self) {
        log::info!("XCP Protocol Information:");
        log::info!("  XCP MAX_CTO = {}", self.max_cto_size);
        log::info!("  XCP MAX_DTO = {}", self.max_dto_size);
        log::info!(
            "  XCP RESOURCES = 0x{:02X} {} {} {} {}",
            self.resources,
            if (self.resources & 0x01) != 0 { "CAL" } else { "" },
            if (self.resources & 0x04) != 0 { "DAQ" } else { "" },
            if (self.resources & 0x10) != 0 { "PGM" } else { "" },
            if (self.resources & 0x40) != 0 { "STM" } else { "" }
        );
        log::info!("  XCP COMM_MODE_BASIC = 0x{:02X}", self.comm_mode_basic);
        log::info!("  XCP COMM_MODE_OPTIONAL = 0x{:02X}", self.comm_mode_optional);
        log::info!("  XCP PROTOCOL_VERSION = 0x{:04X}", self.protocol_version);
        log::info!("  XCP TRANSPORT_LAYER_VERSION = 0x{:04X}", self.transport_layer_version);
        log::info!("  XCP DRIVER_VERSION = 0x{:02X}", self.driver_version);
        log::info!("  XCP MAX_SEGMENTS = {}", self.max_segments);
        log::info!("  XCP FREEZE_SUPPORTED = {}", self.freeze_supported);
        log::info!("  XCP MAX_EVENTS = {}", self.max_events);
    }
}
