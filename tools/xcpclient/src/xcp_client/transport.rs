//--------------------------------------------------------------------------------------------------------------------------------------------------
// transport.rs — Socket I/O, receive task, command/response, connect/disconnect, get_id

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::error::Error;
use std::net::SocketAddr;
use std::sync::Arc;

use parking_lot::Mutex;
use tokio::net::{TcpStream, UdpSocket};
use tokio::select;
use tokio::sync::mpsc::{Receiver, Sender};
use tokio::time::timeout;

use super::*;

impl XcpClient {
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
                    trace!("receive_task: socket receive res={:?}, buf={:?}", res, buf[..12].to_vec());
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
                                    error!("receive_task: stop, corrupt packet received, size {} too small for header", size);
                                    return Err(Box::new(XcpError::new(ERROR_TL_HEADER,0)) as Box<dyn Error>);
                                }
                                let len = buf[i] as usize + ((buf[i + 1] as usize) << 8);
                                if len > size - 4 || len == 0 { // Corrupt packet received, not enough data received or no content
                                    error!("receive_task: stop, corrupt packet received, invalid length {} in header, size={}", len, size   );
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
    pub(super) async fn send_command(&mut self, cmd_bytes: &[u8]) -> Result<Vec<u8>, Box<dyn Error>> {
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
        let socket = if self.protocol == "TCP" {
            // Create TCP socket and connect
            let stream = TcpStream::connect(self.dest_addr).await?;
            debug!("TCP connection established to {:?}", stream.peer_addr()?);
            debug!("TCP local address: {:?}", stream.local_addr()?);
            // Give the server a moment to set up the connection
            tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
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
            let (tx_resp, rx_resp) = tokio::sync::mpsc::channel(1);
            self.rx_cmd_resp = Some(rx_resp); // rx XCP command response channel
            let (tx_daq, rx_daq) = tokio::sync::mpsc::channel(3);
            self.tx_task_control = Some(tx_daq); // tx XCP DAQ control channel
            let daq_decoder_clone = Arc::clone(&daq_decoder);
            self.receive_task = Some(tokio::spawn(async move {
                let _res = XcpClient::receive_task(socket, tx_resp, rx_daq, text_decoder, daq_decoder_clone).await;
            }));
            tokio::time::sleep(tokio::time::Duration::from_millis(100)).await; // wait for the receive task to start
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
        let res = self.send_command(XcpCommandBuilder::new(CC_GET_PAG_PROCESSOR_INFO).add_u8(0).build()).await;
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
        assert!(id_type == IDT_VECTOR_ELF_UPLOAD || id_type == IDT_ASAM_UPLOAD || id_type == IDT_ASAM_NAME || id_type == IDT_ASCII || id_type == IDT_ASAM_EPK); // others not supported yet

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
}
