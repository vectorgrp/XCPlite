//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module elf_reader
// Defines and implements ElfReader
// Read ELF files and extract debug information with DebugData (see copyright notice below)
// ElfReader provides functions to fill a XCP registry with events, segments, variables and metadata

// Based on Github repository a2ltool by DanielT: https://github.com/DanielT/a2ltool

/*
Note on V2.1.10:
Updated to typereader.rs from a2ltool v3.4.1 (commit 0b61aa5, 2026-08-04).
The Class variant is gone.
Struct now carries is_class and inheritance, and the size and Display code follow.
The two Class match arms from the previous fix are collapsed into the Struct arms, and a new test asserts that base members arrive for all four struct/class inheritance combinations.
*/

/*
Reading guide: how this module gets from an ELF file to registry entries

The code is layered top down:

  ElfReader (this file)                 Registers events, calibration segments, variables and metadata in the xcp_registry.
    |                                   It works on the DebugData below and recognizes the marker variables which the
    |                                   XCPlite instrumentation macros emit.
    v
  DebugData (debuginfo/mod.rs)          Plain data extracted from the ELF file: every variable with name, scope, address and
    |                                   type, every type used by a variable, the compilation unit names, the ELF sections
    |                                   and symbols.
    v
  DebugDataReader (debuginfo/dwarf/)    The parser. Opens the ELF file with the `object` crate (sections, symbol table) and
                                        reads the DWARF debug information with the `gimli` crate (variables, types, functions).

  test.rs                               The unit tests of this module, most of them load one of the ELF files in fixtures/.

An ELF file carries two kinds of information which are used here:

  1. Sections and the symbol table (.symtab). This is the linker's view: flat lists of named byte ranges (sections) and
     named addresses (symbols). Used for the XCPlite marker sections (xcp_evts, xcp_epk, xcp_meta), for the address of a
     variable when the DWARF information has none, and for the mangled names of C++ symbols.
  2. DWARF debug information (.debug_info and the other .debug_* sections). This is the compiler's view: a tree of
     "debugging information entries" (DIEs) per compilation unit (one .c/.cpp file), describing every function, scope,
     variable and type of the source code with attributes like name, type, byte size and the location of a variable in
     memory or on the stack. Only present if the code was compiled with -g. A stripped executable has neither .symtab
     nor DWARF, an executable built on macOS has no DWARF in the executable (Mach-O, see load_elf_file).

Marker variables, emitted by the macros in inc/xcplib.h and found by their name in the DWARF variable list:

  calseg__<name>, calblk__<name>   calibration segment or block descriptor, CalSegCreate/CalBlkCreate  (register_segments)
  evt__<name>                      event descriptor in the xcp_evts section, DaqCreateEvent            (register_events)
  trg__<modes>__<name>             event trigger point in a function, DaqTriggerEvent                  (register_event_locations)
  xcp_meta__<kind>__<name>         XCP_COMMENT, XCP_UNIT, XCP_LIMITS, XCP_READ_WRITE, xcp_meta section  (register_metadata)
  XCPLITE__<signature>             addressing mode signature of the target build                       (get_target_signature)

The order of the register_* calls matters: events before event locations (a trigger refers to its event), segments and
events before variables (a variable gets its event and its segment), variables before metadata (metadata is attached to
registered variables). See main.rs for the sequence.

Addresses: VarInfo.address is a pair (address extension, address), the encoding is described in debuginfo/mod.rs.
The XCP address extension tells the target how to interpret an address (absolute, calibration segment relative, stack
relative, ...), see docs/TECHNICAL.md. register_variables converts the pair into the McAddress of the registry.

Useful tools to look at an ELF file while debugging this code (the GNU or LLVM versions of the target toolchain):
    readelf -S <file>                          sections
    nm <file> | c++filt                        symbol table, demangled
    readelf --debug-dump=info <file>           the DWARF tree, as this code sees it
    llvm-dwarfdump --name <varname> <file>     the DIEs of one variable
*/

#![allow(clippy::collapsible_else_if)]

use indexmap::IndexMap;
use regex::Regex;
use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::error::Error;
use std::ffi::OsStr;

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use xcp_registry::{McAddress, McDimType, McEvent, McIdentifier, McObjectQualifier, McObjectType, McSupportData, McValueType, Registry, RegistryError};

/*
Which information can be detected from ELF/DWARF:
    - Events:
        name, compilation unit, function name and CFA offset, but index is unknown
    - Memory segment name, type (naming convention name = reference page), address, length, but number is unknown
    - Variables:
        variable name, typename, absolute address, frame offset, compilation unit, function name, namespace
        static variables in functions get the correct event
        local variables on stack get the correct CFA
        name, type, compilation unit, namespace, location (register or stack)
    - Types:
        typedefs, structs, enums
        basic types: int8/16/32/64, uint8/16/32/64, float, double
        arrays 1D and 2D
        pointers (as ulong or ulonglong)

Key benefits:
    - Instance names get prefixed with function name if local stack or static variables
    - All instances get the correct fixed event id, if there is one in their scope, otherwise default event id is 0
    - Event compilation unit, function and CFA is detected to enable local variable access

Tools:
    dwarfdump --debug-info <filename>
    dwarfdump --debug-info --name <varname> <filename>
    objdump -h  <filename>
    objdump --syms <filename>

Limitations:
    - With -o1 most stack variables are in registers, have to be manually spilled to stack or captured
    - Segment numbers and event index are not constant expressions, need to be read by XCP (current solution) or from the binary persistence file from the target

Possible future improvements:
    - Thread load addressing mode
    - C++ support,  this addressing support, namespaces
    - Measurement of variables and function parameters in registers
    - Just in time compilation of variable access expressions
*/

// Dwarf reader
// This module contains modified code adapted from https://github.com/DanielT/a2ltool
// Original code licensed under MIT/Apache-2.0
// Copyright (c) DanielT
mod debuginfo;
use debuginfo::{DbgDataType, DebugData, FrameBase, TypeInfo, VarInfo};

// The name of the variable a capture struct member was copied from: the macro appends one underscore to it, so exactly one
// trailing underscore is removed here, and a variable which ends with an underscore itself keeps it
fn capture_member_variable_name(member_name: &str) -> &str {
    member_name.strip_suffix('_').unwrap_or(member_name)
}

// Variable which transport information for the A2L creator
pub fn is_a2l_variable(name: &str) -> bool {
    name.starts_with("calseg__")
        || name.starts_with("calblk__")
        || name.starts_with("evt__")
        || name.starts_with("trg__")
        || name.starts_with("cap__")
        || name.starts_with("xcp_meta__")
        || name.starts_with("XCPLITE__")
}

// Variables which never become A2L objects: the internals of the compiler and of the standard library, XCPlite internals and the A2L creator markers
// variables and the marker variables of the sections and macros (see the module comment)
pub fn is_internal_variable(name: &str) -> bool {
    name.starts_with("__") || name.starts_with("gXcp") || name.starts_with("gA2l") || is_a2l_variable(name)
}

// XCP address extension of the captured variables: the first dynamic base address (XCP_ADDR_EXT_DYN + 1 in src/xcp_cfg.h),
// which the capture trigger macros pass as the address of the capture struct
const XCP_ADDR_EXT_CAPTURE: u8 = 3;

//------------------------------------------------------------------------
//  ELF reader and A2L creator

pub(crate) struct ElfReader {
    pub(crate) debug_data: DebugData,
    // Typedef name -> content signature of the struct/class typedefs registered so far, to detect different types with the same name.
    // A2L has one flat name space for typedefs, the type names from DebugData::get_type_name are unique per scope but not per content.
    typedef_signatures: RefCell<HashMap<String, String>>,
}

impl ElfReader {
    // Load debug information from the ELF file
    // The error message describes why the file can not be used (not found, not an ELF file, no DWARF debug information, ...)
    pub fn new(file_name: &str, verbose: usize, unit_idx_limit: (usize, usize)) -> Result<ElfReader, String> {
        info!("Loading debug information from ELF file: {}", file_name);
        let debug_data = DebugData::load_dwarf(OsStr::new(file_name), verbose, unit_idx_limit)?;
        Ok(ElfReader::from_debug_data(debug_data))
    }

    // Create the ELF reader from loaded debug information
    fn from_debug_data(debug_data: DebugData) -> ElfReader {
        ElfReader {
            debug_data,
            typedef_signatures: RefCell::new(HashMap::new()),
        }
    }

