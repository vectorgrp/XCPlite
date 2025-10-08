use anyhow::{Context, Result};
use clap::Parser;
use gimli::{
    AttributeValue, BaseAddresses, CieOrFde, DebugFrame, Dwarf, EhFrame, EndianSlice, LittleEndian,
    Unit, UnwindSection,
};
use memmap2::Mmap;
use object::{Object, ObjectSection};
use std::collections::HashMap;
use std::fs::File;
use std::path::PathBuf;

/// CFA (Canonical Frame Address) lookup tool for ELF/DWARF files
///
/// This tool demonstrates how to use the gimli library to extract
/// CFA offset information from DWARF debug information in ELF files.
/// The CFA offset is crucial for determining the correct addresses of
/// local variables in stack frames for tools like A2L generators.
#[derive(Parser)]
#[command(name = "cfa_lookup")]
#[command(about = "Lookup CFA offsets for functions in ELF/DWARF files")]
struct Args {
    /// Path to the ELF file with DWARF debug information
    elf_file: PathBuf,

    /// Function name to look up CFA information for
    #[arg(short, long)]
    function: Option<String>,

    /// List all functions with their CFA information
    #[arg(short, long)]
    list_all: bool,

    /// Show detailed DWARF information
    #[arg(short, long)]
    verbose: bool,
}

/// Represents CFA information for a function
#[derive(Debug, Clone)]
struct CfaInfo {
    /// Function name
    name: String,
    /// Low PC (start address of function)
    low_pc: u64,
    /// High PC (end address of function)  
    high_pc: u64,
    /// CFA offset from stack pointer (if determinable)
    cfa_offset: Option<i64>,
    /// Compilation unit index
    compilation_unit: usize,
}

fn main() -> Result<()> {
    let args = Args::parse();

    println!("=== GIMLI CFA Lookup Tool ===");
    println!("Analyzing ELF file: {}", args.elf_file.display());

    // Open and memory map the ELF file
    let file = File::open(&args.elf_file)
        .with_context(|| format!("Failed to open file: {}", args.elf_file.display()))?;

    let mmap = unsafe { Mmap::map(&file) }.context("Failed to memory map file")?;

    // Parse the ELF file using the object crate
    let object_file = object::File::parse(&*mmap).context("Failed to parse ELF file")?;

    if args.verbose {
        println!("ELF file format: {:?}", object_file.format());
        println!("Architecture: {:?}", object_file.architecture());
        println!("Endianness: {:?}", object_file.endianness());
    }

    // Load DWARF sections - this is where all the debug information is stored
    let dwarf = load_dwarf_sections(&object_file)?;

    // Extract function information from DWARF
    let functions = extract_function_info(&dwarf, &object_file, args.verbose)?;

    if functions.is_empty() {
        println!("No functions found with debug information.");
        println!("Make sure the ELF file was compiled with debug information (-g flag).");
        return Ok(());
    }

    println!(
        "\nFound {} functions with debug information:",
        functions.len()
    );

    // Handle command line options
    if args.list_all {
        print_all_functions(&functions);
    } else if let Some(func_name) = args.function {
        print_function_cfa(&functions, &func_name);
    } else {
        // Default: show summary and look for specific functions mentioned in the request
        print_summary(&functions);

        // Look for the specific functions mentioned: main, foo, task
        let target_functions = ["main", "foo", "task"];
        for target in &target_functions {
            print_function_cfa(&functions, target);
        }
    }

    Ok(())
}

