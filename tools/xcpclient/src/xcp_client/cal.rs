//--------------------------------------------------------------------------------------------------------------------------------------------------
// cal.rs — Calibration object management, value read/write, segment load/save

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::collections::HashMap;
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

    pub async fn load_calibration_segments_from_file<P: AsRef<std::path::Path>>(&mut self, bin_path: &P) -> Result<(), Box<dyn Error>> {
        info!("Load calibration segments from file {}", bin_path.as_ref().display());

        // Read the Intel-Hex file
        let file_content = std::fs::read_to_string(bin_path)?;
        let ihex_reader = ihex::Reader::new(file_content.as_str());

        // Parse all data records from the Intel-Hex file into a HashMap
        let mut hex_data: HashMap<u32, Vec<u8>> = HashMap::new();
        let mut extended_linear_address: u32 = 0;

        for record in ihex_reader {
            match record {
                Err(e) => {
                    error!("Error parsing IHEX record: {}", e);
                    return Err(Box::new(e) as Box<dyn Error>);
                }
                Ok(ihex::Record::ExtendedLinearAddress(upper_addr)) => {
                    extended_linear_address = (upper_addr as u32) << 16;
                    debug!("IHEX Extended Linear Address: upper=0x{:04X} (base=0x{:08X})", upper_addr, extended_linear_address);
                }
                Ok(ihex::Record::Data { offset, value }) => {
                    let full_address = extended_linear_address | (offset as u32);
                    debug!("IHEX Data record: offset=0x{:04X}, full_addr=0x{:08X}, length={}", offset, full_address, value.len());
                    hex_data.insert(full_address, value);
                }
                Ok(ihex::Record::EndOfFile) => {
                    debug!("IHEX End of file");
                    break;
                }
                Ok(_) => {
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

            if let Some(data) = hex_data.get(&addr) {
                if data.len() != seg_length as usize {
                    warn!("  Segment {} size mismatch: expected {} bytes, got {} bytes", seg_name, seg_length, data.len());
                }

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
