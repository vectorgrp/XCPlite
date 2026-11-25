#![allow(dead_code)]

use std::fs::File;
use std::io::{Read, Write};
use std::mem;

use super::Bin2HexError;

// Compatibility with the C code in src/persistence.c (tBinHeader, tBinContext, tBinEvent, tBinCalSeg, tBinApp)
// The BIN_VERSION check at runtime is the actual protection against format mismatches.
// If the C code changes the format, it should bump BIN_VERSION, and this tool will reject the file.
//
// File layout:
//   BinHeader
//   ContextDescriptor[context_count]          in context id order (0x0206+)
//   EventDescriptor[event_count]              in event id order
//   (CalSegDescriptor + data)[calseg_count]   in calibration segment index order
//   AppDescriptor[app_count]                  SHM mode only (0x0205+)
pub const BIN_SIGNATURE: &str = "XCPLITE__BINARY";
pub const BIN_VERSION: u16 = 0x0206; // Context list after the header, events reference their context by id
pub const BIN_VERSION_0205: u16 = 0x0205; // Without context list
pub const BIN_VERSION_LEGACY: u16 = 0x0204; // Without context list and application list

// Maximum name lengths of the C code (src/xcp_cfg.h), without the terminating 0
pub const XCP_MAX_CONTEXT_NAME: usize = 15;

/// Read a packed raw struct from the file
fn read_raw<T>(file: &mut File) -> Result<T, Bin2HexError> {
    // SAFETY: T is a packed struct of integers and byte arrays, every bit pattern is valid
    unsafe {
        let mut raw: T = mem::zeroed();
        let raw_slice = std::slice::from_raw_parts_mut(&mut raw as *mut T as *mut u8, mem::size_of::<T>());
        file.read_exact(raw_slice)?;
        Ok(raw)
    }
}

/// Write a packed raw struct to the file
fn write_raw<T>(file: &mut File, raw: &T) -> Result<(), Bin2HexError> {
    // SAFETY: T is a packed struct of integers and byte arrays without padding
    let raw_slice = unsafe { std::slice::from_raw_parts(raw as *const T as *const u8, mem::size_of::<T>()) };
    file.write_all(raw_slice)?;
    Ok(())
}

/// A null terminated string from a byte array, ends at the first null byte
fn cstr(bytes: &[u8]) -> String {
    let len = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..len]).to_string()
}

/// Copy a string into a byte array, truncated, the last byte stays 0
fn copy_cstr(dst: &mut [u8], s: &str) {
    let bytes = s.as_bytes();
    let len = bytes.len().min(dst.len() - 1);
    dst[..len].copy_from_slice(&bytes[..len]);
}

//----------------------------------------------------------------------------------------------
// Header

/// BIN file header - corresponds to tBinHeader in C
#[repr(C, packed)]
struct BinHeaderRaw {
    signature: [u8; 16],                          // "XCPLITE__BINARY"
    version: u16,                                 // File version
    event_count: u16,                             // Number of event descriptors
    calseg_count: u16,                            // Number of calibration segment descriptors
    app_count: u16,                               // Number of application descriptors (0x0205+)
    context_count: u16,                           // Number of context descriptors (0x0206+)
    reserved: [u8; 128 - 16 - 2 - 2 - 2 - 2 - 2], // Reserved for future use
    epk: [u8; 128],                               // EPK string, null terminated
}

/// Safe wrapper for BinHeaderRaw
#[derive(Debug, Clone)]
pub struct BinHeader {
    pub signature: String,
    pub version: u16,
    pub event_count: u16,
    pub calseg_count: u16,
    pub app_count: u16,
    pub context_count: u16,
    pub epk: String,
}

impl BinHeader {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        let raw: BinHeaderRaw = read_raw(file)?;

        // Extract and verify signature
        let signature = cstr(&raw.signature);
        if signature != BIN_SIGNATURE {
            return Err(Bin2HexError::InvalidFormat(format!("Invalid signature: expected '{}', got '{}'", BIN_SIGNATURE, signature)));
        }

        // Check version
        let version = raw.version;
        if version != BIN_VERSION && version != BIN_VERSION_0205 && version != BIN_VERSION_LEGACY {
            return Err(Bin2HexError::InvalidFormat(format!(
                "Unsupported file version 0x{:04X}. This tool supports versions 0x{:04X}, 0x{:04X} and 0x{:04X}.",
                version, BIN_VERSION, BIN_VERSION_0205, BIN_VERSION_LEGACY
            )));
        }