    // Get the McValueType for a given TypeInfo, which can be a basic type, pointer or array
    fn get_value_type(&self, reg: &mut Registry, type_info: &TypeInfo, object_type: McObjectType) -> McValueType {
        let type_size = type_info.get_size();
        match &type_info.datatype {
            DbgDataType::Uint8 => McValueType::Ubyte,
            DbgDataType::Uint16 => McValueType::Uword,
            DbgDataType::Uint32 => McValueType::Ulong,
            DbgDataType::Uint64 => McValueType::Ulonglong,
            DbgDataType::Sint8 => McValueType::Sbyte,
            DbgDataType::Sint16 => McValueType::Sword,
            DbgDataType::Sint32 => McValueType::Slong,
            DbgDataType::Sint64 => McValueType::Slonglong,
            DbgDataType::Float => McValueType::Float32Ieee,
            DbgDataType::Double => McValueType::Float64Ieee,
            DbgDataType::Struct { size, members, .. } => {
                if type_info.name.is_some() {
                    // Register a typedef for the struct/class type (reused if it already exists) and reference it by its unique typedef name.
                    // Inherited members of structs and classes are already flattened into `members` by the DWARF reader.
                    McValueType::new_typedef(self.register_struct(reg, object_type, type_info, *size as usize, members))
                } else {
                    warn!("Struct/class type without name in get_value_type");
                    McValueType::Ubyte
                }
            }
            DbgDataType::Enum { size, signed, enumerators } => McValueType::from_integer_size(*size as usize, *signed),

            DbgDataType::TypeRef(typeref, size) => {
                if let Some(typeinfo) = self.debug_data.types.get(typeref) {
                    self.get_value_type(reg, typeinfo, object_type)
                } else {
                    error!("TypeRef {} to unknown in get_field_type", typeref);
                    McValueType::Ubyte
                }
            }

            DbgDataType::Pointer(size, _pointee) => {
                if *size == 4 {
                    McValueType::Ulong
                } else if *size == 8 {
                    McValueType::Ulonglong
                } else {
                    warn!("Unsupported pointer size {} in get_field_type", size);
                    McValueType::Ulonglong
                }
            }

            // These types are not a supported value type (arrays are handled in get_dim_type)
            // DbgDataType::Bitfield | DbgDataType::Union | DbgDataType::FuncPtr | DbgDataType::Other | DbgDataType::Array =>
            _ => {
                warn!("Unsupported type in get_field_type: {:?}", &type_info.datatype);
                //assert!(false, "Unsupported type in get_field_type: {:?}", &type_info.datatype);
                McValueType::Ubyte
            }
        }
    }

    // Get the dimension type for a variable, which is used to determine the number of elements and dimensions for arrays
    fn get_dim_type(&self, reg: &mut Registry, type_info: &TypeInfo, object_type: McObjectType) -> McDimType {
        let type_size = type_info.get_size();
        match &type_info.datatype {
            DbgDataType::Array { arraytype, dim, stride, size } => {
                assert!(dim.len() != 0);
                let elem_type = self.get_value_type(reg, arraytype, object_type);
                if dim.len() > 2 {
                    warn!("Only 1D and 2D arrays supported, got {}D", dim.len());
                    McDimType::new(McValueType::Ubyte, 1, 1)
                } else if dim.len() == 1 {
                    McDimType::new(elem_type, dim[0] as u16, 1)
                } else {
                    McDimType::new(elem_type, dim[0] as u16, dim[1] as u16)
                }
            }
            _ => McDimType::new(self.get_value_type(reg, type_info, object_type), 1, 1),
        }
    }

    // Register a struct/class type as typedef in the registry, including its members, and return the typedef name to reference it.
    // Nested struct/class members are registered recursively.
    // A2L has one flat name space for typedefs, but the DWARF type name is unqualified, so the typedef name is determined as follows:
    // - The typedef is named after the type, the identifier is sanitized (e.g. "TplStruct<short unsigned int>" -> "TplStruct_short_unsigned_int_").
    // - If struct/class types with this name exist in different scopes, the name is qualified with the enclosing namespaces, classes or
    //   functions of the type (motor_control::Input -> "motor_control.Input", MotorController::Params -> "MotorController.Params").
    // - A typedef with the same name and the same content is reused: the same type used by several variables or fields, or
    //   the same type defined again in another compilation unit (the DWARF of every compilation unit has its own copy of a type).
    // - If the name is still in use for a typedef with different content, a numeric suffix is appended ("state_1"). This happens for
    //   different types with the same name and without scope (file local struct types in different C files), and for a type
    //   which is used for measurement and for calibration variables (TYPEDEF_MEASUREMENT vs. TYPEDEF_CHARACTERISTIC components).
    fn register_struct(&self, reg: &mut Registry, object_type: McObjectType, type_info: &TypeInfo, size: usize, members: &IndexMap<String, (TypeInfo, u64)>) -> McIdentifier {
        let type_name = type_info.name.as_deref().unwrap_or("");
        let location = || self.debug_data.make_simple_unit_name(type_info.unit_idx).unwrap_or_else(|| type_info.unit_idx.to_string());

        // Resolve the member types first, this may recursively register nested typedefs
        let first_nested_index = reg.typedef_list.len();
        let mut fields: Vec<(&String, McDimType, u16)> = Vec::with_capacity(members.len());
        for (field_name, (field_type_info, field_offset)) in members {
            let Ok(offset) = u16::try_from(*field_offset) else {
                warn!("Field '{}.{}' skipped, offset {} exceeds the supported range", type_name, field_name, field_offset);
                continue;
            };
            fields.push((field_name, self.get_dim_type(reg, field_type_info, object_type), offset));
        }

        // Content signature: typedefs may share a name only if size, object type and all fields (name, type, dimensions, offset) are identical
        let signature = format!("{size} {object_type:?} {fields:?}");

        // Typedef name: the type name, qualified with the scope of the type if the name is used by different types in different scopes
        // ("motor_control.Input" for motor_control::Input), see DebugData::get_type_name
        let base_name = self.debug_data.get_type_name(type_info).unwrap_or(type_name).to_string();

        // Find the typedef with this content or a free name: base_name, base_name_1, base_name_2, ...
        let mut candidate = base_name.clone();
        let mut suffix = 0;
        let type_id = loop {
            let type_id = McIdentifier::from(candidate.clone());
            match self.typedef_signatures.borrow().get(type_id.as_str()) {
                Some(existing) if *existing == signature => return type_id, // already registered
                Some(_) => {}                                               // used by a typedef with different content
                None if reg.typedef_list.find_typedef(type_id.as_str()).is_none() => break type_id,
                None => {} // used by a typedef which was not registered from the ELF file
            }
            suffix += 1;
            candidate = format!("{base_name}_{suffix}");
        };
        if suffix > 0 {
            warn!(
                "Struct/class type '{}' in {} has a different definition or object type than the existing typedef '{}', registered as typedef '{}'",
                type_name,
                location(),
                base_name,
                type_id
            );
        } else if base_name != type_name {
            info!(
                "Struct/class type '{}' in {} registered as typedef '{}', the type name is used in different scopes",
                type_name,
                location(),
                type_id
            );
        }

        // Register the typedef and its fields
        if let Err(e) = reg.add_typedef(type_id, size) {
            error!("Failed to register typedef '{}' for struct/class type '{}': {}", type_id, type_name, e);
            return type_id;
        }
        for (field_name, field_dim_type, offset) in fields {
            if let Err(e) = reg.add_typedef_field(type_id.as_str(), field_name.clone(), field_dim_type, McSupportData::new(object_type), offset) {
                error!("Failed to register field '{}.{}': {}", type_id, field_name, e);
            }
        }
        // Keep the typedef in front of its nested typedefs in the registry, this is the order of the typedefs in the A2L file
        reg.typedef_list[first_nested_index..].rotate_right(1);
        self.typedef_signatures.borrow_mut().insert(type_id.to_string(), signature);
        type_id
    }

    // Find the addressing mode marker variable (naming convention "XCPLITE__<signature>") and return the signature, if found
    // (CASDD, ACSDD, ...)
    // Addressing mode signature of the target build (CASDD, ACSDD, AXSDD, CXSDD), from the marker variable XCPLITE__<signature>
    // of the XCPlite library (xcplite.c). It is an exported global variable, so the symbol table has it even when the debug
    // information of the library is not parsed (--elf-unit-limit) or was not built with -g. The DWARF variables are the fallback
    pub fn get_target_signature(&self) -> Option<&str> {
        let from_symbols = self.debug_data.symbol_addresses.keys().filter_map(|name| name.strip_prefix("XCPLITE__")).min();
        if from_symbols.is_some() {
            return from_symbols;
        }
        self.debug_data.variables.keys().find_map(|name| name.strip_prefix("XCPLITE__"))
    }

    // Log the compilers which built the ELF file, from the DW_AT_producer of the compilation units: compiler, version and the
    // command line options which matter here, in particular the optimization level and the frame pointer
    pub fn log_compilers(&self) {
        let mut logged: Vec<&str> = Vec::new();
        for producer in self.debug_data.producers.iter().flatten() {
            if !logged.contains(&producer.as_str()) {
                logged.push(producer);
                info!("Compiler: {}", producer);
            }
        }
        if logged.is_empty() {
            debug!("No compiler information (DW_AT_producer) in the debug information of the ELF file");
        }
    }

