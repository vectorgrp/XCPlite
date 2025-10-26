#![allow(dead_code)]

use std::fs::File;
use std::io::Read;
use std::mem;

use crate::Bin2HexError;

// Constants from C code
const BIN_SIGNATURE: &str = "XCPLITE__BINARY";
const BIN_VERSION: u16 = 0x0100;

/// BIN file header - corresponds to tHeader in C
/// C struct: 16 + 2 + 32 + 2 + 2 + 4 = 58 bytes
#[repr(C, packed)]
struct BinHeaderRaw {
    signature: [u8; 16], // "XCPLITE__BINARY"
    version: u16,        // 0x0100
    epk: [u8; 32],       // EPK string, null terminated
    event_count: u16,    // Number of event descriptors
    calseg_count: u16,   // Number of calibration segment descriptors
    res: u32,            // Reserved
}

/// Safe wrapper for BinHeaderRaw
#[derive(Debug, Clone)]
pub struct BinHeader {
    pub signature: String,
    pub version: u16,
    pub epk: String,
    pub event_count: u16,
    pub calseg_count: u16,
    pub res: u32,
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
        let res = raw.res;

        // Check version (warning only)
        if version != BIN_VERSION {
            eprintln!(
                "Warning: File version 0x{:04X} differs from expected 0x{:04X}",
                version, BIN_VERSION
            );
        }

        Ok(BinHeader {
            signature,
            version,
            epk,
            event_count,
            calseg_count,
            res,
        })
    }
}

/// Event descriptor - corresponds to tEventDescriptor in C
/// C struct: 16 + 2 + 2 + 4 + 1 + 3 = 28 bytes
#[repr(C, packed)]
struct EventDescriptorRaw {
    name: [u8; 16],     // Event name, null terminated
    id: u16,            // Event ID
    index: u16,         // Event index
    cycle_time_ns: u32, // Cycle time in nanoseconds
    priority: u8,       // Priority (0=queued, 1=pushing, 2=realtime)
    res: [u8; 3],       // Reserved
}

/// Safe wrapper for EventDescriptorRaw
#[derive(Debug, Clone)]
pub struct EventDescriptor {
    pub name: String,
    pub id: u16,
    pub index: u16,
    pub cycle_time_ns: u32,
    pub priority: u8,
    pub res: [u8; 3],
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
        let res = raw.res;

        Ok(EventDescriptor {
            name,
            id,
            index,
            cycle_time_ns,
            priority,
            res,
        })
    }
}

/// Calibration segment descriptor - corresponds to tCalSegDescriptor in C
/// C struct: 16 + 2 + 2 + 4 = 24 bytes
#[repr(C, packed)]
struct CalSegDescriptorRaw {
    name: [u8; 16], // Calibration segment name, null terminated
    size: u16,      // Size in bytes, multiple of 4
    index: u16,     // Index in calibration segment list
    res: [u8; 4],   // Reserved
}

/// Safe wrapper for CalSegDescriptorRaw
#[derive(Debug, Clone)]
pub struct CalSegDescriptor {
    pub name: String,
    pub size: u16,
    pub index: u16,
    pub res: [u8; 4],
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
        let size = raw.size;
        let index = raw.index;
        let res = raw.res;

        Ok(CalSegDescriptor {
            name,
            size,
            index,
            res,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_struct_sizes() {
        // Verify struct sizes match C definitions
        assert_eq!(mem::size_of::<BinHeaderRaw>(), 58);
        assert_eq!(mem::size_of::<EventDescriptorRaw>(), 28);
        assert_eq!(mem::size_of::<CalSegDescriptorRaw>(), 24);
    }
}
