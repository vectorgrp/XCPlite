//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module dwarf
// Implements DebugDataReader, UnitList and functions to read DWARF debug information from ELF files
// Read ELF files and extract debug information
// Taken from Github repository a2ltool by DanielT

/*
DWARF and gimli in a nutshell, as far as this module needs it

DWARF stores the debug information as a tree of entries, each entry has a tag (what it is) and attributes (its properties):

    DW_TAG_compile_unit  name="xcp_demo.c"                      one per compiled source file
      DW_TAG_base_type   name="unsigned short" byte_size=2
      DW_TAG_variable    name="global_counter" type=<ref> location=<expression>
      DW_TAG_subprogram  name="fastTask" low_pc=0x4200.. high_pc=..
        DW_TAG_variable  name="counter" type=<ref> location=<expression>          a local variable
        DW_TAG_variable  name="static_counter" type=<ref> location=<expression>   a static local
        DW_TAG_lexical_block ...                                                   a nested { } block
      DW_TAG_namespace   name="motor_control"
        DW_TAG_variable ...
      DW_TAG_structure_type name="parameters" byte_size=..
        DW_TAG_member    name="counter_max" type=<ref> data_member_location=4

The tree lives in the .debug_info section. Every entry is identified by its byte offset in that section (a "debug info
offset"), and references between entries (the type of a variable, the origin of an inlined function) are such offsets,
either relative to the start of the section (DW_FORM_ref_addr) or relative to the start of the unit (DW_FORM_ref4 and
friends). This module uses the section relative offset everywhere as the identity of an entry, in particular of a type.

gimli is the Rust DWARF parser. The names which appear in this module:
    Dwarf                       all .debug_* sections of the file, loaded once (load_dwarf_sections)
    UnitHeader                  the header of one compilation unit: version, address size, format, offset in .debug_info.
                                Cheap, Copy. Enough to iterate and read the entries of the unit
    Unit                        a fully resolved unit (UnitHeader + abbreviations + the DWARF 5 base offsets for indexed
                                strings and addresses). Constructed with dwarf.unit(header) when an indexed form has to be
                                resolved, it is not kept because construction is not free
    Abbreviations               the schema of the entries of a unit (.debug_abbrev), needed to decode the entries
    DebuggingInformationEntry   one entry (DIE): tag, attributes, offset. entry.attr_value(DW_AT_xxx) reads an attribute
    AttributeValue              the value of an attribute, an enum over the DWARF "forms" (how the value is stored):
                                Addr, Udata, Data1..Data8, String, DebugStrRef, UnitRef, DebugInfoRef, Exprloc, ... The
                                same attribute may use different forms depending on the compiler, see attributes.rs
    EntriesCursor / next_dfs    depth first traversal of the entries of a unit, entry.depth() gives the nesting level
    EntriesTree                 the subtree below one entry, used to read the children of a type (members, enumerators)
    Expression / Evaluation     a DWARF location expression (a small stack machine program) and its evaluator, see
                                evaluate_exprloc in attributes.rs

The ELF container is read with the `object` crate: sections (name, address, size, data) and symbols (name, address,
kind, binding). The DWARF information is just a set of sections of the ELF file, handed to gimli as byte slices.

DWARF versions: GCC and clang emit DWARF 5 by default today, older toolchains DWARF 4 or 2. The differences which matter
here are the forms: DWARF 5 may store strings and addresses as indices into per unit tables (.debug_str_offsets,
.debug_addr) instead of directly, and location lists moved to .debug_loclists. Both are handled in attributes.rs
*/

use indexmap::IndexMap;
use std::ffi::OsStr;
use std::ops::Index;
use std::{
    collections::{HashMap, HashSet},
    fs::File,
};

type SliceType<'a> = EndianSlice<'a, RunTimeEndian>;

use object::read::{ObjectSection, ObjectSymbol};
use object::{Endianness, Object};

use gimli::{Abbreviations, DebuggingInformationEntry, Dwarf, UnitHeader};
use gimli::{EndianSlice, RunTimeEndian};

use crate::elf_reader::debuginfo::{DbgDataType, DebugData, FrameBase, TypeInfo, VarInfo};

mod attributes;
pub(super) use attributes::get_low_pc_attribute;
use attributes::{
    get_abstract_origin_attribute, get_linkage_name_attribute, get_location_attribute, get_name_attribute, get_producer_attribute, get_specification_attribute,
    get_typeref_attribute,
};

mod typereader;

// All compilation units of the file with their abbreviations, in the order of .debug_info. The index into this list is
// the unit index (unit_idx) used throughout the debug data, and the .debug_info offset of any entry can be mapped back to
// its unit with get_unit
pub(crate) struct UnitList<'a> {
    list: Vec<(UnitHeader<SliceType<'a>>, gimli::Abbreviations)>,
}

// The parser state while the debug information is read. Created in load_elf_dwarf, consumed by collect_debug_data,
// which moves the results into a DebugData. The lifetime is the memory mapped ELF file, the gimli objects borrow from it
struct DebugDataReader<'elffile> {
    dwarf: Dwarf<EndianSlice<'elffile, RunTimeEndian>>, // the .debug_* sections, the entry point to everything gimli reads
    verbose: usize,
    units: UnitList<'elffile>,                              // the compilation units seen so far, filled while load_variables iterates
    unit_names: Vec<Option<String>>,                        // DW_AT_name of each unit, by unit index
    producers: Vec<Option<String>>,                         // DW_AT_producer (compiler, version and options) of each unit, by unit index
    endian: Endianness,                                     // byte order of the target, needed for bitfield offsets
    sections: HashMap<String, (u64, u64)>,                  // ELF section name -> (start, end)
    architecture: object::Architecture,                     // target architecture, for the frame pointer register (FrameBase)
    epk_string: Option<String>,                             // content of the xcp_epk section, the EPK version string of the application
    epk_addr: u64,                                          // address of the xcp_epk section, 0 if there is none
    symbol_addresses: HashMap<String, u64>,                 // ELF symbol table: name -> address, for variables without a DWARF location
    local_static_symbols: HashMap<String, Vec<(u64, u64)>>, // the symbols of the static variables in functions: variable name -> [(address, size)], see resolve_local_static_addresses
    global_symbol_names: HashSet<String>,                   // names of the symbols with global (or weak) binding
    function_symbol_names: HashMap<u64, String>,            // address -> mangled name of a C++ function symbol
    xcp_meta_data: Option<(u64, Vec<u8>)>,                  // (section_base_addr, raw_bytes)
    is_little_endian: bool,
    scope_parent: HashMap<usize, usize>, // .debug_info offset of a named type or scope -> offset of its enclosing scope (namespace, struct, class, union or function)
}