    // Get the EPK string and address from debug_data and set it in the registry application version information, if available
    pub fn register_epk_addr_info(&self, reg: &mut Registry, verbose: usize) {
        info!("===============================================================");
        if self.debug_data.epk_addr > 0 {
            info!("EPK segment memory section found at address = 0x{:08X}", self.debug_data.epk_addr);
            let epk = self.debug_data.epk_string.clone().unwrap_or_else(|| "<unknown>".to_string());
            info!("EPK string: '{}'", epk);
            reg.application.set_version(epk, self.debug_data.epk_addr.try_into().unwrap());
        } else {
            warn!("EPK segment memory section not found in ELF file");
        }
    }

    // Register segments from segment creation markers (calseg__name) found in the code
    pub fn register_segments(&self, reg: &mut Registry, seg_relative: bool, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("===============================================================");
        info!(
            "Registering segment information {}:",
            if !seg_relative { "(absolute addressing mode)" } else { "(relative addressing mode)" }
        );
        info!("===============================================================");

        // Step 1
        // Iterate over all variables and look for segment definition markers, which are created by the CalSegCreate or CalBlkCreate macros
        // Naming convention is "calseg__<name>" or "calblk__<name>"
        // Sort the vector by address to ensure the segments are processed in the order they are defined in the code
        // Index in the vector is now the segment number
        let mut seg_definitions: Vec<(String, &Vec<VarInfo>, u64, Option<u8>)> = Vec::new();
        for (var_name, var_infos) in &self.debug_data.variables {
            let is_calseg = var_name.starts_with("calseg__");
            let is_calblk = var_name.starts_with("calblk__");
            if is_calseg || is_calblk {
                let (seg_name, seg_number) = if is_calseg {
                    (var_name.strip_prefix("calseg__").unwrap_or(var_name), Some(0))
                } else {
                    (var_name.strip_prefix("calblk__").unwrap_or(var_name), None)
                };
                let mut seg_descr_addr = var_infos[0].address.1;
                if seg_name == "epk" {
                    // EPK segment is a special case, it has always index = 0
                    seg_descr_addr = 0;
                }
                assert!(var_infos.len() == 1);
                seg_definitions.push((seg_name.to_string(), var_infos, seg_descr_addr, seg_number));
            }
        }
        seg_definitions.sort_by_key(|x| x.2);
        // Calculate the segment numbers for calseg, calblk doues not have a number
        let mut seg_number: u8 = 0;
        for i in 0..seg_definitions.len() {
            if let Some(0) = seg_definitions[i].3 {
                seg_definitions[i].3 = Some(seg_number);
                seg_number += 1;
            }
        }

        // Print the found segment definition markers
        if verbose >= 1 {
            println!("Found {} segment definition marker variables:", seg_definitions.len());
            for (seg_index, (var_name, var_infos, var_address, seg_number)) in seg_definitions.iter().enumerate() {
                println!("{}: '{}' - number={:?}, addr={:08X}'", seg_index, var_name, seg_number, var_address);
                if verbose >= 2 {
                    let var_info = &var_infos[0];
                    let function_name = if let Some(f) = var_info.function.as_ref() { f.as_str() } else { "" };
                    let unit_idx = var_info.unit_idx;
                    let unit_name = if let Some(name) = self.debug_data.make_simple_unit_name(unit_idx) {
                        name
                    } else {
                        format!("{unit_idx}")
                    };
                    println!("  found in {}:'{}'", unit_name, function_name);
                }
            }
        }

        // Step 2
        // Iterate over the segment definitions and register the segments in the registry
        for (seg_index, (seg_name, var_infos, var_address, seg_number)) in seg_definitions.iter().enumerate() {
            let var_info = &var_infos[0];
            let seg_length: u16;
            let seg_addr: u64;

            // Special case for EPK segment, which does not have a reference page variable, but the segment address and length may be stored in the debug data from the EPK section
            if seg_name == "epk" {
                if let Some(epk_str) = self.debug_data.epk_string.as_ref() {
                    seg_length = epk_str.len().try_into().expect("EPK string length exceeds 64K");
                    seg_addr = self.debug_data.epk_addr;
                } else {
                    error!("No EPK segment memory section in ELF file, segment '{}' skipped", seg_name);
                    continue; // skip this variable
                }
            }
            // Not epk segment
            else {
                // Lookup the reference page variable (by naming convention: same as segment name!) information
                // This may be ambigous, so we use some heuristics to select the right variable
                // @@@@ TODO use the commandline compilation unit filter here
                let seg_var_info = if let Some(x) = self.debug_data.variables.get(seg_name) {
                    let mut valid_candidates: Vec<_> = x.iter().filter(|var_info| var_info.address.0 == 0 && var_info.address.1 != 0).collect();
                    if valid_candidates.len() > 1 {
                        let same_unit_candidates: Vec<_> = valid_candidates.iter().copied().filter(|candidate| candidate.unit_idx == var_info.unit_idx).collect();
                        if same_unit_candidates.len() == 1 {
                            valid_candidates = same_unit_candidates;
                        }
                    }
                    if valid_candidates.len() != 1 {
                        error!(
                            "Calibration segment reference page variable '{}' has {} usable definitions, expected 1 ({} total DWARF entries)",
                            seg_name,
                            valid_candidates.len(),
                            x.len()
                        );
                        if verbose >= 1 {
                            for candidate in x {
                                let unit_name = self.debug_data.make_simple_unit_name(candidate.unit_idx).unwrap_or_else(|| candidate.unit_idx.to_string());
                                let function_name = candidate.function.as_deref().unwrap_or("<global>");
                                println!(
                                    "  candidate in {}:'{}', addr_class={}, addr=0x{:08X}",
                                    unit_name, function_name, candidate.address.0, candidate.address.1
                                );
                            }
                        }
                        continue;
                    }
                    valid_candidates[0]
                } else {
                    error!("Could not find calibration segment reference page variable '{}'", seg_name);
                    continue;
                };

                // Determine segment length
                seg_length = {
                    if let Some(type_info) = self.debug_data.types.get(&seg_var_info.typeref) {
                        println!(
                            "Calibration segment '{}' type information found, type={}, size = {}",
                            seg_name,
                            type_info.name.as_ref().map_or("<unnamed>", |s| s.as_str()),
                            type_info.get_size()
                        );
                        if verbose >= 2 {
                            println!("  type = {}", type_info);
                        }
                        type_info.get_size().try_into().expect("segment size exceeds 64K")
                    } else {
                        error!("Could not determine length type for segment {}", seg_name);
                        0
                    }
                };

                // Determine segment address
                // @@@@ TODO: handle signed relative encoding
                seg_addr = seg_var_info.address.1;
                if !(seg_length > 0 && seg_addr > 0 && seg_var_info.address.0 == 0) {
                    error!(
                        "Calibration segment from cal_<name> '{}' not found, has invalid address {:#x} or size {:#x}, skipped",
                        seg_name, seg_addr, seg_length
                    );
                    continue; // skip this variable
                }

                info!(
                    "Calibration segment '{}' default page variable found in debug data: Address = {:#x}, Size = {:#x}",
                    seg_name, seg_addr, seg_length
                );
            } // not EPK segment

            // Find the segment by name in the registry
            if let Some(reg_seg) = reg.cal_seg_list.find_cal_seg(seg_name) {
                info!("Calibration segment '{}' {}:0x{:08X} found in registry", seg_name, reg_seg.addr_ext, reg_seg.addr);
                // Segment relative addressing mode
                if reg_seg.addr == 0x80000000 + ((reg_seg.index as u32) << 16) {
                    info!("  with segment relative addressing");
                    // Check if length matches
                    if reg_seg.size == seg_length as u32 {
                        reg_seg.set_mem_addr(seg_addr);
                        info!("  matches existing registry entry");
                    } else {
                        warn!("Calibration segment '{}' length does not match existing registry entry", seg_name);
                    }
                }
                // Segment absolute addressing mode
                else {
                    // Check if address and length match
                    if reg_seg.addr as u64 != seg_addr {
                        warn!(
                            "Calibration segment '{}' address does not match existing registry entry, reg = {:08X} vs. {:08X}",
                            seg_name, reg_seg.addr, seg_addr
                        );
                    } else if reg_seg.size != seg_length as u32 {
                        warn!(
                            "Calibration segment '{}' length does not match existing registry entry, reg = {} vs. {}",
                            seg_name, reg_seg.size, seg_length
                        );
                    } else {
                        info!("Calibration segment '{}' matches existing registry entry", seg_name);
                    }
                } // absolute addressing mode
            }
            // already existing
            //
            // If not existing, create the segment
            // Use segment relative or absolute addressing mode
            else {
                info!("Calibration segment '{}' not yet defined in registry", seg_name);

                if seg_relative {
                    // Add in segment relative addressing mode
                    let res = reg.cal_seg_list.add_cal_seg(seg_name.to_string(), *seg_number, seg_length as u32);
                    if let Err(e) = res {
                        error!("Failed to add calibration segment '{}': {}", seg_name, e);
                        continue;
                    }
                } else {
                    // Absolute addressing mode
                    if seg_addr >= 0xFFFFFFFF {
                        error!(
                            "Calibration segment '{}' has 64 bit address {:#x}, which does not fit the 32 bit XCP address range",
                            seg_name, seg_addr
                        );
                        continue; // skip 
                    }
                    if seg_index >= 255 {
                        error!("Too many calibration segments, segment index {} does not fit in u8 for segment '{}'", seg_index, seg_name);
                        continue; // skip
                    }
                    if seg_length == 0 {
                        error!("Calibration segment '{}' has zero length, skipped", seg_name);
                        continue; // skip
                    }
                    let res = reg
                        .cal_seg_list
                        .add_cal_seg_by_addr(seg_name.to_string(), *seg_number, 0, seg_addr as u32, seg_length as u32);
                    if let Err(e) = res {
                        error!("Failed to add calibration segment '{}': {}", seg_name, e);
                        continue;
                    }
                }

                // Set memory address for later lookup of potential calibration variables in this segment
                let new_seg = reg.cal_seg_list.find_cal_seg(seg_name).unwrap();
                new_seg.set_mem_addr(seg_addr);

                info!(
                    "Created segment {}: '{}':  addr = 0x{:08X}, size = {}, mem_addr = 0x{:08X}",
                    seg_index, seg_name, new_seg.addr, new_seg.size, new_seg.mem_addr
                );
            } // not already existing
        } // for
        Ok(())
    }

