//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module debuginfo
// Read ELF files and extract debug information

// Taken from Github repository a2ltool by DanielT

use indexmap::IndexMap;
use std::collections::HashMap;
use std::ffi::OsStr;
use std::fmt::Display;

mod dwarf;

mod cfa;
use cfa::CfaInfo;

// VarInfo holds information about a variable
#[derive(Debug)]
pub(crate) struct VarInfo {
    pub(crate) address: (u8, u64),       // addr_ext, addr
    pub(crate) typeref: usize,           // reference to TypeInfo in DebugData.types
    pub(crate) unit_idx: usize,          // compilation unit index
    pub(crate) function: Option<String>, // function name if variable is local to a function
    pub(crate) namespaces: Vec<String>,  // namespaces the variable is defined in
}

// TypeInfo holds information about a variable's type
// get_size - returns the size of the type in bytes
// Display - formats the type information as a string
#[derive(Debug, Clone)]
pub(crate) struct TypeInfo {
    pub(crate) name: Option<String>,  // not all types have a name
    pub(crate) unit_idx: usize,       // compilation unit index
    pub(crate) datatype: DbgDataType, // the actual type information
    pub(crate) dbginfo_offset: usize, // offset in the debug info section
}

#[derive(Debug, Clone)]
pub(crate) enum DbgDataType {
    Uint8,
    Uint16,
    Uint32,
    Uint64,
    Sint8,
    Sint16,
    Sint32,
    Sint64,
    Float,
    Double,
    Bitfield {
        basetype: Box<TypeInfo>,
        bit_offset: u16,
        bit_size: u16,
    },
    Pointer(u64, usize),
    Struct {
        size: u64,
        members: IndexMap<String, (TypeInfo, u64)>,
    },
    Class {
        size: u64,
        inheritance: IndexMap<String, (TypeInfo, u64)>,
        members: IndexMap<String, (TypeInfo, u64)>,
    },
    Union {
        size: u64,
        members: IndexMap<String, (TypeInfo, u64)>,
    },
    Enum {
        size: u64,
        signed: bool,
        enumerators: Vec<(String, i64)>,
    },
    Array {
        size: u64,
        dim: Vec<u64>,
        stride: u64,
        arraytype: Box<TypeInfo>,
    },
    TypeRef(usize, u64), // dbginfo_offset of the referenced type
    FuncPtr(u64),
    Other(u64),
}

// holds the debug information from an ELF file
#[derive(Debug)]
pub(crate) struct DebugData {
    pub(crate) variables: IndexMap<String, Vec<VarInfo>>, // variable name -> list of VarInfo for instances with that name
    pub(crate) types: HashMap<usize, TypeInfo>,           // type reference -> TypeInfo
    pub(crate) typenames: HashMap<String, Vec<usize>>,    // type name -> list of type references
    pub(crate) demangled_names: HashMap<String, String>,  // mangled name -> demangled name
    pub(crate) unit_names: Vec<Option<String>>,           // list of compilation unit names by unit index
    pub(crate) sections: HashMap<String, (u64, u64)>,     // section name -> (start, end)
    pub(crate) cfa_info: Vec<CfaInfo>,                    // CFA information for functions which contain an event trigger, the CFA is valid for  the location of the event trigger
}

// load_dwarf - loads and parses the DWARF debug information from an ELF file
// make_simple_unit_name - converts a full unit name to a simple unit name
// print_debug_info - prints the debug information to the console
// print_debug_stats - prints a summary of the debug information
impl DebugData {
    /// load the debug info from an elf file
    pub(crate) fn load_dwarf(filename: &OsStr, verbose: usize, unit_idx_limit: usize) -> Result<Self, String> {
        dwarf::load_elf_dwarf(filename, verbose, unit_idx_limit)
    }

    /// convert a full unit name, which might include a path, into a simple unit name
    pub(crate) fn make_simple_unit_name(&self, unit_idx: usize) -> Option<String> {
        let full_name = self.unit_names.get(unit_idx)?.as_deref()?;
        let file_name = if let Some(pos) = full_name.rfind('\\') {
            &full_name[(pos + 1)..]
        } else if let Some(pos) = full_name.rfind('/') {
            &full_name[(pos + 1)..]
        } else {
            full_name
        };

        Some(file_name.replace('.', "_"))
    }