/// Load DWARF debug sections from the ELF file
///
/// DWARF information is stored in multiple sections:
/// - .debug_info: Contains the main debug information tree
/// - .debug_abbrev: Contains abbreviation tables for .debug_info
/// - .debug_str: Contains string tables
/// - .debug_line: Contains line number information
/// - .debug_frame/.eh_frame: Contains frame unwinding information (CFA data)
fn load_dwarf_sections<'a>(
    file: &'a object::File<'a>,
) -> Result<Dwarf<EndianSlice<'a, LittleEndian>>> {
    // Helper closure to load a section by name
    let load_section = |section_name: &str| -> EndianSlice<'a, LittleEndian> {
        let section_data = file
            .section_by_name(section_name)
            .and_then(|section| section.data().ok())
            .unwrap_or(&[]);
        EndianSlice::new(section_data, LittleEndian)
    };

    // Create the DWARF context using the load method
    let dwarf = Dwarf::load(
        |id| -> Result<EndianSlice<'a, LittleEndian>, gimli::Error> {
            use gimli::SectionId;
            let section_name = match id {
                SectionId::DebugAbbrev => ".debug_abbrev",
                SectionId::DebugAddr => ".debug_addr",
                SectionId::DebugAranges => ".debug_aranges",
                SectionId::DebugInfo => ".debug_info",
                SectionId::DebugLine => ".debug_line",
                SectionId::DebugLineStr => ".debug_line_str",
                SectionId::DebugStr => ".debug_str",
                SectionId::DebugStrOffsets => ".debug_str_offsets",
                SectionId::DebugTypes => ".debug_types",
                SectionId::DebugLoc => ".debug_loc",
                SectionId::DebugLocLists => ".debug_loclists",
                SectionId::DebugRanges => ".debug_ranges",
                SectionId::DebugRngLists => ".debug_rnglists",
                _ => return Ok(EndianSlice::new(&[], LittleEndian)),
            };
            Ok(load_section(section_name))
        },
    )?;

    Ok(dwarf)
}

/// Extract function information from DWARF debug data
///
/// This function walks through all compilation units in the DWARF data
/// and extracts information about functions (subprograms in DWARF terminology).
/// For each function, we try to determine its name, address range, and CFA offset.
fn extract_function_info(
    dwarf: &Dwarf<EndianSlice<LittleEndian>>,
    object_file: &object::File,
    verbose: bool,
) -> Result<Vec<CfaInfo>> {
    let mut functions = Vec::new();

    // Iterate through all compilation units
    // Each source file typically corresponds to one compilation unit
    let mut iter = dwarf.units();
    let mut cu_index = 0;

    while let Some(header) = iter.next()? {
        if verbose {
            println!("\n--- Processing Compilation Unit {} ---", cu_index);
        }

        // Get the unit from the header
        let unit = dwarf.unit(header)?;

        // Get the root DIE (Debug Information Entry) of this compilation unit
        let mut entries = unit.entries();

        // Skip the compilation unit DIE itself and process its children
        if let Some((_, entry)) = entries.next_dfs()? {
            if verbose && entry.tag() == gimli::DW_TAG_compile_unit {
                if let Some(name) = get_string_attribute(dwarf, &unit, &entry, gimli::DW_AT_name)? {
                    println!("Compilation unit: {}", name);
                }
            }
        }

        // Process all DIEs in this compilation unit
        while let Some((_depth, entry)) = entries.next_dfs()? {
            // We're looking for subprogram DIEs (functions)
            if entry.tag() == gimli::DW_TAG_subprogram {
                if let Some(func_info) =
                    extract_function_from_die(dwarf, object_file, &unit, &entry, cu_index, verbose)?
                {
                    if verbose {
                        println!(
                            "  Found function: {} at 0x{:08x}-0x{:08x}",
                            func_info.name, func_info.low_pc, func_info.high_pc
                        );
                    }
                    functions.push(func_info);
                }
            }
        }

        cu_index += 1;
    }

    Ok(functions)
}