    // Register events from event creation markers (evt__name) in the code
    pub fn register_events(&self, reg: &mut Registry, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("===============================================================");
        info!("Registering event information:");
        info!("===============================================================");

        // Get the address range of the XCP event descriptor memory section (start is 0 if not found)
        let xcp_event_section_addr = self.debug_data.get_event_section_addr();
        let xcp_event_section_end = self
            .debug_data
            .sections
            .get("xcp_evts")
            .map(|(_, end)| *end)
            .or_else(|| self.debug_data.symbol_addresses.get("__stop_xcp_evts").copied());

        // Placeholder ids for events whose id can not be determined from the event descriptor section
        // They must be unique in the registry, counting down from 0xFFFF keeps them out of the range of real event ids
        // The ids are corrected from the XCP server event information when connected (see --fix-a2l)
        let mut next_undefined_event_id: u16 = 0xFFFF;

        // Location "unit:function" of a marker variable for messages
        let location = |v: &VarInfo| -> String {
            let unit_name = self.debug_data.make_simple_unit_name(v.unit_idx).unwrap_or_else(|| v.unit_idx.to_string());
            format!("{}:{}", unit_name, v.function.as_deref().unwrap_or(""))
        };

        // Iterate over variables
        for (var_name, var_infos) in &self.debug_data.variables {
            // Skip standard library variables and system/compiler internals (__<name>)s
            // Skip global XCP variables (gXCP.. and gA2L..)
            if var_name.starts_with("__") || var_name.starts_with("gXcp") || var_name.starts_with("gA2l") {
                continue;
            }

            // Event definitions (by markers from DaqCreateEvent macro)
            // (thread local) static evt__<name>, name is event name
            if let Some(evt_name) = var_name.strip_prefix("evt__") {
                let Some(first) = var_infos.first() else {
                    continue;
                };

                // The DaqCreateEvent macro emits one event descriptor per call site. If the same event is created in several
                // functions or compilation units, the target creates the event once for the first descriptor in the section
                // (XcpInit scans the section in address order), so the definition with the lowest address is used here as well
                let var_info = var_infos.iter().filter(|v| v.address.1 != 0).min_by_key(|v| v.address.1).unwrap_or(first);
                info!(
                    "Event definition for event '{}' found in {}, addr = {:#x}",
                    evt_name,
                    location(var_info),
                    var_info.address.1
                );
                if var_infos.len() > 1 {
                    let others: Vec<String> = var_infos.iter().filter(|v| !std::ptr::eq(*v, var_info)).map(|v| location(v)).collect();
                    warn!(
                        "Event '{}' is defined {} times, using the definition in {} (also defined in {})",
                        evt_name,
                        var_infos.len(),
                        location(var_info),
                        others.join(", ")
                    );
                }

                // Skip if the event already exists in the registry (e.g. from the XCP server event information)
                if reg.event_list.find_event(evt_name, 0).is_some() {
                    continue;
                }

                // Determine the event id from the position of the event descriptor in the event descriptor section
                let addr = var_info.address.1;
                let mut event_id: Option<u16> = None;
                if xcp_event_section_addr > 0 && addr >= xcp_event_section_addr && xcp_event_section_end.is_none_or(|end| addr < end) {
                    let id = ((addr - xcp_event_section_addr) / 16) as u16; // @@@@ size of tXcpEventDescriptor hardcoded
                    if let Some(other) = reg.event_list.find_event_id(id) {
                        warn!("Event id {} of event '{}' is already used by event '{}'", id, evt_name, other.get_name());
                    } else {
                        event_id = Some(id);
                    }
                } else if xcp_event_section_addr > 0 {
                    warn!("Event definition marker of event '{}' at {:#x} is outside the event descriptor section", evt_name, addr);
                }

                match event_id {
                    Some(id) => {
                        reg.event_list.add_event(McEvent::new(evt_name.to_string(), 0, id, 0))?;
                        info!("New event '{}' found: event id = {}", evt_name, id);
                    }
                    None => {
                        // Use a unique placeholder id, it has to be corrected later from the XCP server event information
                        let mut id = next_undefined_event_id;
                        while reg.event_list.find_event_id(id).is_some() {
                            id = id.saturating_sub(1);
                        }
                        next_undefined_event_id = id.saturating_sub(1);
                        reg.event_list.add_event(McEvent::new(evt_name.to_string(), 0, id, 0))?;
                        warn!(
                            "New event '{}' found, created with undefined event id {:#06x}, correct it with the XCP server event information",
                            evt_name, id
                        );
                    }
                }
            }
        }
        Ok(())
    }

    // Find event triggers in the code and register their location (compilation unit, function, CFA offset)
    pub fn register_event_locations(&self, reg: &mut Registry, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("===============================================================");
        info!("Registering event locations:");
        info!("===============================================================");

        // The captured variables of a function do not depend on its stack frame, so the diagnostics about the stack frame below
        // are only relevant when the function has stack relative variables which are not captured
        let captured = self.capture_member_names();

        // Iterate over variables
        for (var_name, var_infos) in &self.debug_data.variables {
            // Skip standard library variables and system/compiler internals (__<name>)s
            // Skip global XCP variables (gXCP.. and gA2L..)
            if var_name.starts_with("__") || var_name.starts_with("gXcp") || var_name.starts_with("gA2l") {
                continue;
            }

            // trg__<event_name> (thread local static, name is event name)
            // Event definitions (thread local static variables)
            if var_name.starts_with("trg__") {
                // One trigger location per event is expected, the location is used to resolve stack relative variables
                if var_infos.len() > 1 {
                    warn!(
                        "Event trigger marker '{}' is defined {} times, only the first definition is used to locate stack variables",
                        var_name,
                        var_infos.len()
                    );
                }
                let Some(var_info) = var_infos.first() else {
                    continue;
                };

                // Get the event name from format  "trg__<tag>__<eventname>" prefix
                let s = var_name.strip_prefix("trg__").unwrap_or("unnamed");
                let mut parts = s.split("__");
                let evt_mode = parts.next().unwrap_or("");
                let evt_name = parts.next().unwrap_or("");

                let evt_unit_idx = var_infos[0].unit_idx;
                let evt_unit_name = if let Some(name) = self.debug_data.make_simple_unit_name(evt_unit_idx) {
                    name
                } else {
                    format!("{evt_unit_idx}")
                };
                let evt_function = if let Some(f) = var_info.function.as_ref() { f.as_str() } else { "" };
                info!("Event {} trigger found in {}:{}, address resolver mode {}", evt_name, evt_unit_name, evt_function, evt_mode);
                // Stack relative variables of this function which are not captured: they are the ones the diagnostics below are about,
                // the captured variables are addressed relative to the capture struct and do not depend on the stack frame
                let has_uncaptured_stack_variables = self.debug_data.variables.iter().any(|(name, var_infos)| {
                    !is_internal_variable(name)
                        && !captured.contains(&(evt_unit_idx, evt_function, name.as_str()))
                        && var_infos
                            .iter()
                            .any(|v| v.address.0 == 2 && v.unit_idx == evt_unit_idx && v.function.as_deref() == Some(evt_function))
                });

                if var_info.inlined {
                    // Each copy of an inlined function has its own stack frame, the offsets of its local variables are not the same
                    // in all of them. The captured variables are not affected, the trigger passes the capture struct of its own copy
                    if has_uncaptured_stack_variables {
                        warn!(
                            "Event '{}' is triggered in function '{}', which the compiler inlined: the stack frame of an inlined function is ambiguous, \
                             its stack relative variables are not registered. Mark the function XCP_NOINLINE (inc/xcplib.h) to measure them, \
                             or capture them with DaqTriggerEventCapture",
                            evt_name, evt_function
                        );
                    } else {
                        info!("Event '{}' is triggered in function '{}', which the compiler inlined", evt_name, evt_function);
                    }
                }

                // Find the event in the registry
                if let Some(_evt) = reg.event_list.find_event(evt_name, 0) {
                    // The stack variables of the function are registered with their DWARF offsets from the frame base of the function,
                    // which has to be the frame address the trigger macro passes to the target, see FrameBase
                    match var_info.frame_base {
                        FrameBase::Cfa | FrameBase::FramePointer => {}
                        other if has_uncaptured_stack_variables => warn!(
                            "Event '{}' is triggered in function '{}' whose frame base is {:?}, not the frame address the trigger passes: \
                             the stack relative variables of this function are not registered",
                            evt_name, evt_function, other
                        ),
                        other => debug!("Event '{}' is triggered in function '{}' whose frame base is {:?}", evt_name, evt_function, other),
                    }
                    if verbose >= 1 {
                        println!("  Event '{}' trigger in function '{}', frame base {:?}", evt_name, evt_function, var_info.frame_base);
                    }

                    // Store the unit and function name of this event trigger (the stack frame offset is always 0, see FrameBase)
                    match reg.event_list.set_event_location(evt_name, evt_unit_idx, evt_function, 0) {
                        Ok(_) => {}
                        Err(e) => {
                            error!("Failed to set event location for event '{}': {}", evt_name, e);
                        }
                    }
                } else {
                    error!("Event '{}' for trigger not found in registry", evt_name);
                }
                continue; // skip this variable
            }
        }
        Ok(())
    }

