//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module debuginfo
// Implements DebugData, VarInfo, TypeInfo and DbgDataType
// Read ELF files and extract debug information

// Based on Github repository a2ltool by DanielT: https://github.com/DanielT/a2ltool

/*
Note on V2.1.10:
Updated to typereader.rs from a2ltool v3.4.1 (commit 0b61aa5, 2026-08-04).
The Class variant is gone.
Struct now carries is_class and inheritance, and the size and Display code follow.
*/

use indexmap::IndexMap;
use std::collections::HashMap;
use std::ffi::OsStr;
use std::fmt::Display;

mod dwarf;

use crate::elf_reader::is_a2l_variable;

// What the DWARF locations of the local variables of a function are relative to (DW_AT_frame_base of the function), and
// whether this is the frame address which the event trigger macro passes to the target (xcp_get_frame_addr() in inc/xcplib.h):
// GCC uses the canonical frame address and the macro passes __builtin_dwarf_cfa(), clang uses the frame pointer register and
// the macro passes __builtin_frame_address(0). In both cases the variable offsets from the DWARF are used as they are. Any other
// frame base (under clang a function without frame pointer describes its locals relative to the stack pointer) is not what
// the target passes, the stack variables of such a function are not registered, see register_event_locations
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FrameBase {
    Cfa,           // DW_OP_call_frame_cfa (GCC)
    FramePointer,  // the frame pointer register of the architecture (clang)
    Register(u16), // another register, e.g. the stack pointer, DWARF register number
    Unknown,       // no or a complex frame base expression, or not a function scope
}

// VarInfo holds information about one definition of a variable. DebugData.variables maps a variable name to a list of
// VarInfo, because the same name may be defined in several compilation units, functions or namespaces, and because a
// global variable declared in a header shows up in the DWARF of every compilation unit which includes the header.
//
// address is the pair (address extension, address) as evaluated from the DWARF location of the variable, see
// evaluate_exprloc in dwarf/attributes.rs:
//   (0, addr)            a fixed address in memory: global variables and static locals. addr == 0 means no address is known
//                        (a declaration, or the variable was optimized away by the compiler or removed by the linker)
//   (2, 0x80000000+off)  a stack variable: off is the offset of the variable from the frame base of its function, the
//                        0x80000000 is the dummy frame base used in the evaluation. Only useful together with the event
//                        trigger in the function, see register_variables
//   (0x80.., _)          not measurable: the variable lives in a register (0x80), in thread local storage (0x81), its
//                        location is too complex (0x82) or unknown (0xFF)
// The extension values 0 and 2 coincide with the XCP address extensions for absolute and dynamic addressing, the values
// >= 0x80 are internal to this tool
#[derive(Debug)]
pub(crate) struct VarInfo {
    pub(crate) frame_base: FrameBase,    // frame base of the function of a local variable, Unknown for global variables
    pub(crate) address: (u8, u64),       // (address extension, address), see above
    pub(crate) typeref: usize,           // the type of the variable: key of the TypeInfo in DebugData.types (a .debug_info offset)
    pub(crate) unit_idx: usize,          // compilation unit index, index into DebugData.unit_names
    pub(crate) function: Option<String>, // function name if variable is local to a function (stack variable or static local)
    pub(crate) namespaces: Vec<String>,  // namespaces the variable is defined in, outermost first
    pub(crate) inlined: bool,            // the variable belongs to a function which the compiler inlined, see load_variables
}

// TypeInfo holds information about a variable's type
// get_size - returns the size of the type in bytes
// Display - formats the type information as a string
//
// The identity of a type is the offset of its DWARF entry in the .debug_info section (dbginfo_offset). Every compilation
// unit has its own copy of the types it uses, so a struct from a header exists once per unit, with a different offset each.
// Qualifiers (const, volatile) and typedefs are transparent: the type reader follows them to the underlying type, the
// name of a typedef is kept as the name of the type if the underlying type has none
#[derive(Debug, Clone)]
pub(crate) struct TypeInfo {
    pub(crate) name: Option<String>,  // not all types have a name (anonymous structs, pointers, arrays)
    pub(crate) unit_idx: usize,       // compilation unit index
    pub(crate) datatype: DbgDataType, // the actual type information
    pub(crate) dbginfo_offset: usize, // offset of the type's entry in .debug_info, the key in DebugData.types
}