// Create DebugData
// Load and validate ELF/DWARF input, then collect and return parsed DebugData.
// This function constructs a temporary DebugDataReader that owns parser state
// (units, transient names, symbol table cache) and finalizes it into DebugData.
pub(crate) fn load_elf_dwarf(filename: &OsStr, verbose: usize, unit_idx_limit: (usize, usize)) -> Result<DebugData, String> {
    log::debug!("load_elf_dwarf: {}", filename.to_string_lossy());

    // open the file and mmap its content
    let filedata = load_filedata(filename)?;

    // load the elf file using the object crate
    let elffile = load_elf_file(&filename.to_string_lossy(), &filedata, verbose)?;

    // print symbol table
    if verbose >= 3 {
        println!("===============================================================");
        println!("\nSymbol table:");
        for symbol in elffile.symbols() {
            let Ok(name) = symbol.name() else {
                continue;
            };
            if name.is_empty() {
                continue;
            }
            println!("  `{:?}`: addr={:x}, {:?}", name, symbol.address(), symbol);
        }
        println!("");
    }

    // verify that the elf file contains DWARF debug info
    if !elffile.sections().any(|section| section.name() == Ok(".debug_info")) {
        log::error!("DWARF .debug_info section not found");
        return Err(format!(
            "Error: {} does not contain DWARF2+ debug info. The section .debug_info is missing.",
            filename.to_string_lossy()
        ));
    }

    // load the DWARF sections from the elf file
    let dwarf = load_dwarf_sections(&elffile)?;

    // verify that the dwarf data is valid
    if !verify_dwarf_compile_units(&dwarf) {
        return Err(format!(
            "Error: {} does not contain DWARF2+ debug info - zero compile units contain debug info.",
            filename.to_string_lossy()
        ));
    }

    // get the elf sections for DebugDataReader
    let sections = get_elf_sections(&elffile);

    // read the EPK string and address from the xcp_epk ELF section
    let epk_section = elffile.section_by_name("xcp_epk");
    let epk_addr: u64 = epk_section.as_ref().map_or(0, |s| s.address());
    let epk_string: Option<String> = epk_section
        .and_then(|s| s.data().ok())
        .and_then(|data| std::ffi::CStr::from_bytes_until_nul(data).ok())
        .map(|cs| cs.to_string_lossy().into_owned());
    if let Some(ref epk) = epk_string {
        log::debug!("EPK string read from xcp_epk section: '{}' at address 0x{:08X}", epk, epk_addr);
    }

    // read the xcp_meta section raw bytes for metadata (XCP_UNIT / XCP_LIMITS annotations)
    let xcp_meta_section = elffile.section_by_name("xcp_meta");
    let xcp_meta_data: Option<(u64, Vec<u8>)> = xcp_meta_section.and_then(|s| {
        let addr = s.address();
        s.data().ok().map(|data| (addr, data.to_vec()))
    });
    if let Some((addr, ref data)) = xcp_meta_data {
        log::debug!("XCP metadata section (xcp_meta) found at address 0x{:08X}, {} bytes", addr, data.len());
    } else {
        log::debug!("XCP metadata section (xcp_meta) not found in ELF file");
    }
    let is_little_endian = elffile.endianness() == Endianness::Little;

    // create the debug data reader
    log::debug!("Creating debug data reader");
    let dbg_reader = DebugDataReader {
        dwarf,
        verbose,
        units: UnitList::new(),
        unit_names: Vec::new(),
        producers: Vec::new(),
        endian: elffile.endianness(),
        sections,
        architecture: elffile.architecture(),
        epk_string,
        epk_addr,
        symbol_addresses: get_symbol_addresses(&elffile),
        local_static_symbols: get_local_static_symbols(&elffile),
        global_symbol_names: get_global_symbol_names(&elffile),
        function_symbol_names: get_function_symbol_names(&elffile),
        xcp_meta_data,
        is_little_endian,
        scope_parent: HashMap::new(),
    };
    log::debug!("Reading debug info entries");
    Ok(dbg_reader.collect_debug_data(unit_idx_limit))
}

// open a file and mmap its content
fn load_filedata(filename: &OsStr) -> Result<memmap2::Mmap, String> {
    let file = match File::open(filename) {
        Ok(file) => file,
        Err(error) => {
            return Err(format!("Error: could not open file {}: {error}", filename.to_string_lossy()));
        }
    };

    match unsafe { memmap2::Mmap::map(&file) } {
        Ok(mmap) => Ok(mmap),
        Err(err) => Err(format!("Error: Failed to map file '{}': {err}", filename.to_string_lossy())),
    }
}

// read the headers and sections of an elf/object file
fn load_elf_file<'data>(filename: &str, filedata: &'data [u8], verbose: usize) -> Result<object::read::File<'data>, String> {
    log::debug!("load_elf_file: {}", filename);

    // Reject Mach-O files with a clear message, macOS is not supported
    // Executables built on macOS contain no DWARF debug information, the macOS linker leaves it in the object files and in the separate .dSYM bundle,
    // and the XCPlite instrumentation markers are read from ELF sections
    if let Ok(kind) = object::FileKind::parse(filedata)
        && matches!(
            kind,
            object::FileKind::MachO32 | object::FileKind::MachO64 | object::FileKind::MachOFat32 | object::FileKind::MachOFat64
        )
    {
        return Err(format!(
            "Error: '{filename}' is a Mach-O (macOS) binary, macOS is not supported. Executables built on macOS contain no DWARF debug information and the A2L generator reads ELF files only. Build the application on Linux or for an embedded ELF target and use that ELF file"
        ));
    }

    match object::File::parse(filedata) {
        Ok(object_file) => {
            if verbose >= 1 {
                println!("\n====================================================================================================");
                println!("Parsed ELF object file: {}", filename);
                println!("File format: {:?}", object_file.format());
                println!("Architecture: {:?}", object_file.architecture());
                println!("Endianness: {:?}", object_file.endianness());
                println!("");
                println!("\nSections:");
                for section in object_file.sections() {
                    let kind = section.kind();
                    println!(
                        "  Name: {:<20} Addr: 0x{:08x} Size: {} bytes Kind: {:?} ",
                        section.name().unwrap_or("<unknown>"),
                        section.address(),
                        section.size(),
                        kind
                    );
                }
                println!("");
            }

            Ok(object_file)
        }
        Err(err) => Err(format!("Error: Failed to parse file '{filename}': {err}")),
    }
}

// Address ranges of the ELF sections which are loaded to memory: name -> (start, end). Sections without an address
// (the .debug_* sections, .symtab, ...) and empty sections are left out
fn get_elf_sections(elffile: &object::read::File) -> HashMap<String, (u64, u64)> {
    log::debug!("get_elf_sections: Creating ELF sections map for debug data (only size!=0 and addr!=0)");
    let mut map = HashMap::new();
    for section in elffile.sections() {
        let addr = section.address();
        let size = section.size();
        if addr != 0
            && size != 0
            && let Ok(name) = section.name()
        {
            map.insert(name.to_string(), (addr, addr + size));
            log::trace!("elf section: {} addr={addr:x}, size={size:x}", name);
        }
    }

    map
}

// The ELF symbol table (.symtab): symbol name -> address, for all symbols with a name and an address. Functions, global
// and static variables, linker generated symbols (__start_xcp_evts). The names of C++ symbols are mangled (_ZN...)
fn get_symbol_addresses(elffile: &object::read::File) -> HashMap<String, u64> {
    let mut map = HashMap::new();
    for symbol in elffile.symbols() {
        let Ok(name) = symbol.name() else {
            continue;
        };
        if name.is_empty() {
            continue;
        }
        let addr = symbol.address();
        if addr != 0 {
            map.insert(name.to_string(), addr);
        }
    }
    map
}

// The symbols of the static variables in functions, by the name of the variable: GCC names them <name>.<number> (the number
// disambiguates the variables of the same name in different functions, it is unrelated to anything in the DWARF), clang names
// them <function>.<name>, which the unique suffix search finds. Only symbols with local binding are collected, a global symbol
// belongs to a different variable. See resolve_local_static_addresses
fn get_local_static_symbols(elffile: &object::read::File) -> HashMap<String, Vec<(u64, u64)>> {
    let mut map: HashMap<String, Vec<(u64, u64)>> = HashMap::new();
    for symbol in elffile.symbols().filter(|symbol| !symbol.is_global() && symbol.address() != 0) {
        let Ok(name) = symbol.name() else {
            continue;
        };
        let Some((var_name, suffix)) = name.rsplit_once('.') else {
            continue;
        };
        if var_name.is_empty() || suffix.is_empty() || !suffix.bytes().all(|b| b.is_ascii_digit()) {
            continue;
        }
        map.entry(var_name.to_string()).or_default().push((symbol.address(), symbol.size()));
    }
    map
}