        // The application list exists since 0x0205, the context list since 0x0206, the counts are reserved bytes before
        Ok(BinHeader {
            signature,
            version,
            event_count: raw.event_count,
            calseg_count: raw.calseg_count,
            app_count: if version >= BIN_VERSION_0205 { raw.app_count } else { 0 },
            context_count: if version >= BIN_VERSION { raw.context_count } else { 0 },
            epk: cstr(&raw.epk),
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        let mut raw: BinHeaderRaw = unsafe { mem::zeroed() };
        copy_cstr(&mut raw.signature, &self.signature);
        raw.version = self.version;
        raw.event_count = self.event_count;
        raw.calseg_count = self.calseg_count;
        raw.app_count = self.app_count;
        raw.context_count = self.context_count;
        copy_cstr(&mut raw.epk, &self.epk);
        write_raw(file, &raw)
    }
}

//----------------------------------------------------------------------------------------------
// Context

/// Context descriptor - corresponds to tBinContext in C
/// The context list follows the header, in context id order
#[repr(C, packed)]
struct ContextDescriptorRaw {
    id: u16,                                         // Context id 1..n, the position in the list + 1
    app_id: u8,                                      // App ID of the context owner in SHM mode, 0 in local mode
    reserved: [u8; 128 - 2 - 1],                     // Reserved for future use
    name: [u8; XCP_MAX_CONTEXT_NAME + 1],            // Context name, null terminated
    padding: [u8; 128 - (XCP_MAX_CONTEXT_NAME + 1)], // Reserved for longer context names
}

/// Safe wrapper for ContextDescriptorRaw
#[derive(Debug, Clone, PartialEq)]
pub struct ContextDescriptor {
    pub id: u16,
    pub app_id: u8,
    pub name: String,
}

impl ContextDescriptor {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        let raw: ContextDescriptorRaw = read_raw(file)?;
        Ok(ContextDescriptor {
            id: raw.id,
            app_id: raw.app_id,
            name: cstr(&raw.name),
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        let mut raw: ContextDescriptorRaw = unsafe { mem::zeroed() };
        raw.id = self.id;
        raw.app_id = self.app_id;
        copy_cstr(&mut raw.name, &self.name);
        write_raw(file, &raw)
    }
}

/// Read the context list, the file position must be right after the header
pub fn read_context_list(file: &mut File, header: &BinHeader) -> Result<Vec<ContextDescriptor>, Bin2HexError> {
    let mut contexts = Vec::with_capacity(header.context_count as usize);
    for i in 0..header.context_count {
        let context = ContextDescriptor::read_from(file)?;
        if context.id != i + 1 {
            return Err(Bin2HexError::InvalidFormat(format!("Context descriptor {} has id {}, expected {}", i, context.id, i + 1)));
        }
        contexts.push(context);
    }
    Ok(contexts)
}

//----------------------------------------------------------------------------------------------
// Event

/// Event descriptor - corresponds to tBinEvent in C
#[repr(C, packed)]
struct EventDescriptorRaw {
    id: u16,                                 // Event ID
    context: u16,                            // Context id of the event, 0 = no context (0x0205: index of an event instance)
    cycle_time_ns: u32,                      // Cycle time in nanoseconds
    priority: u8,                            // Priority (0=queued, 1=pushing, 2=realtime)
    app_id: u8,                              // App ID (0x0205+)
    reserved: [u8; 128 - 2 - 2 - 4 - 1 - 1], // Reserved for future use
    name: [u8; 128],                         // Event name without the context name, null terminated
}

/// Safe wrapper for EventDescriptorRaw
#[derive(Debug, Clone)]
pub struct EventDescriptor {
    pub id: u16,
    pub context: u16, // Context id of the event, 0 = no context
    pub cycle_time_ns: u32,
    pub priority: u8,
    pub app_id: u8,
    pub name: String, // Event name without the context name
}

impl EventDescriptor {
    /// Name of the context of the event, None for an event without context or an unknown context id
    pub fn context_name<'a>(&self, contexts: &'a [ContextDescriptor]) -> Option<&'a str> {
        if self.context == 0 {
            return None;
        }
        contexts.get(self.context as usize - 1).map(|c| c.name.as_str())
    }

