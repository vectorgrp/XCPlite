use clap::{Arg, Command};
use gimli::{EndianSlice, LittleEndian, UnwindSection};
use object::{Object, ObjectSection};
use std::error::Error;
use std::fs;

#[derive(Debug, Clone)]
pub struct FunctionInfo {
    pub name: String,
    pub compilation_unit: String,
    pub pc_start: u64,
    pub pc_end: u64,
}

pub struct SimpleDwarfReader {
    file_data: Vec<u8>,
}

impl SimpleDwarfReader {
    pub fn new(file_data: Vec<u8>) -> Self {
        SimpleDwarfReader { file_data }
    }

    /// Get CFA offset for a specific PC address using .eh_frame
    pub fn get_cfa_offset_for_pc(&self, target_pc: u64) -> Result<Option<i64>, Box<dyn Error>> {
        let object = object::File::parse(&self.file_data)?;

        let eh_frame_data = object
            .section_by_name(".eh_frame")
            .and_then(|s| s.data().ok())
            .unwrap_or(&[]);

        if eh_frame_data.is_empty() {
            return Ok(None);
        }

        let eh_frame = gimli::EhFrame::new(eh_frame_data, LittleEndian);
        let mut entries = eh_frame.entries(&gimli::BaseAddresses::default());

        while let Some(entry) = entries.next()? {
            if let gimli::CieOrFde::Fde(partial) = entry {
                let fde = partial.parse(|_section, _bases, cie| {
                    // Simple CIE parser - just return a basic CIE
                    Ok(gimli::CommonInformationEntry {
                        offset: cie.offset(),
                        length: cie.length(),
                        format: cie.format(),
                        version: 1,
                        augmentation: gimli::Augmentation::default(),
                        address_size: 8,
                        segment_size: 0,
                        code_alignment_factor: 1,
                        data_alignment_factor: -8,
                        return_address_register: gimli::Register(30), // ARM64 LR
                        initial_instructions: gimli::EndianSlice::new(&[], LittleEndian),
                    })
                })?;

                let pc_start = fde.initial_address();
                let pc_end = pc_start + fde.len();

                if target_pc >= pc_start && target_pc < pc_end {
                    return self.extract_cfa_offset_from_fde(&fde);
                }
            }
        }

        Ok(None)
    }

    fn extract_cfa_offset_from_fde<R: gimli::Reader>(
        &self,
        fde: &gimli::FrameDescriptionEntry<R>,
    ) -> Result<Option<i64>, Box<dyn Error>> {
        let mut ctx = gimli::UnwindContext::new();

        // Try to get unwind info for the FDE
        let bases = gimli::BaseAddresses::default();
        let mut table = fde.unwind_info_for_address(
            &gimli::EhFrame::new(&[], LittleEndian),
            &bases,
            &mut ctx,
            fde.initial_address(),
        )?;

        // Check CFA rule
        if let gimli::CfaRule::RegisterAndOffset { register, offset } = table.cfa() {
            // ARM64: frame pointer is register 29
            if register.0 == 29 {
                return Ok(Some(*offset));
            }
        }

        Ok(None)
    }

    /// Parse functions from symbol table (simpler than full DWARF parsing)
    pub fn parse_functions_from_symbols(&self) -> Result<Vec<FunctionInfo>, Box<dyn Error>> {
        let object = object::File::parse(&self.file_data)?;
        let mut functions = Vec::new();

        for symbol in object.symbols() {
            if symbol.kind() == object::SymbolKind::Text {
                if let Ok(name) = symbol.name() {
                    if !name.is_empty() && !name.starts_with('_') {
                        functions.push(FunctionInfo {
                            name: name.to_string(),
                            compilation_unit: "unknown".to_string(),
                            pc_start: symbol.address(),
                            pc_end: symbol.address() + symbol.size(),
                        });
                    }
                }
            }
        }

        // Sort by address
        functions.sort_by_key(|f| f.pc_start);

        Ok(functions)
    }