// Mangled names of the C++ function symbols by their address, to find the linkage name of a function without DW_AT_linkage_name
// (GCC emits none for a function with internal linkage, e.g. a static function in a C++ compilation unit)
fn get_function_symbol_names(elffile: &object::read::File) -> HashMap<u64, String> {
    elffile
        .symbols()
        .filter(|symbol| symbol.kind() == object::SymbolKind::Text && symbol.address() != 0)
        .filter_map(|symbol| symbol.name().ok().filter(|name| name.starts_with("_Z")).map(|name| (symbol.address(), name.to_string())))
        .collect()
}

// Names of the symbols with global (or weak) binding, all other symbols are local to their compilation unit (static variables and functions)
fn get_global_symbol_names(elffile: &object::read::File) -> HashSet<String> {
    elffile
        .symbols()
        .filter(|symbol| symbol.is_global())
        .filter_map(|symbol| symbol.name().ok().filter(|name| !name.is_empty()).map(str::to_string))
        .collect()
}

// load the DWARF debug info from the .debug_<xyz> sections
fn load_dwarf_sections<'data>(elffile: &object::read::File<'data>) -> Result<gimli::Dwarf<SliceType<'data>>, String> {
    log::debug!("load_dwarf_sections");
    // Dwarf::load takes two closures / functions and uses them to load all the required debug sections
    let loader = |section: gimli::SectionId| get_file_section_reader(elffile, section.name());
    gimli::Dwarf::load(loader)
}

// verify that the dwarf data is valid
fn verify_dwarf_compile_units(dwarf: &gimli::Dwarf<SliceType>) -> bool {
    let mut units_iter = dwarf.debug_info.units();
    let mut units_count = 0;
    while let Ok(Some(_)) = units_iter.next() {
        units_count += 1;
    }

    log::debug!("DWARF compile units: {}", units_count);
    units_count > 0
}

// get a section from the elf file.
// returns a slice referencing the section data if it exists, or an empty slice otherwise
fn get_file_section_reader<'data>(elffile: &object::read::File<'data>, section_name: &str) -> Result<SliceType<'data>, String> {
    if let Some(dbginfo) = elffile.section_by_name(section_name) {
        match dbginfo.data() {
            Ok(val) => Ok(EndianSlice::new(val, get_endian(elffile))),
            Err(e) => Err(e.to_string()),
        }
    } else {
        Ok(EndianSlice::new(&[], get_endian(elffile)))
    }
}

// get the endianity of the elf file
fn get_endian(elffile: &object::read::File) -> RunTimeEndian {
    if elffile.is_little_endian() { RunTimeEndian::Little } else { RunTimeEndian::Big }
}