/// Extract function information from a specific DWARF DIE (Debug Information Entry)
///
/// A DIE represents a program entity (variable, function, type, etc.) in DWARF.
/// For functions (DW_TAG_subprogram), we extract:
/// - Function name (DW_AT_name)
/// - Address range (DW_AT_low_pc, DW_AT_high_pc)
/// - Frame base information for CFA calculation
fn extract_function_from_die(
    dwarf: &Dwarf<EndianSlice<LittleEndian>>,
    object_file: &object::File,
    unit: &Unit<EndianSlice<LittleEndian>>,
    entry: &gimli::DebuggingInformationEntry<EndianSlice<LittleEndian>>,
    cu_index: usize,
    verbose: bool,
) -> Result<Option<CfaInfo>> {
    // Get the function name
    let name = match get_string_attribute(dwarf, unit, entry, gimli::DW_AT_name)? {
        Some(name) => name,
        None => {
            if verbose {
                println!("    Skipping unnamed function");
            }
            return Ok(None);
        }
    };

    // Get the low PC (start address)
    let low_pc = match entry.attr_value(gimli::DW_AT_low_pc)? {
        Some(AttributeValue::Addr(addr)) => addr,
        _ => {
            if verbose {
                println!("    Function '{}' has no low_pc", name);
            }
            return Ok(None);
        }
    };

    // Get the high PC (end address)
    // High PC can be either an absolute address or an offset from low PC
    let high_pc = match entry.attr_value(gimli::DW_AT_high_pc)? {
        Some(AttributeValue::Addr(addr)) => addr,
        Some(AttributeValue::Udata(offset)) => low_pc + offset,
        _ => {
            if verbose {
                println!("    Function '{}' has no high_pc", name);
            }
            return Ok(None);
        }
    };

    // Try to determine CFA offset
    // The frame base attribute tells us how to calculate the frame pointer
    let frame_base_offset = extract_frame_base_offset(entry, verbose)?;

    // Try to determine CFA offset from CFI (Call Frame Information)
    let cfa_offset = extract_cfa_from_cfi(object_file, low_pc, verbose)?;

    // Use CFA offset from CFI, fallback to frame base offset
    let final_cfa_offset = cfa_offset.or(frame_base_offset);

    if verbose {
        println!(
            "    Frame base offset for '{}': {:?}",
            name, frame_base_offset
        );
        println!("    CFI CFA offset for '{}': {:?}", name, cfa_offset);
        println!(
            "    Final CFA offset for '{}': {:?}",
            name, final_cfa_offset
        );
    }

    Ok(Some(CfaInfo {
        name,
        low_pc,
        high_pc,
        cfa_offset: final_cfa_offset,
        compilation_unit: cu_index,
    }))
}

/// Extract CFA offset information from a function DIE
///
/// The CFA (Canonical Frame Address) is the address of the call frame.
/// For local variables, their addresses are typically expressed as offsets
/// from the CFA. This function tries to determine the CFA offset for a function.
///
/// This is a simplified implementation - real CFA calculation can be quite complex
/// and may vary throughout a function's execution.
fn extract_frame_base_offset(
    entry: &gimli::DebuggingInformationEntry<EndianSlice<LittleEndian>>,
    verbose: bool,
) -> Result<Option<i64>> {
    // Look for DW_AT_frame_base attribute
    // This attribute describes how to compute the frame base for local variables
    match entry.attr_value(gimli::DW_AT_frame_base)? {
        Some(AttributeValue::Exprloc(expression)) => {
            // The frame base is described by a DWARF expression
            // For simple cases, this might be something like "DW_OP_call_frame_cfa + offset"

            if verbose {
                println!(
                    "    Frame base expression found (length: {} bytes): {:?}",
                    expression.0.len(),
                    expression.0
                );
            }

            // Parse the expression to try to extract a constant offset
            // This is a simplified parser - real DWARF expressions can be very complex
            if let Some(offset) = parse_simple_cfa_expression(&expression.0) {
                return Ok(Some(offset));
            }

            // For more complex expressions, we might need to evaluate them
            // This would require a full DWARF expression evaluator
            if verbose {
                println!("    Complex frame base expression - cannot extract simple offset");
            }

            Ok(None)
        }
        Some(other) => {
            if verbose {
                println!("    Frame base attribute has unexpected type: {:?}", other);
            }
            Ok(None)
        }
        None => {
            if verbose {
                println!("    No frame base attribute found");
            }
            Ok(None)
        }
    }
}

/// Extract CFA offset from Call Frame Information (CFI) using pure gimli
///
/// This function parses .eh_frame or .debug_frame sections using gimli
/// to find the CFA (Canonical Frame Address) calculation rule for a function.
fn extract_cfa_from_cfi(
    file: &object::File,
    function_address: u64,
    verbose: bool,
) -> Result<Option<i64>> {
    if verbose {
        println!(
            "    Searching for CFA rules at address 0x{:08x}",
            function_address
        );
    }

    // Try .eh_frame first (more common)
    if let Some(cfa_offset) = parse_eh_frame(file, function_address, verbose)? {
        return Ok(Some(cfa_offset));
    }

    // Fallback to .debug_frame
    if let Some(cfa_offset) = parse_debug_frame(file, function_address, verbose)? {
        return Ok(Some(cfa_offset));
    }

    Ok(None)
}