    /// Full event name as shown in the A2L file: <context>.<name> for an event which belongs to a context
    pub fn full_name(&self, contexts: &[ContextDescriptor]) -> String {
        match self.context_name(contexts) {
            Some(context) => format!("{}.{}", context, self.name),
            None => self.name.clone(),
        }
    }

    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        let raw: EventDescriptorRaw = read_raw(file)?;
        Ok(EventDescriptor {
            id: raw.id,
            context: raw.context,
            cycle_time_ns: raw.cycle_time_ns,
            priority: raw.priority,
            app_id: raw.app_id,
            name: cstr(&raw.name),
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        let mut raw: EventDescriptorRaw = unsafe { mem::zeroed() };
        raw.id = self.id;
        raw.context = self.context;
        raw.cycle_time_ns = self.cycle_time_ns;
        raw.priority = self.priority;
        raw.app_id = self.app_id;
        copy_cstr(&mut raw.name, &self.name);
        write_raw(file, &raw)
    }
}

//----------------------------------------------------------------------------------------------
// Calibration segment

/// Undefined memory segment number, the segment is not visible to the XCP client (XCP_UNDEFINED_CALSEG_NUM in C)
pub const CALSEG_NUMBER_UNDEFINED: u8 = 0xFF;

/// Calibration segment descriptor - corresponds to tBinCalSeg in C
#[repr(C, packed)]
struct CalSegDescriptorRaw {
    index: u16,                              // Index in calibration segment list
    size: u16,                               // Size in bytes, multiple of 4
    addr: u32,                               // Address of the calibration segment
    app_id: u8,                              // App ID (0x0205+)
    number: u8,                              // Memory segment number
    reserved: [u8; 128 - 2 - 2 - 4 - 1 - 1], // Reserved for future use
    name: [u8; 128],                         // Calibration segment name, null terminated
}

/// Safe wrapper for CalSegDescriptorRaw
#[derive(Debug, Clone)]
pub struct CalSegDescriptor {
    pub index: u16,
    pub size: u16,
    pub addr: u32,
    pub app_id: u8,
    pub number: u8, // Memory segment number, CALSEG_NUMBER_UNDEFINED for a segment which is not visible
    pub name: String,
}

impl CalSegDescriptor {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        let raw: CalSegDescriptorRaw = read_raw(file)?;
        Ok(CalSegDescriptor {
            index: raw.index,
            size: raw.size,
            addr: raw.addr,
            app_id: raw.app_id,
            number: raw.number,
            name: cstr(&raw.name),
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        let mut raw: CalSegDescriptorRaw = unsafe { mem::zeroed() };
        raw.index = self.index;
        raw.size = self.size;
        raw.addr = self.addr;
        raw.app_id = self.app_id;
        raw.number = self.number;
        copy_cstr(&mut raw.name, &self.name);
        write_raw(file, &raw)
    }
}

//----------------------------------------------------------------------------------------------
// Application

/// Application descriptor - corresponds to tBinApp in C (0x0205+)
/// Layout: [app_id(1) | xcp_init_mode(1) | reserved(126)] [project_name(32) | epk(32) | padding(64)]
/// XCP_PROJECT_NAME_MAX_LENGTH=31, XCP_EPK_MAX_LENGTH=31
#[repr(C, packed)]
struct AppDescriptorRaw {
    app_id: u8,             // App ID of the application
    xcp_init_mode: u8,      // Bitfield of XCP_MODE_XXX given to XcpInit()
    reserved: [u8; 126],    // Reserved for future use
    project_name: [u8; 32], // Application project name, null terminated (XCP_PROJECT_NAME_MAX_LENGTH+1)
    epk: [u8; 32],          // Build version / EPK, null terminated (XCP_EPK_MAX_LENGTH+1)
    padding: [u8; 64],      // 128 - 32 - 32 = 64
}

/// Safe wrapper for AppDescriptorRaw
#[derive(Debug, Clone)]
pub struct AppDescriptor {
    pub app_id: u8,
    pub xcp_init_mode: u8,
    pub project_name: String,
    pub epk: String,
}

impl AppDescriptor {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        let raw: AppDescriptorRaw = read_raw(file)?;
        Ok(AppDescriptor {
            app_id: raw.app_id,
            xcp_init_mode: raw.xcp_init_mode,
            project_name: cstr(&raw.project_name),
            epk: cstr(&raw.epk),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_struct_sizes() {
        // Verify struct sizes match C definitions
        // All structs must be exactly 256 bytes
        assert_eq!(mem::size_of::<BinHeaderRaw>(), 256);
        assert_eq!(mem::size_of::<ContextDescriptorRaw>(), 256);
        assert_eq!(mem::size_of::<EventDescriptorRaw>(), 256);
        assert_eq!(mem::size_of::<CalSegDescriptorRaw>(), 256);
        assert_eq!(mem::size_of::<AppDescriptorRaw>(), 256);
    }
}