// The kinds of types which are distinguished. Basic types are mapped by size and signedness, everything the A2L generator
// can not use (function pointers, unknown encodings) becomes Other(size) and is reported as unsupported when a variable uses it
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
    // A bitfield struct member: basetype is the integer type holding the bits, bit_offset counts from the least significant bit
    Bitfield {
        basetype: Box<TypeInfo>,
        bit_offset: u16,
        bit_size: u16,
    },
    // (pointer size in bytes, dbginfo_offset of the pointed-to type, 0 for void*). Pointers are registered as unsigned integers
    Pointer(u64, usize),
    /// A struct or a class. There is no practical difference between them, both can have base classes in C++;
    /// `is_class` only affects the displayed name. Inherited members are also copied into `members` with adjusted offsets.
    Struct {
        size: u64,
        is_class: bool,
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
    // dim has one entry per dimension (outermost first), stride is the byte distance between elements, size the total size
    Array {
        size: u64,
        dim: Vec<u64>,
        stride: u64,
        arraytype: Box<TypeInfo>,
    },
    // A reference to another type by its dbginfo_offset, with its size. Used for struct/union members of struct/union type
    // (the member refers to the type in DebugData.types instead of holding a copy) and to break recursive array types
    TypeRef(usize, u64),
    // A function pointer, (size in bytes)
    FuncPtr(u64),
    // Any other type, (size in bytes). Not usable for measurement or calibration
    Other(u64),
}

// Holds the debug information from an ELF file, the result of DebugDataReader (dwarf/mod.rs). Everything the ElfReader
// needs is in here, the ELF file and the gimli parser state are gone once this is built
#[derive(Debug)]
pub(crate) struct DebugData {
    pub(crate) variables: IndexMap<String, Vec<VarInfo>>, // variable name -> its definitions, in the order they were found in .debug_info
    pub(crate) types: HashMap<usize, TypeInfo>,           // .debug_info offset of a type -> TypeInfo, only the types used by variables are loaded
    pub(crate) typenames: HashMap<String, Vec<usize>>,    // type name -> the offsets of all types with this name (one per compilation unit and scope)
    pub(crate) qualified_type_names: HashMap<usize, String>, // type reference -> scope qualified name (motor_control.Input) of the struct/class types whose name is used in different scopes
    pub(crate) demangled_names: HashMap<String, String>,     // demangled name -> mangled name, for the variable names which are mangled C++ symbols
    pub(crate) unit_names: Vec<Option<String>>,              // list of compilation unit names by unit index, the DW_AT_name of the unit (usually the source file path)
    pub(crate) producers: Vec<Option<String>>,               // the DW_AT_producer of each unit by unit index: compiler, version and command line options
    pub(crate) sections: HashMap<String, (u64, u64)>,        // ELF section name -> (start address, end address), only sections with an address
    pub(crate) symbol_addresses: HashMap<String, u64>,       // ELF symbol name -> address, the symbol table (.symtab), C++ names are mangled
    pub(crate) epk_string: Option<String>,                   // EPK string read from xcp_epk ELF section
    pub(crate) epk_addr: u64,                                // Address of the xcp_epk ELF section (0 if not found)
    pub(crate) xcp_meta_data: Option<(u64, Vec<u8>)>,        // (section_base_addr, raw_bytes) of xcp_meta section
    pub(crate) is_little_endian: bool,                       // ELF endianness
}