    // Register variables from the ELF debug information into the registry
    //
    // For every variable name in the debug data and every definition of that name (VarInfo: the same name may exist in several
    // compilation units, functions or namespaces) the steps are:
    //  1. Skip runtime internals, marker variables and names which do not match the --elf-var-filter / --elf-unit-filter options
    //  2. Decide the XCP address and the event from the (address extension, address) pair of the variable:
    //     - absolute address (extension 0): global variables and static locals. The event is the event triggered in the
    //       enclosing function of a static local, otherwise the default event. The A2L name is prefixed with the function
    //       (static locals) or the namespace (globals) if the name is not unique
    //     - stack relative address (extension 2): local variables of a function with an event trigger. The DWARF offset of
    //       the variable relative to the frame base plus the CFA offset of the trigger location is encoded together with
    //       the event id into an XCP "dynamic" address, the A2L name is prefixed with the function name
    //     - anything else (registers, thread local storage, optimized away): skipped
    //  3. If the address lies inside a calibration segment, the variable is a characteristic (parameter) with a segment
    //     relative address, otherwise a measurement
    //  4. Convert the DWARF type to a registry type (basic types, arrays, enums as value tables, structs as typedefs)
    //     and add the instance to the registry
    //
    // default_event: event assigned to global variables and to static variables in functions without an event trigger, None for no event
    pub fn register_variables(
        &self,
        reg: &mut Registry,
        seg_relative: bool,
        verbose: usize,
        unit_idx_limit: (usize, usize),
        name_filter: &str,
        unit_filter: &str,
        default_event: Option<u16>,
    ) -> Result<(), Box<dyn Error>> {
        // Load debug information from the ELF file
        info!("===============================================================");
        info!("Registering variables:");
        info!("===============================================================");

        if let Some(event_id) = default_event {
            match reg.event_list.find_event_id(event_id) {
                Some(event) => info!(
                    "Default event '{}' (id {}) for global and static variables without an event trigger",
                    event.get_name(),
                    event_id
                ),
                None => warn!(
                    "Default event id {} for global and static variables without an event trigger is not defined in the registry",
                    event_id
                ),
            }
        }

        // Compile name filter regex if specified
        let name_regex: Option<Regex> = if name_filter.is_empty() {
            None
        } else {
            match Regex::new(name_filter) {
                Ok(re) => {
                    info!("Variable name filter: '{}'", name_filter);
                    Some(re)
                }
                Err(e) => {
                    return Err(format!("Invalid --elf-var-filter regex '{}': {}", name_filter, e).into());
                }
            }
        };

        // Compile compilation unit filter regex if specified
        let unit_regex: Option<Regex> = if unit_filter.is_empty() {
            None
        } else {
            match Regex::new(unit_filter) {
                Ok(re) => {
                    info!("Compilation unit filter: '{}'", unit_filter);
                    Some(re)
                }
                Err(e) => {
                    return Err(format!("Invalid --elf-unit-filter regex '{}': {}", unit_filter, e).into());
                }
            }
        };

        // Local variables which their function captures at an event trigger are registered from the capture struct in
        // register_captures, the stack variable of the same name is not registered a second time
        let captured = self.capture_member_names();

        // Iterate over variables
        for (var_name, var_infos) in &self.debug_data.variables {
            // Skip the internal and marker variables, they never become A2L objects
            if is_internal_variable(var_name) {
                continue;
            }

            // Apply name filter
            if let Some(ref re) = name_regex {
                if !re.is_match(var_name) {
                    continue;
                }
            }

            if var_infos.is_empty() {
                warn!("Variable '{}' has no variable info", var_name);
            }

            let mut a2l_name = var_name.to_string();
            let mut xcp_event_id: Option<u16>;
            // Count the definitions of this name within the compilation unit limit (--elf-unit-limit), including definitions without
            // an address. More than one definition means the A2L name has to be qualified to be unique
            let count = var_infos.iter().filter(|v| v.unit_idx >= unit_idx_limit.0 && v.unit_idx <= unit_idx_limit.1).count();
            // Count the distinct global or static variables with this name by their address
            // (declarations of the same variable in several compilation units resolve to the same address, local variables have no address)
            let mut addresses: Vec<u64> = var_infos
                .iter()
                .filter(|v| v.unit_idx >= unit_idx_limit.0 && v.unit_idx <= unit_idx_limit.1 && v.address.0 == 0 && v.address.1 != 0)
                .map(|v| v.address.1)
                .collect();
            addresses.sort_unstable();
            addresses.dedup();
            let defined_count = addresses.len();

            // Process all variables with this name in different scopes and namespaces
            for var_info in var_infos {
                // @@@@ TODO: Create only variables from specified compilation unit
                if var_info.unit_idx < unit_idx_limit.0 || var_info.unit_idx > unit_idx_limit.1 {
                    continue;
                }

                // Apply compilation unit filter
                if let Some(ref re) = unit_regex {
                    let cu_name = self.debug_data.make_simple_unit_name(var_info.unit_idx).unwrap_or_else(|| format!("{}", var_info.unit_idx));
                    if !re.is_match(&cu_name) {
                        continue;
                    }
                }

                let var_function = var_info.function.as_ref().map(|f| f.as_str());

                // Address encoder: the (address extension, address) pair from the DWARF reader (see VarInfo in debuginfo/mod.rs)
                // becomes the memory address which is checked against the calibration segments and then the XCP address
                let mem_addr_ext: u8 = var_info.address.0;
                let mem_addr: u64 = if mem_addr_ext == 0 {
                    // Encode absolute addressing mode
                    if var_info.address.1 == 0 {
                        debug!("Variable '{}' not registered, no address", var_name);
                        continue; // skip this variable
                    } else if var_info.address.1 >= 0xFFFFFFFF {
                        warn!(
                            "Global variable '{}' not registered, address {:#x} out of the 32 bit XCP address range",
                            var_name, var_info.address.1
                        );
                        continue; // skip this variable
                    } else {
                        // Find an event triggered in the function
                        if let Some(var_function_name) = var_function {
                            if let Some(event) = reg.event_list.find_event_by_location(var_info.unit_idx, var_function_name) {
                                xcp_event_id = Some(event.id);
                                info!("Static variable '{}' local to function '{:?}', event id = {}", var_name, var_function, event.id);
                            } else {
                                info!(
                                    "Static variable '{}' local to function '{:?}', no event found in this function, event id = {:?}",
                                    var_name, var_function, default_event
                                );
                                xcp_event_id = default_event;
                            }
                        } else {
                            info!("Global variable '{}', event id = {:?}", var_name, default_event);
                            xcp_event_id = default_event;
                        }

                        // Multiple variables with this name: local static variables are prefixed with the function name,
                        // global variables defined in several namespaces with their namespace (motor_control.input, valve_control.input)
                        if count > 1 {
                            if let Some(f) = var_function {
                                a2l_name = format!("{}.{}", f, var_name);
                            } else if defined_count > 1 && !var_info.namespaces.is_empty() {
                                a2l_name = format!("{}.{}", var_info.namespaces.join("."), var_name);
                            } else {
                                a2l_name = var_name.to_string();
                            }
                        }
                        var_info.address.1
                    }
                } else if mem_addr_ext == 2 {
                    // Encode stack relative addressing mode
                    // The DWARF reader evaluated the location of the variable with a dummy frame base of 0x80000000 (see evaluate_exprloc
                    // in attributes.rs), so address - 0x80000000 is the offset of the variable from the frame base of its function.
                    // The event trigger in the function passes this frame base to the target as frame address (see FrameBase), so the
                    // offset is used as it is. The variable is only measurable at the trigger point, on the event of its function
                    // Find an event id for this local variable
                    if var_function.is_none() {
                        warn!("Local variable '{}' skipped - function name is required for relative addressing mode", var_name);
                        continue;
                    } else {
                        let var_function_name = var_function.unwrap();
                        if captured.contains(&(var_info.unit_idx, var_function_name, var_name.as_str())) {
                            debug!(
                                "Local variable '{}' in function '{}' is captured, the stack variable is not registered",
                                var_name, var_function_name
                            );
                            continue;
                        }
                        // Each copy of an inlined function has its own stack frame layout and the event may be triggered from any copy,
                        // so there is no stack frame relative address which is valid for all of them, see register_event_locations
                        if var_info.inlined {
                            debug!(
                                "Local variable '{}' of the inlined function '{}' is not registered, the stack frame of an inlined function is ambiguous",
                                var_name, var_function_name
                            );
                            continue;
                        }
                        if !matches!(var_info.frame_base, FrameBase::Cfa | FrameBase::FramePointer) {
                            debug!("Local variable '{}' in function {:?} skipped, frame base {:?}", var_name, var_function, var_info.frame_base);
                            continue;
                        }
                        if let Some(event) = reg.event_list.find_event_by_location(var_info.unit_idx, var_function_name) {
                            // Set the event id for this function
                            // Prefix the variable with the function name
                            xcp_event_id = Some(event.id);
                            if let Some(f) = var_function {
                                a2l_name = format!("{}.{}", f, var_name);
                            } else {
                                a2l_name = var_name.to_string();
                            }
                            info!(
                                "Local variable '{}' in function '{:?}', event id = {:?}, offset = {}",
                                var_name,
                                var_function,
                                xcp_event_id,
                                (var_info.address.1 as i64 - 0x80000000)
                            );

                            // @@@@ TODO: Create functions instead of constants for relative address encoding
                            // Encode dyn addressing mode A2L/XCP address from offset and event id: the low XCP_ADDR_EXT_DYN_OFFSET_BITS bits
                            // are the offset, biased by XCP_ADDR_EXT_DYN_OFFSET_OFFSET so that negative offsets (below the frame address) fit,
                            // the high bits are the event id. The target adds the frame address it received from the trigger of this event
                            let offset: i64 = var_info.address.1 as i64 - 0x80000000;
                            if offset < -(McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64)
                                || offset > (McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK as i64 - McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64)
                            {
                                warn!(
                                    "Local variable '{}' skipped, has offset {} which does not fit the XCP dynamic addressing mode range",
                                    var_name, offset
                                );
                                continue; // skip this variable
                            }

                            (((offset + McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64) as u64) & McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK as u64)
                                | ((event.id as u64) << McAddress::XCP_ADDR_EXT_DYN_OFFSET_BITS)
                        } else {
                            warn!(
                                "Local variable '{}' in function {:?} skipped, could not find event for dyn addressing mode",
                                var_name, var_function
                            );
                            continue; // skip this variable
                        }
                    }
                }
                // @@@@ TODO: Handle other address extensions
                else {
                    debug!("Variable '{}' skipped, has unsupported address extension {:#x}", var_name, mem_addr_ext);
                    continue; // skip this variable
                };

                // Check if the absolute address is in a calibration segment or block
                // For segments with segment relative and absolute addressing mode, we always need to check with the memory address of the segment, not the a2l address
                let seg_name = reg.cal_seg_list.find_cal_seg_by_mem_address(mem_addr);
                let (object_type, mc_addr) = if let Some(seg_name) = seg_name {
                    let seg = reg.cal_seg_list.find_cal_seg(&seg_name).unwrap();
                    let offset: u16 = (mem_addr - seg.mem_addr).try_into().unwrap();
                    // Address extension of characteristics in memory segments is always 0, hardcoded here
                    // @@@@ NOTE: This might change in the future
                    (McObjectType::Characteristic, McAddress::new_a2l(seg.addr + offset as u32, 0))
                } else {
                    // Create a McAddress with event id, mem_addr is relative or absolute
                    // @@@@ TODO: Not implemented dependency on target addressing scheme
                    // Address extension might be 0, 1, 2 depending on the target addressing scheme
                    let addr_ext = if seg_relative && mem_addr_ext == 0 {
                        1 // set to absolute addressing mode
                    } else {
                        mem_addr_ext
                    };
                    if let Some(xcp_event_id) = xcp_event_id {
                        (McObjectType::Measurement, McAddress::new_a2l_with_event(xcp_event_id, mem_addr as u32, addr_ext))
                    } else {
                        (McObjectType::Measurement, McAddress::new_a2l(mem_addr as u32, addr_ext))
                    }
                };

                // Register measurement variable if possible
                if let Some(type_info) = self.debug_data.types.get(&var_info.typeref) {
                    // Register supported variable types in the registry
                    let type_size = type_info.get_size();
                    let type_name = &type_info.name;
                    match &type_info.datatype {
                        DbgDataType::Uint8
                        | DbgDataType::Uint16
                        | DbgDataType::Uint32
                        | DbgDataType::Uint64
                        | DbgDataType::Sint8
                        | DbgDataType::Sint16
                        | DbgDataType::Sint32
                        | DbgDataType::Sint64
                        | DbgDataType::Float
                        | DbgDataType::Double
                        | DbgDataType::Array { .. }
                        | DbgDataType::Struct { .. } => {
                            if verbose >= 2 {
                                print!(
                                    "  Add {} instance for {}: addr = {}:0x{:08x}",
                                    if object_type == McObjectType::Characteristic { "characteristic" } else { "measurement" },
                                    a2l_name,
                                    mem_addr_ext,
                                    mem_addr
                                );
                                if verbose >= 3 {
                                    println!(" type = {}", type_info);
                                } else {
                                    println!();
                                }
                            }
                            let dim_type = self.get_dim_type(reg, type_info, object_type);
                            let res = reg.instance_list.add_instance(a2l_name.clone(), dim_type, McSupportData::new(object_type), mc_addr);
                            match res {
                                Ok(_) => {
                                    if verbose >= 1 {
                                        println!(
                                            "        Registered variable '{}' type_name = '{}', size = {}, event_id = {:?}",
                                            a2l_name,
                                            type_name.as_ref().unwrap_or(&"<unnamed>".to_string()),
                                            type_size,
                                            xcp_event_id
                                        );
                                    }
                                }
                                Err(e) => {
                                    error!("Failed to register variable '{}': {}", a2l_name, e);
                                }
                            }
                        }
                        // Special case for enum types, which are represented as integer types with enumerators described as special unit format "value "NAME" value "NAME" ...".
                        // We convert the enumerators to a unit string and store it in the McSupportData for the instance.
                        DbgDataType::Enum { size, signed, enumerators } => {
                            if verbose >= 2 {
                                print!(
                                    "  Add {} instance for enum {}: addr = {}:0x{:08x}, size = {}, signed = {}, enumerators = {:?}",
                                    if object_type == McObjectType::Characteristic { "characteristic" } else { "measurement" },
                                    a2l_name,
                                    mem_addr_ext,
                                    mem_addr,
                                    size,
                                    signed,
                                    enumerators
                                );
                                if verbose >= 3 {
                                    println!(" type = {}", type_info);
                                } else {
                                    println!();
                                }
                            }
                            let dim_type = self.get_dim_type(reg, type_info, object_type);
                            let unit_string = enumerators_to_unit_string(enumerators);
                            let mc_support_data = if let Some(unit_str) = unit_string {
                                McSupportData::new(object_type).set_unit(unit_str)
                            } else {
                                warn!("Enum variable '{}' has no enumerators, no conversion table generated", a2l_name);
                                McSupportData::new(object_type)
                            };
                            let res = reg.instance_list.add_instance(a2l_name.clone(), dim_type, mc_support_data, mc_addr);
                            match res {
                                Ok(_) => {
                                    if verbose >= 1 {
                                        println!(
                                            "Registered enum variable '{}' with type '{}', size = {}, event id = {:?}, unit = {:?}",
                                            a2l_name,
                                            type_name.as_ref().unwrap_or(&"<unnamed>".to_string()),
                                            type_size,
                                            xcp_event_id,
                                            enumerators_to_unit_string(enumerators)
                                        );
                                    }
                                }
                                Err(e) => {
                                    error!("Failed to register variable '{}': {}", a2l_name, e);
                                }
                            }
                        }

                        _ => {
                            warn!("Variable '{}' has unsupported type: {}", var_name, type_info);
                        }
                    }
                } else {
                    warn!("TypeRef {} of variable '{}' not found in debug info", var_info.typeref, var_name);
                }
            }
        } // var_infos
        Ok(())
    }

