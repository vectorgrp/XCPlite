//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module xcp_client
// Simplified, quick and dirty implementation of an UDP XCP client for integration testing

#![allow(dead_code)] // because of all the unused XCP definitions

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use byteorder::{LittleEndian, ReadBytesExt};

use parking_lot::Mutex;
use std::collections::HashMap;
use std::error::Error;
use std::io::Cursor;
use std::io::Write;
use std::net::SocketAddr;
use std::sync::Arc;

use tokio::net::{TcpStream, UdpSocket};
use tokio::select;
use tokio::sync::mpsc::{self, Receiver, Sender};
use tokio::time::{Duration, timeout};

pub mod xcp;
use xcp::*;
use xcp_lite::registry::*;

use crate::bin_reader::bin_format::CalSegDescriptor;
use crate::bin_reader::bin_format::EventDescriptor;
use crate::bin_reader::write_bin_file;
use crate::bin_reader::write_hex_file;

//--------------------------------------------------------------------------------------------------------------------------------------------------
// XCP Parameters

pub const CMD_TIMEOUT: Duration = Duration::from_secs(3);

//--------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------
// CalibrationObject
// Describes a calibration object with name, address, type, limits and caches it actual value

// Measurement and calibration object attributes

#[derive(Debug, Clone, Copy)]
pub struct A2lAddr {
    pub ext: u8,
    pub addr: u32,
    pub event: Option<u16>,
}

