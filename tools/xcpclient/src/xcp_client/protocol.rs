//--------------------------------------------------------------------------------------------------------------------------------------------------
// protocol.rs — Raw XCP protocol commands (memory access, DAQ configuration, clock, pages)

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::error::Error;
use std::io::Cursor;

use byteorder::{LittleEndian, ReadBytesExt};

use super::*;

impl XcpClient {
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
            "CC_GET_DAQ_PROCESSOR_INFO daq_properties = 0x{:0X}, max_daq = {}, max_event = {}, min_daq = {}, daq_key_byte = 0x{:0X} (header_size={})",
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

    pub(super) async fn free_daq(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_FREE_DAQ).build()).await?;
        Ok(())
    }

    pub(super) async fn alloc_daq(&mut self, count: u16) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_ALLOC_DAQ).add_u8(0).add_u16(count).build()).await?;
        Ok(())
    }

    pub(super) async fn alloc_odt(&mut self, daq: u16, odt: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_ALLOC_ODT).add_u8(0).add_u16(daq).add_u8(odt).build()).await?;
        Ok(())
    }

    pub(super) async fn alloc_odt_entries(&mut self, daq: u16, odt: u8, count: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_ALLOC_ODT_ENTRY).add_u8(0).add_u16(daq).add_u8(odt).add_u8(count).build())
            .await?;
        Ok(())
    }

    pub(super) async fn set_daq_ptr(&mut self, daq: u16, odt: u8, idx: u8) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_SET_DAQ_PTR).add_u8(0).add_u16(daq).add_u8(odt).add_u8(idx).build())
            .await?;
        Ok(())
    }

    pub(super) async fn write_daq(&mut self, ext: u8, addr: u32, len: u8) -> Result<(), Box<dyn Error>> {
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

    pub(super) async fn set_daq_list_mode(&mut self, daq: u16, eventchannel: u16) -> Result<(), Box<dyn Error>> {
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
    pub(super) async fn select_daq_list(&mut self, daq: u16) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_DAQ_LIST).add_u8(2).add_u16(daq).build()).await?;
        Ok(())
    }

    // Prepare, start selected, stop all
    pub(super) async fn prepare_selected_daq_lists(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_SYNCH).add_u8(3 /* prepare selected */).build())
            .await?;
        Ok(())
    }
    pub(super) async fn start_selected_daq_lists(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_SYNCH).add_u8(1 /* start selected */).build())
            .await?;
        Ok(())
    }
    pub(super) async fn stop_all_daq_lists(&mut self) -> Result<(), Box<dyn Error>> {
        self.send_command(XcpCommandBuilder::new(CC_START_STOP_SYNCH).add_u8(0).build()).await?;
        Ok(())
    }

    //-------------------------------------------------------------------------------------------------
    // Clock

    // CC_TIME_CORRELATION_PROPERTIES
    pub(super) async fn time_correlation_properties(&mut self) -> Result<(), Box<dyn Error>> {
        let request: u8 = 2; // set response format to SERVER_CONFIG_RESPONSE_FMT_ADVANCED
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
    pub(super) async fn get_daq_clock_raw(&mut self) -> Result<u64, Box<dyn Error>> {
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
}