impl DebugDataReader<'_> {
    // Get the address of a symbol by its exact name
    // local_only: only symbols with local binding (static variables) are considered
    fn symbol_address(&self, symbol_name: &str, local_only: bool) -> Option<u64> {
        if local_only && self.global_symbol_names.contains(symbol_name) {
            return None;
        }
        self.symbol_addresses.get(symbol_name).copied()
    }

    // Get the address of the only symbol whose (mangled) name ends with the variable name, e.g. _ZZ4mainE7counter for the static variable counter in main
    fn resolve_address_by_unique_suffix(&self, var_name: &str, local_only: bool) -> Option<u64> {
        // Very short names are too ambiguous in mangled symbols.
        if var_name.len() < 4 {
            return None;
        }

        let mut matches = self.symbol_addresses.iter().filter_map(|(symbol_name, addr)| {
            if *addr != 0 && symbol_name.ends_with(var_name) && !(local_only && self.global_symbol_names.contains(symbol_name)) {
                Some(*addr)
            } else {
                None
            }
        });

        let first = matches.next()?;
        if matches.next().is_none() { Some(first) } else { None }
    }

    // Resolve the address of a variable without location attribute from the symbol table: by linkage name, by name,
    // by the mangled name of a variable in a namespace or class scope, or by a unique name suffix
    // local_only: the variable is local to a function, only symbols with local binding (static variables) are considered,
    // a global symbol with the same name belongs to a different variable
    // function_linkage: the mangled name of the enclosing C++ function of a local variable, used for the mangled name of a static local
    // scopes: the namespaces and classes the variable is defined in (outermost first), used for the mangled name
    fn resolve_address_from_symbols(
        &self,
        entry: &DebuggingInformationEntry<SliceType, usize>,
        unit: &UnitHeader<SliceType>,
        var_name: &str,
        local_only: bool,
        function_linkage: Option<&str>,
        scopes: &[String],
    ) -> Option<u64> {
        if let Ok(linkage_name) = get_linkage_name_attribute(entry, &self.dwarf, unit)
            && let Some(addr) = self.symbol_address(&linkage_name, local_only)
        {
            return Some(addr);
        }
        // A symbol with exactly the name of a variable which is local to a function belongs to a different variable, a static at
        // file scope with the same name (both have local binding, so global_symbol_names does not tell them apart). The symbol of
        // a static variable in a function is <name>.<number> under GCC, it is resolved in resolve_local_static_addresses
        if !(local_only && self.local_static_symbols.contains_key(var_name))
            && let Some(addr) = self.symbol_address(var_name, local_only)
        {
            return Some(addr);
        }
        // GCC emits no linkage name for variables with internal linkage in a namespace (static const in a namespace, e.g. the XCP_COMMENT
        // metadata markers), and the mangled symbol name of a namespace scope variable (_ZN13motor_controlL5inputE) does not end with the variable name
        if !local_only && !scopes.is_empty() {
            for mangled in itanium_mangled_names(scopes, var_name) {
                if let Some(addr) = self.symbol_address(&mangled, local_only) {
                    return Some(addr);
                }
            }
        }
        // GCC emits no linkage name and no location for a static const local variable in a C++ function either (the XCP_COMMENT
        // metadata markers in a function), the symbol is found by the mangled name of a function local static (_ZZ3foovE7counter).
        // The unique suffix search below is ambiguous as soon as several functions define a static variable with the same name
        if local_only
            && let Some(mangled) = function_linkage.and_then(|linkage| itanium_local_static_name(linkage, var_name))
            && let Some(addr) = self.symbol_address(&mangled, local_only)
        {
            return Some(addr);
        }
        // The address of a static variable in a function which has a <name>.<number> symbol is resolved in
        // resolve_local_static_addresses, where the size of the variable is known. The suffix search below would find the
        // file scope static of the same name, whose symbol name ends with the variable name as well
        if local_only && self.local_static_symbols.contains_key(var_name) {
            return None;
        }
        self.resolve_address_by_unique_suffix(var_name, local_only)
    }

    // Resolve the addresses of the static variables in functions which have no DW_AT_location, from the symbols GCC names
    // <name>.<number> (see get_local_static_symbols). GCC emits no location for a static const variable in a function, which is
    // how the metadata markers (XCP_COMMENT in inc/xcplib.h) inside a function are declared, so without this they are all lost.
    // The number in the symbol name has no relation to the DWARF, several variables of the same name in different functions can
    // only be told apart by their size. This runs after the types are loaded, because the size of the variable is needed
    fn resolve_local_static_addresses(&self, variables: &mut IndexMap<String, Vec<VarInfo>>, types: &HashMap<usize, TypeInfo>) {
        for (var_name, var_infos) in variables.iter_mut() {
            let Some(symbols) = self.local_static_symbols.get(var_name) else {
                continue;
            };
            // Only variables local to a function and without an address, the ones with a location are already resolved
            let unresolved = var_infos.iter().filter(|v| v.address == (0, 0) && v.function.is_some()).count();
            if unresolved == 0 {
                continue;
            }
            for var_info in var_infos.iter_mut().filter(|v| v.address == (0, 0) && v.function.is_some()) {
                // The size of the variable identifies the symbol when several static variables of this name exist, and confirms
                // the symbol when there is only one. A symbol without size and a variable whose type is not known are accepted
                let size = types.get(&var_info.typeref).map(TypeInfo::get_size);
                let mut matching = symbols.iter().filter(|(_, symbol_size)| *symbol_size == 0 || size.is_none_or(|s| s == *symbol_size));
                let address = matching.next().filter(|_| matching.next().is_none()).map(|(addr, _)| *addr);
                match address {
                    Some(addr) => {
                        log::debug!(
                            "Static variable '{}' in function {:?} resolved to {:#x} from the symbol table",
                            var_name,
                            var_info.function,
                            addr
                        );
                        var_info.address = (0, addr);
                    }
                    None => log::warn!(
                        "Static variable '{}' in function {:?} has no address, its symbol can not be identified: several static \
                         variables of this name and size, or none of the matching size. A metadata marker for it is lost, \
                         use the scope prefixed marker name ('{}__{}')",
                        var_name,
                        var_info.function,
                        var_info.function.as_deref().unwrap_or(""),
                        var_name
                    ),
                }
            }
        }
    }

    // Traverse DWARF entries and finalize collected parser state into DebugData.
    // The order matters: the variables are loaded first, then only the types referenced by variables (loading all types of a
    // large ELF file would take far longer), then the scope qualified names of the types whose plain name is ambiguous
    fn collect_debug_data(mut self, unit_idx_limit: (usize, usize)) -> DebugData {
        let mut variables = self.load_variables(unit_idx_limit);
        let (types, typenames) = self.load_types(&variables);
        self.resolve_local_static_addresses(&mut variables, &types);
        let qualified_type_names = self.load_qualified_type_names(&types, &typenames);
        let varname_list: Vec<&String> = variables.keys().collect();
        let demangled_names = demangle_cpp_varnames(&varname_list);
        let unit_names = std::mem::take(&mut self.unit_names);
        let producers = std::mem::take(&mut self.producers);

        DebugData {
            variables,
            types,
            typenames,
            qualified_type_names,
            demangled_names,
            unit_names,
            producers,
            sections: self.sections,
            symbol_addresses: self.symbol_addresses,
            epk_string: self.epk_string,
            epk_addr: self.epk_addr,
            xcp_meta_data: self.xcp_meta_data,
            is_little_endian: self.is_little_endian,
        }
    }

    // Load all variables from the dwarf data: every DW_TAG_variable entry of every compilation unit whose index is inside
    // the inclusive range unit_idx_limit = (min, max), with its enclosing function and namespaces. The traversal is depth
    // first (next_dfs), the `context` stack mirrors the path from the unit root to the current entry (entry.depth() is the
    // nesting level), so the scopes a variable is nested in are the entries currently on the stack
    fn load_variables(&mut self, unit_idx_limit: (usize, usize)) -> IndexMap<String, Vec<VarInfo>> {
        let mut variables = IndexMap::<String, Vec<VarInfo>>::new();

        let mut iter = self.dwarf.debug_info.units();
        while let Ok(Some(unit)) = iter.next() {
            // get the abbreviations for the unit
            let Ok(abbreviations) = unit.abbreviations(&self.dwarf.debug_abbrev) else {
                let offset = unit.offset().to_debug_info_offset(&unit).unwrap_or(gimli::DebugInfoOffset(0)).0;
                log::warn!("Failed to get abbreviations for unit @{offset:x}");
                continue;
            };

            // store the unit for later reference
            self.units.add(unit, abbreviations);
            let unit_idx = self.units.list.len() - 1;
            // Unit indices increase monotonically, so once the upper limit is exceeded, no further unit can be in range: stop entirely
            if unit_idx > unit_idx_limit.1 {
                break;
            }
            let (unit, abbreviations) = &self.units[unit_idx];

            // The root of the tree inside of a unit is always a DW_TAG_compile_unit or DW_TAG_partial_unit.
            // The global variables are among the immediate children of the unit; static variables
            // in functions are declared inside of DW_TAG_subprogram[/DW_TAG_lexical_block]*.
            // We can easily find all of them by using depth-first traversal of the tree
            let mut entries_cursor = unit.entries(abbreviations);
            if let Ok(Some(entry)) = entries_cursor.next_dfs()
                && (entry.tag() == gimli::constants::DW_TAG_compile_unit || entry.tag() == gimli::constants::DW_TAG_partial_unit)
            {
                // @@@@ warn if unit name is missing
                let unit_name = match get_name_attribute(entry, &self.dwarf, unit) {
                    Ok(name) => {
                        log::trace!("unit name: {}", &name);
                        Some(name)
                    }
                    Err(e) => {
                        log::warn!("Failed to get unit name: {}", e);
                        None
                    }
                };
                // Always recorded (even below the lower limit) so unit_names/producers stay index-aligned with self.units.list
                self.unit_names.push(unit_name);
                self.producers.push(get_producer_attribute(entry, &self.dwarf, unit).ok());
            }

            // Units below the lower limit are still registered above to keep unit indices aligned, but their variables are
            // not collected
            if unit_idx < unit_idx_limit.0 {
                continue;
            }

            // traverse all entries in depth-first order
            // context holds the tag, the name (namespaces and functions only) and the .debug_info offset of the ancestors of the current entry
            let mut context: Vec<Scope> = Vec::new();
            while let Ok(Some(entry)) = entries_cursor.next_dfs() {
                let depth = entry.depth();
                debug_assert!(depth >= 1);
                context.truncate((depth - 1) as usize);
                let tag = entry.tag();
                let offset = entry.offset().to_debug_info_offset(unit).map_or(0, |o| o.0);
                // It's essential to only get those names that might actually be needed.
                // Getting all names unconditionally doubled the runtime of the program
                // as a result of countless useless string allocations and deallocations.
                let scope = match tag {
                    gimli::constants::DW_TAG_namespace => Scope {
                        tag,
                        name: get_name_attribute(entry, &self.dwarf, unit).ok(),
                        linkage_name: None,
                        offset,
                        inlined: false,
                        frame_base: FrameBase::Unknown,
                    },
                    gimli::constants::DW_TAG_subprogram | gimli::constants::DW_TAG_inlined_subroutine => {
                        // A function which the compiler inlined is described by an abstract instance (DW_AT_inline, GCC keeps the static
                        // variables there), an inlined copy at each call site (DW_TAG_inlined_subroutine) and, if the function is also
                        // called, an out of line copy (a DW_TAG_subprogram without a name, referring to the abstract instance).
                        // The stack frame of such a function is ambiguous, see get_varinfo_from_context
                        let inlined = tag == gimli::constants::DW_TAG_inlined_subroutine
                            || is_inlined_subprogram(entry)
                            || get_abstract_origin_attribute(entry, unit, abbreviations).is_some();
                        Scope {
                            tag,
                            name: self.get_subprogram_name(entry, unit, abbreviations),
                            linkage_name: self.get_subprogram_linkage_name(entry, unit, abbreviations),
                            offset,
                            inlined,
                            frame_base: get_frame_base(self.architecture, entry, unit.encoding()),
                        }
                    }
                    _ => Scope {
                        tag,
                        name: None,
                        linkage_name: None,
                        offset,
                        inlined: false,
                        frame_base: FrameBase::Unknown,
                    },
                };
                context.push(scope);
                debug_assert_eq!(depth as usize, context.len());

                // Remember the enclosing scope of named types, of nested scopes and of variables declared in a namespace or class,
                // to qualify the names of types which are defined in a namespace, class or function (motor_control::Input, Controller::Params)
                // and to find the scope of a variable definition which refers to its declaration (DW_AT_specification).
                // Only offsets are stored here, the scope names are resolved in load_qualified_type_names for the few types which need them.
                if (is_scope_tag(tag) || is_type_tag(tag) || tag == gimli::constants::DW_TAG_variable)
                    && let Some(parent) = context[..context.len() - 1].iter().rev().find(|s| is_scope_tag(s.tag))
                    && (tag != gimli::constants::DW_TAG_variable || parent.tag != gimli::constants::DW_TAG_subprogram)
                {
                    self.scope_parent.insert(offset, parent.offset);
                }

                if entry.tag() == gimli::constants::DW_TAG_variable {
                    // Get variable information
                    let (function, function_linkage, namespaces, inlined, frame_base) = get_varinfo_from_context(&context);
                    match self.get_variable(entry, unit, abbreviations, function.is_some(), function_linkage.as_deref(), &namespaces) {
                        Ok((name, typeref, address)) => {
                            let var_infos = variables.entry(name).or_default();
                            // A static variable of an inlined function may be described in the abstract instance and again in each copy
                            if inlined && address.0 == 0 && address.1 != 0 && var_infos.iter().any(|v| v.unit_idx == unit_idx && v.address == address && v.function == function) {
                                continue;
                            }
                            // GCC describes a namespace scope (or static member) variable with a declaration entry inside the namespace
                            // and a definition entry at compilation unit level (DW_AT_specification). Both resolve to the same address,
                            // the variable is kept once with the namespaces of the declaration
                            if function.is_none()
                                && address.0 == 0
                                && address.1 != 0
                                && let Some(existing) = var_infos.iter_mut().find(|v| v.unit_idx == unit_idx && v.address == address && v.function.is_none())
                            {
                                if existing.namespaces.is_empty() {
                                    existing.namespaces = namespaces;
                                }
                            } else {
                                var_infos.push(VarInfo {
                                    address, // may be 0 for local variables
                                    typeref,
                                    unit_idx,
                                    function,
                                    namespaces,
                                    inlined,
                                    frame_base,
                                });
                            }
                        }
                        Err(errmsg) => {
                            let offset = entry.offset().to_debug_info_offset(unit).unwrap_or(gimli::DebugInfoOffset(0)).0;
                            log::debug!("Could not load variable @{offset:x}: {errmsg}");
                        }
                    }
                }
            }
        }

        variables
    }

    // Determine the scope qualified names of the struct and class types whose name is used by different types in different scopes.
    // The DWARF name of a type is its unqualified name (DW_AT_name "Input" for motor_control::Input), the enclosing scopes
    // (namespaces, classes, functions) recorded during the traversal in load_variables tell such types apart.
    // The result maps a type reference to its qualified name ("motor_control.Input") and contains only the types which need
    // qualification: the name is used in more than one scope and the type has a scope. All other types keep their plain name.
    fn load_qualified_type_names(&self, types: &HashMap<usize, TypeInfo>, typenames: &HashMap<String, Vec<usize>>) -> HashMap<usize, String> {
        // Scopes of all struct/class types, outermost first. The type of a variable or member may be a const or volatile
        // qualified type, the name and the scope are the ones of the named type behind the qualifier
        let mut scopes: HashMap<usize, Vec<String>> = HashMap::new();
        for (offset, typeinfo) in types {
            if matches!(typeinfo.datatype, DbgDataType::Struct { .. })
                && let Some(named_offset) = self.get_named_type_offset(*offset)
            {
                scopes.insert(*offset, self.get_scope_path(named_offset));
            }
        }

        // Type names which are used by struct/class types in more than one scope
        let mut ambiguous: HashSet<&String> = HashSet::new();
        for (type_name, type_refs) in typenames {
            let mut distinct: Vec<&[String]> = Vec::new();
            for type_ref in type_refs {
                if let Some(scope) = scopes.get(type_ref)
                    && !distinct.contains(&scope.as_slice())
                {
                    distinct.push(scope.as_slice());
                }
            }
            if distinct.len() > 1 {
                log::debug!(
                    "Type name '{}' is used by {} different struct/class types, the typedef names are qualified with their scope",
                    type_name,
                    distinct.len()
                );
                ambiguous.insert(type_name);
            }
        }

        // Qualified names for the types with an ambiguous name and a scope
        let mut qualified_type_names = HashMap::new();
        for (offset, scope) in &scopes {
            if scope.is_empty() {
                continue;
            }
            if let Some(type_name) = types.get(offset).and_then(|t| t.name.as_ref())
                && ambiguous.contains(type_name)
            {
                let qualified = format!("{}.{}", scope.join("."), type_name);
                log::trace!("type {} @{offset:x} is qualified as {}", type_name, qualified);
                qualified_type_names.insert(*offset, qualified);
            }
        }
        qualified_type_names
    }

    // Read tag, name and type reference of the debug info entry at the given .debug_info offset
    fn read_entry(&self, dbginfo_offset: usize) -> Option<(gimli::DwTag, Option<String>, Option<usize>)> {
        let unit_idx = self.units.get_unit(dbginfo_offset)?;
        let (unit, abbrev) = &self.units[unit_idx];
        let unit_offset = gimli::DebugInfoOffset(dbginfo_offset).to_unit_offset(unit)?;
        let mut entries_tree = unit.entries_tree(abbrev, Some(unit_offset)).ok()?;
        let node = entries_tree.root().ok()?;
        let entry = node.entry();
        let name = get_name_attribute(entry, &self.dwarf, unit).ok();
        let typeref = get_typeref_attribute(entry, unit).ok();
        Some((entry.tag(), name, typeref))
    }

    // Follow const, volatile and similar qualifiers to the named type definition (struct, class, union, enum or typedef)
    fn get_named_type_offset(&self, dbginfo_offset: usize) -> Option<usize> {
        let mut offset = dbginfo_offset;
        for _ in 0..16 {
            // limited number of qualifier levels, to be safe with malformed debug info
            let (tag, _, typeref) = self.read_entry(offset)?;
            if is_type_tag(tag) {
                return Some(offset);
            }
            match tag {
                gimli::constants::DW_TAG_const_type
                | gimli::constants::DW_TAG_volatile_type
                | gimli::constants::DW_TAG_packed_type
                | gimli::constants::DW_TAG_restrict_type
                | gimli::constants::DW_TAG_immutable_type
                | gimli::constants::DW_TAG_atomic_type => offset = typeref?,
                _ => return None,
            }
        }
        None
    }

    // Get the names of the scopes enclosing a debug info entry, outermost scope first
    // Anonymous scopes (unnamed namespaces, anonymous structs) are skipped
    fn get_scope_path(&self, dbginfo_offset: usize) -> Vec<String> {
        let mut path = Vec::new();
        let mut offset = dbginfo_offset;
        // a parent always precedes its children in .debug_info, so the chain of parents terminates
        while let Some(parent) = self.scope_parent.get(&offset) {
            if let Some((_, Some(name), _)) = self.read_entry(*parent) {
                path.push(name);
            }
            offset = *parent;
        }
        path.reverse();
        path
    }

    // Linkage (mangled) name of a C++ function (DW_TAG_subprogram or DW_TAG_inlined_subroutine entry), None for C functions.
    // Found like the name, on the entry itself, its abstract instance or its declaration. A function with internal linkage
    // (static function) has no DW_AT_linkage_name, its mangled name is the function symbol at its start address (DW_AT_low_pc)
    fn get_subprogram_linkage_name<'a>(
        &self,
        entry: &DebuggingInformationEntry<SliceType<'a>, usize>,
        unit: &UnitHeader<SliceType<'a>>,
        abbrev: &gimli::Abbreviations,
    ) -> Option<String> {
        if let Ok(name) = get_linkage_name_attribute(entry, &self.dwarf, unit) {
            return Some(name);
        }
        let name = get_name_attribute(entry, &self.dwarf, unit).unwrap_or_default();
        if let Some(low_pc) = get_low_pc_attribute(entry, &name, |index| self.dwarf.unit(*unit).and_then(|unit| self.dwarf.address(&unit, index)))
            && let Some(name) = self.function_symbol_names.get(&low_pc)
        {
            return Some(name.clone());
        }
        let origin = get_abstract_origin_attribute(entry, unit, abbrev).or_else(|| get_specification_attribute(entry, unit, abbrev))?;
        get_linkage_name_attribute(&origin, &self.dwarf, unit).ok().or_else(|| {
            let declaration = get_specification_attribute(&origin, unit, abbrev)?;
            get_linkage_name_attribute(&declaration, &self.dwarf, unit).ok()
        })
    }

    // Name of a function (DW_TAG_subprogram or DW_TAG_inlined_subroutine entry).
    // The copies of an inlined function refer to the abstract instance (DW_AT_abstract_origin) and the definition of a declared
    // function, e.g. a C++ member function, refers to its declaration (DW_AT_specification), the name is found there
    fn get_subprogram_name<'a>(&self, entry: &DebuggingInformationEntry<SliceType<'a>, usize>, unit: &UnitHeader<SliceType<'a>>, abbrev: &gimli::Abbreviations) -> Option<String> {
        if let Ok(name) = get_name_attribute(entry, &self.dwarf, unit) {
            return Some(name);
        }
        let origin = get_abstract_origin_attribute(entry, unit, abbrev).or_else(|| get_specification_attribute(entry, unit, abbrev))?;
        get_name_attribute(&origin, &self.dwarf, unit).ok().or_else(|| {
            // the abstract instance of an inlined member function refers to the declaration in the class
            let declaration = get_specification_attribute(&origin, unit, abbrev)?;
            get_name_attribute(&declaration, &self.dwarf, unit).ok()
        })
    }

    // Read one DW_TAG_variable entry: returns (name, type reference, (address extension, address)), see VarInfo for the address encoding.
    // The three cases are the ways DWARF splits the description of a variable over several entries:
    //  - DW_AT_specification: the entry is the definition of a variable which was declared elsewhere (a C++ namespace or static
    //    class member: GCC puts the declaration into the namespace/class and the definition with the location at unit level).
    //    Name and type are found in the declaration, the location in this entry
    //  - DW_AT_abstract_origin: the entry is a copy of a variable of an inlined function, the original ("abstract instance")
    //    holds name and type, the copy holds the location valid for this inlined copy
    //  - neither: the usual case, everything is in this entry
    // The address is 0 if the entry has no location (a declaration, a variable optimized away by the compiler) or if the location
    // is not a plain address, see evaluate_exprloc. A missing address is resolved from the symbol table if possible
    // (declarations of global variables, static variables without location)
    // local: the variable is local to a function, only symbols with local binding are considered to resolve the address
    // function_linkage: the mangled name of the enclosing C++ function, to resolve the symbol of a static local variable
    // namespaces: the namespaces the entry is nested in (outermost first)
    fn get_variable<'a>(
        &self,
        entry: &DebuggingInformationEntry<SliceType<'a>, usize>,
        unit: &UnitHeader<SliceType<'a>>,
        abbrev: &gimli::Abbreviations,
        local: bool,
        function_linkage: Option<&str>,
        namespaces: &[String],
    ) -> Result<(String, usize, (u8, u64)), String> {
        // if debugging information entry A has a DW_AT_specification or DW_AT_abstract_origin attribute
        // pointing to another debugging information entry B, any attributes of B are considered to be part of A.
        if let Some(specification_entry) = get_specification_attribute(entry, unit, abbrev) {
            // the entry refers to a specification, which contains the name and type reference
            let name = get_name_attribute(&specification_entry, &self.dwarf, unit)?;
            log::debug!("get_variable '{}':", name);
            let typeref = get_typeref_attribute(&specification_entry, unit)?;
            let mut address = get_location_attribute(self, entry, unit.encoding(), &self.units.list.len() - 1, &name).unwrap_or((0u8, 0u64));
            // The definition entry is at compilation unit level, the scope of the variable is the one of its declaration (specification)
            let specification_scopes = specification_entry
                .offset()
                .to_debug_info_offset(unit)
                .map(|o| self.get_scope_path(o.0))
                .unwrap_or_default();
            if address == (0u8, 0u64)
                && let Some(sym_addr) = self
                    .resolve_address_from_symbols(entry, unit, &name, local, function_linkage, namespaces)
                    .or_else(|| self.resolve_address_from_symbols(&specification_entry, unit, &name, local, function_linkage, &specification_scopes))
            {
                address = (0u8, sym_addr);
            }
            if address.0 >= 0x80 {
                log::debug!("  {} is a register, tls or has unknown location", name);
            } else if address.1 == 0 {
                log::debug!("  {} has no address", name);
            }
            Ok((name, typeref, address))
        } else if let Some(abstract_origin_entry) = get_abstract_origin_attribute(entry, unit, abbrev) {
            // the entry refers to an abstract origin, which should also be considered when getting the name and type ref
            let name = get_name_attribute(entry, &self.dwarf, unit).or_else(|_| get_name_attribute(&abstract_origin_entry, &self.dwarf, unit))?;
            log::debug!("'{}':", name);
            let typeref = get_typeref_attribute(entry, unit).or_else(|_| get_typeref_attribute(&abstract_origin_entry, unit))?;
            let mut address = get_location_attribute(self, entry, unit.encoding(), &self.units.list.len() - 1, &name).unwrap_or((0u8, 0u64));
            if address == (0u8, 0u64)
                && let Some(sym_addr) = self
                    .resolve_address_from_symbols(entry, unit, &name, local, function_linkage, namespaces)
                    .or_else(|| self.resolve_address_from_symbols(&abstract_origin_entry, unit, &name, local, function_linkage, namespaces))
            {
                address = (0u8, sym_addr);
            }
            if address.0 >= 0x80 {
                log::debug!("  {} is a register, tls or has unknown location", name);
            } else if address.1 == 0 {
                log::debug!("  {} has no address", name);
            }
            Ok((name, typeref, address))
        } else {
            // usual case: there is no specification or abstract origin and all info is part of this entry
            let name = get_name_attribute(entry, &self.dwarf, unit)?;
            log::debug!("'{}':", name);
            let typeref = get_typeref_attribute(entry, unit)?;
            let mut address = get_location_attribute(self, entry, unit.encoding(), &self.units.list.len() - 1, &name).unwrap_or((0u8, 0u64));
            if address == (0u8, 0u64)
                && let Some(sym_addr) = self.resolve_address_from_symbols(entry, unit, &name, local, function_linkage, namespaces)
            {
                address = (0u8, sym_addr);
            }
            if address.0 >= 0x80 {
                log::debug!("  {} is a register, tls or has unknown location", name);
            } else if address.1 == 0 {
                log::debug!(". {} has no address", name);
            }
            Ok((name, typeref, address))
        }
    }
}