impl std::fmt::Display for A2lAddr {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if let Some(event) = self.event {
            write!(f, "{}:0x{:08X} event {}", self.ext, self.addr, event)
        } else {
            write!(f, "{}:0x{:08X}", self.ext, self.addr)
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub enum A2lTypeEncoding {
    Signed,
    Unsigned,
    Float,
    Blob,
}

impl From<&McValueType> for A2lTypeEncoding {
    fn from(value_type: &McValueType) -> A2lTypeEncoding {
        match value_type {
            McValueType::Bool | McValueType::Ubyte | McValueType::Uword | McValueType::Ulong | McValueType::Ulonglong => A2lTypeEncoding::Unsigned,
            McValueType::Sbyte | McValueType::Sword | McValueType::Slong | McValueType::Slonglong => A2lTypeEncoding::Signed,
            McValueType::Float32Ieee | McValueType::Float64Ieee => A2lTypeEncoding::Float,
            _ => A2lTypeEncoding::Blob,
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct A2lType {
    pub size: usize,
    pub encoding: A2lTypeEncoding,
}

impl std::fmt::Display for A2lType {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self.encoding {
            A2lTypeEncoding::Signed => write!(f, "{} byte signed", self.size),
            A2lTypeEncoding::Unsigned => write!(f, "{} byte unsigned", self.size),
            A2lTypeEncoding::Float => write!(f, "{} byte float", self.size),
            A2lTypeEncoding::Blob => write!(f, "{} byte blob", self.size),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct A2lLimits {
    pub lower: f64,
    pub upper: f64,
}

#[derive(Debug, Clone, Copy)]
pub struct XcpCalibrationObjectHandle(usize);

impl XcpCalibrationObjectHandle {
    pub fn get_name(self, xcp_client: &mut XcpClient) -> &str {
        xcp_client.get_calibration_object(self).get_name()
    }
    pub fn get_a2l_addr(self, xcp_client: &mut XcpClient) -> A2lAddr {
        xcp_client.get_calibration_object(self).get_a2l_addr()
    }
    pub fn get_a2l_type(self, xcp_client: &mut XcpClient) -> A2lType {
        xcp_client.get_calibration_object(self).get_a2l_type()
    }
}

#[derive(Debug)]
pub struct XcpClientCalibrationObject {
    name: String,
    a2l_addr: A2lAddr,
    get_type: A2lType,
    a2l_limits: A2lLimits,
    value: Vec<u8>,
}

impl XcpClientCalibrationObject {
    pub fn new(name: &str, a2l_addr: A2lAddr, get_type: A2lType, a2l_limits: A2lLimits) -> XcpClientCalibrationObject {
        XcpClientCalibrationObject {
            name: name.to_string(),
            a2l_addr,
            get_type,
            a2l_limits,
            value: Vec::new(),
        }
    }

    pub fn get_name(&self) -> &str {
        &self.name
    }

    pub fn get_a2l_type(&self) -> A2lType {
        self.get_type
    }

    pub fn get_a2l_addr(&self) -> A2lAddr {
        self.a2l_addr
    }

    pub fn set_value(&mut self, bytes: &[u8]) {
        self.value = bytes.to_vec();
    }

    pub fn get_value(&mut self) -> &[u8] {
        &self.value
    }

    pub fn get_value_u64(&self) -> u64 {
        let mut value = 0u64;
        for i in (0..self.get_type.size).rev() {
            value <<= 8;
            value += self.value[i] as u64;
        }
        value
    }

    pub fn get_value_i64(&self) -> i64 {
        let size: usize = self.get_type.size;
        let mut value = 0;
        if self.value[size - 1] & 0x80 != 0 {
            value = -1;
        }
        for i in (0..size).rev() {
            value <<= 8;
            assert!(value & 0xFF == 0);
            value |= self.value[i] as i64;
        }
        value
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------
// MeasurementObject
// Describes a measurement object with name, address, type and event

#[derive(Debug, Copy, Clone)]
pub struct XcpMeasurementObjectHandle(pub usize);

impl XcpMeasurementObjectHandle {
    pub fn get_name(self, xcp_client: &mut XcpClient) -> &str {
        xcp_client.get_measurement_object(self).get_name()
    }
    pub fn get_a2l_addr(self, xcp_client: &mut XcpClient) -> A2lAddr {
        xcp_client.get_measurement_object(self).get_a2l_addr()
    }
    pub fn get_a2l_type(self, xcp_client: &mut XcpClient) -> A2lType {
        xcp_client.get_measurement_object(self).get_a2l_type()
    }
}

#[derive(Debug, Clone)]
pub struct XcpClientMeasurementObject {
    name: String,
    pub a2l_addr: A2lAddr,
    pub a2l_type: A2lType,
    pub daq: u16,
    pub odt: u8,
    pub offset: u16,
}

impl XcpClientMeasurementObject {
    pub fn new(name: &str, a2l_addr: A2lAddr, a2l_type: A2lType) -> XcpClientMeasurementObject {
        XcpClientMeasurementObject {
            name: name.to_string(),
            a2l_addr,
            a2l_type,
            daq: 0,
            odt: 0,
            offset: 0,
        }
    }

    pub fn get_name(&self) -> &str {
        &self.name
    }
    pub fn get_a2l_addr(&self) -> A2lAddr {
        self.a2l_addr
    }
    pub fn get_a2l_type(&self) -> A2lType {
        self.a2l_type
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------
// Decoder traits for XCP messages

//--------------------------------------------------------------------------------------------------------------------------------------------------
// Text decoder trait for XCP SERV_TEXT messages

pub trait XcpTextDecoder {
    /// Handle incomming SERV_TEXT data from XCP server
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

//--------------------------------------------------------------------------------------------------------------------------------------------------
// DAQ decoder trait for XCP DAQ messages

/// DAQ information
/// Describes a single ODT entry
#[derive(Debug)]
pub struct OdtEntry {
    pub name: String,
    pub a2l_type: A2lType,
    pub a2l_addr: A2lAddr,
    pub offset: u16, // offset from data start, not including daq header and timestamp
}

pub trait XcpDaqDecoder {
    /// Handle incomming DAQ packet from XCP server
    /// Transport layer header has been stripped
    fn decode(&mut self, lost: u32, data: &[u8]);

    /// Measurement start
    /// Decoding information: ODT entry table and 64 bit start timestamp
    fn start(&mut self, odt_entries: Vec<Vec<OdtEntry>>, timestamp_raw64: u64);

    /// Measurement stop
    fn stop(&mut self) {}

    /// Set measurement timestamp resolution in ns per raw timestamp tick and DAQ header size (2 (ODTB/DAQB or 4 (ODTB,_,DAQW))
    fn set_daq_properties(&mut self, timestamp_resolution: u64, daq_header_size: u8);

    /// Get the event count
    fn get_event_count(&self) -> usize {
        0
    }

    /// Get the byte count
    fn get_byte_count(&self) -> usize {
        0
    }
}

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
            XcpSocket::Udp(udp_socket) => udp_socket.send_to(buf, addr).await,
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
    tcp: bool,
    // Information from connect and get_comm_mode_info commands
    pub resources: u8,
    pub comm_mode_basic: u8,
    pub max_cto_size: u8,
    pub max_dto_size: u16,
    pub protocol_version: u16,
    pub transport_layer_version: u16,
    pub comm_mode_optional: u8,
    pub driver_version: u8,
    pub max_segments: u8,
    pub freeze_supported: bool,
    pub max_events: u16,

    pub registry: Option<xcp_lite::registry::Registry>,

    timestamp_resolution_ns: u64,
    daq_header_size: u8,

    bind_addr: SocketAddr,
    dest_addr: SocketAddr,

    socket: Option<XcpSocket>,
    receive_task: Option<tokio::task::JoinHandle<()>>,
    rx_cmd_resp: Option<mpsc::Receiver<Vec<u8>>>,
    tx_task_control: Option<mpsc::Sender<XcpTaskControl>>,
    task_control: XcpTaskControl,
    daq_decoder: Option<Arc<Mutex<dyn XcpDaqDecoder>>>,
    ctr: u16,

    calibration_object_list: Vec<XcpClientCalibrationObject>,
    measurement_object_list: Vec<XcpClientMeasurementObject>,
}

impl XcpClient {
    //------------------------------------------------------------------------
    // new
    //
    #[allow(clippy::type_complexity)]
    pub fn new(tcp: bool, dest_addr: SocketAddr, bind_addr: SocketAddr) -> XcpClient {
        XcpClient {
            tcp,
            bind_addr,
            dest_addr,
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
        }
    }

    pub fn set_registry(&mut self, registry: xcp_lite::registry::Registry) {
        self.registry = Some(registry);
    }

    //------------------------------------------------------------------------
    // Helper function for socket receive
    async fn socket_receive(socket: &XcpSocket, buf: &mut [u8]) -> Result<(usize, Option<SocketAddr>), std::io::Error> {
        match socket {
            XcpSocket::Udp(udp_socket) => udp_socket.recv_from(buf).await.map(|(size, addr)| (size, Some(addr))),
            XcpSocket::Tcp(tcp_stream) => {
                let mut header = [0u8; 4];
                let mut bytes_read = 0;
                while bytes_read < 4 {
                    tcp_stream.readable().await?;
                    match tcp_stream.try_read(&mut header[bytes_read..]) {
                        Ok(n) => {
                            bytes_read += n;
                            if n == 0 {
                                return Err(std::io::Error::new(std::io::ErrorKind::UnexpectedEof, "Connection closed"));
                            }
                        }
                        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            continue;
                        }
                        Err(e) => return Err(e),
                    }
                }

                let len = header[0] as usize + ((header[1] as usize) << 8);
                if len == 0 || len > buf.len() - 4 {
                    return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, format!("Invalid XCP header length: {}", len)));
                }
                buf[0..4].copy_from_slice(&header);
                let mut bytes_read = 0;
                while bytes_read < len {
                    tcp_stream.readable().await?;
                    match tcp_stream.try_read(&mut buf[4 + bytes_read..4 + len]) {
                        Ok(n) => {
                            bytes_read += n;
                            if n == 0 {
                                return Err(std::io::Error::new(std::io::ErrorKind::UnexpectedEof, "Connection closed"));
                            }
                        }
                        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            continue;
                        }
                        Err(e) => return Err(e),
                    }
                }

                Ok((len + 4, None))
            }
        }
    }

    //------------------------------------------------------------------------
    // receiver task
    // Handle incoming data from XCP server
    async fn receive_task(
        socket: XcpSocket,
        tx_resp: Sender<Vec<u8>>,
        mut rx_daq_decoder: Receiver<XcpTaskControl>,
        decode_serv_text: impl XcpTextDecoder,
        decode_daq: Arc<Mutex<impl XcpDaqDecoder>>,
    ) -> Result<(), Box<dyn Error>> {
        let mut ctr_last: u16 = 0;
        let mut ctr_first: bool = true;
        let mut ctr_lost: u32 = 0;

        let mut buf: [u8; 8000] = [0; 8000];
        let mut task_control: Option<XcpTaskControl> = None;

        loop {
            select! {

                // Handle the data from rx_daq_decoder
                res = rx_daq_decoder.recv() => {
                    match res {
                        Some(c) => {
                            debug!("receive_task: task control status changed: connected={} running={}", c.connected, c.running);

                            // Disconnect
                            if !c.connected { // Handle the data from rx_daq_decoder
                                debug!("receive_task: stop, disconnect");
                                return Ok(());
                            }

                            // Start DAQ
                            if c.running {
                                debug!("receive_task: start DAQ");
                                ctr_first = true;
                                ctr_last = 0;
                                ctr_lost = 0;

                            }

                            task_control = Some(c);
                        }
                        None => { // The sender has been dropped
                            debug!("receive_task: stop, channel closed");
                            return Ok(());
                        }
                    }
                } // rx_daq_decoder.recv

                // Handle the data from socket
                res = Self::socket_receive(&socket, &mut buf) => {
                    match res {
                        Ok((size, _addr)) => {
                            // Handle the data from recv_from/read
                            if size == 0 {
                                warn!("receive_task: stop, socket closed");
                                return Ok(());
                            }

                            let mut i: usize = 0;
                            while i < size {
                                // Decode the next transport layer message header in the packet
                                if size < 5 {
                                    return Err(Box::new(XcpError::new(ERROR_TL_HEADER,0)) as Box<dyn Error>);
                                }
                                let len = buf[i] as usize + ((buf[i + 1] as usize) << 8);
                                if len > size - 4 || len == 0 { // Corrupt packet received, not enough data received or no content
                                    return Err(Box::new(XcpError::new(ERROR_TL_HEADER,0)) as Box<dyn Error>);
                                }
                                let ctr = buf[i + 2] as u16 + ((buf[i + 3] as u16) << 8);
                                if ctr_first {
                                    ctr_first = false;
                                } else if ctr != ctr_last.wrapping_add(1) {
                                    ctr_lost += ctr.wrapping_sub(ctr_last) as u32;

                                }
                                ctr_last = ctr;
                                let pid = buf[i + 4];
                                trace!("RX: i = {}, len = {}, pid = {}", i, len, pid,);
                                match pid {
                                    0xFF => {
                                        // Command response
                                        let response = &buf[(i + 4)..(i + 4 + len)];
                                        trace!("receive_task: XCP response = {:?}", response);
                                        tx_resp.send(response.to_vec()).await?;
                                    }
                                    0xFE => {
                                        // Command error response
                                        let response = &buf[(i + 4)..(i + 6)];
                                        trace!("receive_task: XCP error response = {:?}", response);
                                        tx_resp.send(response.to_vec()).await?;
                                    }
                                    0xFD => {
                                        // Event
                                        let event_code = buf[i + 5];
                                        match event_code {
                                            0x07 => { info!("receive_task: stop, SESSION_TERMINATDED"); return Err(Box::new(XcpError::new(ERROR_SESSION_TERMINATION,0)) as Box<dyn Error>); },
                                            _ => warn!("xcp_receive: ignored XCP event = 0x{:0X}", event_code),
                                        }

                                    }
                                    0xFC => {
                                        // Service
                                        let service_code = buf[i + 5];
                                        if service_code == 0x01 {
                                            decode_serv_text.decode(&buf[i + 6..i + len + 4]);
                                        } else {
                                            // Unknown PID
                                            warn!(
                                                "receive_task: ignored unknown service request code = 0x{:0X}",
                                                service_code
                                            );
                                        }
                                    }
                                    _ => {
                                        // Check that we got a DAQ control
                                        if let Some(c) = &task_control {

                                            // Handle DAQ data if DAQ running
                                            if c.running {
                                                let mut m = decode_daq.lock(); // @@@@ TODO Unnecessary mutex ?????
                                                m.decode(ctr_lost, &buf[i + 4..i + 4 + len]);
                                                ctr_lost = 0;
                                            } // running
                                        }
                                    }
                                } // match pid
                                i = i + len + 4;
                            } // while message in packet


                        }
                        Err(e) => {
                            // Handle the error from recv_from/read
                            warn!("receive_task: stop, socket error {}",e);
                            return Err(Box::new(XcpError::new(ERROR_TL_HEADER,0)) as Box<dyn Error>);
                        }
                    }
                } // socket receive
            }
        } // loop
    }

    //------------------------------------------------------------------------
    // XCP command service
    // Send a XCP command and wait for the response
    // @@@@ Must be &mut self because of the mpsc::Receiver
    async fn send_command(&mut self, cmd_bytes: &[u8]) -> Result<Vec<u8>, Box<dyn Error>> {
        //
        // Send command
        let socket = self.socket.as_ref().unwrap();
        socket.send_to(cmd_bytes, self.dest_addr).await?;

        debug!("xcp_command: sent command = {:?}", cmd_bytes);

        // Wait for response channel with timeout
        let res = timeout(CMD_TIMEOUT, self.rx_cmd_resp.as_mut().unwrap().recv()).await; // rx channel
        match res {
            Ok(res) => {
                match res {
                    Some(data) => {
                        trace!("xcp_command: res = {:?}", data);
                        match data[0] {
                            0xFF => {
                                // XCP positive response
                                Ok(data)
                            }
                            0xFE => {
                                // XCP negative response, return error code with XcpError
                                Err(Box::new(XcpError::new(data[1], cmd_bytes[4])) as Box<dyn Error>)
                            }
                            _ => {
                                panic!("xcp_command: bug in receive_task");
                            }
                        }
                    }
                    None => {
                        // Empty response, channel has been closed because receive task terminated
                        info!("xcp_command: receive_task terminated");
                        Err(Box::new(XcpError::new(ERROR_TASK_TERMINATED, cmd_bytes[4])) as Box<dyn Error>)
                    }
                }
            }
            Err(_) => {
                // Timeout, return with XcpError
                Err(Box::new(XcpError::new(ERROR_CMD_TIMEOUT, cmd_bytes[4])) as Box<dyn Error>)
            }
        }
    }

    //------------------------------------------------------------------------
    // Connect/disconnect to server, create receive task

    pub async fn connect<D, T>(&mut self, connect_mode: u8, daq_decoder: Arc<Mutex<D>>, text_decoder: T) -> Result<(), Box<dyn Error>>
    where
        T: XcpTextDecoder + Send + 'static,
        D: XcpDaqDecoder + Send + 'static,
    {
        // Create socket
        let socket = if self.tcp {
            // Create TCP socket and connect
            let stream = TcpStream::connect(self.dest_addr).await?;
            debug!("TCP connection established to {:?}", stream.peer_addr()?);
            debug!("TCP local address: {:?}", stream.local_addr()?);
            // Give the server a moment to set up the connection
            tokio::time::sleep(Duration::from_millis(100)).await;
            XcpSocket::Tcp(Arc::new(stream))
        } else {
            // Create UDP socket
            let udp_socket = UdpSocket::bind(self.bind_addr).await?;
            XcpSocket::Udp(Arc::new(udp_socket))
        };
        self.socket = Some(socket);

        // Spawn a rx task to handle incoming data
        // Hand over the DAQ decoder and the text decoder
        // clone the socket
        // Create channels for command responses and DAQ state control
        debug!("Start RX task");
        {
            let socket = match &self.socket {
                Some(XcpSocket::Udp(udp_sock)) => XcpSocket::Udp(Arc::clone(udp_sock)),
                Some(XcpSocket::Tcp(tcp_stream)) => XcpSocket::Tcp(Arc::clone(tcp_stream)),
                None => unreachable!(),
            };
            let (tx_resp, rx_resp) = mpsc::channel(1);
            self.rx_cmd_resp = Some(rx_resp); // rx XCP command response channel
            let (tx_daq, rx_daq) = mpsc::channel(3);
            self.tx_task_control = Some(tx_daq); // tx XCP DAQ control channel
            let daq_decoder_clone = Arc::clone(&daq_decoder);
            self.receive_task = Some(tokio::spawn(async move {
                let _res = XcpClient::receive_task(socket, tx_resp, rx_daq, text_decoder, daq_decoder_clone).await;
            }));
            tokio::time::sleep(Duration::from_millis(100)).await; // wait for the receive task to start
        }

        // Connect
        debug!("XCP CONNECT");
        let data = self.send_command(XcpCommandBuilder::new(CC_CONNECT).add_u8(connect_mode).build()).await?;
        assert!(data.len() >= 8);
        let resources = data[1];
        let comm_mode_basic = data[2];
        let max_cto_size: u8 = data[3];
        let max_dto_size: u16 = (data[4] as u16) | ((data[5] as u16) << 8);
        let protocol_version: u8 = data[6];
        let transport_layer_version: u8 = data[7];
        self.resources = resources;
        self.comm_mode_basic = comm_mode_basic;
        self.max_cto_size = max_cto_size;
        self.max_dto_size = max_dto_size;
        self.protocol_version = protocol_version as u16;
        self.transport_layer_version = transport_layer_version as u16;
        debug!(
            "XCP CONNECT -> resources=0x{:02X} comm_mode_basic=0x{:02X} max_cto_size={} max_dto_size={} protocol_version=0x{:02X} transport_layer_version=0x{:02X}",
            resources, comm_mode_basic, max_cto_size, max_dto_size, protocol_version, transport_layer_version
        );

        // Get version info
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_VERSION).add_u8(0).build()).await?;
        self.protocol_version = (data[2] as u16) << 8 | data[3] as u16;
        self.transport_layer_version = (data[4] as u16) << 8 | data[5] as u16;
        debug!(
            "XCP GET_VERSION -> protocol_version=0x{:04X} transport_layer_version=0x{:04X}",
            self.protocol_version, self.transport_layer_version
        );

        // Get comm mode info
        if self.comm_mode_basic & 0x80 != 0 {
            let data = self.send_command(XcpCommandBuilder::new(CC_GET_COMM_MODE_INFO).add_u8(0).build()).await?;
            self.comm_mode_optional = data[2]; // Master block mode and interleaved mode not supported yet
            self.driver_version = data[7];
            debug!(
                "XCP GET_COMM_MODE_INFO -> comm_mode_optional=0x{:02X} driver_version=0x{:02X}",
                self.comm_mode_optional, self.driver_version
            );
        }

        // Get calibration page count and freeze support
        let res = self.send_command(XcpCommandBuilder::new(CC_GET_PAGE_PROCESSOR_INFO).add_u8(0).build()).await;
        match res {
            Ok(data) => {
                assert!(data.len() >= 3);
                self.max_segments = data[1];
                self.freeze_supported = (data[2] & 0x01) != 0;
            }
            Err(e) => {
                if e.is::<XcpError>() {
                    if e.downcast_ref::<XcpError>().unwrap().get_error_code() != CRC_CMD_UNKNOWN {
                        warn!("GET_PAGE_PROCESSOR_INFO failed: {}", e);
                    } else {
                        info!("GET_PAGE_PROCESSOR_INFO not supported by server");
                    }
                }

                self.max_segments = 0;
                self.freeze_supported = false;
            }
        }

        // Get DAQ header size and event count
        self.get_daq_processor_info().await?;

        // let data = self.send_command(XcpCommandBuilder::new(CC_GET_DAQ_PROCESSOR_INFO).add_u8(0).build()).await?;
        // self.max_events = (data[4] as u16) | ((data[5] as u16) << 8);

        // Notify the rx task
        self.task_control.connected = true; // the task will end, when it gets connected = false over the XcpControl channel
        self.task_control.running = false;
        self.tx_task_control.as_ref().unwrap().send(self.task_control).await.unwrap();

        assert!(self.is_connected());

        // Initialize DAQ clock
        self.time_correlation_properties().await?; // Set 64 bit response format for GET_DAQ_CLOCK
        self.timestamp_resolution_ns = self.get_daq_resolution_info().await?;

        // Set the DAQ decoder
        daq_decoder.lock().set_daq_properties(self.timestamp_resolution_ns, self.daq_header_size);

        // Keep the the DAQ decoder for measurement start
        self.daq_decoder = Some(daq_decoder);

        Ok(())
    }

    pub fn get_daq_decoder(&mut self) -> Option<Arc<Mutex<dyn XcpDaqDecoder>>> {
        self.daq_decoder.as_ref().map(|d| d.clone())
    }

    //------------------------------------------------------------------------
    pub async fn disconnect(&mut self) -> Result<(), Box<dyn Error>> {
        // Ignore errors and assume disconnected

        // Disconnect
        let _ = self.send_command(XcpCommandBuilder::new(CC_DISCONNECT).add_u8(0).build()).await;

        // Stop XCP client task
        self.task_control.connected = false;
        self.task_control.running = false;
        let _ = self.tx_task_control.as_ref().unwrap().send(self.task_control).await;

        // Make sure receive_task has terminated
        if let Some(receive_task) = self.receive_task.take() {
            let res = receive_task.await;
            if let Err(e) = res {
                error!("{:?}", e);
            }
        }

        Ok(())
    }

    //------------------------------------------------------------------------
    pub fn is_connected(&mut self) -> bool {
        self.task_control.connected
    }

    //------------------------------------------------------------------------
    // Get server identification
    // Returns (size, name) where name is only set if the server returned the name in the response, otherwise the caller must do an upload to get the data
    pub async fn get_id(&mut self, id_type: u8) -> Result<(u32, Option<String>), Box<dyn Error>> {
        assert!( id_type == IDT_VECTOR_ELF_UPLOAD || id_type == IDT_ASAM_UPLOAD || id_type == IDT_ASAM_NAME || id_type == IDT_ASCII || id_type == IDT_ASAM_EPK); // others not supported yet

        let data = self.send_command(XcpCommandBuilder::new(CC_GET_ID).add_u8(id_type).build()).await?;
        assert_eq!(data[0], 0xFF);
        let mode = data[1]; // 0 = data by upload, 1 = data in response

        // Decode size
        let mut size = 0u32;
        for i in (4..8).rev() {
            size = (size << 8) | (data[i] as u32);
        }
        debug!("GET_ID mode={} -> size = {}", id_type, size);

        // Data ready for upload
        if mode == 0 {
            // Upload the result immediately, if size fits in one upload command
            if size < self.max_cto_size as u32 {
                let data = self.upload(size as u8).await?;
                let name = String::from_utf8(data[1..=(size as usize)].to_vec());
                match name {
                    Ok(name) => {
                        debug!("  -> text result = {}", name);
                        Ok((0, Some(name)))
                    }
                    Err(_) => {
                        error!("GET_ID mode={} -> invalid string {:?}", id_type, data);
                        Err(Box::new(XcpError::new(CRC_CMD_SYNTAX, CC_GET_ID)) as Box<dyn Error>)
                    }
                }
            } else {
                // Return size for later upload
                Ok((size, None))
            }
        }
        // Data in response
        else {
            // Decode string
            let name = String::from_utf8(data[8..(size as usize + 8)].to_vec());
            match name {
                Ok(name) => {
                    debug!("  -> text result = {}", name);
                    Ok((0, Some(name)))
                }
                Err(_) => {
                    error!("GET_ID mode={} -> invalid string {:?}", id_type, data);
                    Err(Box::new(XcpError::new(CRC_CMD_SYNTAX, CC_GET_ID)) as Box<dyn Error>)
                }
            }
        }
    }

    //------------------------------------------------------------------------
    // Execute a XCP command with no other parameters
    pub async fn command(&mut self, command_code: u8) -> Result<Vec<u8>, Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(command_code).build()).await
    }

    //------------------------------------------------------------------------
    // calibration segment and page control

    pub async fn get_ecu_page(&mut self, segment: u8) -> Result<u8, Box<dyn Error>> {
        let mode = CAL_PAGE_MODE_ECU;
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_CAL_PAGE).add_u8(mode).add_u8(segment).build()).await?;
        let page = if data[3] != 0 { 1 } else { 0 };
        Ok(page)
    }

    pub async fn get_xcp_page(&mut self, segment: u8) -> Result<u8, Box<dyn Error>> {
        let mode = CAL_PAGE_MODE_XCP;
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_CAL_PAGE).add_u8(mode).add_u8(segment).build()).await?;
        let page = if data[3] != 0 { 1 } else { 0 };
        Ok(page)
    }

    pub async fn set_ecu_page(&mut self, page: u8) -> Result<(), Box<dyn Error>> {
        let mode = CAL_PAGE_MODE_ECU | 0x80; // All segments
        self.send_command(XcpCommandBuilder::new(CC_SET_CAL_PAGE).add_u8(mode).add_u8(0).add_u8(page).build())
            .await?;
        Ok(())
    }

    pub async fn set_xcp_page(&mut self, page: u8) -> Result<(), Box<dyn Error>> {
        let mode = CAL_PAGE_MODE_XCP | 0x80; // All segments
        self.send_command(XcpCommandBuilder::new(CC_SET_CAL_PAGE).add_u8(mode).add_u8(0).add_u8(page).build())
            .await?;
        Ok(())
    }

    //------------------------------------------------------------------------
    // XCP memory access services (calibration and polling of measurement values)

    pub async fn set_mta(&mut self, addr_ext: u8, addr: u32) -> Result<(), Box<dyn Error>> {
        trace!("set_mta addr={}:{:08X}", addr_ext, addr);
        self.send_command(XcpCommandBuilder::new(CC_SET_MTA).add_u8(0).add_u8(0).add_u8(addr_ext).add_u32(addr).build())
            .await?;
        Ok(())
    }

    pub async fn short_download(&mut self, addr: u32, ext: u8, data_bytes: &[u8]) -> Result<(), Box<dyn Error>> {
        let len: u8 = data_bytes.len().try_into().unwrap();
        trace!("short_download addr={}:{:08X},{} data={:?}", ext, addr, len, data_bytes);
        self.send_command(
            XcpCommandBuilder::new(CC_SHORT_DOWNLOAD)
                .add_u8(len)
                .add_u8(0)
                .add_u8(ext)
                .add_u32(addr)
                .add_u8_slice(data_bytes)
                .build(),
        )
        .await?;
        Ok(())
    }
    pub async fn short_upload(&mut self, addr: u32, ext: u8, size: u8) -> Result<Vec<u8>, Box<dyn Error>> {
        trace!("short_upload addr={}:{:08X},{}", ext, addr, size);
        let data = self
            .send_command(XcpCommandBuilder::new(CC_SHORT_UPLOAD).add_u8(size).add_u8(0).add_u8(ext).add_u32(addr).build())
            .await?;
        Ok(data)
    }

    pub async fn upload(&mut self, size: u8) -> Result<Vec<u8>, Box<dyn Error>> {
        trace!("upload size={}", size);
        let data = self.send_command(XcpCommandBuilder::new(CC_UPLOAD).add_u8(size).build()).await?;
        Ok(data)
    }

    pub async fn download(&mut self, data_bytes: &[u8]) -> Result<(), Box<dyn Error>> {
        let n = data_bytes.len();
        trace!("download len={}, data={:?}", n, data_bytes);
        if n >= (self.max_cto_size - 2) as usize {
            return Err(Box::new(XcpError::new(CRC_CMD_SYNTAX, CC_DOWNLOAD)) as Box<dyn Error>);
        }
        self.send_command(XcpCommandBuilder::new(CC_DOWNLOAD).add_u8(n as u8).add_u8_slice(data_bytes).build())
            .await?;
        Ok(())
    }
    pub async fn modify_begin(&mut self) -> Result<(), Box<dyn Error>> {
        trace!("modify_begin");
        self.send_command(XcpCommandBuilder::new(CC_USER).add_u8(1).add_u8(0).add_u8(0).build()).await?;
        Ok(())
    }

    pub async fn modify_end(&mut self) -> Result<(), Box<dyn Error>> {
        trace!("modify_end");
        self.send_command(XcpCommandBuilder::new(CC_USER).add_u8(2).add_u8(0).add_u8(0).build()).await?;
        Ok(())
    }

    //------------------------------------------------------------------------
    // XCP memory access services, upload and download of larger data blocks

    // Upload a memory block of block_size bytes from the XCP server
    pub async fn upload_memory_block(&mut self, block_size: u32) -> Result<Vec<u8>, Box<dyn Error>> {
        trace!("upload_memory_block block_size={}", block_size);

        let mut size = block_size;
        let mut result = Vec::new();
        while size > 0 {
            let n = if size >= self.max_cto_size as u32 { self.max_cto_size - 1 } else { size as u8 };
            size -= n as u32;
            let data = self.upload(n).await?;
            result.extend_from_slice(&data[1..=n as usize]);
        }
        Ok(result)
    }

    // Download a memory block of data_bytes to the XCP server
    pub async fn download_memory_block(&mut self, data_bytes: &[u8]) -> Result<(), Box<dyn Error>> {
        let mut block_size = data_bytes.len();
        trace!("download_memory_block block_size={}", block_size);
        let mut pos = 0;
        while block_size > 0 {
            let n = if block_size >= self.max_cto_size as usize - 1 {
                self.max_cto_size as usize - 2
            } else {
                block_size
            };
            self.download(&data_bytes[pos..(pos + n)]).await?;
            block_size -= n;
            pos += n;
        }
        Ok(())
    }

    //------------------------------------------------------------------------
    // XCP segment info services

    /// Get segment info
    pub async fn get_segment_info(&mut self, segment_number: u8) -> Result<(u8, u32, u16, String), Box<dyn Error>> {
        //addr
        let data = self
            .send_command(
                XcpCommandBuilder::new(CC_GET_SEGMENT_INFO)
                    .add_u8(0) // get basic address info
                    .add_u8(segment_number)
                    .add_u8(0) // get addr
                    .add_u8(0)
                    .build(),
            )
            .await?;

        let addr = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        // Length
        let data = self
            .send_command(
                XcpCommandBuilder::new(CC_GET_SEGMENT_INFO)
                    .add_u8(0) // get basic address info
                    .add_u8(segment_number)
                    .add_u8(1) // get length
                    .add_u8(0)
                    .build(),
            )
            .await?;
        let length = u32::from_le_bytes([data[4], data[5], data[6], data[7]]).try_into().unwrap();

        // Name
        let data = self
            .send_command(
                XcpCommandBuilder::new(CC_GET_SEGMENT_INFO)
                    .add_u8(0) // get standard info
                    .add_u8(segment_number)
                    .add_u8(2) // get name
                    .add_u8(0)
                    .build(),
            )
            .await?;
        let name_length: u8 = u32::from_le_bytes([data[4], data[5], data[6], data[7]]).try_into().unwrap();
        let data = self.upload(name_length).await?;
        let res = String::from_utf8(data[1..=(name_length as usize)].to_vec());
        let name = match res {
            Ok(name) => name,
            Err(_) => {
                return Err(Box::new(XcpError::new(CRC_CMD_SYNTAX, CC_GET_SEGMENT_INFO)) as Box<dyn Error>);
            }
        };

        // Addr extension
        let addr_ext: u8 = 0; // @@@@ Segment address extension not supported yet

        Ok((addr_ext, addr, length, name))
    }

    /// Get page info
    pub async fn get_page_info(&mut self, segment_number: u8, page_number: u8) -> Result<Vec<u8>, Box<dyn Error>> {
        let data = self
            .send_command(
                XcpCommandBuilder::new(CC_GET_PAGE_INFO)
                    .add_u8(0) // Reserved
                    .add_u8(segment_number)
                    .add_u8(page_number)
                    .build(),
            )
            .await?;
        Ok(data)
    }

    //------------------------------------------------------------------------
    // XCP DAQ services

    /// Get DAQ clock timestamp resolution in ns
    pub async fn get_daq_processor_info(&mut self) -> Result<(), Box<dyn Error>> {
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_DAQ_PROCESSOR_INFO).build()).await?;
        let mut c = Cursor::new(&data[1..]);

        let daq_properties = ReadBytesExt::read_u8(&mut c)?;
        assert!((daq_properties & 0x10) == 0x10, "DAQ timestamps must be available");
        let max_daq = ReadBytesExt::read_u16::<LittleEndian>(&mut c)?;
        self.max_events = ReadBytesExt::read_u16::<LittleEndian>(&mut c)?;
        let min_daq = ReadBytesExt::read_u8(&mut c)?;
        let daq_key_byte = ReadBytesExt::read_u8(&mut c)?;
        self.daq_header_size = (daq_key_byte >> 6) + 1;
        assert!(self.daq_header_size == 4 || self.daq_header_size == 2, "DAQ header type must be ODT_FIL_DAQW or ODT_DAQB");

        debug!(
            "GET_DAQ_PROPERTIES daq_properties = 0x{:0X}, max_daq = {}, max_event = {}, min_daq = {}, daq_key_byte = 0x{:0X} (header_size={})",
            daq_properties, max_daq, self.max_events, min_daq, daq_key_byte, self.daq_header_size
        );
        Ok(())
    }

    pub async fn get_daq_event_info(&mut self, event_id: u16) -> Result<String, Box<dyn Error>> {
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_DAQ_EVENT_INFO).add_u8(0).add_u16(event_id).build()).await?;
        let event_name_len = data[3];
        let data = self.upload(event_name_len).await?;
        let res = String::from_utf8(data[1..=(event_name_len as usize)].to_vec());
        match res {
            Ok(event_name) => {
                return Ok(event_name);
            }
            Err(_) => Err(Box::new(XcpError::new(CRC_CMD_SYNTAX, CC_GET_DAQ_EVENT_INFO)) as Box<dyn Error>),
        }
    }

    async fn free_daq(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_FREE_DAQ).build()).await?;
        Ok(())
    }

    async fn alloc_daq(&mut self, count: u16) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_ALLOC_DAQ).add_u8(0).add_u16(count).build()).await?;
        Ok(())
    }

    async fn alloc_odt(&mut self, daq: u16, odt: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_ALLOC_ODT).add_u8(0).add_u16(daq).add_u8(odt).build()).await?;
        Ok(())
    }

    async fn alloc_odt_entries(&mut self, daq: u16, odt: u8, count: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_ALLOC_ODT_ENTRY).add_u8(0).add_u16(daq).add_u8(odt).add_u8(count).build())
            .await?;
        Ok(())
    }

    async fn set_daq_ptr(&mut self, daq: u16, odt: u8, idx: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_SET_DAQ_PTR).add_u8(0).add_u16(daq).add_u8(odt).add_u8(idx).build())
            .await?;
        Ok(())
    }

    async fn write_daq(&mut self, ext: u8, addr: u32, len: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(
            XcpCommandBuilder::new(CC_WRITE_DAQ)
                .add_u8(0) // bit offset
                .add_u8(len)
                .add_u8(ext)
                .add_u32(addr)
                .build(),
        )
        .await?;
        Ok(())
    }

    async fn set_daq_list_mode(&mut self, daq: u16, eventchannel: u16) -> Result<(), Box<dyn Error>> {
        const XCP_DAQ_MODE_TIMESTAMP: u8 = 0x10; // Timestamp always on, no other mode supported by XCPlite
        let mode: u8 = XCP_DAQ_MODE_TIMESTAMP;
        let priority = 0x00; // Always use priority 0, no DAQ list flush for specific events, priorization supported by XCPlite
        self.send_command(
            XcpCommandBuilder::new(CC_SET_DAQ_LIST_MODE)
                .add_u8(mode)
                .add_u16(daq)
                .add_u16(eventchannel)
                .add_u8(1) // prescaler
                .add_u8(priority)
                .build(),
        )
        .await?;
        Ok(())
    }

    // Select DAQ list
    async fn select_daq_list(&mut self, daq: u16) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_DAQ_LIST).add_u8(2).add_u16(daq).build()).await?;
        Ok(())
    }

    // Prepare, start selected, stop all
    async fn prepare_selected_daq_lists(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_SYNCH).add_u8(3 /* prepare selected */).build())
            .await?;
        Ok(())
    }
    async fn start_selected_daq_lists(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_SYNCH).add_u8(1 /* start selected */).build())
            .await?;
        Ok(())
    }
    async fn stop_all_daq_lists(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_SYNCH).add_u8(0).build()).await?;
        Ok(())
    }

    //-------------------------------------------------------------------------------------------------
    // Clock

    // CC_TIME_CORRELATION_PROPERTIES
    async fn time_correlation_properties(&mut self) -> Result<(), Box<dyn Error>> {
        let request: u8 = 2; // set responce format to SERVER_CONFIG_RESPONSE_FMT_ADVANCED
        let properties: u8 = 0;
        let cluster_id: u16 = 0;
        let _data = self
            .send_command(
                XcpCommandBuilder::new(CC_TIME_CORRELATION_PROPERTIES)
                    .add_u8(request)
                    .add_u8(properties)
                    .add_u8(0)
                    .add_u16(cluster_id)
                    .build(),
            )
            .await?;
        debug!("TIME_CORRELATION_PROPERIES set response format to SERVER_CONFIG_RESPONSE_FMT_ADVANCED");
        Ok(())
    }

    /// Get DAQ clock timestamp resolution in ns
    pub async fn get_daq_resolution_info(&mut self) -> Result<u64, Box<dyn Error>> {
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_DAQ_RESOLUTION_INFO).build()).await?;
        let mut c = Cursor::new(&data[1..]);

        let granularity_daq = ReadBytesExt::read_u8(&mut c)?;
        let max_size_daq = ReadBytesExt::read_u8(&mut c)?;
        let _granularity_stim = ReadBytesExt::read_u8(&mut c)?;
        let _max_size_stim = ReadBytesExt::read_u8(&mut c)?;
        let timestamp_mode = ReadBytesExt::read_u8(&mut c)?;
        let timestamp_ticks = ReadBytesExt::read_u16::<LittleEndian>(&mut c)?;

        assert!(granularity_daq == 0x01, "support only 1 byte DAQ granularity");
        assert!(timestamp_mode & 0x07 == 0x04, "support only 32 bit DAQ timestamps");
        assert!(timestamp_mode & 0x08 == 0x08, "support only fixed DAQ timestamps");

        // Calculate timestamp resolution in ns per tick
        let mut timestamp_unit = timestamp_mode >> 4; // 1ns=0, 10ns=1, 100ns=2, 1us=3, 10us=4, 100us=5, 1ms=6, 10ms=7, 100ms=8, 1s=9
        let mut timestamp_resolution_ns: u64 = timestamp_ticks as u64;
        while timestamp_unit > 0 {
            timestamp_resolution_ns *= 10;
            timestamp_unit -= 1;
        }
        self.timestamp_resolution_ns = timestamp_resolution_ns;

        debug!(
            "GET_DAQ_RESOLUTION_INFO granularity_daq={} max_size_daq={} timestamp_mode={} timestamp_resolution={}ns",
            granularity_daq, max_size_daq, timestamp_mode, timestamp_resolution_ns
        );
        Ok(timestamp_resolution_ns)
    }

    // Get DAQ clock raw value in ticks of timestamp_resolution ns
    async fn get_daq_clock_raw(&mut self) -> Result<u64, Box<dyn Error>> {
        let data = self.send_command(XcpCommandBuilder::new(CC_GET_DAQ_CLOCK).build()).await?;
        let mut c = Cursor::new(&data[2..]);

        // Trigger info and payload format
        // TIME_OF_TS_SAMPLING: (trigger_info >> 3) & 0x03 : 3-reception, 2-transmission, 1-low jitter, 0-during commend processing
        // TRIGGER_INITIATOR:   (trigger_info >> 0) & 0x07 : not relevant for GET_DAQ_CLOCK
        // FMT_XCP_SLV: (payload_fmt >> 0) & 0x03 let payload_fmt = data[3];
        let trigger_info = ReadBytesExt::read_u8(&mut c)?;
        let payload_fmt = ReadBytesExt::read_u8(&mut c)?;

        // Timestamp
        let timestamp64 = if payload_fmt == 1 {
            // 32 bit slave clock
            ReadBytesExt::read_u32::<LittleEndian>(&mut c)? as u64
        } else if payload_fmt == 2 {
            // 64 bit slave clock
            ReadBytesExt::read_u64::<LittleEndian>(&mut c)?
        } else {
            return Err(Box::new(XcpError::new(CRC_OUT_OF_RANGE, CC_GET_DAQ_CLOCK)) as Box<dyn Error>);
        };

        trace!("GET_DAQ_CLOCK trigger_info=0x{:2X}, payload_fmt=0x{:2X} time={}", trigger_info, payload_fmt, timestamp64);
        Ok(timestamp64)
    }

    /// Get DAQ clock in ns
    pub async fn get_daq_clock(&mut self) -> Result<u64, Box<dyn Error>> {
        let timestamp64 = self.get_daq_clock_raw().await?;
        let timestamp_ns = timestamp64 * self.timestamp_resolution_ns;
        Ok(timestamp_ns)
    }


    //-------------------------------------------------------------------------------------------------
    // ELF upload

    pub async fn upload_elf_file<P: AsRef<std::path::Path>>(&mut self, elf_path: &P) -> Result<(), Box<dyn Error>> {
        // Send XCP GET_ID IDT_VECTOR_ELF_UPLOAD command to set MTA
        let (file_size, _) = self.get_id(IDT_VECTOR_ELF_UPLOAD).await?;
        if file_size == 0 {
            error!("ELF file not available, GET_ID returned size 0");
            return Err(Box::new(XcpError::new(ERROR_GENERIC, CC_GET_ID)) as Box<dyn Error>);
        }

        // Check if the ELF file already exists and warn about overwriting
        if elf_path.as_ref().exists() {
            warn!("ELF file {} already exists, overwriting", elf_path.as_ref().display());
        }

        // Upload the ELF file
        info!("Upload ELF to {}", elf_path.as_ref().display());
        let file = std::fs::File::create(elf_path)?;
        let mut writer = std::io::BufWriter::new(file);
        let mut size = file_size;
        while size > 0 {
            let n = if size >= self.max_cto_size as u32 { self.max_cto_size - 1 } else { size as u8 };
            size -= n as u32;
            let data = self.upload(n).await?;
            trace!("xcp_client.upload: {} bytes = {:?}", data.len(), data);
            writer.write_all(&data[1..=n as usize])?;
        }
        writer.flush()?;
        debug!("ELF upload completed, {} bytes loaded", file_size);

        Ok(())
    }



    //-------------------------------------------------------------------------------------------------
    // A2L upload

    pub async fn upload_a2l_file<P: AsRef<std::path::Path>>(&mut self, a2l_path: &P) -> Result<(), Box<dyn Error>> {
        // Send XCP GET_ID 4 command to set MTA
        let (file_size, _) = self.get_id(IDT_ASAM_UPLOAD).await?;
        if file_size == 0 {
            error!("A2L file not available, GET_ID 4 returned size 0");
            return Err(Box::new(XcpError::new(ERROR_GENERIC, CC_GET_ID)) as Box<dyn Error>);
        }

        // Check if the A2L file already exists and warn about overwriting
        if a2l_path.as_ref().exists() {
            warn!("A2L file {} already exists, overwriting", a2l_path.as_ref().display());
        }

        // Upload the A2L file
        info!("Upload A2L to {}.a2l", a2l_path.as_ref().display());
        let file = std::fs::File::create(a2l_path)?;
        let mut writer = std::io::BufWriter::new(file);
        let mut size = file_size;
        while size > 0 {
            let n = if size >= self.max_cto_size as u32 { self.max_cto_size - 1 } else { size as u8 };
            size -= n as u32;
            let data = self.upload(n).await?;
            trace!("xcp_client.upload: {} bytes = {:?}", data.len(), data);
            writer.write_all(&data[1..=n as usize])?;
        }
        writer.flush()?;
        debug!("A2L upload completed, {} bytes loaded", file_size);

        Ok(())
    }

    // Get the A2L via XCP upload and GET_ID4 (IDT_ASAM_UPLOAD) and load it into the registry
    pub async fn upload_a2l_into_registry<P: AsRef<std::path::Path>>(&mut self, a2l_path: &P, reg: &mut xcp_lite::registry::Registry) -> Result<(), Box<dyn Error>> {
        // Upload the A2L file
        self.upload_a2l_file(&a2l_path).await?;

        // Load the A2L file into the registry
        // @@@@ TODO xcp_client does not support arrays, instances and typedefs yet, flatten the registry and mangle the names
        reg.load_a2l(&a2l_path, true, true, true, true)?;
        info!(
            " A2L file contains {} instances, {} events and {} calibration segments",
            reg.instance_list.len(),
            reg.event_list.len(),
            reg.cal_seg_list.len()
        );
        Ok(())
    }

    // Get the A2L via XCP upload and GET_ID4 (IDT_ASAM_UPLOAD) and load it into the registry
    pub fn load_a2l_file_into_registry<P: AsRef<std::path::Path>>(&mut self, a2l_path: &P, reg: &mut xcp_lite::registry::Registry) -> Result<(), Box<dyn Error>> {
        // Load the A2L file into the registry
        // @@@@ TODO xcp_client does not support arrays, instances and typedefs yet, flatten the registry and mangle the names
        reg.load_a2l(&a2l_path, true, true, true, true)?;
        info!(
            " A2L file contains {} instances, {} events and {} calibration segments",
            reg.instance_list.len(),
            reg.event_list.len(),
            reg.cal_seg_list.len()
        );
        Ok(())
    }

    pub fn get_epk(&self) -> Option<&str> {
        self.registry.as_ref().map(|r| r.application.get_version())
    }

    //------------------------------------------------------------------------
    // Get event and segment information from XCP server and add to registry

    pub async fn get_event_segment_info(&mut self, reg: &mut xcp_lite::registry::Registry) -> Result<(), Box<dyn Error>> {
        info!("Reading event and segment information from connected XCP server:");

        // Get event information
        for i in 0..self.max_events {
            let name = self.get_daq_event_info(i).await?;
            info!(" Event {}: {}", i, name);
            reg.event_list.add_event(McEvent::new(name, 0, i, 0)).unwrap();
        }

        // Get segment information
        let mut n = 0;
        for i in 0..self.max_segments {
            let (addr_ext, addr, length, name) = self.get_segment_info(i).await?;
            info!(" Segment {}: {} addr={}:0x{:08X} length={} ", i, name, addr_ext, addr, length);

            // Otherwise the EPK segment would be handled like a normal calibration segment with 2 pages
            // Segment relative addressing is ignored, all addresses are treated as raw A2L addr_ext/addr
            // Segment relative addressing would be reg.cal_seg_list.add_cal_seg(name, i as u16, length as u32).unwrap();
            reg.cal_seg_list.add_cal_seg_by_addr(name, n, addr_ext, addr, length as u32).unwrap();

            n += 1;
        }

        Ok(())
    }

    //------------------------------------------------------------------------
    // Registry
    // Get a list available measurement and calibration object names from registry matching a regular expression

    pub fn get_registry(&self) -> &xcp_lite::registry::Registry {
        self.registry.as_ref().unwrap()
    }

    pub fn find_characteristics(&self, expr: &str) -> Vec<String> {
        let registry = self.registry.as_ref().unwrap();
        registry.instance_list.find_instances_regex(expr, xcp_lite::registry::McObjectType::Characteristic, None)
    }

    pub fn find_measurements(&self, expr: &str) -> Vec<String> {
        let registry = self.registry.as_ref().unwrap();
        registry.instance_list.find_instances_regex(expr, xcp_lite::registry::McObjectType::Measurement, None)
    }

    //------------------------------------------------------------------------
    // XcpCalibrationObject, XcpCalibrationObjectHandle (index pointer to XcpCalibrationObject),
    // XcpXcpCalibrationObjectHandle is assumed immutable and the actual value is cached

    pub fn get_calibration_object(&self, handle: XcpCalibrationObjectHandle) -> &XcpClientCalibrationObject {
        &self.calibration_object_list[handle.0]
    }

    /// Create a calibration object by name from the registry and upload its current value from the XCP server
    /// name may be a regular expression matching exactly one characteristic
    pub async fn create_calibration_object(&mut self, name: &str) -> Result<XcpCalibrationObjectHandle, Box<dyn Error>> {
        let registry = self.registry.as_ref().unwrap();
        match registry.instance_list.get_instance(name, xcp_lite::registry::McObjectType::Characteristic, None) {
            None => {
                error!("Characteristic {} not found", name);
                Err(Box::new(XcpError::new(ERROR_NOT_FOUND, 0)) as Box<dyn Error>)
            }
            Some(instance) => {
                let (ext, addr) = instance.get_address().get_a2l_addr(registry);
                let a2l_addr: A2lAddr = A2lAddr { ext, addr, event: None };
                let a2l_type: A2lType = A2lType {
                    size: instance.value_size(),
                    encoding: instance.value_type().into(),
                };
                let a2l_limits: A2lLimits = A2lLimits {
                    lower: instance.get_min().unwrap(),
                    upper: instance.get_max().unwrap(),
                };
                let mut o = XcpClientCalibrationObject::new(instance.get_name(), a2l_addr, a2l_type, a2l_limits);
                let size = o.get_type.size;
                assert!(size < 256, "xcp_client currently supports only <256 byte values");
                if self.is_connected() {
                    let resp = self.short_upload(o.a2l_addr.addr, o.a2l_addr.ext, size as u8).await?;
                    o.value = resp[1..=o.get_type.size].to_vec();
                    trace!("upload {}: addr = {:?} type = {:?} limit={:?} value={:?}\n", name, a2l_addr, a2l_type, a2l_limits, o.value);
                }
                self.calibration_object_list.push(o);
                Ok(XcpCalibrationObjectHandle(self.calibration_object_list.len() - 1))
            }
        }
    }

    pub async fn set_value_u64(&mut self, handle: XcpCalibrationObjectHandle, value: u64) -> Result<(), Box<dyn Error>> {
        let obj = &self.calibration_object_list[handle.0];
        if (value as f64) > obj.a2l_limits.upper || (value as f64) < obj.a2l_limits.lower {
            return Err(Box::new(XcpError::new(ERROR_LIMIT, 0)) as Box<dyn Error>);
        }
        let size: usize = obj.get_type.size;
        let slice = &value.to_le_bytes()[0..size];
        self.short_download(obj.a2l_addr.addr, obj.a2l_addr.ext, slice).await?;
        self.calibration_object_list[handle.0].set_value(slice);
        Ok(())
    }
    pub async fn set_value_i64(&mut self, handle: XcpCalibrationObjectHandle, value: i64) -> Result<(), Box<dyn Error>> {
        let obj = &self.calibration_object_list[handle.0];
        if (value as f64) > obj.a2l_limits.upper || (value as f64) < obj.a2l_limits.lower {
            return Err(Box::new(XcpError::new(ERROR_LIMIT, 0)) as Box<dyn Error>);
        }
        let size: usize = obj.get_type.size;
        let slice = &value.to_le_bytes()[0..size];
        self.short_download(obj.a2l_addr.addr, obj.a2l_addr.ext, slice).await?;
        self.calibration_object_list[handle.0].set_value(slice);
        Ok(())
    }
    pub async fn set_value_f64(&mut self, handle: XcpCalibrationObjectHandle, value: f64) -> Result<(), Box<dyn Error>> {
        let obj = &self.calibration_object_list[handle.0];
        if value > obj.a2l_limits.upper || value < obj.a2l_limits.lower {
            return Err(Box::new(XcpError::new(ERROR_LIMIT, 0)) as Box<dyn Error>);
        }
        let value_size: usize = obj.get_type.size;
        let value_type: A2lType = obj.get_type;
        let value: u64 = match value_type {
            A2lType {
                size: 4,
                encoding: A2lTypeEncoding::Float,
            } => {
                let v = value as f32;
                v.to_bits() as u64
            }
            A2lType {
                size: 8,
                encoding: A2lTypeEncoding::Float,
            } => value.to_bits(),
            A2lType {
                size,
                encoding: A2lTypeEncoding::Signed,
            } => {
                let v = value as i64;
                v as u64
            }
            A2lType {
                size,
                encoding: A2lTypeEncoding::Unsigned,
            } => {
                let v = value as u64;
                v
            }
            _ => {
                error!("set_value_f64: unsupported type {:?}", value_type);
                return Err(Box::new(XcpError::new(ERROR_TYPE_MISMATCH, 0)) as Box<dyn Error>);
            }
        };
        let slice = &value.to_le_bytes()[0..value_size];
        self.short_download(obj.a2l_addr.addr, obj.a2l_addr.ext, slice).await?;
        self.calibration_object_list[handle.0].set_value(slice);
        Ok(())
    }

    pub async fn read_value_u64(&mut self, index: XcpCalibrationObjectHandle) -> Result<u64, Box<dyn Error>> {
        let obj = &self.calibration_object_list[index.0];
        let a2l_addr = obj.a2l_addr;
        let get_type = obj.get_type;
        let size = obj.get_type.size;
        assert!(size < 256, "xcp_client currently supports only <256 byte values");
        let resp = self.short_upload(a2l_addr.addr, a2l_addr.ext, size as u8).await?;
        let value = resp[1..=get_type.size].to_vec();
        self.calibration_object_list[index.0].value = value;
        Ok(self.get_value_u64(index))
    }

    pub fn get_value_u64(&mut self, index: XcpCalibrationObjectHandle) -> u64 {
        let obj = &self.calibration_object_list[index.0];
        obj.get_value_u64()
    }

    pub fn get_value_i64(&mut self, index: XcpCalibrationObjectHandle) -> i64 {
        let obj = &self.calibration_object_list[index.0];
        obj.get_value_i64()
    }
    pub fn get_value_f64(&mut self, index: XcpCalibrationObjectHandle) -> f64 {
        let obj = &self.calibration_object_list[index.0];
        let v = obj.get_value_u64();
        match obj.get_type.size {
            8 => {
                // Convert to f64
                f64::from_bits(v)
            }
            4 => {
                // Convert to f32
                f32::from_bits(v as u32) as f64
            }
            _ => {
                error!("get_value_f64: size = {}", obj.get_type.size);
                0.0
            }
        }
    }

    //------------------------------------------------------------------------
    // XcpMeasurementObject, XcpMeasurementObjectHandle (index pointer to XcpCMeasurementObject),
    //

    /// Create a measurement object by name from the registry
    /// name may be a regular expression matching exactly one measurement
    pub fn create_measurement_object(&mut self, name: &str) -> Option<XcpMeasurementObjectHandle> {
        let registry = self.registry.as_ref().unwrap();
        match registry.instance_list.get_instance(name, xcp_lite::registry::McObjectType::Measurement, None) {
            None => {
                debug!("Measurement {} not found", name);
                None
            }
            Some(instance) => {
                let (ext, addr) = instance.get_address().get_a2l_addr(registry);
                if instance.event_id().is_none() {
                    log::error!("event_id for measurement object {} not found, addr = {}:0x{:0X}", name, ext, addr);
                    return None;
                }
                let event = instance.event_id().unwrap();
                let a2l_addr: A2lAddr = A2lAddr { ext, addr, event: Some(event) };
                let a2l_type: A2lType = A2lType {
                    size: instance.value_size(),
                    encoding: instance.value_type().into(),
                };
                let o = XcpClientMeasurementObject::new(name, a2l_addr, a2l_type);
                debug!("Create measurement object {}: addr = {:08X} type = {:?}", name, a2l_addr.addr, a2l_type);
                debug!("-> {:?} ", o);
                self.measurement_object_list.push(o);
                Some(XcpMeasurementObjectHandle(self.measurement_object_list.len() - 1))
            }
        }
    }

    pub fn get_measurement_object(&self, handle: XcpMeasurementObjectHandle) -> &XcpClientMeasurementObject {
        &self.measurement_object_list[handle.0]
    }

    //------------------------------------------------------------------------
    // DAQ init, start, stop
    //

    /// Get clock resolution in ns
    pub fn get_timestamp_resolution(&self) -> u64 {
        self.timestamp_resolution_ns
    }

    /// Start DAQ
    pub async fn start_measurement(&mut self) -> Result<(), Box<dyn Error>> {
        debug!("Start measurement");

        // Init
        let signal_count = self.measurement_object_list.len();
        let mut daq_odt_entries: Vec<Vec<OdtEntry>> = Vec::with_capacity(8);

        // Store all events in a hashmap (eventnumber, signalcount)
        let mut event_map: HashMap<u16, u16> = HashMap::new();
        let mut min_event: u16 = 0xFFFF;
        let mut max_event: u16 = 0;
        for i in 0..signal_count {
            let event = self.measurement_object_list[i].get_a2l_addr().event.unwrap();
            if event < min_event {
                min_event = event;
            }
            if event > max_event {
                max_event = event;
            }
            let count = event_map.entry(event).or_insert(0);
            *count += 1;
        }
        let event_count: u16 = event_map.len() as u16;
        debug!("event/daq count = {}", event_count);

        // Transform the event hashmap to a sorted array
        let mut event_list: Vec<(u16, u16)> = Vec::new();
        for (event, count) in event_map.into_iter() {
            event_list.push((event, count));
        }
        event_list.sort_by(|a, b| a.0.cmp(&b.0));

        // Alloc a DAQ list for each event
        assert!(event_count <= 1024, "event_count > 1024");
        let daq_count: u16 = event_count;
        self.free_daq().await?;
        self.alloc_daq(daq_count).await?;
        debug!("alloc_daq count={}", daq_count);

        // Alloc one ODT for each DAQ list (event)
        // @@@@ TODO Restriction: Only one ODT per DAQ list supported yet
        for daq in 0..daq_count {
            self.alloc_odt(daq, 1).await?;
            debug!("Alloc daq={}, odt_count={}", daq, 1);
        }

        // Alloc ODT entries (signal count) for each ODT/DAQ list
        for daq in 0..daq_count {
            let odt_entry_count = event_list[daq as usize].1;
            assert!(odt_entry_count < 0x7C, "odt_entry_count >= 0x7C");
            self.alloc_odt_entries(daq, 0, odt_entry_count as u8).await?;
            debug!("Alloc odt_entries: daq={}, odt={}, odt_entry_count={}", daq, 0, odt_entry_count);
        }

        // Create all ODT entries for each daq/event list and store information for the DAQ decoder
        for daq in 0..daq_count {
            //
            let event = event_list[daq as usize].0;
            let odt = 0; // Only one odt per daq list supported yet
            let odt_entry_count = self.measurement_object_list.len();

            // Create ODT entries for this daq list
            let mut odt_entries = Vec::new();
            let mut odt_size: u16 = 0;
            self.set_daq_ptr(daq, odt, 0).await?;
            for odt_entry in 0..odt_entry_count {
                let m = &mut self.measurement_object_list[odt_entry];
                let a2l_addr = m.a2l_addr;
                if a2l_addr.event == Some(event) {
                    // Only add signals for the daq list event
                    let a2l_type: A2lType = m.a2l_type;
                    m.daq = daq;
                    m.odt = odt;
                    m.offset = odt_size + 6;

                    debug!(
                        "WRITE_DAQ {} daq={}, odt={},  type={:?}, size={}, ext={}, addr=0x{:08X}, offset={}",
                        m.name,
                        daq,
                        odt,
                        a2l_type.encoding,
                        a2l_type.size,
                        a2l_addr.ext,
                        a2l_addr.addr,
                        odt_size + 6
                    );

                    odt_entries.push(OdtEntry {
                        name: m.name.clone(),
                        a2l_type,
                        a2l_addr,
                        offset: odt_size,
                    });

                    let size = a2l_type.size;
                    assert!(size < 256, "xcp_client currently supports only <256 byte values");
                    self.write_daq(a2l_addr.ext, a2l_addr.addr, size as u8).await?;

                    odt_size += a2l_type.size as u16;
                    if odt_size > self.max_dto_size - 6 {
                        return Err(Box::new(XcpError::new(ERROR_ODT_SIZE, 0)) as Box<dyn Error>);
                    }
                }
            } // odt_entries

            daq_odt_entries.push(odt_entries);
        }

        // Set DAQ list events
        for daq in 0..daq_count {
            let event = event_list[daq as usize].0;
            self.set_daq_list_mode(daq, event).await?;
            debug!("Set event: daq={}, event={}", daq, event);
        }

        // Select and prepare all DAQ lists
        for daq in 0..daq_count {
            self.select_daq_list(daq).await?;
        }
        self.prepare_selected_daq_lists().await?;

        // Reset the DAQ decoder and set measurement start time
        let daq_clock = self.get_daq_clock_raw().await?;
        self.daq_decoder.as_ref().unwrap().lock().start(daq_odt_entries, daq_clock);

        // Send running=true throught the DAQ control channel to the receive task
        self.task_control.running = true;
        self.tx_task_control.as_ref().unwrap().send(self.task_control).await.unwrap();

        // Start DAQ
        self.start_selected_daq_lists().await?;

        Ok(())
    }

    /// Stop DAQ
    pub async fn stop_measurement(&mut self) -> Result<(), Box<dyn Error>> {
        debug!("Stop measurement");

        // Stop DAQ
        let res = self.stop_all_daq_lists().await;

        // Send running=false throught the DAQ control channel to the receive task
        self.task_control.running = false;
        self.tx_task_control.as_ref().unwrap().send(self.task_control).await?;

        // Stop the DAQ decoder
        self.daq_decoder.as_ref().unwrap().lock().stop();

        // Clear the measurement object list
        self.measurement_object_list.clear();

        res
    }

    //---------------------------------------------------------------------------------
    // Calibration page management

    /// Set all calibration segments to page 0, working page
    pub async fn init_calibration_segments(&mut self) -> Result<(), Box<dyn Error>> {
        let reg = self.registry.as_mut().unwrap();
        for index in 0..reg.cal_seg_list.len() {
            let ecu_page = self.get_ecu_page(index.try_into().unwrap()).await?;
            let xcp_page = self.get_xcp_page(index.try_into().unwrap()).await?;
            info!("Calibration segment {}: ecu_page={}, xcp_page={}", index, ecu_page, xcp_page);
        }

        // Set all segments to working page 0
        info!("Set ECU page access to working page for all segments");
        self.set_ecu_page(0).await?;
        info!("Set XCP page access to working page for all segments");
        self.set_xcp_page(0).await?;

        Ok(())
    }

    //---------------------------------------------------------------------------------
    // Upload and Download of calibration data

    // Usage:
    // xcp_client.load_calibration_segments_from_file(&bin_filename).await?;
    // xcp_client.save_calibration_segments_to_file(&bin_filename).await?;

    pub async fn load_calibration_segments_from_file<P: AsRef<std::path::Path>>(&mut self, bin_path: &P) -> Result<(), Box<dyn Error>> {
        info!("Load calibration segments from file {}", bin_path.as_ref().display());

        // Read the Intel-Hex file
        let file_content = std::fs::read_to_string(bin_path)?;
        let ihex_reader = ihex::Reader::new(file_content.as_str());

        // Parse all data records from the Intel-Hex file into a HashMap
        // Key: full 32-bit address, Value: data bytes
        let mut hex_data: HashMap<u32, Vec<u8>> = HashMap::new();

        // Track the upper 16 bits of the address from ExtendedLinearAddress records
        let mut extended_linear_address: u32 = 0;

        for record in ihex_reader {
            match record {
                Err(e) => {
                    error!("Error parsing IHEX record: {}", e);
                    return Err(Box::new(e) as Box<dyn Error>);
                }
                Ok(ihex::Record::ExtendedLinearAddress(upper_addr)) => {
                    // Store the upper 16 bits (shifted left by 16)
                    extended_linear_address = (upper_addr as u32) << 16;
                    debug!("IHEX Extended Linear Address: upper=0x{:04X} (base=0x{:08X})", upper_addr, extended_linear_address);
                }
                Ok(ihex::Record::Data { offset, value }) => {
                    // Combine upper 16 bits with lower 16 bits to get full 32-bit address
                    let full_address = extended_linear_address | (offset as u32);
                    debug!("IHEX Data record: offset=0x{:04X}, full_addr=0x{:08X}, length={}", offset, full_address, value.len());
                    hex_data.insert(full_address, value);
                }
                Ok(ihex::Record::EndOfFile) => {
                    debug!("IHEX End of file");
                    break;
                }
                Ok(_) => {
                    // Ignore other record types (ExtendedSegmentAddress, StartSegmentAddress, etc.)
                    debug!("IHEX: Ignoring other record type");
                }
            }
        }

        // Extract all data we need from registry before any mutable borrows
        let cal_seg_data: Vec<_> = (&self.registry.as_ref().unwrap().cal_seg_list)
            .into_iter()
            .map(|cal_seg| (cal_seg.get_index(), cal_seg.get_name(), cal_seg.size, cal_seg.addr_ext, cal_seg.addr))
            .collect();

        // Now iterate over the extracted data and download to XCP server
        for (seg_index, seg_name, seg_length, addr_ext, addr) in cal_seg_data {
            info!(" Load segment {} (index={} addr={}:0x{:08X} length={})", seg_name, seg_index, addr_ext, addr, seg_length);

            // Find the data for this segment address
            if let Some(data) = hex_data.get(&addr) {
                if data.len() != seg_length as usize {
                    warn!("  Segment {} size mismatch: expected {} bytes, got {} bytes", seg_name, seg_length, data.len());
                }

                // Download the data to the XCP server
                self.set_mta(addr_ext, addr).await?;
                self.download_memory_block(&data).await?;
                debug!("  Downloaded {} bytes to segment {}", data.len(), seg_name);
            } else {
                warn!("  No data found in IHEX file for segment {} at address 0x{:08X}", seg_name, addr);
            }
        }

        Ok(())
    }

    pub async fn save_calibration_segments_to_file<P: AsRef<std::path::Path>>(&mut self, bin_path: &P) -> Result<(), Box<dyn Error>> {
        info!("Save calibration segments to file {}", bin_path.as_ref().display());

        assert!(bin_path.as_ref().extension().is_some());

        // First, collect all segment information from registry (immutable borrow)
        let cal_seg_info: Vec<_> = self
            .registry
            .as_ref()
            .unwrap()
            .cal_seg_list
            .into_iter()
            .map(|cal_seg| (cal_seg.get_index(), cal_seg.size, cal_seg.addr, cal_seg.addr_ext, cal_seg.get_name().to_string()))
            .collect();

        // Now upload data for each segment (mutable borrows of self)
        let mut cal_seg_desc: Vec<(CalSegDescriptor, Vec<u8>)> = Vec::new();
        for (index, size, addr, addr_ext, name) in cal_seg_info {
            // Upload the data from the XCP server
            self.set_mta(addr_ext, addr).await.unwrap();
            let data: Vec<u8> = self.upload_memory_block(size).await.unwrap();
            debug!("  Uploaded {} bytes from segment {}", data.len(), name);
            cal_seg_desc.push((
                CalSegDescriptor {
                    index,
                    size: size as u16,
                    addr,
                    app_id: 0, // Multi application mode not implemented
                    name,
                },
                data,
            ));
        }

        // If file extension is .hex
        if bin_path.as_ref().extension().unwrap() == "hex" {
            write_hex_file(&bin_path.as_ref().to_path_buf(), &cal_seg_desc)?;
        } else {
            let event_desc = self
                .registry
                .as_ref()
                .unwrap()
                .event_list
                .into_iter()
                .map(|event| EventDescriptor {
                    index: event.index,
                    id: event.id,
                    cycle_time_ns: event.target_cycle_time_ns,
                    priority: 0,
                    app_id: 0, // Multi application mode not implemented
                    name: event.get_name().to_string(),
                })
                .collect::<Vec<_>>();
            write_bin_file(&bin_path.as_ref().to_path_buf(), self.get_epk().unwrap(), &event_desc, &cal_seg_desc)?;
        }

        info!("Successfully saved {} segment(s) to {}", cal_seg_desc.len(), bin_path.as_ref().display());

        Ok(())
    }
}