    // Names of the captured variables: (compilation unit, function, variable name) of every member of every capture struct
    fn capture_member_names(&self) -> HashSet<(usize, &str, &str)> {
        let mut names = HashSet::new();
        for (var_name, var_infos) in &self.debug_data.variables {
            if !var_name.starts_with("cap__") {
                continue;
            }
            for var_info in var_infos {
                if let Some(function) = var_info.function.as_deref()
                    && let Some(type_info) = self.debug_data.types.get(&var_info.typeref)
                    && let DbgDataType::Struct { members, .. } = &type_info.datatype
                {
                    for member_name in members.keys() {
                        names.insert((var_info.unit_idx, function, capture_member_variable_name(member_name)));
                    }
                }
            }
        }
        names
    }

    /// Register the captured local variables of the event triggers (DaqTriggerEventCapture in inc/xcplib.h)
    ///
    /// The macro declares a struct cap__<event> in the function, copies the given variables into it when the event is triggered and
    /// passes the address of the struct to the target as the base address of address extension 3. So the XCP address of a captured
    /// variable is the offset of its member in the struct, and the variables themselves may stay in registers.
    /// The members are registered with the name of the original variable, qualified with the function like a local variable
    /// (foo.counter), and with the event of the trigger as fixed event. Unlike stack frame relative variables they do not depend on
    /// the stack frame of the function, so the function may be inlined
    pub fn register_captures(&self, reg: &mut Registry, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("===============================================================");
        info!("Registering captured variables:");
        info!("===============================================================");

        for (var_name, var_infos) in &self.debug_data.variables {
            let Some(event_name) = var_name.strip_prefix("cap__") else {
                continue;
            };
            let Some(var_info) = var_infos.first() else {
                continue;
            };
            // The copies of an inlined function have the same capture struct and the same function, they are one capture site
            let sites: HashSet<(usize, Option<&str>)> = var_infos.iter().map(|v| (v.unit_idx, v.function.as_deref())).collect();
            if sites.len() > 1 {
                warn!(
                    "Event '{}' is triggered with a capture in {} functions, only the one in function {:?} is used, the others are lost",
                    event_name,
                    sites.len(),
                    var_info.function
                );
            }
            let Some(function) = var_info.function.as_deref() else {
                warn!("Capture struct '{}' is not in a function, the captured variables are not registered", var_name);
                continue;
            };

            // The event of the trigger is the fixed event of all captured variables
            let Some(event_id) = reg.event_list.find_event(event_name, 0).map(|event| event.get_id()) else {
                error!(
                    "Event '{}' of the capture in function '{}' is not defined, the captured variables are not registered",
                    event_name, function
                );
                continue;
            };

            // The members of the capture struct are the captured variables
            let Some(type_info) = self.debug_data.types.get(&var_info.typeref) else {
                warn!("Capture struct '{}' in function '{}' has no type information", var_name, function);
                continue;
            };
            let DbgDataType::Struct { members, .. } = &type_info.datatype else {
                warn!("Capture struct '{}' in function '{}' is not a struct: {}", var_name, function, type_info);
                continue;
            };
            info!("Capture of event '{}' in function '{}' with {} variables", event_name, function, members.len());

            for (member_name, (member_type, offset)) in members {
                // The macro appends one underscore to the name of the variable, see XCP_CAP_MEMBER in inc/xcplib.h
                let var_name = capture_member_variable_name(member_name);
                let a2l_name = format!("{}.{}", function, var_name);

                // A member of struct or union type refers to the loaded type instead of repeating it, see the type reader
                let member_type = match &member_type.datatype {
                    DbgDataType::TypeRef(type_ref, _) => match self.debug_data.types.get(type_ref) {
                        Some(type_info) => type_info,
                        None => {
                            warn!("Captured variable '{}' skipped, its type {} is not in the debug information", a2l_name, type_ref);
                            continue;
                        }
                    },
                    _ => member_type,
                };

                // The XCP address is the offset of the member in the capture struct, with the event id in the high bits, the target
                // adds the address of the struct it received from the trigger of this event (address extension 3)
                if *offset > McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK as u64 {
                    warn!("Captured variable '{}' skipped, its offset {} in the capture struct is out of range", a2l_name, offset);
                    continue;
                }
                let a2l_addr = (*offset & McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK as u64) | ((event_id as u64) << McAddress::XCP_ADDR_EXT_DYN_OFFSET_BITS);
                let mc_addr = McAddress::new_a2l_with_event(event_id, a2l_addr as u32, XCP_ADDR_EXT_CAPTURE);

                let mc_support_data = match &member_type.datatype {
                    DbgDataType::Uint8
                    | DbgDataType::Uint16
                    | DbgDataType::Uint32
                    | DbgDataType::Uint64
                    | DbgDataType::Sint8
                    | DbgDataType::Sint16
                    | DbgDataType::Sint32
                    | DbgDataType::Sint64
                    | DbgDataType::Float
                    | DbgDataType::Double
                    | DbgDataType::Array { .. }
                    | DbgDataType::Struct { .. } => McSupportData::new(McObjectType::Measurement),
                    // Enums are measured as their integer type with the enumerators as conversion table, see register_variables
                    DbgDataType::Enum { enumerators, .. } => match enumerators_to_unit_string(enumerators) {
                        Some(unit_string) => McSupportData::new(McObjectType::Measurement).set_unit(unit_string),
                        None => McSupportData::new(McObjectType::Measurement),
                    },
                    _ => {
                        warn!("Captured variable '{}' has unsupported type: {}", a2l_name, member_type);
                        continue;
                    }
                };

                if verbose >= 2 {
                    println!("  Add measurement instance for captured {}: addr = {}:0x{:08x}", a2l_name, XCP_ADDR_EXT_CAPTURE, a2l_addr);
                }
                let dim_type = self.get_dim_type(reg, member_type, McObjectType::Measurement);
                match reg.instance_list.add_instance(a2l_name.clone(), dim_type, mc_support_data, mc_addr) {
                    Ok(_) => info!("Captured variable '{}' in function '{}', event id = {}, offset = {}", var_name, function, event_id, offset),
                    Err(e) => error!("Failed to register captured variable '{}': {}", a2l_name, e),
                }
            }
        }
        Ok(())
    }