    /// print the debug statistics
    pub(crate) fn print_debug_stats(&self) {
        println!("\n====================================================================================================");

        println!("debug_data information summary:");
        println!("  Compilation units: {} units", self.unit_names.len());
        println!("  Sections: {} sections", self.sections.len());
        println!("  Type names: {} named types", self.typenames.len());
        println!("  Types: {} total types", self.types.len());
        println!("  Demangled names: {} entries", self.demangled_names.len());

        let mut variable_count = 0;
        for (name, var_infos) in &self.variables {
            variable_count += var_infos.len();
        }
        println!("  Variables {} with {} unique names", variable_count, self.variables.len());

        //Print compilation units
        println!("\n====================================================================================================");
        println!("Compilation Units in debug_data.unit_names:");
        for (idx, unit_name) in self.unit_names.iter().enumerate() {
            let unit_name = self.make_simple_unit_name(idx);
            if unit_name.is_none() {
                println!("  Unit {}: <unnamed>", idx);
            } else {
                println!("  Unit {}: {}", idx, unit_name.as_ref().unwrap());
            }
        }
        println!();
    }

    // level 0 .. 5 stats, variables, variable types, demangled names, type names, types
    pub(crate) fn print_debug_info(&self, level: usize, unit_idx_limit: usize) {
        // level = 0
        println!("\n====================================================================================================");
        self.print_debug_stats();

        // Print sections sorted by address
        println!("\n====================================================================================================");
        println!("Memory Sections in debug_data.sections:");
        let mut sections: Vec<(&String, &(u64, u64))> = self.sections.iter().collect();
        sections.sort_by_key(|&(_, (addr, _))| *addr);
        let mut last_addr: u64 = 0;
        for (name, (addr, size)) in sections {
            println!("  '{}': 0x{:08x}, {} bytes ({})", name, *addr, *addr - last_addr, *size);
            last_addr = *addr;
        }

        if level >= 4 {
            //Print type names
            println!("\n====================================================================================================");
            println!("Type names in debug_data.typenames)");
            for (type_name, type_refs) in &self.typenames {
                println!("Type name '{}': {} references", type_name, type_refs.len());
                for type_ref in type_refs {
                    if let Some(type_info) = self.types.get(type_ref) {
                        println!("  -> type_ref={}, size={} bytes, unit={}", type_ref, type_info.get_size(), type_info.unit_idx);
                    }
                }
            }

            if level >= 5 {
                // Print types
                println!("\n====================================================================================================");
                println!("Types in debug_data.types:");
                for (type_ref, type_info) in &self.types {
                    let type_name = if let Some(name) = &type_info.name { name } else { "" };
                    println!(
                        "TypeRef {}: name = '{}', size = {} bytes, unit = {}, type={}",
                        type_ref,
                        type_name,
                        type_info.get_size(),
                        type_info.unit_idx,
                        type_info
                    );
                }
            }

            // Print demangled names
            if level >= 3 {
                println!("\n====================================================================================================");
                println!("\nDemangled Names");
                for (mangled_name, demangled_name) in &self.demangled_names {
                    println!("  '{}' -> '{}'", mangled_name, demangled_name);
                }
            }
        }

        // Print A2L Creator variables
        println!("\n====================================================================================================");
        println!("A2L Creator variables in compilation unit 0..{unit_idx_limit}:");
        for (var_name, var_info) in &self.variables {
            if var_name.starts_with("cal__") || var_name.starts_with("evt__") || var_name.starts_with("trg__") {
                print!("'{}': ", var_name);
                assert!(var_info.len() == 1);
                let var = &var_info[0];
                let unit_name = if let Some(name) = self.make_simple_unit_name(var.unit_idx) {
                    name
                } else {
                    "<unnamed>".to_string()
                };
                let function_name = if let Some(name) = &var.function { name } else { "<global>" };
                let name_space = if var.namespaces.len() > 0 { var.namespaces.join("::") } else { "".to_string() };
                println!(" {}:'{}' {}: addr={}:0x{:08X}", unit_name, function_name, name_space, var.address.0, var.address.1);
            }
        }

        // Print variables
        if level >= 1 {
            if level >= 5 {
                println!("\n====================================================================================================");
                println!("System variables  in compilation unit 0..{unit_idx_limit}:");
                for (var_name, var_info) in &self.variables {
                    if var_name.starts_with("__") {
                        println!("{}: ", var_name);
                    }
                }
            }

            println!("\n====================================================================================================");
            println!("Variables in compilation unit 0..{unit_idx_limit}:");
            if level <= 1 {
                println!("  (Skipping internal variables '__<name>' and global XCP variables 'gXcp..' and 'gA2l..')");
            }

            for (var_name, var_info) in &self.variables {
                // Count all variable in unit_idx
                let count = var_info.iter().filter(|v| v.unit_idx <= unit_idx_limit).count();

                // Skip standard library variables and system/compiler internals (__<name>)s
                // Skip global XCP variables (gXCP.. and gA2L..)
                if level < 5 && var_name.starts_with("__") || var_name.starts_with("gXcp") || var_name.starts_with("gA2l") {
                    continue;
                }

                // print only variables from compilation unit 0..=unit_idx
                if count == 1 && var_info[0].unit_idx > unit_idx_limit {
                    continue;
                }

                // Iterate over all variable infos for this variable name in unit_idx
                if level >= 1 {
                    println!("{} {}: ", var_name, count);
                } else if level >= 2 {
                    if count > 1 {
                        println!("{} {}: ", var_name, count);
                    }
                    for var in var_info {
                        // print only variables from compilation unit 0..=unit_idx
                        if var.unit_idx > unit_idx_limit {
                            continue; // print only variables from compilation unit 0..=unit_idx
                        }
                        if count <= 1 {
                            print!("{} : ", var_name);
                        }
                        let unit_name = if let Some(name) = self.make_simple_unit_name(var.unit_idx) {
                            name
                        } else {
                            "<unnamed>".to_string()
                        };
                        let function_name = if let Some(name) = &var.function { name } else { "<global>" };
                        let name_space = if var.namespaces.len() > 0 { var.namespaces.join("::") } else { "".to_string() };
                        print!(" {}:'{}' {}: addr={}:0x{:08X}", unit_name, function_name, name_space, var.address.0, var.address.1);
                        if let Some(type_info) = self.types.get(&var.typeref) {
                            let type_name = if let Some(name) = &type_info.name { name } else { "" };
                            print!(", type='{}', size={}", type_name, type_info.get_size());
                        }
                        println!();
                    }
                }
            }
        }

        println!();
    }
}