/// Parse .eh_frame section to find CFA offset for a function
fn parse_eh_frame(
    file: &object::File,
    function_address: u64,
    verbose: bool,
) -> Result<Option<i64>> {
    let eh_frame_section = match file.section_by_name(".eh_frame") {
        Some(section) => section,
        None => {
            if verbose {
                println!("    No .eh_frame section found");
            }
            return Ok(None);
        }
    };

    let eh_frame_data = eh_frame_section.data()?;
    let eh_frame_address = eh_frame_section.address();

    if verbose {
        println!(
            "    Found .eh_frame section with {} bytes at 0x{:08x}",
            eh_frame_data.len(),
            eh_frame_address
        );
    }

    let eh_frame = EhFrame::new(eh_frame_data, LittleEndian);
    parse_cfi_section(
        &eh_frame,
        eh_frame_address,
        function_address,
        verbose,
        ".eh_frame",
    )
}

/// Parse .debug_frame section to find CFA offset for a function  
fn parse_debug_frame(
    file: &object::File,
    function_address: u64,
    verbose: bool,
) -> Result<Option<i64>> {
    let debug_frame_section = match file.section_by_name(".debug_frame") {
        Some(section) => section,
        None => {
            if verbose {
                println!("    No .debug_frame section found");
            }
            return Ok(None);
        }
    };

    let debug_frame_data = debug_frame_section.data()?;
    let debug_frame_address = debug_frame_section.address();

    if verbose {
        println!(
            "    Found .debug_frame section with {} bytes at 0x{:08x}",
            debug_frame_data.len(),
            debug_frame_address
        );
    }

    let debug_frame = DebugFrame::new(debug_frame_data, LittleEndian);
    parse_cfi_section(
        &debug_frame,
        debug_frame_address,
        function_address,
        verbose,
        ".debug_frame",
    )
}

/// Parse a CFI section (either .eh_frame or .debug_frame) using gimli
fn parse_cfi_section<R: gimli::Reader>(
    section: &impl UnwindSection<R>,
    section_address: u64,
    function_address: u64,
    verbose: bool,
    section_name: &str,
) -> Result<Option<i64>> {
    // Set up proper base addresses for CFI parsing
    let mut bases = BaseAddresses::default();

    // For .eh_frame, we need to set the eh_frame base address
    if section_name == ".eh_frame" {
        bases = bases.set_eh_frame(section_address);
    }

    let mut entries = section.entries(&bases);

    while let Some(entry) = entries.next()? {
        match entry {
            CieOrFde::Cie(_) => {
                // Common Information Entry - skip for now
                continue;
            }
            CieOrFde::Fde(partial_fde) => {
                // Frame Description Entry - this contains the function-specific info
                let fde =
                    partial_fde.parse(|_, bases, offset| section.cie_from_offset(bases, offset))?;

                let fde_start = fde.initial_address();
                let fde_end = fde_start + fde.len();

                // Check if this FDE covers our function
                if function_address >= fde_start && function_address < fde_end {
                    if verbose {
                        println!(
                            "    Found FDE in {} covering 0x{:08x}-0x{:08x}",
                            section_name, fde_start, fde_end
                        );
                    }

                    // Parse the unwind instructions to get CFA rules
                    return parse_fde_for_cfa(&fde, section, &bases, verbose);
                }
            }
        }
    }

    if verbose {
        println!(
            "    No FDE found in {} for address 0x{:08x}",
            section_name, function_address
        );
    }

    Ok(None)
}

