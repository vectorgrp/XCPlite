#![allow(dead_code)]

use std::fs::File;
use std::io::Read;
use std::mem;

use super::Bin2HexError;

// Compatibility with C code in persistence.h
// The BIN_VERSION check at runtime is the actual protection against format mismatches.
// If the C code changes the format, it should bump BIN_VERSION, and this tool will reject the file.
pub const BIN_SIGNATURE: &str = "XCPLITE__BINARY";
pub const BIN_VERSION: u16 = 0x0205;
pub const BIN_VERSION_LEGACY: u16 = 0x0204;

/// BIN file header - corresponds to tHeader in C
#[repr(C, packed)]
struct BinHeaderRaw {
    signature: [u8; 16],                      // "XCPLITE__BINARY"
    version: u16,                             // File version
    event_count: u16,                         // Number of event descriptors
    calseg_count: u16,                        // Number of calibration segment descriptors
    app_count: u16,                           // Number of application descriptors (v0x0205+)
    reserved: [u8; 128 - 16 - 2 - 2 - 2 - 2], // Reserved for future use
    epk: [u8; 128],                           // EPK string, null terminated
}

/// Safe wrapper for BinHeaderRaw
#[derive(Debug, Clone)]
pub struct BinHeader {
    pub signature: String,
    pub version: u16,
    pub event_count: u16,
    pub calseg_count: u16,
    pub app_count: u16,
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

        // Check version - supports 0x0205 (current) and 0x0204 (legacy)
        if version != BIN_VERSION && version != BIN_VERSION_LEGACY {
            return Err(Bin2HexError::InvalidFormat(format!(
                "Unsupported file version 0x{:04X}. This tool supports versions 0x{:04X} and 0x{:04X}.",
                version, BIN_VERSION, BIN_VERSION_LEGACY
            )));
        }

        // app_count is only valid in v0x0205+; treat as 0 for legacy files
        let app_count = if version == BIN_VERSION {
            raw.app_count
        } else {
            0
        };

        Ok(BinHeader {
            signature,
            version,
            event_count,
            calseg_count,
            app_count,
            epk,
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        use std::io::Write;

        // Create raw struct
        let mut raw: BinHeaderRaw = unsafe { mem::zeroed() };

        // Copy signature
        let signature_bytes = self.signature.as_bytes();
        let len = signature_bytes.len().min(raw.signature.len() - 1);
        raw.signature[..len].copy_from_slice(&signature_bytes[..len]);

        // Set version and counts
        raw.version = self.version;
        raw.event_count = self.event_count;
        raw.calseg_count = self.calseg_count;

        // Copy EPK
        let epk_bytes = self.epk.as_bytes();
        let epk_len = epk_bytes.len().min(raw.epk.len() - 1);
        raw.epk[..epk_len].copy_from_slice(&epk_bytes[..epk_len]);

        // Write raw struct to file
        unsafe {
            let raw_ptr = &raw as *const BinHeaderRaw as *const u8;
            let raw_slice = std::slice::from_raw_parts(raw_ptr, mem::size_of::<BinHeaderRaw>());
            file.write_all(raw_slice)?;
        }

        Ok(())
    }
}

/// Event descriptor - corresponds to tEventDescriptor in C
#[repr(C, packed)]
struct EventDescriptorRaw {
    id: u16,                                 // Event ID
    index: u16,                              // Event index
    cycle_time_ns: u32,                      // Cycle time in nanoseconds
    priority: u8,                            // Priority (0=queued, 1=pushing, 2=realtime)
    app_id: u8,                              // App ID (v0x0205+, 0 in legacy)
    reserved: [u8; 128 - 2 - 2 - 4 - 1 - 1], // Reserved for future use
    name: [u8; 128],                         // Event name, null terminated
}

/// Safe wrapper for EventDescriptorRaw
#[derive(Debug, Clone)]
pub struct EventDescriptor {
    pub id: u16,
    pub index: u16,
    pub cycle_time_ns: u32,
    pub priority: u8,
    pub app_id: u8,
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
        let app_id = raw.app_id;