// TypeInfo holds information about a variable's type
impl TypeInfo {
    pub(crate) fn get_size(&self) -> u64 {
        match &self.datatype {
            DbgDataType::Uint8 => 1,
            DbgDataType::Uint16 => 2,
            DbgDataType::Uint32 => 4,
            DbgDataType::Uint64 => 8,
            DbgDataType::Sint8 => 1,
            DbgDataType::Sint16 => 2,
            DbgDataType::Sint32 => 4,
            DbgDataType::Sint64 => 8,
            DbgDataType::Float => 4,
            DbgDataType::Double => 8,
            DbgDataType::Bitfield { basetype, .. } => basetype.get_size(),
            DbgDataType::Pointer(size, _)
            | DbgDataType::Other(size)
            | DbgDataType::Struct { size, .. }
            | DbgDataType::Class { size, .. }
            | DbgDataType::Union { size, .. }
            | DbgDataType::Enum { size, .. }
            | DbgDataType::Array { size, .. }
            | DbgDataType::FuncPtr(size)
            | DbgDataType::TypeRef(_, size) => *size,
        }
    }
}

impl Display for TypeInfo {
    /*


        /// print detailed type information
        pub(crate) fn print_type_info(&self, type_info: &TypeInfo) {
            let type_name = if let Some(name) = &type_info.name { name } else { "" };
            let type_size = type_info.get_size();

            print!("    TypeInfo: {}", type_name);
            // print!(" (unit_idx = {}, dbginfo_offset = {})",type_info.unit_idx, type_info.dbginfo_offset);

            match &type_info.datatype {
                DbgDataType::Uint8 | DbgDataType::Uint16 | DbgDataType::Uint32 | DbgDataType::Uint64 => {
                    println!(" Integer: {} byte unsigned", type_size);
                }
                DbgDataType::Sint8 | DbgDataType::Sint16 | DbgDataType::Sint32 | DbgDataType::Sint64 => {
                    println!(" Integer: {} byte signed", type_size);
                }
                DbgDataType::Float | DbgDataType::Double => {
                    println!(" Floating point: {} byte", type_size);
                }

                DbgDataType::Pointer(typeref, size) => {
                    println!(" Pointer: typeref = {}, size = {} ", typeref, size);
                }
                DbgDataType::Array { arraytype, dim, stride, size } => {
                    println!(" Array: typeref = {}, dim = {:?}, stride = {} bytes, size = {} bytes", arraytype, dim, stride, size);
                }
                DbgDataType::Struct { size, members } => {
                    println!(" Struct: {} fields, size = {}", members.len(), size);
                    for (name, (type_info, member_offset)) in members {
                        let member_size = type_info.get_size();
                        println!("      Field '{}': size = {} bytes, offset = {} bytes", name, member_size, member_offset);
                    }
                }
                DbgDataType::Union { members, size } => {
                    println!(" Union: {} members, size = {} bytes", members.len(), size);
                }
                DbgDataType::Enum { size, signed, enumerators } => {
                    println!(" Enum: {} variants, size = {} bytes", enumerators.len(), size);
                    for (name, value) in enumerators {
                        println!("      Variant '{}': value={}", name, value);
                    }
                }
                DbgDataType::Bitfield { basetype, bit_offset, bit_size } => {
                    println!(" Bitfield: base type = {:?}, offset = {} bits, size = {} bits", basetype.datatype, bit_offset, bit_size);
                }
                DbgDataType::Class { size, inheritance, members } => {
                    println!(" Class: {} members, size = {} bytes", members.len(), size);
                }
                DbgDataType::FuncPtr(size) => {
                    println!(" Function pointer: size = {} bytes", size);
                }
                DbgDataType::TypeRef(typeref, size) => {
                    println!(" TypeRef: typeref = {}, size = {} bytes", typeref, size);
                }
                _ => {
                    println!(" Other type: {:?}", &type_info.datatype);
                }
            }
        }


    */

    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self.datatype {
            DbgDataType::Uint8 => f.write_str("Uint8"),
            DbgDataType::Uint16 => f.write_str("Uint16"),
            DbgDataType::Uint32 => f.write_str("Uint32"),
            DbgDataType::Uint64 => f.write_str("Uint64"),
            DbgDataType::Sint8 => f.write_str("Sint8"),
            DbgDataType::Sint16 => f.write_str("Sint16"),
            DbgDataType::Sint32 => f.write_str("Sint32"),
            DbgDataType::Sint64 => f.write_str("Sint64"),
            DbgDataType::Float => f.write_str("Float"),
            DbgDataType::Double => f.write_str("Double"),
            DbgDataType::Bitfield { .. } => f.write_str("Bitfield"),
            DbgDataType::Pointer(_, _) => write!(f, "Pointer(...)"),
            DbgDataType::Other(osize) => write!(f, "Other({osize})"),
            DbgDataType::FuncPtr(osize) => write!(f, "function pointer({osize})"),
            DbgDataType::Struct { members, .. } => {
                if let Some(name) = &self.name {
                    write!(f, "Struct {name}({} members)", members.len())
                } else {
                    write!(f, "Struct <anonymous>({} members)", members.len())
                }
            }
            DbgDataType::Class { members, .. } => {
                if let Some(name) = &self.name {
                    write!(f, "Class {name}({} members)", members.len())
                } else {
                    write!(f, "Class <anonymous>({} members)", members.len())
                }
            }
            DbgDataType::Union { members, .. } => {
                if let Some(name) = &self.name {
                    write!(f, "Union {name}({} members)", members.len())
                } else {
                    write!(f, "Union <anonymous>({} members)", members.len())
                }
            }
            DbgDataType::Enum { enumerators, .. } => {
                if let Some(name) = &self.name {
                    write!(f, "Enum {name}({} enumerators)", enumerators.len())
                } else {
                    write!(f, "Enum <anonymous>({} enumerators)", enumerators.len())
                }
            }
            DbgDataType::Array { dim, arraytype, .. } => {
                write!(f, "Array({dim:?} x {arraytype})")
            }
            DbgDataType::TypeRef(t_ref, _) => write!(f, "TypeRef({t_ref})"),
        }
    }
}

#[cfg(test)]
mod test {}