    /// Read XCP_UNIT / XCP_LIMITS / XCP_COMMENT metadata from the xcp_meta ELF section
    /// and apply them to already-registered instances in the registry.
    /// Must be called after register_variables.
    pub fn register_metadata(&self, reg: &mut Registry, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("===============================================================");
        info!("Registering metadata from xcp_meta section:");
        info!("===============================================================");

        // Get meta_base_addr and meta_end
        let (meta_base_addr, meta_data) = match &self.debug_data.xcp_meta_data {
            Some(data) => data,
            None => {
                warn!("No xcp_meta section found, skipping metadata registration");
                return Ok(());
            }
        };
        let meta_end = meta_base_addr + meta_data.len() as u64;

        // The names of all functions, to keep a marker at file scope away from the local variables of a function, see below
        let function_names: HashSet<&str> = self.debug_data.variables.values().flatten().filter_map(|v| v.function.as_deref()).collect();
        let is_le = self.debug_data.is_little_endian;
        assert!(is_le, "Big endian is not supported for meta data registration");

        // Search for metadata variables (xcp_meta__<kind>__<base_name>) in the debug data
        // Add meta data to the registry instances
        for (var_name, var_infos) in &self.debug_data.variables {
            // Only process metadata variables
            let Some(rest) = var_name.strip_prefix("xcp_meta__") else {
                continue;
            };
            let Some((kind, base_name)) = rest.split_once("__") else {
                warn!("Unexpected xcp_meta__ variable name format: '{}'", var_name);
                continue;
            };
            if var_infos.is_empty() {
                continue;
            }

            // Every marker with this name is processed separately: markers without scope prefix in different namespaces or functions
            // (XCP_COMMENT(input, ...) in namespace motor_control and in namespace valve_control) share the DWARF name.
            // A declaration entry without address (GCC declaration/definition pairs) is skipped.
            let markers: Vec<&VarInfo> = var_infos.iter().filter(|v| v.address.0 == 0 && v.address.1 != 0).collect();
            if markers.is_empty() {
                warn!("Metadata variable '{}' address is 0", var_name);
                continue;
            }
            for marker in markers {
                // Get the section offset of the metadata variable
                let var_addr = marker.address.1;
                if var_addr < *meta_base_addr || var_addr >= meta_end {
                    warn!("Metadata variable '{}' address 0x{:08X} is outside xcp_meta section", var_name, var_addr);
                    continue;
                }
                let offset = (var_addr - meta_base_addr) as usize;

                // Decode base_name: __ is the path separator, e.g. "params__delay_us" means
                // instance "params", field "delay_us".  Replace all __ with . to get the dot path.
                let dot_path = base_name.replace("__", ".");

                // The name is looked up qualified with the scope of the metadata marker first, then unqualified:
                // a marker in the same namespace as the variable (XCP_COMMENT(input, ...) in namespace motor_control) refers to motor_control.input,
                // a marker in the same function as a local variable (XCP_COMMENT(counter, ...) in foo) refers to foo.counter.
                // The unqualified lookup keeps markers with an explicit prefix (foo__counter -> foo.counter), markers for the field of
                // an instance (params__delay_us -> params.delay_us), markers in a function for a variable which is not one of its own
                // (a global variable used there) and markers at file scope working.
                // It is left out when the function of the marker has a variable of this name: the marker belongs to that variable and
                // must not be applied to a global variable of the same name when the local variable is not measurable and has no instance
                let scope = match &marker.function {
                    Some(function) => function.clone(),
                    None => marker.namespaces.join("."),
                };
                let mut candidates: Vec<String> = Vec::with_capacity(2);
                if !scope.is_empty() {
                    candidates.push(format!("{scope}.{dot_path}"));
                }
                let names_own_variable = marker.function.is_some()
                    && self
                        .debug_data
                        .variables
                        .get(base_name)
                        .is_some_and(|vars| vars.iter().any(|v| v.function == marker.function && v.unit_idx == marker.unit_idx));
                if !names_own_variable {
                    candidates.push(dot_path);
                }

                let mut applied = false;
                for path in &candidates {
                    // Path A — typedef field metadata (instance + dot-separated field path)
                    // Applies when base_name contains __, i.e. it encodes a struct field reference.
                    // The instance name may contain dots itself (namespace qualified instances like motor_control.input), so every
                    // split into instance name and field path is tried, the longest instance name first.
                    // Uses set_instance_field_support_data which walks the typedef tree.
                    for (split, _) in path.rmatch_indices('.') {
                        let (instance_name, field_path) = (&path[..split], &path[split + 1..]);
                        if apply_field_metadata(reg, var_name, kind, instance_name, field_path, meta_data, offset, is_le, verbose) {
                            applied = true;
                            break;
                        }
                    }

                    // Path B — direct instance metadata (simple variable or flattened typedef)
                    // The instance with exactly this name first: a marker in a function refers to a variable of this function only,
                    // the qualified candidate covers a prefixed static local (foo.static_counter), the exact path an unprefixed one
                    // (static_counter) or an explicit prefix (foo__counter). A marker at file scope names the global variable
                    // (counter) and not the local variables of the same name in functions (foo.counter, task.counter).
                    let escaped = path.replace('.', "\\.");
                    let mut names: Vec<String> = reg.instance_list.find_instances_regex(&format!(r"^{escaped}$"), McObjectType::Unspecified, None);

                    // A marker at file scope with no instance of this name also matches an instance whose name ends with ".{path}",
                    // which is how a marker reaches a field of a typedef instance (delay_us -> params.delay_us). Instances qualified
                    // with a function name are excluded, the local variables of a function are named by a marker in that function
                    if names.is_empty() && marker.function.is_none() {
                        names = reg.instance_list.find_instances_regex(&format!(r"^(.*\.)?{escaped}$"), McObjectType::Unspecified, None);
                        names.retain(|name| !name.split_once('.').is_some_and(|(prefix, _)| function_names.contains(prefix)));
                    }
                    for name in &names {
                        if let Some(inst) = reg.instance_list.get_instance_mut(name, None) {
                            apply_instance_metadata(inst, kind, meta_data, offset, is_le);
                            applied = true;
                            info!("Metadata {} {} applied to instance '{}'", kind, var_name, name);
                        }
                    }
                    if applied {
                        break;
                    }
                }

                if !applied {
                    warn!("Metadata '{}': no matching registry entry for '{}'", var_name, candidates.join("' or '"));
                }
            }
        }

        Ok(())
    }
}