// Tags of debug info entries which are a named scope for the types and variables nested inside of them
fn is_scope_tag(tag: gimli::DwTag) -> bool {
    matches!(
        tag,
        gimli::constants::DW_TAG_namespace
            | gimli::constants::DW_TAG_structure_type
            | gimli::constants::DW_TAG_class_type
            | gimli::constants::DW_TAG_union_type
            | gimli::constants::DW_TAG_subprogram
    )
}

// Tags of debug info entries which define a named type
fn is_type_tag(tag: gimli::DwTag) -> bool {
    matches!(
        tag,
        gimli::constants::DW_TAG_structure_type
            | gimli::constants::DW_TAG_class_type
            | gimli::constants::DW_TAG_union_type
            | gimli::constants::DW_TAG_enumeration_type
            | gimli::constants::DW_TAG_typedef
    )
}

// Mangled names (Itanium C++ ABI) of a variable in nested namespaces or classes, with external and with internal linkage:
// motor_control::input -> _ZN13motor_control5inputE, static motor_control::input -> _ZN13motor_controlL5inputE
fn itanium_mangled_names(scopes: &[String], name: &str) -> [String; 2] {
    let mut prefix = String::from("_ZN");
    for scope in scopes {
        prefix.push_str(&format!("{}{}", scope.len(), scope));
    }
    [format!("{prefix}{}{name}E", name.len()), format!("{prefix}L{}{name}E", name.len())]
}

