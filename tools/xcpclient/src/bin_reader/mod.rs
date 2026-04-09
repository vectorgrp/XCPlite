//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module bin_reader
// Load and save XCPlite binary files (.BIN)

#![allow(clippy::type_complexity)]

use std::collections::HashMap;
use std::fs::File;

use std::io::{self, Read};
use std::io::{Seek, SeekFrom, Write};
use std::path::PathBuf;
use thiserror::Error;

pub mod bin_format;
use bin_format::{BinHeader, CalSegDescriptor, EventDescriptor};
use bin_format::{BIN_SIGNATURE, BIN_VERSION};

#[derive(Error, Debug)]
pub enum Bin2HexError {
    #[error("I/O error: {0}")]
    Io(#[from] io::Error),

    #[error("Invalid file format: {0}")]
    InvalidFormat(String),

    #[error("Intel-Hex write error: {0}")]
    IntelHexWrite(#[from] ihex::WriterError),

    #[error("Intel-Hex read error: {0}")]
    IntelHexRead(#[from] ihex::ReaderError),

    #[error("Segment mismatch: {0}")]
    SegmentMismatch(String),
}

pub fn write_bin_file(
    _path: &PathBuf,
    epk: &str,
    events: &[EventDescriptor],
    calseg_data: &[(CalSegDescriptor, Vec<u8>)],
) -> Result<(), Bin2HexError> {
    let mut file = File::create(_path)?;

    // Create header
    let header = BinHeader {
        signature: BIN_SIGNATURE.to_string(),
        version: BIN_VERSION,
        event_count: events.len() as u16,
        calseg_count: calseg_data.len() as u16,
        app_count: 0,
        epk: epk.to_string(),
    };

    // Write header
    header.write_to(&mut file)?;

    // Write events
    for event in events {
        event.write_to(&mut file)?;
    }

    // Write calibration segments
    for (desc, data) in calseg_data {
        desc.write_to(&mut file)?;
        file.write_all(data)?;
    }

    Ok(())
}

pub fn read_bin_file(
    path: &PathBuf,
    verbose: bool,
) -> Result<
    (
        BinHeader,
        Vec<EventDescriptor>,
        Vec<(CalSegDescriptor, Vec<u8>)>,
    ),
    Bin2HexError,
> {
    let mut file = File::open(path)?;

    // Read header
    let header = BinHeader::read_from(&mut file)?;

    if verbose {
        println!("BIN File Header:");
        println!("  Signature: {}", header.signature);
        println!("  Version: 0x{:04X}", header.version);
        println!("  EPK: {}", header.epk);
        println!("  Event Count: {}", header.event_count);
        println!("  CalSeg Count: {}", header.calseg_count);
        println!();
    }

    // Read events
    let mut events = Vec::new();
    for i in 0..header.event_count {
        let event = EventDescriptor::read_from(&mut file)?;
        if verbose {
            println!("Read Event {}: {}", i, event.name);
        }
        events.push(event);
    }

    if verbose {
        println!();
    }

    // Read calibration segments
    let mut calseg_data = Vec::new();

    for i in 0..header.calseg_count {
        let calseg_desc = CalSegDescriptor::read_from(&mut file)?;

        if verbose {
            println!("Calibration Segment {}:", i);
            println!("  Name: {}", calseg_desc.name);
            println!("  Size: {} bytes", calseg_desc.size);
            println!("  Index: {}", calseg_desc.index);
        }

        // Read calibration segment data
        let mut data = vec![0u8; calseg_desc.size as usize];
        file.read_exact(&mut data)?;

        if verbose {
            println!("  Data: {} bytes read", data.len());
            // Print first few bytes
            if !data.is_empty() {
                print!("  First bytes: ");
                for (idx, byte) in data.iter().take(16).enumerate() {
                    print!("{:02X} ", byte);
                    if (idx + 1) % 16 == 0 {
                        println!();
                    }
                }
                if data.len() < 16 {
                    println!();
                }
            }
            println!();
        }

        calseg_data.push((calseg_desc, data));
    }

    Ok((header, events, calseg_data))
}

fn dump_bin_file(path: &PathBuf, verbose: bool) -> Result<(), Bin2HexError> {
    let (header, events, calseg_data) = read_bin_file(path, false)?;

    // Print header information
    println!("========================================");
    println!("BIN File: {}", path.display());
    println!("========================================");
    println!();
    println!("HEADER:");
    println!("  Signature:    {}", header.signature);
    println!("  Version:      0x{:04X}", header.version);
    println!("  EPK:          {}", header.epk);
    println!("  Event Count:  {}", header.event_count);
    println!("  CalSeg Count: {}", header.calseg_count);
    println!();

    // Print event information
    if !events.is_empty() {
        println!("EVENTS:");
        println!();

        for (idx, event) in events.iter().enumerate() {
            println!("Event {}:", idx);
            println!("  Name:         {}", event.name);
            println!("  ID:           {}", event.id);
            println!("  Index:        {}", event.index);
            println!("  Cycle Time:   {} ns", event.cycle_time_ns);
            println!("  Priority:     {}", event.priority);
            println!();
        }
    }

    // Print calibration segment information
    println!("CALIBRATION SEGMENTS:");
    println!();

    for (idx, (desc, data)) in calseg_data.iter().enumerate() {
        println!("Segment {}:", idx);
        println!("  Name:    {}", desc.name);
        println!("  Index:   {}", desc.index);
        println!("  Size:    {} bytes", desc.size);
        println!("  Address: 0x{:08X}", desc.addr);

        if verbose {
            println!();
            println!("  Data (hex dump):");
            dump_hex_data(data, desc.addr);
        }

        println!();
    }

    if !verbose {
        println!("(Use --verbose to see hex dump of calibration data)");
    }

    Ok(())
}

fn dump_hex_data(data: &[u8], base_address: u32) {
    const BYTES_PER_LINE: usize = 16;

    for (line_idx, chunk) in data.chunks(BYTES_PER_LINE).enumerate() {
        let address = base_address + (line_idx * BYTES_PER_LINE) as u32;

        // Print full 32-bit address
        print!("    {:08X}:  ", address);

        // Print hex bytes
        for (byte_idx, byte) in chunk.iter().enumerate() {
            print!("{:02X} ", byte);

            // Add extra space in the middle for readability
            if byte_idx == 7 {
                print!(" ");
            }
        }

        // Pad if this is the last line and it's not complete
        if chunk.len() < BYTES_PER_LINE {
            for i in chunk.len()..BYTES_PER_LINE {
                print!("   ");
                if i == 7 {
                    print!(" ");
                }
            }
        }

        // Print ASCII representation
        print!(" |");
        for byte in chunk.iter() {
            if *byte >= 0x20 && *byte <= 0x7E {
                print!("{}", *byte as char);
            } else {
                print!(".");
            }
        }
        print!("|");

        println!();
    }
}

pub fn write_hex_file(
    path: &PathBuf,
    calseg_data: &[(CalSegDescriptor, Vec<u8>)],
) -> Result<(), Bin2HexError> {
    let mut records = Vec::new();

    for (desc, data) in calseg_data {
        // Use the address from the descriptor
        let segment_address = desc.addr;

        log::debug!(
            "Writing segment '{}' (index {}) at address 0x{:08X}",
            desc.name,
            desc.index,
            segment_address
        );

        // Create data records for this segment
        // Split into chunks (typically 16 or 32 bytes per record)
        const CHUNK_SIZE: usize = 32;

        for (chunk_idx, chunk) in data.chunks(CHUNK_SIZE).enumerate() {
            let chunk_address = segment_address + (chunk_idx * CHUNK_SIZE) as u32;

            // Handle extended linear address (32-bit addressing)
            let extended_addr = (chunk_address >> 16) as u16;
            if chunk_idx == 0 || (chunk_address & 0xFFFF) < CHUNK_SIZE as u32 {
                // Add Extended Linear Address record when upper 16 bits change
                records.push(ihex::Record::ExtendedLinearAddress(extended_addr));
            }

            // Add data record
            let lower_addr = (chunk_address & 0xFFFF) as u16;
            records.push(ihex::Record::Data {
                offset: lower_addr,
                value: chunk.to_vec(),
            });
        }
    }

    // Add end-of-file record
    records.push(ihex::Record::EndOfFile);

    // Write to file
    let hex_content = ihex::create_object_file_representation(&records)?;
    std::fs::write(path, hex_content)?;

    log::debug!(
        "\nIntel-Hex file written successfully to: {}",
        path.display()
    );
    log::debug!("Total records: {}", records.len());

    Ok(())
}

fn read_hex_file(path: &PathBuf) -> Result<std::collections::HashMap<u32, Vec<u8>>, Bin2HexError> {
    let hex_string = std::fs::read_to_string(path)?;
    let records = ihex::Reader::new(&hex_string).collect::<Result<Vec<_>, _>>()?;

    log::debug!("Reading Intel-Hex file: {}", path.display());
    log::debug!("  Total records: {}", records.len());

    let mut segments: HashMap<u32, Vec<u8>> = HashMap::new();
    let mut current_extended_addr: u32 = 0;

    for record in records {
        match record {
            ihex::Record::Data { offset, value } => {
                let full_address = current_extended_addr | (offset as u32);

                // Find which segment this address belongs to by checking if it falls
                // within any existing segment's range
                let mut found_segment = None;
                for (&segment_base, segment_data) in segments.iter() {
                    let segment_end = segment_base + segment_data.len() as u32;
                    if full_address >= segment_base && full_address < segment_end + 0x100 {
                        // Allow small gap (256 bytes) for continuation
                        found_segment = Some(segment_base);
                        break;
                    }
                }

                let segment_base = if let Some(base) = found_segment {
                    base
                } else {
                    // New segment starts at this address

                    log::debug!("  Found segment at 0x{:08X}", full_address);

                    full_address
                };

                let segment_data = segments.entry(segment_base).or_default();

                // Calculate offset within segment (relative to segment base)
                let offset_in_segment = (full_address - segment_base) as usize;

                // Extend vector if needed
                if segment_data.len() < offset_in_segment + value.len() {
                    segment_data.resize(offset_in_segment + value.len(), 0);
                }

                // Copy data
                segment_data[offset_in_segment..offset_in_segment + value.len()]
                    .copy_from_slice(&value);
            }
            ihex::Record::ExtendedLinearAddress(addr) => {
                current_extended_addr = (addr as u32) << 16;

                log::debug!("  Extended address: 0x{:08X}", current_extended_addr);
            }
            ihex::Record::EndOfFile => {
                log::debug!("  End of file");
            }
            _ => {
                // Ignore other record types
            }
        }
    }

    log::debug!("  Found {} segment(s)", segments.len());

    Ok(segments)
}

fn apply_hex_to_bin(
    bin_path: &PathBuf,
    hex_path: &PathBuf,
    verbose: bool,
) -> Result<(), Bin2HexError> {
    if verbose {
        println!("Applying Intel-Hex data to BIN file");
        println!("  BIN file: {}", bin_path.display());
        println!("  HEX file: {}", hex_path.display());
        println!();
    }

    // Read the hex file
    let hex_segments = read_hex_file(hex_path)?;

    if hex_segments.is_empty() {
        println!("Warning: No data found in HEX file");
        return Ok(());
    }

    // Open BIN file for reading and writing
    let mut file = std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(bin_path)?;

    // Read BIN header to get segment information
    let header = BinHeader::read_from(&mut file)?;

    if verbose {
        println!("BIN File Info:");
        println!("  EPK: {}", header.epk);
        println!("  Segments: {}", header.calseg_count);
        println!();
    }

    // Skip events
    for _ in 0..header.event_count {
        EventDescriptor::read_from(&mut file)?;
    }

    // PHASE 1: Read all segment descriptors and validate completeness BEFORE any writes
    let mut segment_info = Vec::new();
    for i in 0..header.calseg_count {
        let calseg_desc = CalSegDescriptor::read_from(&mut file)?;
        let data_position = file.stream_position()?;

        // Use the address from the descriptor
        let segment_addr = calseg_desc.addr;

        // Skip the data for now - we'll come back to write it
        file.seek(SeekFrom::Current(calseg_desc.size as i64))?;

        segment_info.push((i, calseg_desc, data_position, segment_addr));
    }

    // PHASE 2: Validate all segments that will be updated
    for (i, calseg_desc, data_position, segment_addr) in &segment_info {
        if let Some(hex_data) = hex_segments.get(segment_addr) {
            if verbose {
                println!(
                    "Validating segment {} '{}' at file offset 0x{:X}",
                    i, calseg_desc.name, data_position
                );
                println!(
                    "  Segment address: 0x{:08X}, size: {} bytes",
                    segment_addr, calseg_desc.size
                );
                println!("  HEX data size: {} bytes", hex_data.len());
            }

            // Special check for EPK segment (first segment, name == "epk")
            if *i == 0 && calseg_desc.name.trim() == "epk" {
                // Read current BIN EPK data
                let mut bin_epk_data = vec![0u8; calseg_desc.size as usize];
                file.seek(SeekFrom::Start(*data_position))?;
                file.read_exact(&mut bin_epk_data)?;

                // Check size
                if hex_data.len() != calseg_desc.size as usize {
                    return Err(Bin2HexError::SegmentMismatch(format!(
                        "EPK segment size mismatch: BIN={} HEX={}",
                        calseg_desc.size,
                        hex_data.len()
                    )));
                }
                // Check content
                if bin_epk_data != *hex_data {
                    return Err(Bin2HexError::SegmentMismatch(
                        "EPK segment content mismatch between BIN and HEX. Refusing to patch."
                            .to_string(),
                    ));
                }
            }

            // Validate size - HEX data must exactly match or exceed BIN segment size
            if hex_data.len() < calseg_desc.size as usize {
                return Err(Bin2HexError::SegmentMismatch(format!(
                    "HEX data for segment '{}' ({} bytes) does not completely cover BIN segment ({} bytes). Refusing to patch.",
                    calseg_desc.name,
                    hex_data.len(),
                    calseg_desc.size
                )));
            }

            if verbose {
                println!("  ✓ Validation passed");
                println!();
            }
        }
    }

    // PHASE 3: All validations passed, now write the data
    let mut updated_count = 0;
    for (i, calseg_desc, data_position, segment_addr) in &segment_info {
        if let Some(hex_data) = hex_segments.get(segment_addr) {
            // Seek to data position and write
            file.seek(SeekFrom::Start(*data_position))?;
            file.write_all(&hex_data[0..calseg_desc.size as usize])?;

            updated_count += 1;

            if verbose {
                println!(
                    "Updated segment {} '{}' at file offset 0x{:X}",
                    i, calseg_desc.name, data_position
                );
            }
        } else if verbose {
            println!(
                "Skipped segment {} '{}' (not in HEX file)",
                i, calseg_desc.name
            );
        }
    }

    if verbose {
        println!();
    }

    if verbose {
        println!("Update complete!");
        println!("  Updated {} segment(s)", updated_count);
    } else {
        println!(
            "Updated {} of {} segment(s) in '{}'",
            updated_count,
            header.calseg_count,
            bin_path.display()
        );
    }

    Ok(())
}
