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
use bin_format::{
    read_context_list, AppDescriptor, BinHeader, CalSegDescriptor, ContextDescriptor,
    EventDescriptor, BIN_VERSION, CALSEG_NUMBER_UNDEFINED,
};

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
        Vec<AppDescriptor>,
        Vec<ContextDescriptor>,
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
        println!("  App Count: {}", header.app_count);
        println!("  Context Count: {}", header.context_count);
        println!();
    }

    // Read the context list, it follows the header
    let contexts = read_context_list(&mut file, &header)?;
    if verbose {
        for context in &contexts {
            println!(
                "Read Context {}: {} (app_id={})",
                context.id, context.name, context.app_id
            );
        }
        if !contexts.is_empty() {
            println!();
        }
    }

    // Read events
    let mut events = Vec::new();
    for i in 0..header.event_count {
        let event = EventDescriptor::read_from(&mut file)?;
        if verbose {
            println!("Read Event {}: {}", i, event.full_name(&contexts));
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

    // Read application descriptors (v0x0205+)
    let mut app_descriptors = Vec::new();
    for i in 0..header.app_count {
        let app = AppDescriptor::read_from(&mut file)?;
        if verbose {
            println!(
                "Read App {}: {} (app_id={})",
                i, app.project_name, app.app_id
            );
        }
        app_descriptors.push(app);
    }

    if verbose && header.app_count > 0 {
        println!();
    }

    Ok((header, events, calseg_data, app_descriptors, contexts))
}

fn dump_bin_file(path: &PathBuf, verbose: bool) -> Result<(), Bin2HexError> {
    let (header, events, calseg_data, app_descriptors, contexts) = read_bin_file(path, false)?;

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
    if header.app_count > 0 {
        println!("  App Count:    {}", header.app_count);
    }
    if header.context_count > 0 {
        println!("  Context Count: {}", header.context_count);
    }
    println!();

    // Print the context list
    if !contexts.is_empty() {
        println!("CONTEXTS:");
        println!();
        for context in contexts.iter() {
            if header.app_count > 0 {
                println!(
                    "  {}: {} (app_id={})",
                    context.id, context.name, context.app_id
                );
            } else {
                println!("  {}: {}", context.id, context.name);
            }
        }
        println!();
    }

    // Print event information
    if !events.is_empty() {
        println!("EVENTS:");
        println!();

        for (idx, event) in events.iter().enumerate() {
            println!("Event {}:", idx);
            println!("  Name:         {}", event.full_name(&contexts));
            println!("  ID:           {}", event.id);
            if let Some(context_name) = event.context_name(&contexts) {
                println!("  Context:      {} ({})", context_name, event.context);
            } else if event.context != 0 && header.version < BIN_VERSION {
                // Written by an older XCPlite version with anonymous event instances <name>_<index>
                println!("  Index:        {}", event.context);
            } else if event.context != 0 {
                println!("  Context:      unknown ({})", event.context);
            }
            println!("  Cycle Time:   {} ns", event.cycle_time_ns);
            println!("  Priority:     {}", event.priority);
            if header.app_count > 0 {
                println!("  App ID:       {}", event.app_id);
            }
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
        if desc.number != CALSEG_NUMBER_UNDEFINED {
            println!("  Number:  {}", desc.number);
        } else {
            println!("  Number:  - (not visible)");
        }
        println!("  Size:    {} bytes", desc.size);
        println!("  Address: 0x{:08X}", desc.addr);
        if header.app_count > 0 {
            println!("  App ID:  {}", desc.app_id);
        }

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

    // Print application descriptors (v0x0205+)
    if !app_descriptors.is_empty() {
        println!();
        println!("APPLICATIONS:");
        println!();
        for app in app_descriptors.iter() {
            println!("Application {}:", app.app_id);
            println!("  Project Name: {}", app.project_name);
            println!("  EPK:          {}", app.epk);
            println!();
        }
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
) -> Result<Vec<(u32, Vec<u8>)>, Bin2HexError> {
    let hex_string = std::fs::read_to_string(path)?;

    if verbose {
        println!("Reading Intel-Hex file: {}", path.display());
    }

    let mut hex_records = Vec::new();
    let mut current_extended_addr: u32 = 0;
    let mut segment_addressing = false;
    let mut record_count = 0usize;

    for record in ihex::Reader::new(&hex_string) {
        record_count += 1;
        let (base, is_segment) = match record? {
            ihex::Record::Data { offset, mut value } => {
                let full_address = current_extended_addr
                    .checked_add(offset as u32)
                    .ok_or_else(|| Bin2HexError::InvalidFormat("Intel-Hex address overflow".into()))?;
                if value.is_empty() {
                    continue;
                }
                // Use u64 so a record ending at 0xFFFF_FFFF is valid without wrapping.
                if full_address as u64 + value.len() as u64 > u32::MAX as u64 + 1 {
                    return Err(Bin2HexError::InvalidFormat(
                        "Intel-Hex record address overflow".into(),
                    ));
                }
                // Type 02 wraps the offset, not the base, at 64 KiB. Split only
                // those records so both pieces use the normal range validation.
                let before_wrap = 0x1_0000 - offset as usize;
                if segment_addressing && value.len() > before_wrap {
                    let wrapped = value.split_off(before_wrap);
                    hex_records.push((current_extended_addr, wrapped));
                }
                hex_records.push((full_address, value));
                continue;
            }
            ihex::Record::ExtendedLinearAddress(addr) => ((addr as u32) << 16, false),
            ihex::Record::ExtendedSegmentAddress(addr) => ((addr as u32) << 4, true),
            ihex::Record::EndOfFile => {
                if verbose {
                    println!("  End of file");
                }
                break;
            }
            _ => continue, // Start-address records do not change the data address.
        };
        current_extended_addr = base;
        segment_addressing = is_segment;
        if verbose {
            println!("  Extended address: 0x{:08X}", base);
        }
    }
    // The parsed bytes are all we need for sorting and merging.
    drop(hex_string);

    // Sorting lets us join touching records even if the file lists them out of order.
    // Keep gaps as gaps; filling them with zeros would make missing data look valid.
    hex_records.sort_unstable_by_key(|(address, _)| *address);
    let mut segments: Vec<(u32, Vec<u8>)> = Vec::new();
    for (address, data) in hex_records {
        // After sorting, only the last range can overlap this record.
        if let Some((start, bytes)) = segments.last_mut() {
            let end = *start as u64 + bytes.len() as u64;
            if address as u64 <= end {
                let offset = (address - *start) as usize;
                let overlap = data.len().min(bytes.len() - offset);
                // Repeated addresses are fine if both records give the same bytes.
                if let Some(index) = data[..overlap]
                    .iter()
                    .zip(&bytes[offset..offset + overlap])
                    .position(|(new, previous)| new != previous)
                {
                    return Err(Bin2HexError::InvalidFormat(format!(
                        "Conflicting HEX data at address 0x{:08X}",
                        address + index as u32
                    )));
                }
                // Identical overlaps are already present; append only new bytes.
                bytes.extend_from_slice(&data[overlap..]);
                continue;
            }
        }
        segments.push((address, data));
    }

    if verbose {
        println!("  Total records: {}", record_count);
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

    // Skip contexts and events
    read_context_list(&mut file, &header)?;
    for _ in 0..header.event_count {
        EventDescriptor::read_from(&mut file)?;
    }

    // PHASE 1: Read, validate, and queue updates. A later segment
    // may still fail validation, in which case the whole BIN must stay unchanged.
    let file_length = file.metadata()?.len();
    let mut updates = Vec::new();
    for i in 0..header.calseg_count {
        let calseg_desc = CalSegDescriptor::read_from(&mut file)?;
        let data_position = file.stream_position()?;
        let data_end = data_position
            .checked_add(calseg_desc.size as u64)
            .ok_or_else(|| Bin2HexError::InvalidFormat("BIN segment offset overflow".into()))?;
        // Seeking past EOF succeeds, so check that the segment's bytes really exist.
        if data_end > file_length {
            return Err(Bin2HexError::InvalidFormat(format!(
                "Truncated BIN segment '{}'",
                calseg_desc.name
            )));
        }

        let segment_addr = calseg_desc.addr;

        if calseg_desc.size == 0 {
            continue;
        }
        let segment_end = segment_addr as u64 + calseg_desc.size as u64;
        if segment_end > u32::MAX as u64 + 1 {
            return Err(Bin2HexError::InvalidFormat(format!(
                "Calibration segment '{}' address overflow",
                calseg_desc.name
            )));
        }

        // Skip ranges that end before this segment starts. The first remaining
        // range is the only one that could cover the whole segment without a gap.
        let first = hex_segments.partition_point(|(start, bytes)| {
            *start as u64 + bytes.len() as u64 <= segment_addr as u64
        });
        if let Some((start, bytes)) = hex_segments
            .get(first)
            .filter(|(start, _)| (*start as u64) < segment_end)
        {
            let range_end = *start as u64 + bytes.len() as u64;
            if *start > segment_addr || range_end < segment_end {
                // Some bytes are present, so we cannot treat this segment as absent.
                return Err(Bin2HexError::SegmentMismatch(format!(
                    "HEX data does not completely cover segment '{}' at 0x{:08X} ({} bytes). Refusing to patch.",
                    calseg_desc.name, segment_addr, calseg_desc.size
                )));
            }
            // One HEX range can hold several BIN segments; take only this one's bytes.
            let offset = (segment_addr - *start) as usize;
            let hex_data = &bytes[offset..offset + calseg_desc.size as usize];

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

            // Compare just the EPK bytes, even if this range also holds other segments.
            if i == 0 && calseg_desc.name.trim() == "epk" {
                // We are still at this segment's data, so read the EPK directly.
                let mut bin_epk_data = vec![0u8; calseg_desc.size as usize];
                file.read_exact(&mut bin_epk_data)?;

                // Check content
                if bin_epk_data.as_slice() != hex_data {
                    return Err(Bin2HexError::SegmentMismatch(
                        "EPK segment content mismatch between BIN and HEX. Refusing to patch."
                            .to_string(),
                    ));
                }
            }

            if verbose {
                println!("  ✓ Validation passed");
                println!();
            }
            // Save this slice; writes wait until every segment has passed validation.
            updates.push((i, calseg_desc.name, data_position, hex_data));
        } else if verbose {
            println!(
                "Skipped segment {} '{}' (not in HEX file)",
                i, calseg_desc.name
            );
        }
        // Move to the next descriptor without changing any segment data.
        file.seek(SeekFrom::Start(data_end))?;
    }

    // PHASE 2: All segments passed validation; now write the queued slices.
    let updated_count = updates.len();
    for (i, name, data_position, hex_data) in updates {
        file.seek(SeekFrom::Start(data_position))?;
        file.write_all(hex_data)?;

        if verbose {
            println!(
                "Updated segment {} '{}' at file offset 0x{:X}",
                i, name, data_position
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
        let (_header, _events, calseg_data, _app_descriptors, _contexts) =
            read_bin_file(&bin_path, args.verbose)?;

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

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use std::sync::atomic::{AtomicU64, Ordering};

    // Each test gets its own files so cases can run in parallel.
    struct Fixture(PathBuf);

    impl Fixture {
        fn new() -> Self {
            static NEXT_ID: AtomicU64 = AtomicU64::new(0);
            loop {
                let path = std::env::temp_dir().join(format!(
                    "xcplite-bintool-{}-{}",
                    std::process::id(),
                    NEXT_ID.fetch_add(1, Ordering::Relaxed)
                ));
                // Another run may reuse the same PID. Claim a fresh directory
                // rather than sharing its files or failing on a stale fixture.
                match std::fs::create_dir(&path) {
                    Ok(()) => return Self(path),
                    Err(err) if err.kind() == io::ErrorKind::AlreadyExists => continue,
                    Err(err) => panic!("Cannot create test directory {}: {err}", path.display()),
                }
            }
        }

        fn bin(&self, name: &str, segments: &[(&str, u32, &[u8])]) -> PathBuf {
            let path = self.0.join(name);
            let mut file = File::create(&path).unwrap();
            BinHeader {
                signature: bin_format::BIN_SIGNATURE.into(),
                version: bin_format::BIN_VERSION,
                event_count: 0,
                calseg_count: segments.len().try_into().unwrap(),
                app_count: 0,
                context_count: 0,
                epk: "test".into(),
            }
            .write_to(&mut file)
            .unwrap();
            for (index, (name, addr, data)) in segments.iter().enumerate() {
                CalSegDescriptor {
                    index: index.try_into().unwrap(),
                    size: data.len().try_into().unwrap(),
                    addr: *addr,
                    app_id: 0,
                    number: index.try_into().unwrap(),
                    name: name.to_string(),
                }
                .write_to(&mut file)
                .unwrap();
                file.write_all(data).unwrap();
            }
            path
        }

        fn hex(&self, mut records: Vec<ihex::Record>) -> PathBuf {
            let path = self.0.join("input.hex");
            records.push(ihex::Record::EndOfFile);
            std::fs::write(
                &path,
                ihex::create_object_file_representation(&records).unwrap(),
            )
            .unwrap();
            path
        }
    }

    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn data_record(offset: u16, data: &[u8]) -> ihex::Record {
        ihex::Record::Data {
            offset,
            value: data.to_vec(),
        }
    }

    fn segment_data(path: &PathBuf) -> Vec<Vec<u8>> {
        let (_, _, segments, _, _) = read_bin_file(path, false).unwrap();
        segments.into_iter().map(|(_, data)| data).collect()
    }

    fn assert_rejected_without_writes(bin: &PathBuf, hex: &PathBuf) -> Bin2HexError {
        let before = std::fs::read(bin).unwrap();
        let err = apply_hex_to_bin(bin, hex, false).unwrap_err();
        // Comparing the whole file also catches accidental truncation or extension.
        assert_eq!(std::fs::read(bin).unwrap(), before);
        err
    }

    #[test]
    fn incomplete_hex_data_leaves_the_entire_bin_unchanged() {
        // Missing bytes at the start, middle, or end must leave the whole BIN
        // untouched, including the first segment whose data is complete.
        for records in [
            vec![data_record(0x2000, &[1, 2]), data_record(0x2006, &[7, 8])],
            vec![data_record(0x2000, &[1, 2, 3, 4])],
            // Some data is present, even though no range starts at the segment base.
            vec![data_record(0x2002, &[3, 4, 5, 6, 7, 8])],
        ] {
            let fixture = Fixture::new();
            let bin = fixture.bin(
                "target.bin",
                &[("first", 0x1000, &[0xAA; 4]), ("second", 0x2000, &[0xBB; 8])],
            );
            let mut all_records = vec![data_record(0x1000, &[9, 10, 11, 12])];
            all_records.extend(records);
            let hex = fixture.hex(all_records);

            let err = assert_rejected_without_writes(&bin, &hex);
            assert!(matches!(err, Bin2HexError::SegmentMismatch(_)));
        }
    }

    #[test]
    fn contiguous_out_of_order_records_restore_complete_segment() {
        let fixture = Fixture::new();
        let bin = fixture.bin("target.bin", &[("parameters", 0x1000, &[0xAA; 8])]);
        // Record order does not change coverage: these two records touch exactly.
        let hex = fixture.hex(vec![
            data_record(0x1004, &[5, 6, 7, 8]),
            data_record(0x1000, &[1, 2, 3, 4]),
        ]);

        apply_hex_to_bin(&bin, &hex, false).unwrap();
        assert_eq!(segment_data(&bin), vec![vec![1, 2, 3, 4, 5, 6, 7, 8]]);
    }

    #[test]
    fn segment_address_records_add_offsets() {
        let fixture = Fixture::new();
        let bin = fixture.bin(
            "target.bin",
            &[
                ("correct", 0x12380, &[0xAA; 4]),
                ("bitwise_or", 0x12340, &[0xBB; 4]),
                ("ignored_base", 0x0040, &[0xCC; 4]),
            ],
        );
        // The base and offset share a set bit: OR would patch the wrong segment.
        let hex = fixture.hex(vec![
            ihex::Record::ExtendedSegmentAddress(0x1234),
            data_record(0x0040, &[1, 2, 3, 4]),
        ]);

        apply_hex_to_bin(&bin, &hex, false).unwrap();
        assert_eq!(segment_data(&bin), vec![vec![1, 2, 3, 4], vec![0xBB; 4], vec![0xCC; 4]]);
    }

    #[test]
    fn segment_address_mode_switches_preserve_boundary_behavior() {
        // Type 02 wraps the offset at 64 KiB; type 04 carries into the next address.
        // Try both orders so neither mode leaks into the following address record.
        for reverse in [false, true] {
            let fixture = Fixture::new();
            let bin = fixture.bin(
                "target.bin",
                &[
                    ("segment_tail", 0x2233E, &[0xAA; 2]),
                    ("segment_wrap", 0x12340, &[0xAA; 2]),
                    ("segment_carry", 0x22340, &[0xAA; 2]),
                    ("linear_tail", 0x1234_FFFE, &[0xAA; 2]),
                    ("linear_carry", 0x1235_0000, &[0xAA; 2]),
                    ("linear_wrap", 0x1234_0000, &[0xAA; 2]),
                ],
            );
            let mut blocks = vec![
                vec![ihex::Record::ExtendedSegmentAddress(0x1234), data_record(0xFFFE, &[1, 2, 3, 4])],
                vec![ihex::Record::ExtendedLinearAddress(0x1234), data_record(0xFFFE, &[5, 6, 7, 8])],
            ];
            if reverse {
                blocks.reverse();
            }
            let hex = fixture.hex(blocks.into_iter().flatten().collect());

            apply_hex_to_bin(&bin, &hex, false).unwrap();
            assert_eq!(
                segment_data(&bin),
                vec![vec![1, 2], vec![3, 4], vec![0xAA; 2], vec![5, 6], vec![7, 8], vec![0xAA; 2]]
            );
        }
    }

    #[test]
    fn segment_address_wrap_checks_overlaps() {
        // Wrapped bytes still go through the usual overlap checks before any writes.
        for repeated_byte in [3, 9] {
            let fixture = Fixture::new();
            let bin = fixture.bin(
                "target.bin",
                &[("head", 0x12340, &[0xAA; 2]), ("tail", 0x2233E, &[0xBB; 2])],
            );
            let hex = fixture.hex(vec![
                ihex::Record::ExtendedSegmentAddress(0x1234),
                data_record(0xFFFE, &[1, 2, 3, 4]),
                data_record(0x0000, &[repeated_byte]),
            ]);

            if repeated_byte == 3 {
                apply_hex_to_bin(&bin, &hex, false).unwrap();
                assert_eq!(segment_data(&bin), vec![vec![3, 4], vec![1, 2]]);
            } else {
                let err = assert_rejected_without_writes(&bin, &hex);
                assert!(matches!(err, Bin2HexError::InvalidFormat(_)));
                assert!(err.to_string().contains("0x00012340"));
            }
        }
    }

    #[test]
    fn data_and_overlaps_at_max_address_are_validated() {
        // A record may end on the last address, or start there with one byte.
        // Repeating the final byte is fine; changing it must leave the BIN untouched.
        for (address, data, repeated_byte) in [
            (0xFFFF_FFFC, vec![1, 2, 3, 4], None),
            (u32::MAX, vec![0xA5], None),
            (0xFFFF_FFFC, vec![1, 2, 3, 4], Some(4)),
            (0xFFFF_FFFC, vec![1, 2, 3, 4], Some(9)),
        ] {
            for reverse in [false, true] {
                let fixture = Fixture::new();
                let original = vec![0xAA; data.len()];
                let bin = fixture.bin("target.bin", &[("parameters", address, &original)]);
                let mut records = vec![data_record(address as u16, &data)];
                if let Some(byte) = repeated_byte {
                    records.push(data_record(0xFFFF, &[byte]));
                }
                if reverse {
                    records.reverse();
                }
                records.insert(0, ihex::Record::ExtendedLinearAddress(0xFFFF));
                let hex = fixture.hex(records);

                if repeated_byte == Some(9) {
                    let err = assert_rejected_without_writes(&bin, &hex);
                    assert!(matches!(err, Bin2HexError::InvalidFormat(_)));
                    assert!(err.to_string().contains("0xFFFFFFFF"));
                } else {
                    apply_hex_to_bin(&bin, &hex, false).unwrap();
                    assert_eq!(segment_data(&bin), vec![data.clone()]);
                }
            }
        }
    }

    #[test]
    fn address_overflows_are_rejected_before_any_writes() {
        // Check overflow in HEX records and in BIN descriptors separately.
        // Even a complete earlier segment must remain unchanged on either error.
        for (address, size, offset, data, error) in [
            (0xFFFF_FFFC, 4, 0xFFFE, vec![1, 2, 3, 4], "record address overflow"),
            (u32::MAX, 1, 0xFFFF, vec![1, 2], "record address overflow"),
            (0xFFFF_FFFE, 4, 0xFFFE, vec![1, 2], "segment 'second' address overflow"),
        ] {
            let fixture = Fixture::new();
            let original = vec![0xBB; size];
            let bin = fixture.bin(
                "target.bin",
                &[("first", 0x1000, &[0xAA; 4]), ("second", address, &original)],
            );
            let hex = fixture.hex(vec![
                data_record(0x1000, &[5, 6, 7, 8]),
                ihex::Record::ExtendedLinearAddress(0xFFFF),
                data_record(offset, &data),
            ]);

            let err = assert_rejected_without_writes(&bin, &hex);
            assert!(matches!(err, Bin2HexError::InvalidFormat(_)));
            assert!(err.to_string().contains(error));
        }
    }

    #[test]
    fn default_addressing_matches_explicit_linear_base_zero() {
        // Keep the implicit zero base consistent with an explicit type 04 record.
        // The carry must not overwrite bytes at address zero.
        for explicit_base in [false, true] {
            let fixture = Fixture::new();
            let bin = fixture.bin(
                "target.bin",
                &[
                    ("tail", 0xFFFE, &[0xAA; 2]),
                    ("carry", 0x10000, &[0xBB; 2]),
                    ("zero", 0, &[0xCC; 2]),
                ],
            );
            let mut records = Vec::new();
            if explicit_base {
                records.push(ihex::Record::ExtendedLinearAddress(0));
            }
            records.push(data_record(0xFFFE, &[1, 2, 3, 4]));
            let hex = fixture.hex(records);

            apply_hex_to_bin(&bin, &hex, false).unwrap();
            assert_eq!(segment_data(&bin), vec![vec![1, 2], vec![3, 4], vec![0xCC; 2]]);
        }
    }

    #[test]
    fn nearby_segments_round_trip_through_hex() {
        // Run the real round trip with touching segments and with a gap.
        // Both segments must be restored even when their records merge into one range.
        for second_addr in [0x1004, 0x1020] {
            let fixture = Fixture::new();
            let source = fixture.bin(
                "source.bin",
                &[("first", 0x1000, &[1, 2, 3, 4]), ("second", second_addr, &[5, 6, 7, 8])],
            );
            let target = fixture.bin(
                "target.bin",
                &[("first", 0x1000, &[0xAA; 4]), ("second", second_addr, &[0xBB; 4])],
            );
            let hex = fixture.0.join("source.hex");
            let (_, _, segments, _, _) = read_bin_file(&source, false).unwrap();
            write_hex_file(&hex, &segments, false).unwrap();

            apply_hex_to_bin(&target, &hex, false).unwrap();
            assert_eq!(segment_data(&target), vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]);
        }
    }

    #[test]
    fn record_starting_before_segments_restores_only_the_requested_slices() {
        let fixture = Fixture::new();
        let bin = fixture.bin(
            "target.bin",
            &[("first", 0x1010, &[0xAA; 16]), ("second", 0x1030, &[0xBB; 16])],
        );
        // The prefix and bytes between the two segments must stay out of the updates.
        let data: Vec<u8> = (0..64).collect();
        let hex = fixture.hex(vec![data_record(0x1000, &data)]);

        apply_hex_to_bin(&bin, &hex, false).unwrap();
        assert_eq!(segment_data(&bin), vec![data[16..32].to_vec(), data[48..64].to_vec()]);
    }

    #[test]
    fn absent_and_zero_length_segments_are_skipped() {
        let fixture = Fixture::new();
        let bin = fixture.bin(
            "target.bin",
            &[
                ("before", 0x1FFC, &[0xAA; 4]),
                ("present", 0x2000, &[0xBB; 4]),
                ("after", 0x2004, &[0xCC; 4]),
                ("empty", u32::MAX, &[]),
            ],
        );
        // The neighboring segments touch the HEX range but have no bytes to restore.
        let hex = fixture.hex(vec![data_record(0x2000, &[1, 2, 3, 4])]);

        apply_hex_to_bin(&bin, &hex, false).unwrap();
        assert_eq!(
            segment_data(&bin),
            vec![vec![0xAA; 4], vec![1, 2, 3, 4], vec![0xCC; 4], vec![]]
        );
    }

    #[test]
    fn conflicting_overlaps_are_rejected_before_any_writes() {
        // Cover extending, contained, and equal-start overlaps, in both input orders.
        // Swapping the records must still report the same conflicting address.
        for (first, second, address) in [
            (data_record(0x2000, &[1, 2, 3, 4]), data_record(0x2002, &[9, 4, 5, 6]), 0x2002),
            (data_record(0x2000, &[1, 2, 3, 4]), data_record(0x2001, &[9]), 0x2001),
            (data_record(0x2000, &[1, 2, 3, 4]), data_record(0x2000, &[1, 9, 3, 4]), 0x2001),
        ] {
            for reverse in [false, true] {
                let fixture = Fixture::new();
                let bin = fixture.bin(
                    "target.bin",
                    &[("first", 0x1000, &[0xAA; 4]), ("second", 0x2000, &[0xBB; 4])],
                );
                let mut records = vec![first.clone(), second.clone()];
                if reverse {
                    records.reverse();
                }
                records.insert(0, data_record(0x1000, &[5, 6, 7, 8]));
                let hex = fixture.hex(records);

                let err = assert_rejected_without_writes(&bin, &hex);
                assert!(matches!(err, Bin2HexError::InvalidFormat(_)));
                assert!(err.to_string().contains(&format!("0x{address:08X}")));
            }
        }
    }

    #[test]
    fn identical_overlaps_remain_valid() {
        // Check each overlap shape on its own so a full duplicate cannot hide a
        // missing tail when the shorter records are merged.
        for records in [
            vec![
                data_record(0x1000, &[1, 2, 3, 4]),
                data_record(0x1002, &[3, 4, 5, 6, 7, 8]),
            ],
            vec![
                data_record(0x1000, &[1, 2, 3, 4, 5, 6, 7, 8]),
                data_record(0x1001, &[2, 3]),
            ],
            vec![
                data_record(0x1000, &[1, 2, 3, 4]),
                data_record(0x1000, &[1, 2, 3, 4, 5, 6, 7, 8]),
            ],
        ] {
            for reverse in [false, true] {
                let fixture = Fixture::new();
                let bin = fixture.bin("target.bin", &[("parameters", 0x1000, &[0xAA; 8])]);
                let mut records = records.clone();
                if reverse {
                    records.reverse();
                }
                let hex = fixture.hex(records);

                apply_hex_to_bin(&bin, &hex, false).unwrap();
                assert_eq!(segment_data(&bin), vec![vec![1, 2, 3, 4, 5, 6, 7, 8]]);
            }
        }
    }

    #[test]
    fn epk_validation_uses_only_the_epk_slice_of_a_merged_range() {
        // EPK and parameter records merge, but only the EPK bytes must match the BIN.
        for epk in [b"test", b"oops"] {
            let fixture = Fixture::new();
            let bin = fixture.bin(
                "target.bin",
                &[("epk", 0x1000, b"test"), ("parameters", 0x1004, &[0xAA; 4])],
            );
            let hex = fixture.hex(vec![
                data_record(0x1000, epk),
                data_record(0x1004, &[1, 2, 3, 4]),
            ]);

            if epk == b"test" {
                apply_hex_to_bin(&bin, &hex, false).unwrap();
                assert_eq!(segment_data(&bin), vec![b"test".to_vec(), vec![1, 2, 3, 4]]);
            } else {
                let err = assert_rejected_without_writes(&bin, &hex);
                assert!(matches!(err, Bin2HexError::SegmentMismatch(_)));
            }
        }
    }

    #[test]
    fn truncated_bin_segment_is_rejected_before_any_writes() {
        // Reject a short final segment even when HEX does not include it.
        // An earlier valid update must not change or extend the damaged BIN.
        for include_second in [true, false] {
            let fixture = Fixture::new();
            let bin = fixture.bin(
                "target.bin",
                &[("first", 0x1000, &[0xAA; 4]), ("second", 0x2000, &[0xBB; 4])],
            );
            let file = std::fs::OpenOptions::new().write(true).open(&bin).unwrap();
            file.set_len(file.metadata().unwrap().len() - 2).unwrap();
            drop(file);
            let mut records = vec![data_record(0x1000, &[1, 2, 3, 4])];
            if include_second {
                records.push(data_record(0x2000, &[5, 6, 7, 8]));
            }
            let hex = fixture.hex(records);

            let err = assert_rejected_without_writes(&bin, &hex);
            assert!(matches!(err, Bin2HexError::InvalidFormat(_)));
            assert!(err.to_string().contains("Truncated BIN segment 'second'"));
        }
    }
}