// Itanium mangled name of a static local variable of a C++ function: _ZZ<function encoding>E<len><name>,
// e.g. _ZZ3foovE7counter for counter in foo(), _ZZL8fastTaskPvE7counter for counter in the static function fastTask(void*).
// function_linkage is the mangled name of the function (_Z3foov), None is returned if it is not an Itanium mangled name
fn itanium_local_static_name(function_linkage: &str, name: &str) -> Option<String> {
    let encoding = function_linkage.strip_prefix("_Z")?;
    Some(format!("_ZZ{encoding}E{}{name}", name.len()))
}

// An ancestor of the current entry in the depth-first traversal of load_variables
struct Scope {
    tag: gimli::DwTag,
    name: Option<String>,         // namespaces and functions only
    linkage_name: Option<String>, // mangled name of a C++ function, used to resolve the symbols of its static variables
    offset: usize,                // .debug_info offset
    inlined: bool,                // a function which the compiler inlined: its abstract instance, an inlined copy or the out of line copy
    frame_base: FrameBase,        // functions only: what the locations of the local variables are relative to
}

// Frame base of a function (DW_AT_frame_base), see FrameBase
fn get_frame_base(architecture: object::Architecture, entry: &DebuggingInformationEntry<SliceType, usize>, encoding: gimli::Encoding) -> FrameBase {
    match entry.attr_value(gimli::constants::DW_AT_frame_base) {
        Some(gimli::AttributeValue::Exprloc(expression)) => classify_frame_base(architecture, expression, encoding),
        _ => FrameBase::Unknown,
    }
}

