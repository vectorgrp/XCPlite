#![allow(dead_code)]

use std::fs::File;
use std::io::Read;
use std::mem;

use crate::Bin2HexError;

// Compatibility with C code in persistency.h
// The BIN_VERSION check at runtime is the actual protection against format mismatches.
// If the C code changes the format, it should bump BIN_VERSION, and this tool will reject the file.

const BIN_SIGNATURE: &str = "XCPLITE__BINARY";
const BIN_VERSION: u16 = 0x0203;

// This code only assumes the size of the header and descriptor structures
const BIN_HEADER_SIZE: usize = 256;
const BIN_EVENT_DESC_SIZE: usize = 256;
const BIN_CALSEG_DESC_SIZE: usize = 256;

/// BIN file header - corresponds to tHeader in C
/// Version 0x0203: 256 bytes total

#[repr(C, packed)]
struct BinHeaderRaw {
    signature: [u8; 16],                               // "XCPLITE__BINARY"
    version: u16,                                      // 0x0203
    event_count: u16,                                  // Number of event descriptors
    calseg_count: u16,                                 // Number of calibration segment descriptors
    reserved: [u8; 128],                               // Reserved for future use
    epk: [u8; BIN_HEADER_SIZE - 16 - 2 - 2 - 2 - 128], // EPK string, null terminated (106 bytes remaining)
}

/// Safe wrapper for BinHeaderRaw
#[derive(Debug, Clone)]
pub struct BinHeader {
    pub signature: String,
    pub version: u16,
    pub event_count: u16,
    pub calseg_count: u16,
    pub epk: String,
}

impl BinHeader {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        // Read raw struct directly from file using unsafe
        let raw: BinHeaderRaw = unsafe {
            let mut raw: BinHeaderRaw = mem::zeroed();
            let raw_ptr = &mut raw as *mut BinHeaderRaw as *mut u8;
            let raw_slice = std::slice::from_raw_parts_mut(raw_ptr, mem::size_of::<BinHeaderRaw>());
            file.read_exact(raw_slice)?;
            raw
        };

        // Extract and verify signature
        let signature = String::from_utf8_lossy(&raw.signature)
            .trim_end_matches('\0')
            .to_string();

        if signature != BIN_SIGNATURE {
            return Err(Bin2HexError::InvalidFormat(format!(
                "Invalid signature: expected '{}', got '{}'",
                BIN_SIGNATURE, signature
            )));
        }

        // Extract EPK
        let epk = String::from_utf8_lossy(&raw.epk)
            .trim_end_matches('\0')
            .to_string();

        // Copy values from packed struct to avoid unaligned access
        let version = raw.version;
        let event_count = raw.event_count;
        let calseg_count = raw.calseg_count;

        // Check version - only 0x0203 is supported
        if version != BIN_VERSION {
            return Err(Bin2HexError::InvalidFormat(format!(
                "Unsupported file version 0x{:04X}. This tool only supports version 0x{:04X}.",
                version, BIN_VERSION
            )));
        }

        Ok(BinHeader {
            signature,
            version,
            event_count,
            calseg_count,
            epk,
        })
    }
}

/// Event descriptor - corresponds to tEventDescriptor in C
/// New version 0x0203: 256 bytes total
/// Fields: 2 + 2 + 4 + 1 + 128 + (256 - 2 - 2 - 4 - 1 - 128) = 256 bytes
#[repr(C, packed)]
struct EventDescriptorRaw {
    id: u16,                                               // Event ID
    index: u16,                                            // Event index
    cycle_time_ns: u32,                                    // Cycle time in nanoseconds
    priority: u8,        // Priority (0=queued, 1=pushing, 2=realtime)
    reserved: [u8; 128], // Reserved for future use
    name: [u8; BIN_EVENT_DESC_SIZE - 2 - 2 - 4 - 1 - 128], // Event name, null terminated (119 bytes remaining)
}

/// Safe wrapper for EventDescriptorRaw
#[derive(Debug, Clone)]
pub struct EventDescriptor {
    pub id: u16,
    pub index: u16,
    pub cycle_time_ns: u32,
    pub priority: u8,
    pub name: String,
}

impl EventDescriptor {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        // Read raw struct directly from file using unsafe
        let raw: EventDescriptorRaw = unsafe {
            let mut raw: EventDescriptorRaw = mem::zeroed();
            let raw_ptr = &mut raw as *mut EventDescriptorRaw as *mut u8;
            let raw_slice =
                std::slice::from_raw_parts_mut(raw_ptr, mem::size_of::<EventDescriptorRaw>());
            file.read_exact(raw_slice)?;
            raw
        };

        // Extract name
        let name = String::from_utf8_lossy(&raw.name)
            .trim_end_matches('\0')
            .to_string();

        // Copy values from packed struct to avoid unaligned access
        let id = raw.id;
        let index = raw.index;
        let cycle_time_ns = raw.cycle_time_ns;
        let priority = raw.priority;

        Ok(EventDescriptor {
            id,
            index,
            cycle_time_ns,
            priority,
            name,
        })
    }
}

/// Calibration segment descriptor - corresponds to tCalSegDescriptor in C
/// New version 0x0203: 256 bytes total
/// Fields: 2 + 2 + 4 + 128 + (256 - 2 - 2 - 4 - 128) = 256 bytes
#[repr(C, packed)]
struct CalSegDescriptorRaw {
    index: u16,                                         // Index in calibration segment list
    size: u16,                                          // Size in bytes, multiple of 4
    addr: u32,                                          // Address of the calibration segment
    reserved: [u8; 128],                                // Reserved for future use
    name: [u8; BIN_CALSEG_DESC_SIZE - 2 - 2 - 4 - 128], // Calibration segment name, null terminated (120 bytes remaining)
}

/// Safe wrapper for CalSegDescriptorRaw
#[derive(Debug, Clone)]
pub struct CalSegDescriptor {
    pub index: u16,
    pub size: u16,
    pub addr: u32,
    pub name: String,
}

impl CalSegDescriptor {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        // Read raw struct directly from file using unsafe
        let raw: CalSegDescriptorRaw = unsafe {
            let mut raw: CalSegDescriptorRaw = mem::zeroed();
            let raw_ptr = &mut raw as *mut CalSegDescriptorRaw as *mut u8;
            let raw_slice =
                std::slice::from_raw_parts_mut(raw_ptr, mem::size_of::<CalSegDescriptorRaw>());
            file.read_exact(raw_slice)?;
            raw
        };

        // Extract name
        let name = String::from_utf8_lossy(&raw.name)
            .trim_end_matches('\0')
            .to_string();

        // Copy values from packed struct to avoid unaligned access
        let index = raw.index;
        let size = raw.size;
        let addr = raw.addr;

        Ok(CalSegDescriptor {
            index,
            size,
            addr,
            name,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_struct_sizes() {
        // Verify struct sizes match C definitions for version 0x0203
        // All structs must be exactly 256 bytes
        assert_eq!(mem::size_of::<BinHeaderRaw>(), 256);
        assert_eq!(mem::size_of::<EventDescriptorRaw>(), 256);
        assert_eq!(mem::size_of::<CalSegDescriptorRaw>(), 256);
    }
}