    /// Find function by name
    pub fn find_function(&self, name: &str) -> Result<Option<FunctionInfo>, Box<dyn Error>> {
        let functions = self.parse_functions_from_symbols()?;

        for func in functions {
            if func.name == name || func.name.contains(name) {
                return Ok(Some(func));
            }
        }

        Ok(None)
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let matches = Command::new("Simple DWARF CFA Reader")
        .version("0.1.0")
        .about("Looks up CFA offsets for functions in ELF executables")
        .arg(
            Arg::new("executable")
                .short('e')
                .long("executable")
                .value_name("FILE")
                .help("Path to the ELF executable")
                .required(true),
        )
        .arg(
            Arg::new("function")
                .short('f')
                .long("function")
                .value_name("NAME")
                .help("Function name to look up"),
        )
        .arg(
            Arg::new("pc")
                .short('p')
                .long("pc")
                .value_name("ADDRESS")
                .help("PC address to look up (hex format: 0x2054)"),
        )
        .arg(
            Arg::new("list-functions")
                .short('l')
                .long("list-functions")
                .help("List all available functions")
                .action(clap::ArgAction::SetTrue),
        )
        .get_matches();

    let executable_path = matches.get_one::<String>("executable").unwrap();
    let file_data = fs::read(executable_path)?;
    let reader = SimpleDwarfReader::new(file_data);

    // List all functions if requested
    if matches.get_flag("list-functions") {
        println!("Available functions:");
        let functions = reader.parse_functions_from_symbols()?;
        for func in functions {
            println!(
                "  {} PC: 0x{:x}-0x{:x}",
                func.name, func.pc_start, func.pc_end
            );
        }
        return Ok(());
    }

    // Look up by PC address
    if let Some(pc_str) = matches.get_one::<String>("pc") {
        let pc = if pc_str.starts_with("0x") {
            u64::from_str_radix(&pc_str[2..], 16)?
        } else {
            pc_str.parse::<u64>()?
        };

        println!("Looking up CFA offset for PC: 0x{:x}", pc);

        match reader.get_cfa_offset_for_pc(pc)? {
            Some(offset) => {
                println!("CFA Offset: {} bytes", offset);
                println!("\nFor A2L generation:");
                println!(
                    "  Variable address = CFA offset ({}) + DWARF variable offset",
                    offset
                );
                println!(
                    "  Example: If DWARF says DW_OP_fbreg -24, then A2L address = {} + (-24) = {}",
                    offset,
                    offset - 24
                );
            }
            None => {
                println!("CFA Offset: Not found for PC 0x{:x}", pc);
            }
        }
        return Ok(());
    }

    // Look up specific function
    if let Some(function_name) = matches.get_one::<String>("function") {
        if let Some(func) = reader.find_function(function_name)? {
            println!("Function: {}", func.name);
            println!("PC Range: 0x{:x}-0x{:x}", func.pc_start, func.pc_end);

            // Use middle of function for CFA lookup
            let pc = func.pc_start + (func.pc_end - func.pc_start) / 2;

            match reader.get_cfa_offset_for_pc(pc)? {
                Some(offset) => {
                    println!("CFA Offset: {} bytes", offset);
                    println!("\nFor A2L generation:");
                    println!(
                        "  Variable address = CFA offset ({}) + DWARF variable offset",
                        offset
                    );
                    println!("  Example: If DWARF says DW_OP_fbreg -24, then A2L address = {} + (-24) = {}", 
                        offset, offset - 24);
                }
                None => {
                    println!("CFA Offset: Not found (no .eh_frame data for this function)");
                }
            }
        } else {
            eprintln!("Function '{}' not found", function_name);

            // Suggest similar function names
            let functions = reader.parse_functions_from_symbols()?;
            let similar: Vec<_> = functions
                .iter()
                .filter(|f| f.name.contains(function_name) || function_name.contains(&f.name))
                .take(5)
                .collect();

            if !similar.is_empty() {
                eprintln!("\nSimilar functions found:");
                for func in similar {
                    eprintln!("  {}", func.name);
                }
            }
        }
    } else {
        eprintln!("Please specify either --function, --pc, or --list-functions");
        return Ok(());
    }

    Ok(())
}