// Classify a frame base expression: the CFA, the frame pointer register of the architecture, another register or unknown
fn classify_frame_base(architecture: object::Architecture, expression: gimli::Expression<SliceType>, encoding: gimli::Encoding) -> FrameBase {
    let mut operations = expression.operations(encoding);
    let (Ok(Some(operation)), Ok(None)) = (operations.next(), operations.next()) else {
        return FrameBase::Unknown;
    };
    let register = match operation {
        gimli::Operation::CallFrameCFA => return FrameBase::Cfa,
        gimli::Operation::Register { register } => register.0,
        gimli::Operation::RegisterOffset { register, offset: 0, .. } => register.0,
        _ => return FrameBase::Unknown,
    };
    if frame_pointer_registers(architecture).contains(&register) {
        FrameBase::FramePointer
    } else {
        FrameBase::Register(register)
    }
}

// DWARF register numbers of the frame pointer register of an architecture
fn frame_pointer_registers(architecture: object::Architecture) -> &'static [u16] {
    match architecture {
        object::Architecture::X86_64 => &[6],                                  // rbp
        object::Architecture::I386 => &[5],                                    // ebp
        object::Architecture::Aarch64 => &[29],                                // x29
        object::Architecture::Arm => &[7, 11],                                 // r7 (Thumb), r11 (ARM)
        object::Architecture::Riscv32 | object::Architecture::Riscv64 => &[8], // s0 / fp
        object::Architecture::Xtensa => &[7, 15],                              // a7 (windowed ABI), a15 (call0 ABI)
        object::Architecture::PowerPc | object::Architecture::PowerPc64 => &[31],
        _ => &[],
    }
}

// DWARF register numbers of the stack pointer register of an architecture, used in evaluate_exprloc to recognize a
// variable location expressed directly as "DW_OP_breg<sp> <offset>" (seen with Clang, which sometimes emits this
// instead of routing the local through DW_AT_frame_base/DW_OP_fbreg like GCC does). This is only used to make the
// diagnostic more specific: the resulting offset is relative to the live SP value, which is a different reference
// point than DW_AT_frame_base (they differ by the function's stack frame size), so it must NOT be treated as a
// frame-base-relative offset (address extension 2) without further work.
fn stack_pointer_registers(architecture: object::Architecture) -> &'static [u16] {
    match architecture {
        object::Architecture::X86_64 => &[7],                                    // rsp
        object::Architecture::I386 => &[4],                                      // esp
        object::Architecture::Aarch64 => &[31],                                  // sp
        object::Architecture::Arm => &[13],                                      // sp
        object::Architecture::Riscv32 | object::Architecture::Riscv64 => &[2],   // sp
        object::Architecture::PowerPc | object::Architecture::PowerPc64 => &[1], // r1
        _ => &[],
    }
}

// The entry is the abstract instance of an inlined function (DW_AT_inline)
fn is_inlined_subprogram(entry: &DebuggingInformationEntry<SliceType, usize>) -> bool {
    matches!(
        entry.attr_value(gimli::constants::DW_AT_inline),
        Some(gimli::AttributeValue::Inline(gimli::constants::DW_INL_inlined | gimli::constants::DW_INL_declared_inlined))
    )
}

// Get the innermost enclosing function (a subprogram or the inlined copy of a function) with its linkage name and frame base,
// the enclosing namespaces (outermost first) and whether the variable belongs to an inlined function, from the traversal context
fn get_varinfo_from_context(context: &[Scope]) -> (Option<String>, Option<String>, Vec<String>, bool, FrameBase) {
    let function_scope = context
        .iter()
        .rev()
        .find(|s| s.tag == gimli::constants::DW_TAG_subprogram || s.tag == gimli::constants::DW_TAG_inlined_subroutine);
    let function = function_scope.and_then(|s| s.name.clone());
    let function_linkage = function_scope.and_then(|s| s.linkage_name.clone());
    let namespaces: Vec<String> = context
        .iter()
        .filter_map(|s| (s.tag == gimli::constants::DW_TAG_namespace).then(|| s.name.clone()).flatten())
        .collect();
    let inlined = context.iter().any(|s| s.inlined);
    let frame_base = function_scope.map_or(FrameBase::Unknown, |s| s.frame_base);
    (function, function_linkage, namespaces, inlined, frame_base)
}

// Demangle the variable names which are mangled C++ symbols (_ZN13motor_control5inputE -> motor_control::input), for the
// lookup of a variable by its C++ name. Returns demangled name -> mangled name. Uses the cpp_demangle crate, without the
// parameter list and return type of functions
fn demangle_cpp_varnames(input: &[&String]) -> HashMap<String, String> {
    let mut demangled_symbols = HashMap::<String, String>::new();
    let demangle_opts = cpp_demangle::DemangleOptions::new().no_params().no_return_type();
    for varname in input {
        // some really simple strings can be processed by the demangler, e.g "c" -> "const", which is wrong here.
        // by only processing symbols that start with _Z (variables in classes/namespaces) this problem is avoided
        if varname.starts_with("_Z")
            && let Ok(sym) = cpp_demangle::Symbol::new(*varname)
        {
            // exclude useless demangled names like "typeinfo for std::type_info" or "{vtable(std::type_info)}"
            if let Ok(demangled) = sym.demangle_with_options(&demangle_opts)
                && !demangled.contains(' ')
                && !demangled.starts_with("{vtable")
            {
                demangled_symbols.insert(demangled, (*varname).clone());
            }
        }
    }

    demangled_symbols
}

// UnitList holds a list of all UnitHeaders in the Dwarf data for convenient access
impl<'a> UnitList<'a> {
    fn new() -> Self {
        Self { list: Vec::new() }
    }

    // Append a unit, its index in the list becomes its unit index
    fn add(&mut self, unit: UnitHeader<SliceType<'a>>, abbrev: Abbreviations) {
        self.list.push((unit, abbrev));
    }

    // Find the unit which contains the entry at the given .debug_info offset. The units are laid out one after the other in
    // .debug_info, so the unit is the one whose range [offset, offset + length) contains the entry
    fn get_unit(&self, itemoffset: usize) -> Option<usize> {
        for (idx, (unit, _)) in self.list.iter().enumerate() {
            let unitoffset = unit.offset().to_debug_info_offset(unit).unwrap().0;
            if unitoffset < itemoffset && unitoffset + unit.length_including_self() > itemoffset {
                return Some(idx);
            }
        }

        None
    }
}