        Ok(EventDescriptor {
            id,
            index,
            cycle_time_ns,
            priority,
            app_id,
            name,
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        use std::io::Write;

        // Create raw struct
        let mut raw: EventDescriptorRaw = unsafe { mem::zeroed() };

        // Set values
        raw.id = self.id;
        raw.index = self.index;
        raw.cycle_time_ns = self.cycle_time_ns;
        raw.priority = self.priority;

        // Copy name
        let name_bytes = self.name.as_bytes();
        let name_len = name_bytes.len().min(raw.name.len() - 1);
        raw.name[..name_len].copy_from_slice(&name_bytes[..name_len]);

        // Write raw struct to file
        unsafe {
            let raw_ptr = &raw as *const EventDescriptorRaw as *const u8;
            let raw_slice =
                std::slice::from_raw_parts(raw_ptr, mem::size_of::<EventDescriptorRaw>());
            file.write_all(raw_slice)?;
        }

        Ok(())
    }
}

/// Calibration segment descriptor - corresponds to tCalSegDescriptor in C
#[repr(C, packed)]
struct CalSegDescriptorRaw {
    index: u16,                          // Index in calibration segment list
    size: u16,                           // Size in bytes, multiple of 4
    addr: u32,                           // Address of the calibration segment
    app_id: u8,                          // App ID (v0x0205+, 0 in legacy)
    reserved: [u8; 128 - 2 - 2 - 4 - 1], // Reserved for future use
    name: [u8; 128],                     // Calibration segment name, null terminated
}

/// Safe wrapper for CalSegDescriptorRaw
#[derive(Debug, Clone)]
pub struct CalSegDescriptor {
    pub index: u16,
    pub size: u16,
    pub addr: u32,
    pub app_id: u8,
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
        let app_id = raw.app_id;

        Ok(CalSegDescriptor {
            index,
            size,
            addr,
            app_id,
            name,
        })
    }

    pub fn write_to(&self, file: &mut File) -> Result<(), Bin2HexError> {
        use std::io::Write;

        // Create raw struct
        let mut raw: CalSegDescriptorRaw = unsafe { mem::zeroed() };

        // Set values
        raw.index = self.index;
        raw.size = self.size;
        raw.addr = self.addr;

        // Copy name
        let name_bytes = self.name.as_bytes();
        let name_len = name_bytes.len().min(raw.name.len() - 1);
        raw.name[..name_len].copy_from_slice(&name_bytes[..name_len]);

        // Write raw struct to file
        unsafe {
            let raw_ptr = &raw as *const CalSegDescriptorRaw as *const u8;
            let raw_slice =
                std::slice::from_raw_parts(raw_ptr, mem::size_of::<CalSegDescriptorRaw>());
            file.write_all(raw_slice)?;
        }

        Ok(())
    }
}

/// Application descriptor - corresponds to tAppDescriptor in C (v0x0205+)
/// Layout: [app_id(1) | reserved(127)] [project_name(32) | epk(32) | padding(64)]
/// XCP_PROJECT_NAME_MAX_LENGTH=31, XCP_EPK_MAX_LENGTH=31
#[repr(C, packed)]
struct AppDescriptorRaw {
    app_id: u8,             // App ID of the application
    reserved: [u8; 127],    // Reserved for future use
    project_name: [u8; 32], // Application project name, null terminated (XCP_PROJECT_NAME_MAX_LENGTH+1)
    epk: [u8; 32],          // Build version / EPK, null terminated (XCP_EPK_MAX_LENGTH+1)
    padding: [u8; 64],      // 128 - 32 - 32 = 64
}

/// Safe wrapper for AppDescriptorRaw
#[derive(Debug, Clone)]
pub struct AppDescriptor {
    pub app_id: u8,
    pub project_name: String,
    pub epk: String,
}

impl AppDescriptor {
    pub fn read_from(file: &mut File) -> Result<Self, Bin2HexError> {
        let raw: AppDescriptorRaw = unsafe {
            let mut raw: AppDescriptorRaw = mem::zeroed();
            let raw_ptr = &mut raw as *mut AppDescriptorRaw as *mut u8;
            let raw_slice =
                std::slice::from_raw_parts_mut(raw_ptr, mem::size_of::<AppDescriptorRaw>());
            file.read_exact(raw_slice)?;
            raw
        };

        let project_name = String::from_utf8_lossy(&raw.project_name)
            .trim_end_matches('\0')
            .to_string();

        let epk = String::from_utf8_lossy(&raw.epk)
            .trim_end_matches('\0')
            .to_string();

        let app_id = raw.app_id;

        Ok(AppDescriptor {
            app_id,
            project_name,
            epk,
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
        assert_eq!(mem::size_of::<EventDescriptorRaw>(), 256);
        assert_eq!(mem::size_of::<CalSegDescriptorRaw>(), 256);
        assert_eq!(mem::size_of::<AppDescriptorRaw>(), 256);
    }
}
