//--------------------------------------------------------------------------------------------------------------------------------------------------
// a2l.rs — ELF/A2L file upload and registry population

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::error::Error;
use std::io::Write;

use xcp_registry::McEvent;

use super::*;

impl XcpClient {
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
    pub async fn upload_a2l_into_registry<P: AsRef<std::path::Path>>(&mut self, a2l_path: &P, reg: &mut xcp_registry::Registry) -> Result<(), Box<dyn Error>> {
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
    pub fn load_a2l_file_into_registry<P: AsRef<std::path::Path>>(&mut self, a2l_path: &P, reg: &mut xcp_registry::Registry) -> Result<(), Box<dyn Error>> {
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

    pub async fn get_event_segment_info(&mut self, reg: &mut xcp_registry::Registry) -> Result<(), Box<dyn Error>> {
        info!("Reading event and segment information from connected XCP server:");

        // Get event information
        for i in 0..self.max_events {
            let name = self.get_daq_event_info(i).await?;
            info!(" Event {}: {}", i, name);
            reg.event_list.add_event(McEvent::new(name, 0, i, 0)).unwrap();
        }

        // Get segment information
        for i in 0..self.max_segments {
            let (addr_ext, addr, length, name) = self.get_segment_info(i).await?;
            info!(" Segment {}: {} addr={}:0x{:08X} length={} ", i, name, addr_ext, addr, length);

            // Segment relative addressing is ignored, all addresses are treated as raw A2L addr_ext/addr
            reg.cal_seg_list.add_cal_seg_by_addr(name, Some(i), addr_ext, addr, length as u32).unwrap();
        }

        Ok(())
    }

    //------------------------------------------------------------------------
    // Registry
    // Get a list of available measurement and calibration object names from registry matching a regular expression

    pub fn get_registry(&self) -> &xcp_registry::Registry {
        self.registry.as_ref().unwrap()
    }

    pub fn find_characteristics(&self, expr: &str) -> Vec<String> {
        let registry = self.registry.as_ref().unwrap();
        registry.instance_list.find_instances_regex(expr, xcp_registry::McObjectType::Characteristic, None)
    }

    pub fn find_measurements(&self, expr: &str) -> Vec<String> {
        let registry = self.registry.as_ref().unwrap();
        registry.instance_list.find_instances_regex(expr, xcp_registry::McObjectType::Measurement, None)
    }
}