/// Parse Frame Description Entry to extract CFA offset
fn parse_fde_for_cfa<R: gimli::Reader, Section: gimli::UnwindSection<R>>(
    fde: &gimli::FrameDescriptionEntry<R>,
    section: &Section,
    bases: &BaseAddresses,
    verbose: bool,
) -> Result<Option<i64>> {
    // Create unwind context for parsing the instructions
    let mut ctx = gimli::UnwindContext::new();

    // Initialize unwind table for this FDE
    let mut table = fde.rows(section, bases, &mut ctx)?;

    // Get the first row which contains the initial CFA rule
    if let Some(row) = table.next_row()? {
        let cfa = row.cfa();

        match *cfa {
            gimli::CfaRule::RegisterAndOffset { register, offset } => {
                if verbose {
                    println!("    CFA rule: register {} + offset {}", register.0, offset);
                }

                // For AArch64, register 31 is the stack pointer (SP)
                // For other architectures, you'd check different register numbers
                match register.0 {
                    31 => {
                        if verbose {
                            println!("    CFA = SP + {} (AArch64)", offset);
                        }
                        return Ok(Some(offset));
                    }
                    7 => {
                        if verbose {
                            println!("    CFA = RSP + {} (x86_64)", offset);
                        }
                        return Ok(Some(offset));
                    }
                    _ => {
                        if verbose {
                            println!(
                                "    CFA uses register {} (unknown architecture)",
                                register.0
                            );
                        }
                        return Ok(Some(offset));
                    }
                }
            }
            gimli::CfaRule::Expression(_) => {
                if verbose {
                    println!("    CFA defined by expression (too complex to parse here)");
                }
                return Ok(None);
            }
        }
    }

    if verbose {
        println!("    No CFA rule found in FDE");
    }

    Ok(None)
}

/// Extract CFA offset using readelf as an external tool
///

/// Parse a simple DWARF expression to extract CFA offset
///
/// This is a very basic parser that handles common cases like:
/// - DW_OP_call_frame_cfa (CFA + 0)
/// - DW_OP_call_frame_cfa + DW_OP_plus_uconst(offset)
/// - DW_OP_fbreg(offset) (frame base register + offset)
///
/// Real DWARF expressions can be much more complex and might require
/// a full expression evaluator.
fn parse_simple_cfa_expression(expr: &[u8]) -> Option<i64> {
    if expr.is_empty() {
        return None;
    }

    match expr[0] {
        // DW_OP_call_frame_cfa = 0x9c
        0x9c => {
            if expr.len() == 1 {
                // Just CFA, offset is 0
                Some(0)
            } else if expr.len() > 1 && expr[1] == 0x23 {
                // DW_OP_plus_uconst = 0x23
                // Try to decode the ULEB128 constant that follows
                if let Some((offset, _)) = decode_uleb128(&expr[2..]) {
                    Some(offset as i64)
                } else {
                    None
                }
            } else {
                // More complex expression
                None
            }
        }
        // DW_OP_fbreg = 0x91
        0x91 => {
            // Frame base register + SLEB128 offset
            if let Some((offset, _)) = decode_sleb128(&expr[1..]) {
                Some(offset)
            } else {
                None
            }
        }
        _ => None,
    }
}

/// Decode ULEB128 (Unsigned Little Endian Base 128) integer
/// This is a variable-length encoding used in DWARF
fn decode_uleb128(data: &[u8]) -> Option<(u64, usize)> {
    let mut result = 0u64;
    let mut shift = 0;
    let mut i = 0;

    for &byte in data {
        i += 1;
        result |= ((byte & 0x7f) as u64) << shift;

        if byte & 0x80 == 0 {
            return Some((result, i));
        }

        shift += 7;
        if shift >= 64 {
            return None; // Overflow
        }
    }

    None
}

/// Decode SLEB128 (Signed Little Endian Base 128) integer
/// This is a variable-length encoding used in DWARF for signed values
fn decode_sleb128(data: &[u8]) -> Option<(i64, usize)> {
    let mut result = 0i64;
    let mut shift = 0;
    let mut i = 0;
    let mut byte = 0u8;

    for &b in data {
        byte = b;
        i += 1;
        result |= ((byte & 0x7f) as i64) << shift;
        shift += 7;

        if byte & 0x80 == 0 {
            break;
        }

        if shift >= 64 {
            return None; // Overflow
        }
    }

    // Sign extend if necessary
    if shift < 64 && (byte & 0x40) != 0 {
        result |= !0i64 << shift;
    }

    Some((result, i))
}