// units[unit_idx] gives the header and abbreviations of a unit
impl<'a> Index<usize> for UnitList<'a> {
    type Output = (UnitHeader<SliceType<'a>>, gimli::Abbreviations);

    fn index(&self, idx: usize) -> &Self::Output {
        &self.list[idx]
    }
}

#[cfg(test)]
mod test {
    use super::*;

    // Frame base expressions: the CFA (GCC), the frame pointer register of the architecture (clang), other registers and expressions
    #[test]
    fn test_classify_frame_base() {
        use object::Architecture::{Aarch64, Arm, X86_64};
        let encoding = gimli::Encoding {
            format: gimli::Format::Dwarf32,
            version: 5,
            address_size: 8,
        };
        let classify = |architecture, bytes: &[u8]| classify_frame_base(architecture, gimli::Expression(EndianSlice::new(bytes, RunTimeEndian::Little)), encoding);
        assert_eq!(classify(Aarch64, &[0x9c]), FrameBase::Cfa); // DW_OP_call_frame_cfa
        assert_eq!(classify(Aarch64, &[0x50 + 29]), FrameBase::FramePointer); // DW_OP_reg29 x29
        assert_eq!(classify(Aarch64, &[0x50 + 31]), FrameBase::Register(31)); // DW_OP_reg31 sp
        assert_eq!(classify(Aarch64, &[0x90, 29]), FrameBase::FramePointer); // DW_OP_regx 29
        assert_eq!(classify(Arm, &[0x50 + 7]), FrameBase::FramePointer); // r7 (Thumb)
        assert_eq!(classify(Arm, &[0x50 + 11]), FrameBase::FramePointer); // r11 (ARM)
        assert_eq!(classify(Arm, &[0x50 + 13]), FrameBase::Register(13)); // sp
        assert_eq!(classify(X86_64, &[0x50 + 6]), FrameBase::FramePointer); // rbp
        assert_eq!(classify(X86_64, &[0x70 + 6, 0]), FrameBase::FramePointer); // DW_OP_breg6 0
        assert_eq!(classify(X86_64, &[0x70 + 6, 0x10]), FrameBase::Unknown); // DW_OP_breg6 16
        assert_eq!(classify(X86_64, &[0x91, 0x10]), FrameBase::Unknown); // DW_OP_fbreg 16
        assert_eq!(classify(X86_64, &[0x9c, 0x23, 0x08]), FrameBase::Unknown); // CFA plus constant
        assert_eq!(classify(X86_64, &[]), FrameBase::Unknown);
    }

    // C++ type test fixture, see fixtures/cpp_types.cpp
    static ELF_FILE_NAMES: [&str; 1] = [concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/cpp_types.elf")];

    #[test]
    fn test_itanium_mangled_names() {
        let namespace = ["motor_control".to_string()];
        assert_eq!(itanium_mangled_names(&namespace, "input"), ["_ZN13motor_control5inputE", "_ZN13motor_controlL5inputE"]);
        let nested = ["diagnostics".to_string(), "detail".to_string()];
        assert_eq!(itanium_mangled_names(&nested, "input")[0], "_ZN11diagnostics6detail5inputE");
    }

    #[test]
    fn test_itanium_local_static_name() {
        assert_eq!(itanium_local_static_name("_Z3foov", "counter").as_deref(), Some("_ZZ3foovE7counter"));
        assert_eq!(
            itanium_local_static_name("_ZL8fastTaskPv", "xcp_meta__comment__static_counter").as_deref(),
            Some("_ZZL8fastTaskPvE33xcp_meta__comment__static_counter")
        );
        assert_eq!(itanium_local_static_name("main", "counter"), None);
    }

    // Qualified names of struct types whose name is used in different scopes: namespaces, nested namespaces, enclosing classes
    // and const/volatile qualified variables, see fixtures/cpp_namespaces.cpp
    #[test]
    fn test_load_qualified_type_names() {
        let filename = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/cpp_namespaces.elf");
        let debugdata = DebugData::load_dwarf(OsStr::new(filename), 0, (0, usize::MAX)).unwrap();
        let type_name_of = |varinfo: &VarInfo| -> String {
            let type_info = debugdata.types.get(&varinfo.typeref).expect("type of variable");
            debugdata.get_type_name(type_info).expect("type name").to_string()
        };
        let variable = |name: &str| &debugdata.variables.get(name).unwrap_or_else(|| panic!("variable {name}"))[0];

        // The three variables named "input" are in different namespaces and have types of different scopes
        let inputs = debugdata.variables.get("input").expect("variables named input");
        assert_eq!(inputs.len(), 3);
        let mut namespaces: Vec<Vec<String>> = inputs.iter().map(|v| v.namespaces.clone()).collect();
        namespaces.sort();
        assert_eq!(namespaces, vec![vec!["diagnostics", "detail"], vec!["motor_control"], vec!["valve_control"]]);
        let mut type_names: Vec<String> = inputs.iter().map(type_name_of).collect();
        type_names.sort();
        assert_eq!(type_names, vec!["diagnostics.detail.Input", "motor_control.Input", "valve_control.Input"]);

        // A type from another namespace than the variable, and const/volatile qualified variables
        assert_eq!(variable("last_motor_input").namespaces, vec!["diagnostics"]);
        assert_eq!(type_name_of(variable("last_motor_input")), "motor_control.Input");
        assert_eq!(type_name_of(variable("volatile_motor_input")), "motor_control.Input");
        assert_eq!(type_name_of(variable("const_valve_input")), "valve_control.Input");

        // A type with a unique name keeps its plain name, a type without scope keeps its plain name even if the name is ambiguous
        assert_eq!(type_name_of(variable("output")), "Output");
        let mut config_names: Vec<String> = debugdata.variables.get("config").unwrap().iter().map(type_name_of).collect();
        config_names.sort();
        assert_eq!(config_names, vec!["Config", "valve_control.Config"]);

        // The scope of a struct type nested in a class is the class
        let motor_controller = debugdata.types.get(&variable("motor_controller").typeref).unwrap();
        assert_eq!(debugdata.get_type_name(motor_controller), Some("MotorController"));
        let DbgDataType::Struct { members, .. } = &motor_controller.datatype else {
            panic!("MotorController is not a struct");
        };
        let DbgDataType::TypeRef(params_ref, _) = members.get("params").unwrap().0.datatype else {
            panic!("MotorController.params is not a type reference");
        };
        assert_eq!(debugdata.get_type_name(debugdata.types.get(&params_ref).unwrap()), Some("MotorController.Params"));
    }

    #[test]
    fn test_load_data() {
        for filename in ELF_FILE_NAMES {
            let debugdata = DebugData::load_dwarf(OsStr::new(filename), 1, (0, usize::MAX)).unwrap();
            // 14 globals in cpp_types.cpp, compilers may add a few more (e.g. static members)
            assert!(debugdata.variables.len() >= 14, "only {} variables found", debugdata.variables.len());
            assert!(debugdata.variables.get("g_sink").is_some());

            for (_, varinfo) in &debugdata.variables {
                assert!(debugdata.types.contains_key(&varinfo[0].typeref));
            }

            let datatype_of = |name: &str| -> &DbgDataType {
                let varinfo = debugdata.variables.get(name).unwrap_or_else(|| panic!("variable {name} not found"));
                &debugdata.types.get(&varinfo[0].typeref).unwrap().datatype
            };
            assert!(matches!(datatype_of("g_plain"), DbgDataType::Struct { is_class: false, .. }));
            assert!(matches!(datatype_of("g_pubclass"), DbgDataType::Struct { is_class: true, .. }));
            assert!(matches!(datatype_of("g_bigenum"), DbgDataType::Enum { signed: true, .. }));
        }
    }
}