// load_dwarf - loads and parses the DWARF debug information from an ELF file
// make_simple_unit_name - converts a full unit name to a simple unit name
// print_debug_info - prints the debug information to the console
// print_debug_stats - prints a summary of the debug information
impl DebugData {
    /// load the debug info from an elf file
    pub(crate) fn load_dwarf(filename: &OsStr, verbose: usize, unit_idx_limit: (usize, usize)) -> Result<Self, String> {
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

    /// Name of a struct/class type for its A2L typedef: the DWARF type name, qualified with the scope of the type
    /// (namespace.Type, Class.Type) if the name is used by different types in different scopes. None for types without a name.
    pub(crate) fn get_type_name<'a>(&'a self, type_info: &'a TypeInfo) -> Option<&'a str> {
        let type_name = type_info.name.as_deref()?;
        Some(self.qualified_type_names.get(&type_info.dbginfo_offset).map_or(type_name, String::as_str))
    }

    // Get the address of the XCP event descriptor memory section
    pub(crate) fn get_event_section_addr(&self) -> u64 {
        // Find section 'xcp_evts'
        if let Some((start, end)) = self.sections.get("xcp_evts") {
            log::info!("Found XCP event descriptor memory section at address = 0x{:08X}, size = {} bytes", start, end - start);
            return *start;
        }

        // Some linker scripts merge the xcp_evts input section into another output
        // section. In that case, use the boundary symbols generated by the linker.
        if let (Some(start), Some(stop)) = (self.symbol_addresses.get("__start_xcp_evts"), self.symbol_addresses.get("__stop_xcp_evts")) {
            if start < stop {
                log::info!(
                    "Found XCP event descriptors using linker symbols at address = 0x{:08X}, size = {} bytes",
                    start,
                    stop - start
                );
                return *start;
            }
            log::warn!("Invalid XCP event descriptor linker symbol range: start = 0x{:08X}, stop = 0x{:08X}", start, stop);
        }

        log::warn!("XCP event descriptor memory section (xcp_evts) and linker boundary symbols not found");
        0
    }

    /// print the debug statistics
    pub(crate) fn print_debug_stats(&self) {
        println!("\n====================================================================================================");
        println!("DebugData information summary:");
        println!("  Compilation units: {} units", self.unit_names.len());
        let mut compilers: Vec<&str> = self.producers.iter().flatten().map(String::as_str).collect();
        compilers.sort_unstable();
        compilers.dedup();
        for compiler in compilers {
            println!("  Compiler: {}", compiler);
        }
        println!("  Sections: {}", self.sections.len());
        print!("  Endianness: ");
        if self.is_little_endian {
            println!("Little Endian");
        } else {
            println!("Big Endian");
        }
        let mut variable_count = 0;
        for (name, var_infos) in &self.variables {
            variable_count += var_infos.len();
        }
        println!("  Variables {} with {} unique names", variable_count, self.variables.len());
        println!("  Demangled names: {} entries", self.demangled_names.len());
        println!("  Type names: {} named types", self.typenames.len());
        println!("  Types: {} total types", self.types.len());
        println!("  EPK string: `{}` at address 0x{:08X}", self.epk_string.as_deref().unwrap_or("<not found>"), self.epk_addr);
        if let Some((addr, data)) = &self.xcp_meta_data {
            println!("  XCP metadata section (xcp_meta) found at address 0x{:08X}, {} bytes", addr, data.len());
        } else {
            println!("  XCP metadata section (xcp_meta) not found");
        }
    }

    // Print debuf info, called if verbose >0
    // level 0 .. 5 stats, variables, variable types, demangled names, type names, types
    // level >= 2 print variables
    // level >= 2 print variables details
    // level >= 3 print demangled names
    // level >= 4 print type names
    // level >= 5 print types
    pub(crate) fn print_debug_info(&self, level: usize, unit_idx_limit: (usize, usize)) {
        //
        self.print_debug_stats();

        //Print all compilation units
        println!("\n====================================================================================================");
        println!("Compilation units in debug_data.unit_names:");
        for (idx, unit_name) in self.unit_names.iter().enumerate() {
            let unit_name = self.make_simple_unit_name(idx);
            if unit_name.is_none() {
                println!("  Unit {}: <unnamed>", idx);
            } else {
                println!("  Unit {}: {}", idx, unit_name.as_ref().unwrap());
            }
        }

        // Print sections sorted by address
        println!("\n====================================================================================================");
        println!("DWARF sections by address:");
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
            println!("DWARF type names:");
            for (type_name, type_refs) in &self.typenames {
                println!("Type name '{}': {} references", type_name, type_refs.len());
                for type_ref in type_refs {
                    if let Some(type_info) = self.types.get(type_ref) {
                        let qualified_name = self.qualified_type_names.get(type_ref).map(String::as_str).unwrap_or("");
                        println!(
                            "  -> type_ref={}, size={} bytes, unit={}, qualified name='{}'",
                            type_ref,
                            type_info.get_size(),
                            type_info.unit_idx,
                            qualified_name
                        );
                    }
                }
            }

            if level >= 5 {
                // Print types
                println!("\n====================================================================================================");
                println!("DWARF types:");
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
                println!("\nDemangled Names:");
                for (mangled_name, demangled_name) in &self.demangled_names {
                    println!("  '{}' -> '{}'", mangled_name, demangled_name);
                }
            }
        }

        // Print A2L Creator variables
        println!("\n====================================================================================================");
        println!("A2L Creator variables:");
        for (var_name, var_info) in &self.variables {
            if is_a2l_variable(var_name) {
                let var = &var_info[0];
                let unit_name = if let Some(name) = self.make_simple_unit_name(var.unit_idx) {
                    name
                } else {
                    "<unnamed>".to_string()
                };
                let function_name = if let Some(name) = &var.function { name } else { "<global>" };
                let name_space = if var.namespaces.len() > 0 { var.namespaces.join("::") } else { "".to_string() };
                println!(
                    "{}':  {}:'{}' {}: addr={}:0x{:08X}",
                    var_name, unit_name, function_name, name_space, var.address.0, var.address.1
                );
            }
        }
        println!("");

        // Print all variables
        if level >= 2 {
            println!("\n====================================================================================================");
            println!("Variables:");
            println!("  (Skipping system variables '__<name>' and global XCP variables 'gXcp..' and 'gA2l..')");

            for (var_name, var_info) in &self.variables {
                // Count all variable in unit_idx
                let count = var_info.iter().filter(|v| v.unit_idx >= unit_idx_limit.0 && v.unit_idx <= unit_idx_limit.1).count();

                // Skip standard library variables and system/compiler internals (__<name>)s
                // Skip global XCP variables (gXCP.. and gA2L..)
                if level < 5 && var_name.starts_with("__") || var_name.starts_with("gXcp") || var_name.starts_with("gA2l") {
                    continue;
                }

                // print only variables from compilation unit
                if count == 1 && (var_info[0].unit_idx < unit_idx_limit.0 || var_info[0].unit_idx > unit_idx_limit.1) {
                    continue;
                }

                // Iterate over all variable infos for this variable name in unit_idx
                if level >= 2 {
                    println!("{} {}: ", var_name, count);
                } else if level >= 3 {
                    if count > 1 {
                        println!("{} {}: ", var_name, count);
                    }
                    for var in var_info {
                        // print only variables from compilation unit 0..=unit_idx
                        if var.unit_idx < unit_idx_limit.0 || var.unit_idx > unit_idx_limit.1 {
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
    }
}

// TypeInfo holds information about a variable's type
impl TypeInfo {
    // Size of the type in bytes. The size of a bitfield is the size of its containing integer type
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
            | DbgDataType::Union { size, .. }
            | DbgDataType::Enum { size, .. }
            | DbgDataType::Array { size, .. }
            | DbgDataType::FuncPtr(size)
            | DbgDataType::TypeRef(_, size) => *size,
        }
    }
}

impl Display for TypeInfo {
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
            DbgDataType::Struct { members, is_class, .. } => {
                let kind = if *is_class { "Class" } else { "Struct" };
                if let Some(name) = &self.name {
                    write!(f, "{kind} {name}({} members)", members.len())
                } else {
                    write!(f, "{kind} <anonymous>({} members)", members.len())
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