// Convert an enumerators vec to the XCP/A2L COMPU_VTAB string format: `value "NAME" value "NAME" ...`
fn enumerators_to_unit_string(enumerators: &[(String, i64)]) -> Option<String> {
    if enumerators.is_empty() {
        return None;
    }
    let parts: Vec<String> = enumerators.iter().map(|(name, value)| format!(r#"{} "{}""#, value, name)).collect();
    Some(parts.join(" "))
}

// Read a null-terminated UTF-8 string from a byte slice at a given offset
fn read_cstr_at(data: &[u8], offset: usize) -> Option<String> {
    if offset >= data.len() {
        return None;
    }
    let end = data[offset..].iter().position(|&b| b == 0).map(|p| offset + p).unwrap_or(data.len());
    String::from_utf8(data[offset..end].to_vec()).ok()
}

// Path A helper: apply metadata to a typedef field via set_instance_field_support_data.
// Returns true if the metadata was successfully applied.
fn apply_field_metadata(
    reg: &mut Registry,
    var_name: &str,
    kind: &str,
    instance_name: &str,
    field_path: &str,
    meta_data: &[u8],
    offset: usize,
    is_le: bool,
    verbose: usize,
) -> bool {
    let support_data = match kind {
        "unit" | "comment" => {
            let Some(value) = read_cstr_at(meta_data, offset) else {
                warn!("Failed to read string for metadata variable '{}'", var_name);
                return false;
            };
            let sd = McSupportData::new(McObjectType::Unspecified);
            if kind == "unit" { sd.set_unit(value) } else { sd.set_comment(value) }
        }
        "min" | "max" => {
            if offset + 8 > meta_data.len() {
                warn!("Not enough bytes for f64 at offset {} in xcp_meta for '{}'", offset, var_name);
                return false;
            }
            let bytes: [u8; 8] = meta_data[offset..offset + 8].try_into().unwrap();
            let value = if is_le { f64::from_le_bytes(bytes) } else { f64::from_be_bytes(bytes) };
            let sd = McSupportData::new(McObjectType::Unspecified);
            if kind == "min" { sd.set_min(Some(value)) } else { sd.set_max(Some(value)) }
        }
        "read_write" => {
            let sd = McSupportData::new(McObjectType::Unspecified);
            sd.set_read_write()
        }
        _ => {
            warn!("Unknown metadata kind '{}' in variable '{}'", kind, var_name);
            return false;
        }
    };

    match reg.set_instance_field_support_data(instance_name, field_path, support_data) {
        Ok(()) => {
            info!("  Metadata {} applied to typedef field '{}.{}'", var_name, instance_name, field_path);
            true
        }
        Err(RegistryError::NotFound(_)) => false, // no such instance or field — not an error, Path B will try
        Err(e) => {
            warn!("Metadata '{}': set_instance_field_support_data failed: {}", var_name, e);
            false
        }
    }
}

// Path B helper: apply metadata directly to an McInstance's mc_support_data.
fn apply_instance_metadata(inst: &mut xcp_registry::McInstance, kind: &str, meta_data: &[u8], offset: usize, is_le: bool) {
    match kind {
        "read_write" => {
            inst.mc_support_data.update_qualifier(McObjectQualifier::ReadWrite);
        }
        "unit" | "comment" => {
            if let Some(value) = read_cstr_at(meta_data, offset) {
                if kind == "unit" {
                    inst.mc_support_data.update_unit(value);
                } else {
                    inst.mc_support_data.update_comment(value);
                }
            }
        }
        "min" | "max" => {
            if offset + 8 <= meta_data.len() {
                let bytes: [u8; 8] = meta_data[offset..offset + 8].try_into().unwrap();
                let value = if is_le { f64::from_le_bytes(bytes) } else { f64::from_be_bytes(bytes) };
                if kind == "min" {
                    inst.mc_support_data.update_min(Some(value));
                } else {
                    inst.mc_support_data.update_max(Some(value));
                }
            }
        }
        _ => {}
    }
}

//------------------------------------------------------------------------
// Tests

#[cfg(test)]
mod test;
