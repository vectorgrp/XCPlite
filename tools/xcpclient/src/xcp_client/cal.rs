//--------------------------------------------------------------------------------------------------------------------------------------------------
// cal.rs — Calibration object management, value read/write, segment load/save

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::error::Error;

use crate::bin_reader::bin_format::{CalSegDescriptor, EventDescriptor};
use crate::bin_reader::{write_bin_file, write_hex_file};

use super::*;

impl XcpClient {
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
        match registry.instance_list.get_instance(name, xcp_registry::McObjectType::Characteristic, None) {
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
                // Instances of a typedef (struct) have no limits, xcp_client supports only basic type instances
                let (Some(lower), Some(upper)) = (instance.get_min(), instance.get_max()) else {
                    error!("Characteristic {} is not a basic type value", name);
                    return Err(Box::new(XcpError::new(ERROR_NOT_FOUND, 0)) as Box<dyn Error>);
                };
                let a2l_limits: A2lLimits = A2lLimits { lower, upper };
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
                size: _,
                encoding: A2lTypeEncoding::Signed,
            } => {
                let v = value as i64;
                v as u64
            }
            A2lType {
                size: _,
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

    //---------------------------------------------------------------------------------
    // Calibration page management

    /// Set all calibration segments to page 0, working page
    pub async fn init_calibration_segments(&mut self) -> Result<(), Box<dyn Error>> {
        let calseg_numbers = self
            .registry
            .as_ref()
            .unwrap()
            .cal_seg_list
            .0
            .iter()
            .filter_map(|cal_seg| cal_seg.get_number())
            .collect::<Vec<u8>>();
        for number in &calseg_numbers {
            let ecu_page = self.get_ecu_page(*number).await?;
            let xcp_page = self.get_xcp_page(*number).await?;
            info!("Calibration segment {}: ecu_page={}, xcp_page={}", number, ecu_page, xcp_page);
        }

        // Set all segments to working page 0
        if calseg_numbers.len() > 0 {
            info!("Set ECU page access to working page for all segments");
            self.set_ecu_page(0).await?;
            info!("Set XCP page access to working page for all segments");
            self.set_xcp_page(0).await?;
        }

        Ok(())
    }

    //---------------------------------------------------------------------------------
    // Upload and Download of calibration data

    /// Load complete calibration segments from an Intel-HEX file.
    /// Empty segments and segments absent from the file are skipped. Partial data,
    /// conflicting overlaps, and address overflow are rejected before any XCP writes.
    pub async fn load_calibration_segments_from_file<P: AsRef<std::path::Path>>(&mut self, bin_path: &P) -> Result<(), Box<dyn Error>> {
        info!("Load calibration segments from file {}", bin_path.as_ref().display());

        // Read the Intel-Hex file
        let file_content = std::fs::read_to_string(bin_path)?;
        // Store whole records rather than allocating an address entry for every byte.
        // Convert addresses before sorting, since address records apply in file order.
        let mut hex_records = Vec::new();
        let mut base = 0u32;
        for record in ihex::Reader::new(&file_content) {
            match record? {
                ihex::Record::ExtendedLinearAddress(upper) => base = (upper as u32) << 16,
                ihex::Record::ExtendedSegmentAddress(segment) => base = (segment as u32) << 4,
                ihex::Record::Data { offset, value } => {
                    if value.is_empty() {
                        continue;
                    }
                    let start = base.checked_add(offset as u32).ok_or("Intel-HEX address overflow")?;
                    start.checked_add(value.len() as u32 - 1).ok_or("Intel-HEX address overflow")?;
                    hex_records.push((start, value));
                }
                ihex::Record::EndOfFile => break,
                _ => {}
            }
        }

        // Merge adjacent or overlapping records into sorted, disjoint ranges.
        // Sparse gaps stay unallocated; overlaps must contain identical bytes.
        hex_records.sort_unstable_by_key(|(address, _)| *address);
        let mut hex_ranges: Vec<(u32, Vec<u8>)> = Vec::new();
        for (address, data) in hex_records {
            // Sorting guarantees that only the last merged range can overlap
            // this record; all earlier ranges end before its starting address.
            if let Some((start, bytes)) = hex_ranges.last_mut() {
                // An exclusive end can be 2^32 when the final byte is at UINT32_MAX.
                let end = *start as u64 + bytes.len() as u64;
                if address as u64 <= end {
                    let offset = (address - *start) as usize;
                    let overlap = data.len().min(bytes.len() - offset);
                    if let Some(index) = data[..overlap].iter().zip(&bytes[offset..offset + overlap]).position(|(a, b)| a != b) {
                        let address = address + index as u32;
                        return Err(format!("Conflicting HEX data at address 0x{address:08X}").into());
                    }
                    // Contained duplicates add nothing; extending records add only their new tail.
                    bytes.extend_from_slice(&data[overlap..]);
                    continue;
                }
            }
            hex_ranges.push((address, data));
        }

        // Prepare every supplied segment before sending commands, so invalid data
        // in a later segment cannot leave earlier segments already downloaded.
        let mut downloads = Vec::new();
        for segment in &self.registry.as_ref().unwrap().cal_seg_list {
            let (seg_index, seg_name, seg_length, addr_ext, addr) =
                (segment.get_index(), segment.get_name(), segment.size, segment.addr_ext, segment.addr);
            if seg_length == 0 {
                continue;
            }
            let last = addr.checked_add(seg_length - 1).ok_or_else(|| format!("Calibration segment {seg_name} address overflow"))?;
            // Disjoint sorted ranges also have sorted ends, allowing a binary
            // search past all ranges that finish before this segment.
            let first = hex_ranges.partition_point(|(start, bytes)| *start as u64 + bytes.len() as u64 <= addr as u64);
            let mut data = Vec::new();
            for (start, bytes) in &hex_ranges[first..] {
                if *start > last {
                    break;
                }
                let begin = addr.saturating_sub(*start) as usize;
                // Clamp to the intersection. Its exclusive end needs u64 when
                // the segment's last byte is at address 0xFFFFFFFF.
                let end = (last as u64 + 1 - *start as u64).min(bytes.len() as u64) as usize;
                data.extend_from_slice(&bytes[begin..end]);
            }
            if data.is_empty() {
                warn!("  No data found in IHEX file for segment {} at address 0x{:08X}", seg_name, addr);
                continue;
            }
            // Disjoint ranges contain `seg_length` bytes only if no bytes are missing.
            if data.len() != seg_length as usize {
                return Err(format!("Incomplete HEX data for segment {seg_name} at 0x{addr:08X}: expected {seg_length} bytes, got {}", data.len()).into());
            }
            downloads.push((seg_index, seg_name, addr_ext, addr, data));
        }
        // Release the input image before potentially slow network transfers.
        drop(hex_ranges);
        drop(file_content);

        for (seg_index, seg_name, addr_ext, addr, data) in downloads {
            info!(" Load segment {} (index={} addr={}:0x{:08X} length={})", seg_name, seg_index, addr_ext, addr, data.len());
            self.set_mta(addr_ext, addr).await?;
            self.download_memory_block(&data).await?;
            debug!("  Downloaded {} bytes to segment {}", data.len(), seg_name);
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
            self.set_mta(addr_ext, addr).await.unwrap();
            let data: Vec<u8> = self.upload_memory_block(size).await.unwrap();
            debug!("  Uploaded {} bytes from segment {}", data.len(), name);
            cal_seg_desc.push((
                CalSegDescriptor {
                    index,
                    size: size as u16,
                    addr,
                    app_id: 0,
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
                    app_id: 0,
                    name: event.get_name().to_string(),
                })
                .collect::<Vec<_>>();
            write_bin_file(&bin_path.as_ref().to_path_buf(), self.get_epk().unwrap(), &event_desc, &cal_seg_desc)?;
        }

        info!("Successfully saved {} segment(s) to {}", cal_seg_desc.len(), bin_path.as_ref().display());

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use xcp_registry::{McAddress, McDimType, McObjectType, McSupportData, McValueType, Registry};

    // Registry as created from an ELF file (--elf): a calibration segment struct is one instance of a typedef
    fn struct_characteristic_registry() -> Registry {
        let mut reg = Registry::new();
        let cal = McSupportData::new(McObjectType::Characteristic);
        reg.add_typedef("parameters", 8).unwrap();
        reg.add_typedef_field("parameters", "period_ms", McDimType::new(McValueType::Ulong, 1, 1), cal.clone(), 0)
            .unwrap();
        reg.add_typedef_field("parameters", "counter_max", McDimType::new(McValueType::Uword, 1, 1), cal.clone(), 4)
            .unwrap();
        reg.instance_list
            .add_instance(
                "parameters",
                McDimType::new(McValueType::new_typedef("parameters"), 1, 1),
                cal,
                McAddress::new_a2l(0x1000, 0),
            )
            .unwrap();
        reg
    }

    fn new_client(reg: Registry) -> XcpClient {
        let addr: SocketAddr = "127.0.0.1:5555".parse().unwrap();
        let mut client = XcpClient::new("UDP", addr, addr, 0);
        client.set_registry(reg);
        client
    }

    // A typedef instance is not a basic type value, this is an error, not a panic
    #[tokio::test]
    async fn create_calibration_object_of_struct_instance_fails() {
        let mut client = new_client(struct_characteristic_registry());
        assert_eq!(client.find_characteristics(".*"), vec!["parameters".to_string()]);
        assert!(client.create_calibration_object("parameters").await.is_err());
    }

    // The flattened registry (main.rs flattens it after the A2L file is written) has the struct members as basic type values
    #[tokio::test]
    async fn create_calibration_object_of_flattened_struct_member() {
        let mut reg = struct_characteristic_registry();
        reg.flatten_typedefs();
        let mut client = new_client(reg);
        let mut names = client.find_characteristics(".*");
        names.sort();
        assert_eq!(names, vec!["parameters.counter_max".to_string(), "parameters.period_ms".to_string()]);
        let h = client.create_calibration_object("parameters.counter_max").await.unwrap();
        let o = client.get_calibration_object(h);
        assert_eq!(o.get_a2l_addr().addr, 0x1004);
        assert_eq!(o.get_a2l_type().size, 2);
    }

    // HEX loader regressions: cargo test xcp_client::cal::tests::load_hex
    // Each test owns a separate fixture, removed by Drop even when an assertion fails.
    struct HexFixture(PathBuf);
    impl HexFixture {
        fn new(records: &[ihex::Record]) -> Self {
            static NEXT: AtomicUsize = AtomicUsize::new(0);
            let path = std::env::temp_dir().join(format!("xcplite-hex-test-{}-{}.hex", std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)));
            let fixture = Self(path);
            std::fs::write(&fixture.0, ihex::create_object_file_representation(records).unwrap()).unwrap();
            fixture
        }
    }
    impl Drop for HexFixture {
        fn drop(&mut self) {
            let _ = std::fs::remove_file(&self.0);
        }
    }

    fn hex_record(offset: u16, bytes: &[u8]) -> ihex::Record {
        ihex::Record::Data { offset, value: bytes.to_vec() }
    }

    // Only the reply channel is simulated. Parsing, segment assembly, serialization,
    // and actual UDP command transmission all use production code.
    async fn load_hex_fixture(fixture: &HexFixture, segments: &[(u32, u32)]) -> (Result<(), Box<dyn Error>>, Vec<Vec<u8>>) {
        let mut registry = Registry::new();
        for (index, &(address, size)) in segments.iter().enumerate() {
            registry.cal_seg_list
                .add_cal_seg_by_addr(format!("segment{index}"), Some(index as u8), 0, address, size)
                .unwrap();
        }
        let peer = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let socket = UdpSocket::bind("127.0.0.1:0").await.unwrap();
        let mut client = XcpClient::new("UDP", peer.local_addr().unwrap(), socket.local_addr().unwrap(), 0);
        // A 128-byte CTO leaves 126 bytes per DOWNLOAD. Larger fixtures exercise
        // chunking through the same peer as the segment reconstruction tests.
        client.max_cto_size = 128;
        client.set_registry(registry);
        client.socket = Some(XcpSocket::Udp(Arc::new(socket)));
        let (acknowledge, receiver) = mpsc::channel(1);
        client.rx_cmd_resp = Some(receiver);
        let (finish, mut finished) = tokio::sync::oneshot::channel::<()>();
        let server = tokio::spawn(async move {
            let mut commands = Vec::new();
            let mut packet = [0u8; 512];
            loop {
                tokio::select! {
                    _ = &mut finished => break,
                    result = peer.recv_from(&mut packet) => {
                        let (length, _) = result.unwrap();
                        assert!(length >= 5);
                        assert_eq!(u16::from_le_bytes([packet[0], packet[1]]) as usize, length - 4);
                        commands.push(packet[4..length].to_vec());
                        acknowledge.send(vec![0xFF]).await.unwrap();
                    }
                }
            }
            commands
        });
        tokio::time::timeout(Duration::from_secs(5), async {
            let result = client.load_calibration_segments_from_file(&fixture.0).await;
            finish.send(()).unwrap();
            (result, server.await.unwrap())
        })
        .await
        .expect("HEX test timed out")
    }

    fn downloaded_hex_data(commands: &[Vec<u8>]) -> BTreeMap<u32, u8> {
        let mut address = 0u64;
        let mut bytes = BTreeMap::new();
        for command in commands {
            match command[0] {
                CC_SET_MTA => {
                    assert_eq!(command.len(), 8);
                    address = u32::from_le_bytes(command[4..8].try_into().unwrap()) as u64;
                }
                CC_DOWNLOAD => {
                    assert_eq!(command[1] as usize, command.len() - 2);
                    for &byte in &command[2..] {
                        assert!(address <= u32::MAX as u64);
                        bytes.insert(address as u32, byte);
                        address += 1;
                    }
                }
                other => panic!("unexpected command {other}"),
            }
        }
        bytes
    }

    fn expected_hex_data(address: u32, values: &[u8]) -> BTreeMap<u32, u8> {
        values.iter().enumerate().map(|(index, &byte)| (address + index as u32, byte)).collect()
    }

    #[tokio::test]
    async fn load_hex_round_trip_restores_all_records() {
        let fixture = HexFixture::new(&[ihex::Record::EndOfFile]);
        let data: Vec<u8> = (0..93).collect();
        // The real writer emits three records (32 + 32 + 29 bytes).
        // Restoring all 93 bytes catches the original first-record-only bug.
        let descriptor = CalSegDescriptor {
            index: 0,
            size: 93,
            addr: 0x1000,
            app_id: 0,
            name: "hex_test".into(),
        };
        write_hex_file(&fixture.0, &[(descriptor, data.clone())]).unwrap();
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 93)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1000, &data));
    }

    #[tokio::test]
    async fn load_hex_large_segment_advances_mta_across_downloads() {
        let fixture = HexFixture::new(&[ihex::Record::EndOfFile]);
        // Vary the bytes so skipped, repeated, or reordered chunks change the result.
        let data: Vec<u8> = (0..300).map(|index| index as u8).collect();
        let address = 0x1000;
        let descriptor = CalSegDescriptor {
            index: 0,
            size: 300,
            addr: address,
            app_id: 0,
            name: "large_hex_test".into(),
        };
        // The writer emits 32-byte HEX records. The loader must assemble them
        // before splitting the segment into protocol-sized DOWNLOAD commands.
        write_hex_file(&fixture.0, &[(descriptor, data.clone())]).unwrap();
        let (result, commands) = load_hex_fixture(&fixture, &[(address, 300)]).await;
        result.unwrap();

        // The helper uses MAX_CTO=128; two header bytes leave 126 payload bytes.
        // Thus 300 bytes require 126 + 126 + 48, with only one initial SET_MTA.
        assert_eq!(commands.len(), 4, "expected one SET_MTA and three DOWNLOAD commands");
        assert_eq!(commands.iter().filter(|command| command[0] == CC_SET_MTA).count(), 1);
        assert_eq!(commands[0][0], CC_SET_MTA);
        assert_eq!(u32::from_le_bytes(commands[0][4..8].try_into().unwrap()), address);
        // Replay each DOWNLOAD's byte count as a peer advances its MTA, checking
        // the expected position after every chunk rather than only the final sum.
        let mut mta = address;
        for (command, expected_mta) in commands[1..].iter().zip([address + 126, address + 252, address + 300]) {
            assert_eq!(command[0], CC_DOWNLOAD);
            mta += command[1] as u32;
            assert_eq!(mta, expected_mta, "incorrect MTA advancement between chunks");
        }
        // Check all 300 bytes at their expected addresses across chunk boundaries.
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(address, &data));
    }

    #[tokio::test]
    async fn load_hex_out_of_order_records_are_assembled_by_address() {
        let data: Vec<u8> = (0..48).collect();
        let fixture = HexFixture::new(&[hex_record(0x1020, &data[32..]), hex_record(0x1000, &data[..16]), hex_record(0x1010, &data[16..32]), ihex::Record::EndOfFile]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 48)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1000, &data));
    }

    #[tokio::test]
    async fn load_hex_identical_overlaps_are_allowed() {
        let complete = hex_record(0x1000, &[1, 2, 3, 4]);
        let overlap = hex_record(0x1002, &[3, 4]);
        // Identical bytes are harmless regardless of record order.
        for records in [
            [complete.clone(), overlap.clone(), ihex::Record::EndOfFile],
            [overlap, complete, ihex::Record::EndOfFile],
        ] {
            let fixture = HexFixture::new(&records);
            let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 4)]).await;
            result.unwrap();
            assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1000, &[1, 2, 3, 4]));
        }
    }

    #[tokio::test]
    async fn load_hex_conflicting_overlaps_are_rejected() {
        let complete = hex_record(0x1000, &[1, 2, 3, 4]);
        let conflict = hex_record(0x1002, &[0xEE, 4]);
        // A conflicting byte must fail before any writes, not depend on which
        // record happens to occur last in the file.
        for records in [
            [complete.clone(), conflict.clone(), ihex::Record::EndOfFile],
            [conflict, complete, ihex::Record::EndOfFile],
        ] {
            let fixture = HexFixture::new(&records);
            let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 4)]).await;
            let error = result.expect_err("conflicting HEX data reported success");
            assert_eq!(error.to_string(), "Conflicting HEX data at address 0x00001002");
            assert!(commands.is_empty(), "conflicting HEX data changed calibration values");
        }
    }

    #[tokio::test]
    async fn load_hex_overlaps_can_contain_or_extend_records() {
        // Combine a contained duplicate, a partial overlap extending the tail,
        // and an empty record. Only the six distinct bytes should be downloaded.
        let fixture = HexFixture::new(&[
            hex_record(0x1002, &[3, 4, 5, 6]),
            hex_record(0x1001, &[2, 3]),
            hex_record(0x1000, &[]),
            hex_record(0x1000, &[1, 2]),
            hex_record(0x1000, &[1, 2, 3, 4]),
            hex_record(0x1000, &[1, 2, 3, 4]),
            ihex::Record::EndOfFile,
        ]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 6)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1000, &[1, 2, 3, 4, 5, 6]));
    }

    #[tokio::test]
    async fn load_hex_sparse_ranges_preserve_large_gaps() {
        // Widely separated addresses must not allocate or fill the gap between them.
        let fixture = HexFixture::new(&[
            hex_record(0x1000, &[1, 2, 3, 4]),
            ihex::Record::ExtendedLinearAddress(0xFFFF),
            hex_record(0xFFFF, &[9]),
            ihex::Record::EndOfFile,
        ]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 4), (u32::MAX, 1)]).await;
        result.unwrap();
        let mut expected = expected_hex_data(0x1000, &[1, 2, 3, 4]);
        expected.insert(u32::MAX, 9);
        assert_eq!(downloaded_hex_data(&commands), expected);
    }

    #[tokio::test]
    async fn load_hex_record_can_cover_multiple_segments() {
        // One HEX record contains both registered segments and extra bytes.
        // The 16-byte gap at 0x1020..0x1030 must not be written to the target.
        let data: Vec<u8> = (0..64).collect();
        let fixture = HexFixture::new(&[hex_record(0x1000, &data), ihex::Record::EndOfFile]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1010, 16), (0x1030, 16)]).await;
        result.unwrap();
        // Continuing from the first segment's MTA would write into the gap.
        // Each segment therefore needs its own SET_MTA with the correct base.
        let mta_commands: Vec<_> = commands.iter().filter(|command| command[0] == CC_SET_MTA).collect();
        assert_eq!(mta_commands.len(), 2, "each separated segment must set its own MTA");
        assert_eq!(u32::from_le_bytes(mta_commands[0][4..8].try_into().unwrap()), 0x1010);
        assert_eq!(u32::from_le_bytes(mta_commands[1][4..8].try_into().unwrap()), 0x1030);
        // Check each segment separately, then rule out writes outside those ranges.
        let downloaded = downloaded_hex_data(&commands);
        assert_eq!(downloaded.range(0x1010..0x1020).map(|(_, byte)| *byte).collect::<Vec<_>>(), data[16..32]);
        assert_eq!(downloaded.range(0x1030..0x1040).map(|(_, byte)| *byte).collect::<Vec<_>>(), data[48..64]);
        assert_eq!(downloaded.len(), 32, "the gap between segments must not be downloaded");
    }

    #[tokio::test]
    async fn load_hex_linear_address_boundary_is_preserved() {
        let data: Vec<u8> = (0..64).collect();
        // A segment can cross a 64 KiB boundary and require two address records.
        let fixture = HexFixture::new(&[
            ihex::Record::ExtendedLinearAddress(0x1234),
            hex_record(0xFFE0, &data[..32]),
            ihex::Record::ExtendedLinearAddress(0x1235),
            hex_record(0, &data[32..]),
            ihex::Record::EndOfFile,
        ]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1234FFE0, 64)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1234FFE0, &data));
    }

    #[tokio::test]
    async fn load_hex_segment_addresses_are_supported() {
        let fixture = HexFixture::new(&[ihex::Record::ExtendedSegmentAddress(0x1000), hex_record(0x20, &[1, 2, 3, 4]), ihex::Record::EndOfFile]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x10020, 4)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x10020, &[1, 2, 3, 4]));
    }

    #[tokio::test]
    async fn load_hex_incomplete_segments_fail_before_any_writes() {
        // Exercise a missing tail, missing start, and interior hole. The complete
        // first segment must remain untouched when the later segment is invalid.
        for incomplete in [
            vec![hex_record(0x2000, &[0, 1])],
            vec![hex_record(0x2001, &[1, 2, 3])],
            vec![hex_record(0x2000, &[0, 1]), hex_record(0x2003, &[3])],
        ] {
            let mut records = vec![hex_record(0x1000, &[1, 2, 3, 4])];
            records.extend(incomplete);
            records.push(ihex::Record::EndOfFile);
            let fixture = HexFixture::new(&records);
            let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 4), (0x2000, 4)]).await;
            let error = result.expect_err("incomplete segment reported success").to_string();
            assert!(error.contains("Incomplete HEX data") && error.contains("segment1"), "unexpected error: {error}");
            assert!(commands.is_empty(), "validation failure changed calibration data");
        }
    }

    #[tokio::test]
    async fn load_hex_absent_segments_are_still_skipped() {
        // Partial input is an error, but an entirely omitted segment is still skipped.
        let fixture = HexFixture::new(&[hex_record(0x1000, &[1, 2, 3, 4]), ihex::Record::EndOfFile]);
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 4), (0x2000, 4)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1000, &[1, 2, 3, 4]));
    }

    #[tokio::test]
    async fn load_hex_zero_length_segments_are_skipped() {
        let fixture = HexFixture::new(&[hex_record(0x1000, &[1, 2, 3, 4]), ihex::Record::EndOfFile]);
        // Even at the last address, an empty segment requires no range arithmetic
        // or commands, whether or not the HEX file has data at its base address.
        let (result, commands) = load_hex_fixture(&fixture, &[(0x1000, 0), (u32::MAX, 0)]).await;
        result.unwrap();
        assert!(commands.is_empty(), "zero-length segments generated commands");

        // Skipping an empty segment must still allow subsequent segments to load.
        let (result, commands) = load_hex_fixture(&fixture, &[(u32::MAX, 0), (0x1000, 4)]).await;
        result.unwrap();
        assert_eq!(commands.len(), 2, "expected SET_MTA and DOWNLOAD only for the nonempty segment");
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(0x1000, &[1, 2, 3, 4]));
    }

    #[tokio::test]
    async fn load_hex_addresses_must_not_wrap() {
        let fixture = HexFixture::new(&[ihex::Record::ExtendedLinearAddress(0xFFFF), hex_record(0xFFFF, &[1, 2]), ihex::Record::EndOfFile]);
        let (result, commands) = load_hex_fixture(&fixture, &[(u32::MAX, 1)]).await;
        assert!(result.is_err());
        assert!(commands.is_empty());
        let fixture = HexFixture::new(&[ihex::Record::ExtendedLinearAddress(0xFFFF), hex_record(0xFFFF, &[1]), ihex::Record::EndOfFile]);
        let (result, commands) = load_hex_fixture(&fixture, &[(u32::MAX, 2)]).await;
        assert!(result.is_err());
        assert!(commands.is_empty());
        let (result, commands) = load_hex_fixture(&fixture, &[(u32::MAX, 1)]).await;
        result.unwrap();
        assert_eq!(downloaded_hex_data(&commands), expected_hex_data(u32::MAX, &[1]));
    }
}
