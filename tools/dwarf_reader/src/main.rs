use clap::{Arg, Command};
use std::error::Error;
use std::process::Command as ProcessCommand;

#[derive(Debug, Clone)]
pub struct FunctionInfo {
    pub name: String,
    pub pc_start: u64,
    pub pc_end: u64,
}

#[derive(Debug)]
pub struct CfaEntry {
    pub pc_start: u64,
    pub pc_end: u64,
    pub cfa_offset: i64,
}

pub struct SimpleCfaReader {
    executable_path: String,
    cfa_cache: Option<Vec<CfaEntry>>,
}

impl SimpleCfaReader {
    pub fn new(executable_path: String) -> Self {
        SimpleCfaReader {
            executable_path,
            cfa_cache: None,
        }
    }

    /// Parse CFA entries using remote readelf command on Raspberry Pi
    fn parse_cfa_entries(&mut self) -> Result<&Vec<CfaEntry>, Box<dyn Error>> {
        if self.cfa_cache.is_some() {
            return Ok(self.cfa_cache.as_ref().unwrap());
        }

        // Use remote readelf on Raspberry Pi to get .eh_frame information
        let remote_path = if let Some(relative_path) = self
            .executable_path
            .strip_prefix("/Users/Rainer.Zaiser/git/XCPlite-RainerZ/")
        {
            format!("~/XCPlite-RainerZ/{}", relative_path)
        } else {
            // If it's already a relative path, use it as-is
            format!(
                "~/XCPlite-RainerZ/{}",
                self.executable_path.trim_start_matches("../")
            )
        };

        let output = ProcessCommand::new("ssh")
            .args(&[
                "rainer@192.168.0.206",
                &format!("readelf -wF {}", remote_path),
            ])
            .output()?;

        if !output.status.success() {
            return Err(format!(
                "readelf command failed: {}",
                String::from_utf8_lossy(&output.stderr)
            )
            .into());
        }

        let output_str = String::from_utf8(output.stdout)?;
        let mut cfa_entries = Vec::new();

        for line in output_str.lines() {
            if let Some(entry) = self.parse_cfa_line(line)? {
                cfa_entries.push(entry);
            }
        }

        cfa_entries.sort_by_key(|e| e.pc_start);

        // Merge adjacent entries with same CFA offset
        let mut merged_entries: Vec<CfaEntry> = Vec::new();
        for entry in cfa_entries {
            if let Some(last) = merged_entries.last_mut() {
                if last.cfa_offset == entry.cfa_offset && last.pc_end == entry.pc_start {
                    // Extend the range
                    last.pc_end = entry.pc_end;
                    continue;
                }
            }
            merged_entries.push(entry);
        }

        self.cfa_cache = Some(merged_entries);
        Ok(self.cfa_cache.as_ref().unwrap())
    }

    fn parse_cfa_line(&self, line: &str) -> Result<Option<CfaEntry>, Box<dyn Error>> {
        // Parse lines like:
        // "000000000000205c x29+96   u     c-96  c-88"
        if !line.contains("x29+") {
            return Ok(None);
        }

        // Parse lines like: "000000000000205c x29+96   u     c-96  c-88"
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 {
            return Ok(None);
        }

        // Parse PC address (first column)
        let pc_str = parts[0];
        if let Ok(pc) = u64::from_str_radix(pc_str, 16) {
            // Parse CFA column (second column) like "x29+96"
            let cfa_part = parts[1];
            if let Some(plus_pos) = cfa_part.find("x29+") {
                let offset_start = plus_pos + 4; // Skip "x29+"
                let offset_str = &cfa_part[offset_start..];
                if let Ok(cfa_offset) = offset_str.parse::<i64>() {
                    // For simplicity, use a small range around this PC
                    return Ok(Some(CfaEntry {
                        pc_start: pc,
                        pc_end: pc + 4, // 4 byte instruction
                        cfa_offset,
                    }));
                }
            }
        }

        Ok(None)
    }

    /// Parse functions using remote nm command on Raspberry Pi
    pub fn parse_functions(&self) -> Result<Vec<FunctionInfo>, Box<dyn Error>> {
        let remote_path = if let Some(relative_path) = self
            .executable_path
            .strip_prefix("/Users/Rainer.Zaiser/git/XCPlite-RainerZ/")
        {
            format!("~/XCPlite-RainerZ/{}", relative_path)
        } else {
            // If it's already a relative path, use it as-is
            format!(
                "~/XCPlite-RainerZ/{}",
                self.executable_path.trim_start_matches("../")
            )
        };

        let output = ProcessCommand::new("ssh")
            .args(&["rainer@192.168.0.206", &format!("nm -n {}", remote_path)])
            .output()?;

        if !output.status.success() {
            return Err(format!(
                "nm command failed: {}",
                String::from_utf8_lossy(&output.stderr)
            )
            .into());
        }

        let output_str = String::from_utf8_lossy(&output.stdout);
        let mut functions = Vec::new();

        for line in output_str.lines() {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 3 && parts[1] == "T" {
                if let Ok(addr) = u64::from_str_radix(parts[0], 16) {
                    let name = parts[2].to_string();
                    if !name.starts_with('_') && !name.contains('.') {
                        functions.push(FunctionInfo {
                            name,
                            pc_start: addr,
                            pc_end: addr + 1, // Will be updated with size info if available
                        });
                    }
                }
            }
        }

        // Update end addresses based on next function start
        for i in 0..functions.len() {
            if i + 1 < functions.len() {
                functions[i].pc_end = functions[i + 1].pc_start;
            }
        }

        Ok(functions)
    }

    /// Get CFA offset for a specific PC address
    pub fn get_cfa_offset_for_pc(&mut self, target_pc: u64) -> Result<Option<i64>, Box<dyn Error>> {
        let cfa_entries = self.parse_cfa_entries()?;

        for entry in cfa_entries {
            if target_pc >= entry.pc_start && target_pc < entry.pc_end {
                return Ok(Some(entry.cfa_offset));
            }
        }

        Ok(None)
    }

    /// Get CFA offset for a specific function
    pub fn get_cfa_offset_for_function(
        &mut self,
        func: &FunctionInfo,
    ) -> Result<Option<i64>, Box<dyn Error>> {
        // Use middle of function for CFA lookup (should be constant throughout)
        let pc = func.pc_start + (func.pc_end - func.pc_start) / 2;
        self.get_cfa_offset_for_pc(pc)
    }

    /// Find function by name
    pub fn find_function(&self, name: &str) -> Result<Option<FunctionInfo>, Box<dyn Error>> {
        let functions = self.parse_functions()?;

        for func in functions {
            if func.name == name || func.name.contains(name) {
                return Ok(Some(func));
            }
        }

        Ok(None)
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let matches = Command::new("Simple CFA Reader")
        .version("0.1.0")
        .about("Looks up CFA offsets for functions in ELF executables using remote readelf")
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
    let mut reader = SimpleCfaReader::new(executable_path.clone());

    // List all functions if requested
    if matches.get_flag("list-functions") {
        println!("Available functions:");
        let functions = reader.parse_functions()?;
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

            match reader.get_cfa_offset_for_function(&func)? {
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
            let functions = reader.parse_functions()?;
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
