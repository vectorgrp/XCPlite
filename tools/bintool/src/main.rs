// bintool - XCPlite BIN File Tool
//
// Inspect BIN file:
//  bintool cpp_demo.bin --dump --verbose
//
// Convert BIN to HEX:
//  bintool cpp_demo.bin
//
// Apply HEX to BIN:
//  bintool cpp_demo.bin --apply-hex cpp_demo.hex

#![allow(clippy::type_complexity)]

use clap::Parser;
use std::fs::File;
use std::io::{self, Read};
use std::path::PathBuf;
use thiserror::Error;

mod bin_format;
use bin_format::{BinHeader, CalSegDescriptor, EventDescriptor};

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

#[derive(Parser, Debug)]
#[command(name = "bintool")]
#[command(author = "RainerZ")]
#[command(version)] // Automatically uses version from Cargo.toml
#[command(about = "XCPlite BIN file tool - inspect, convert, and update calibration data", long_about = None)]
struct Args {
    /// Input .BIN file path (can be specified as positional argument or with --bin)
    #[arg(value_name = "BIN_FILE")]
    bin_file: Option<PathBuf>,

    /// Input .BIN file path (alternative to positional argument)
    #[arg(short, long, value_name = "FILE")]
    bin: Option<PathBuf>,

    /// Output .HEX file path (optional, defaults to input with .hex extension)
    #[arg(short = 'o', long, value_name = "FILE", conflicts_with = "apply_hex")]
    hex: Option<PathBuf>,

    /// Apply Intel-Hex file data to update the BIN file (HEX to BIN mode)
    /// Can be specified as just the HEX filename - BIN file will be derived from it
    #[arg(long, value_name = "FILE", conflicts_with = "hex")]
    apply_hex: Option<PathBuf>,

    /// Dump BIN file header and segment information (does not convert)
    /// With --verbose, also shows hex dump of calibration data
    #[arg(short, long, conflicts_with_all = ["hex", "apply_hex"])]
    dump: bool,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,
}

fn read_bin_file(
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

fn write_hex_file(
    path: &PathBuf,
    calseg_data: &[(CalSegDescriptor, Vec<u8>)],
    verbose: bool,
) -> Result<(), Bin2HexError> {
    let mut records = Vec::new();

    for (desc, data) in calseg_data {
        // Use the address from the descriptor
        let segment_address = desc.addr;

        if verbose {
            println!(
                "Writing segment '{}' (index {}) at address 0x{:08X}",
                desc.name, desc.index, segment_address
            );
        }

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

    if verbose {
        println!(
            "\nIntel-Hex file written successfully to: {}",
            path.display()
        );
        println!("Total records: {}", records.len());
    }

    Ok(())
}

fn read_hex_file(
    path: &PathBuf,
    verbose: bool,
) -> Result<std::collections::HashMap<u32, Vec<u8>>, Bin2HexError> {
    use std::collections::HashMap;

    let hex_string = std::fs::read_to_string(path)?;
    let records = ihex::Reader::new(&hex_string).collect::<Result<Vec<_>, _>>()?;

    if verbose {
        println!("Reading Intel-Hex file: {}", path.display());
        println!("  Total records: {}", records.len());
    }

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
                    if verbose {
                        println!("  Found segment at 0x{:08X}", full_address);
                    }
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
                if verbose {
                    println!("  Extended address: 0x{:08X}", current_extended_addr);
                }
            }
            ihex::Record::EndOfFile => {
                if verbose {
                    println!("  End of file");
                }
            }
            _ => {
                // Ignore other record types
            }
        }
    }

    if verbose {
        println!("  Found {} segment(s)", segments.len());
    }

    Ok(segments)
}

fn apply_hex_to_bin(
    bin_path: &PathBuf,
    hex_path: &PathBuf,
    verbose: bool,
) -> Result<(), Bin2HexError> {
    use std::io::{Seek, SeekFrom, Write};

    if verbose {
        println!("Applying Intel-Hex data to BIN file");
        println!("  BIN file: {}", bin_path.display());
        println!("  HEX file: {}", hex_path.display());
        println!();
    }

    // Read the hex file
    let hex_segments = read_hex_file(hex_path, verbose)?;

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

fn main() -> Result<(), Bin2HexError> {
    let args = Args::parse();

    // Determine the BIN file path from positional arg or --bin flag
    let bin_path = args.bin_file.or(args.bin).ok_or_else(|| {
        Bin2HexError::InvalidFormat(
            "BIN file must be specified (either as argument or with --bin)".to_string(),
        )
    })?;

    // Helper function to derive HEX filename from BIN filename
    let derive_hex_path = |bin_path: &PathBuf| -> PathBuf {
        let mut hex_path = bin_path.clone();
        hex_path.set_extension("hex");
        hex_path
    };

    // Determine mode based on arguments
    if args.dump {
        // Dump mode: Show BIN file contents
        dump_bin_file(&bin_path, args.verbose)?;
    } else if let Some(hex_path) = args.apply_hex {
        // HEX to BIN mode: Apply hex data to update BIN file
        apply_hex_to_bin(&bin_path, &hex_path, args.verbose)?;
    } else {
        // BIN to HEX mode: Convert BIN to HEX
        let hex_path = args.hex.unwrap_or_else(|| derive_hex_path(&bin_path));

        if args.verbose {
            println!("bintool - XCPlite BIN File Tool");
            println!("Input file:  {}", bin_path.display());
            println!("Output file: {}", hex_path.display());
            println!();
        }

        // Read BIN file
        let (_header, _events, calseg_data) = read_bin_file(&bin_path, args.verbose)?;

        if calseg_data.is_empty() {
            println!("Warning: No calibration segments found in BIN file");
            return Ok(());
        }

        // Write Intel-Hex file
        write_hex_file(&hex_path, &calseg_data, args.verbose)?;

        println!("Conversion complete!");
        println!(
            "  Converted {} calibration segment(s) from '{}'",
            calseg_data.len(),
            bin_path.display()
        );
        println!("  Output written to '{}'", hex_path.display());
    }

    Ok(())
}