/// Get a string attribute from a DWARF DIE
///
/// String attributes in DWARF can be stored in different ways:
/// - Directly in the DIE (DW_FORM_string)
/// - As an offset into the string table (DW_FORM_strp)
/// - Other forms...
fn get_string_attribute(
    dwarf: &Dwarf<EndianSlice<LittleEndian>>,
    _unit: &Unit<EndianSlice<LittleEndian>>,
    entry: &gimli::DebuggingInformationEntry<EndianSlice<LittleEndian>>,
    attr: gimli::DwAt,
) -> Result<Option<String>> {
    if let Some(attr_value) = entry.attr_value(attr)? {
        match attr_value {
            AttributeValue::DebugStrRef(offset) => {
                if let Ok(s) = dwarf.string(offset) {
                    let s = s.to_string_lossy().into_owned();
                    return Ok(Some(s));
                }
            }
            AttributeValue::String(s) => {
                let s = s.to_string_lossy().into_owned();
                return Ok(Some(s));
            }
            _ => {}
        }
    }
    Ok(None)
}

/// Print summary of all functions found
fn print_summary(functions: &[CfaInfo]) {
    println!("\n=== Function Summary ===");

    // Group by compilation unit
    let mut by_cu: HashMap<usize, Vec<&CfaInfo>> = HashMap::new();
    for func in functions {
        by_cu.entry(func.compilation_unit).or_default().push(func);
    }

    for (cu_idx, cu_functions) in by_cu {
        println!(
            "\nCompilation Unit {}: {} functions",
            cu_idx,
            cu_functions.len()
        );
        for func in cu_functions {
            let cfa_info = match func.cfa_offset {
                Some(offset) => format!("CFA+{}", offset),
                None => "CFA unknown".to_string(),
            };
            println!(
                "  {} (0x{:08x}-0x{:08x}) [{}]",
                func.name, func.low_pc, func.high_pc, cfa_info
            );
        }
    }
}

/// Print detailed information for all functions
fn print_all_functions(functions: &[CfaInfo]) {
    println!("\n=== All Functions ===");

    for (i, func) in functions.iter().enumerate() {
        println!("\nFunction #{}: {}", i + 1, func.name);
        println!("  Compilation Unit: {}", func.compilation_unit);
        println!(
            "  Address Range: 0x{:08x} - 0x{:08x} (size: {} bytes)",
            func.low_pc,
            func.high_pc,
            func.high_pc - func.low_pc
        );

        match func.cfa_offset {
            Some(offset) => {
                println!("  CFA Offset: {} (0x{:x})", offset, offset);
                println!(
                    "  Local variables are likely at: CFA + {} + variable_offset",
                    offset
                );
            }
            None => {
                println!("  CFA Offset: Unknown - may require complex DWARF expression evaluation");
                println!("  Note: This might indicate a more complex frame layout");
            }
        }
    }
}

/// Print CFA information for a specific function
fn print_function_cfa(functions: &[CfaInfo], function_name: &str) {
    let matches: Vec<_> = functions
        .iter()
        .filter(|f| f.name == function_name)
        .collect();

    if matches.is_empty() {
        println!("\n❌ Function '{}' not found!", function_name);
        println!("Available functions:");
        for func in functions.iter().take(10) {
            println!("  - {}", func.name);
        }
        if functions.len() > 10 {
            println!("  ... and {} more", functions.len() - 10);
        }
        return;
    }

    println!("\n✅ Function '{}' found:", function_name);

    for func in matches {
        println!("\n  📍 Location:");
        println!("    Compilation Unit: {}", func.compilation_unit);
        println!(
            "    Address Range: 0x{:08x} - 0x{:08x}",
            func.low_pc, func.high_pc
        );
        println!("    Size: {} bytes", func.high_pc - func.low_pc);

        println!("\n  🎯 CFA Information:");
        match func.cfa_offset {
            Some(offset) => {
                println!("    CFA Offset: {} (0x{:x})", offset, offset);
                println!("    ✨ For A2L generation:");
                println!(
                    "       Local variable address = CFA + {} + variable_stack_offset",
                    offset
                );
                println!("       Where CFA is the Canonical Frame Address at function entry");
            }
            None => {
                println!("    ⚠️  CFA Offset: Could not determine");
                println!("       This may indicate:");
                println!("       - Complex DWARF expression for frame base");
                println!("       - Optimized code with variable frame layout");
                println!("       - Missing debug information");
            }
        }

        println!("\n  💡 Usage for A2L Generation:");
        println!("     1. Use this CFA offset as the base for local variable addresses");
        println!("     2. Add the variable's stack offset (from DWARF location expressions)");
        println!("     3. The result is the offset from the current stack pointer");
    }
}
