//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module elf_reader
// Read ELF files and extract debug information

#![allow(clippy::collapsible_else_if)]

use indexmap::IndexMap;
use std::error::Error;
use std::ffi::OsStr;

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use xcp_lite::registry::{McAddress, McDimType, McEvent, McObjectType, McSupportData, McValueType, Registry};

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
    - All instances get the correct fixed event id, if there is one in their scope, default is event id 0
    - Event compilation unit, function and CFA is detected to enable local variable access

    Todo:
    - test arrays and nested structs

    - No DW_AT_location means optimized away

Detect TLS Variables:

TLS Variables:
Check for missing DW_AT_location + thread-local context
Look for variables referencing .tdata/.tbss sections
Parse DW_TAG_variable with TLS-specific location expressions
DW_OP_form_tls_address, etc





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
use debuginfo::{DbgDataType, DebugData, TypeInfo};

//------------------------------------------------------------------------
//  ELF reader and A2L creator

pub(crate) struct ElfReader {
    pub(crate) debug_data: DebugData,
}

impl ElfReader {
    pub fn new(file_name: &str, verbose: usize, unit_idx_limit: usize) -> Option<ElfReader> {
        // Load debug information from the ELF file
        info!("Loading debug information from ELF file: {}", file_name);
        let debug_data = DebugData::load_dwarf(OsStr::new(file_name), verbose, unit_idx_limit);
        match debug_data {
            Ok(debug_data) => Some(ElfReader { debug_data }),
            Err(e) => {
                error!("Failed to load debug info from '{}': {}", file_name, e);
                None
            }
        }
    }

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
            DbgDataType::Struct { size, members } => {
                if let Some(type_name) = &type_info.name {
                    // Register the typedef struct for the value type typedef
                    if let Some(name) = type_info.name.as_ref() {
                        let _ = self.register_struct(reg, object_type, name.clone(), *size as usize, members);
                    }
                    McValueType::new_typedef(type_name.clone())
                } else {
                    warn!("Struct type without name in get_field_type");
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

            DbgDataType::Pointer(pointee, size) => {
                if *size == 4 {
                    McValueType::Ulong
                } else if *size == 8 {
                    McValueType::Ulonglong
                } else {
                    warn!("Unsupported pointer size {} in get_field_type", size);
                    McValueType::Ulonglong
                }
            }

            // These type are not a supported value type
            // DbgDataType::Bitfield | DbgDataType::Pointer | DbgDataType::FuncPtr | DbgDataType::Class | DbgDataType::Union | DbgDataType::Enum  | DbgDataType::Other =>
            _ => {
                error!("Unsupported type in get_field_type: {:?}", &type_info.datatype);
                assert!(false, "Unsupported type in get_field_type: {:?}", &type_info.datatype);
                McValueType::Ubyte
            }
        }
    }

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

    fn register_struct(
        &self,
        reg: &mut Registry,
        object_type: McObjectType,
        type_name: String,
        size: usize,
        members: &IndexMap<String, (TypeInfo, u64)>,
    ) -> Result<(), Box<dyn Error>> {
        let typedef = reg.add_typedef(type_name.clone(), size)?;
        for (field_name, (type_info, field_offset)) in members {
            let field_dim_type = self.get_dim_type(reg, type_info, object_type);
            let field_mc_support_data = McSupportData::new(object_type);
            reg.add_typedef_field(&type_name, field_name.clone(), field_dim_type, field_mc_support_data, (*field_offset).try_into().unwrap())?;
        }
        Ok(())
    }

    pub fn get_target_signature(&self) -> Option<&str> {
        // Iterate over variables and look for XCPlite addressing mode marker
        for (var_name, var_infos) in &self.debug_data.variables {
            if !var_name.starts_with("XCPLITE__") {
                continue;
            }
            if let Some(signature) = var_name.strip_prefix("XCPLITE__") {
                return Some(signature);
            }
        }
        return None;
    }

    pub fn register_segments_and_events(&self, reg: &mut Registry, segment_relative: bool, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("Registering segment and event information:");

        let mut next_event_id: u16 = 0;
        let mut next_segment_number: u16 = 0;

        // Iterate over variables
        for (var_name, var_infos) in &self.debug_data.variables {
            // Skip standard library variables and system/compiler internals (__<name>)s
            // Skip global XCP variables (gXCP.. and gA2L..)
            if var_name.starts_with("__") || var_name.starts_with("gXcp") || var_name.starts_with("gA2l") {
                continue;
            }

            // From CalSegCreate macro
            // cal__<name> (local scope static, name is calibration segment name and type name)
            // Calibration segment definition
            if var_name.starts_with("cal__") {
                assert!(var_infos.len() == 1); // @@@@ Only one definition allowed
                let var_info = &var_infos[0];
                let function_name = if let Some(f) = var_info.function.as_ref() { f.as_str() } else { "" };
                let unit_idx = var_info.unit_idx;
                let unit_name = if let Some(name) = self.debug_data.make_simple_unit_name(unit_idx) {
                    name
                } else {
                    format!("{unit_idx}")
                };

                // remove the "cal__" prefix to get the segment name
                let seg_name = var_name.strip_prefix("cal__").unwrap_or(var_name);
                info!(
                    "Calibration segment definition marker variable 'cal__{}' for segment '{}' found in {}:'{}'",
                    seg_name, seg_name, unit_name, function_name
                );

                // If it is not the epk segment
                if seg_name == "epk" {
                    info!("  'epk' calibration segment is predefined, skipping");
                    // Now we know, there is an epk segment
                    // Set the EPK information in the registry
                    // @@@@ TODO: How to determine the EPK string
                    reg.application.set_version("<unknown>", 0x80000000);
                    continue;
                } else {
                    // Lookup the reference page variable (naming convention is segment name!) information
                    let seg_var_info = if let Some(x) = self.debug_data.variables.get(seg_name) {
                        if x.len() != 1 {
                            error!("Calibration segment reference page variable '{}' has {} definitions, expected 1", seg_name, x.len());
                            continue;
                        }
                        &x[0]
                    } else {
                        error!("Could not find calibration segment reference page variable '{}'", seg_name);
                        continue;
                    };

                    // Determine segment length
                    let length = {
                        if let Some(type_info) = self.debug_data.types.get(&seg_var_info.typeref) {
                            info!(
                                "  Segment '{}' type information found, type={}, size = {}",
                                seg_name,
                                type_info.name.as_ref().map_or("<unnamed>", |s| s.as_str()),
                                type_info.get_size()
                            );
                            if verbose >= 1 {
                                info!("{}", type_info);
                            }
                            type_info.get_size()
                        } else {
                            error!("Could not determine length type for segment {}", seg_name);
                            0
                        }
                    };

                    // Determine segment address
                    let (addr_ext, addr) = (seg_var_info.address.0, seg_var_info.address.1.try_into().unwrap()); // @@@@ TODO: Handle 64 bit addresses and signed relative

                    if !(length > 0 && addr > 0 && addr_ext == 0) {
                        error!(
                            "Calibration segment from cal_<name> '{}' not found, has invalid address {:#x} or size {:#x}, skipped",
                            seg_name, addr, length
                        );
                        continue; // skip this variable
                    }

                    info!(
                        "  Segment '{}' default page variable found in debug data: Address = {}:{:#x}, Size = {:#x}",
                        seg_name, addr_ext, addr, length
                    );

                    // Find the segment by name in the registry
                    if let Some(reg_seg) = reg.cal_seg_list.find_cal_seg(seg_name) {
                        info!("Calibration segment '{}' {}:0x{:08X} found in registry", seg_name, reg_seg.addr_ext, reg_seg.addr);
                        // Segment relative addressing mode
                        if reg_seg.addr == 0x80000000 + ((reg_seg.index as u32) << 16) {
                            info!("  with segment relative addressing");
                            // Check if length matches
                            if reg_seg.size == length as u32 {
                                reg_seg.set_mem_addr(addr);
                                info!("  matches existing registry entry");
                            } else {
                                warn!("Calibration segment '{}' length does not match existing registry entry", seg_name);
                                unimplemented!();
                            }
                        }
                        // Absolute addressing mode
                        else {
                            // Check if address and length match
                            if reg_seg.mem_addr == addr && reg_seg.size == length as u32 {
                                info!(" matches existing registry entry");
                            } else {
                                warn!("Calibration segment '{}' does not match existing registry entry, registry information updated", seg_name);
                                unimplemented!();
                            }
                        }

                        continue; // segment already exists, leave it as it is
                    }
                    // If not create the segment
                    // Use segment relative or absolute addressing mode
                    else {
                        info!("Calibration segment '{}' not yet defined in registry", seg_name);

                        if segment_relative {
                            // Add in segment relative addressing mode
                            let res = reg.cal_seg_list.add_cal_seg(seg_name.to_string(), next_segment_number, length as u32);
                            if let Err(e) = res {
                                error!("Failed to add calibration segment '{}': {}", seg_name, e);
                                continue;
                            }

                            // Set memory address
                            let new_seg = reg.cal_seg_list.find_cal_seg(seg_name).unwrap();
                            new_seg.set_mem_addr(addr);

                            info!(
                                "Created segment {}: '{}' segment relative addressing mode, addr = 0x{:08X}, size = {}, mem_addr = 0x{:08X}",
                                next_segment_number, seg_name, new_seg.addr, new_seg.size, new_seg.mem_addr
                            );
                        } else {
                            // Absolute addressing mode
                            if addr >= 0xFFFFFFFF {
                                error!(
                                    "Calibration segment '{}' has 64 bit address {:#x}, which does not fit the 32 bit XCP address range",
                                    seg_name, addr
                                );
                                continue; // skip this variable
                            }
                            let res = reg
                                .cal_seg_list
                                .add_cal_seg_by_addr(seg_name.to_string(), next_segment_number, addr_ext, addr as u32, length as u32);
                            if let Err(e) = res {
                                error!("Failed to add calibration segment '{}': {}", seg_name, e);
                                continue;
                            }

                            // Set memory address
                            let new_seg = reg.cal_seg_list.find_cal_seg(seg_name).unwrap();
                            new_seg.set_mem_addr(addr);

                            info!(
                                "Not yet defined segment: Created segment {}: '{}' absolute addr = {:#x}, size = {:#x}",
                                new_seg.index, new_seg.name, new_seg.addr, new_seg.size
                            );
                            next_segment_number += 1;
                            continue;
                        }
                    }
                }
            }

            // evt__<name> (thread local static, name is event name)
            // Event definitions (thread local static variaables)
            if var_name.starts_with("evt__") {
                // remove the "evt__" prefix
                let evt_name = var_name.strip_prefix("evt__").unwrap_or("unnamed");
                let evt_unit_idx = var_infos[0].unit_idx;
                let evt_unit_name = if let Some(name) = self.debug_data.make_simple_unit_name(evt_unit_idx) {
                    name
                } else {
                    format!("{evt_unit_idx}")
                };
                let evt_function = if let Some(f) = var_infos[0].function.as_ref() { f.as_str() } else { "" };
                info!("Event definition for event '{}' found in {}:{}", evt_name, evt_unit_name, evt_function);
                // Find the event in the registry
                if let Some(_evt) = reg.event_list.find_event(evt_name, 0) {
                    continue; // event already exists
                } else {
                    // @@@@ TODO: Event number unknown !!!!!!!!!!!!!!!
                    reg.event_list.add_event(McEvent::new(evt_name.to_string(), 0, next_event_id, 0)).unwrap();
                    error!("Unknown event '{}': Created with event id = {}", evt_name, next_event_id);
                    next_event_id += 1;
                    continue; // skip this variable
                }
            }
        }
        Ok(())
    }

    // Find event triggers in the code and register their location (compilation unit, function, CFA offset)
    pub fn register_event_locations(&self, reg: &mut Registry, verbose: usize) -> Result<(), Box<dyn Error>> {
        info!("Registering event locations:");

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
                assert!(var_infos.len() == 1); // Only one definition allowed
                let var_info = &var_infos[0];

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
                info!(
                    "  Event {} trigger found in {}:{}, address resolver mode {}",
                    evt_name, evt_unit_name, evt_function, evt_mode
                );

                // Find the event in the registry
                if let Some(_evt) = reg.event_list.find_event(evt_name, 0) {
                    // Try to lookup the canonical stack frame address offset from the function name
                    let mut evt_cfa: i32 = 0;
                    for cfa_info in self.debug_data.cfa_info.iter() {
                        if cfa_info.unit_idx == evt_unit_idx && cfa_info.function == evt_function {
                            if let Some(x) = cfa_info.cfa_offset {
                                evt_cfa = x as i32;
                            } else {
                                warn!("Could not determine CFA offset for function '{}'", evt_function);
                            }
                            break;
                        }
                    }

                    if verbose >= 1 {
                        info!("  Event '{}' trigger in function '{}', cfa = {}", evt_name, evt_function, evt_cfa);
                    }

                    // Store the unit and function name and canonical stack frame address offset for this event trigger
                    match reg.event_list.set_event_location(evt_name, evt_unit_idx, evt_function, evt_cfa) {
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

    pub fn register_variables(&self, reg: &mut Registry, segment_relative: bool, verbose: usize, unit_idx_limit: usize) -> Result<(), Box<dyn Error>> {
        // Load debug information from the ELF file
        info!("Registering variables:");

        // Iterate over variables
        for (var_name, var_infos) in &self.debug_data.variables {
            // Skip standard library variables and system/compiler internals (__<name>)s
            // Skip global XCP variables (gXCP.. and gA2L..) and special marker variables (cal__, evt__, trg__)
            if var_name.starts_with("__")
                || var_name.starts_with("gXcp")
                || var_name.starts_with("gA2l")
                || var_name.starts_with("cal__")
                || var_name.starts_with("evt__")
                || var_name.starts_with("trg__")
            {
                continue;
            }

            if var_infos.is_empty() {
                warn!("Variable '{}' has no variable info", var_name);
            }

            let mut a2l_name = var_name.to_string();
            let mut xcp_event_id = 0; // default event id is 0, async event in transmit thread

            // daq__<event_name>__<var_name> (local scope static variables)
            // Check for captured variables with format "daq__<event_name>__<var_name>"
            if var_name.starts_with("daq__") {
                // remove the "daq__" prefix
                let new_name = var_name.strip_prefix("daq__").unwrap_or(var_name);
                // get event name and variable name
                let mut parts = new_name.split("__");
                let event_name = parts.next().unwrap_or("");
                let var_name = parts.next().unwrap_or("");
                // Find the event in the registry
                if let Some(id) = reg.event_list.find_event(event_name, 0) {
                    xcp_event_id = id.id;
                    a2l_name = format!("{}.{}", event_name, var_name);
                } else {
                    warn!("Event '{}' for captured variable '{}' not found in registry", event_name, var_name);
                    continue; // skip this variable
                }
            }

            // Count variables with this name in compilation unit 0
            let count = var_infos.iter().filter(|v| v.unit_idx <= unit_idx_limit).count();

            // Process all variable with this name in different scopes and namespaces
            for var_info in var_infos {
                // @@@@ TODO: Create only variables from specified compilation unit
                if var_info.unit_idx > unit_idx_limit {
                    continue;
                }

                let var_function = if let Some(f) = var_info.function.as_ref() { f.as_str() } else { "" };

                // Address encoder
                let mem_addr_ext: u8 = var_info.address.0;
                let mem_addr: u64 = if mem_addr_ext == 0 {
                    // Encode absolute addressing mode
                    if var_info.address.1 == 0 {
                        debug!("Variable '{}' in function '{}' skipped, no address", var_name, var_function);
                        continue; // skip this variable
                    } else if var_info.address.1 >= 0xFFFFFFFF {
                        warn!(
                            "Variable '{}' skipped, has 64 bit address {:#x}, which does not fit the 32 bit XCP address range",
                            var_name, var_info.address.1
                        );
                        continue; // skip this variable
                    } else {
                        // find an event triggered in this function
                        if let Some(event) = reg.event_list.find_event_by_location(var_info.unit_idx, var_function) {
                            xcp_event_id = event.id;
                            info!("Variable '{}' is local to function '{}', using event id = {}", var_name, var_function, xcp_event_id);
                        } else {
                            debug!("Variable '{}' is local to function '{}', but no event found", var_name, var_function);
                        }
                        // multiple variables with this name, prefix with function name
                        if count > 1 {
                            a2l_name = format!("{}.{}", var_function, var_name);
                        }
                        var_info.address.1
                    }
                }
                // Encode relative addressing mode
                else if mem_addr_ext == 2 {
                    // Find an event id for this local variable
                    if let Some(event) = reg.event_list.find_event_by_location(var_info.unit_idx, var_function) {
                        // Set the event id for this function
                        // Prefix the variable with the function name
                        xcp_event_id = event.id;
                        let cfa: i64 = event.cfa as i64;
                        a2l_name = format!("{}.{}", var_function, var_name);
                        debug!(
                            "Variable '{}' is local to function '{}', using event id = {}, dwarf_offset = {} cfa = {}",
                            var_name,
                            var_function,
                            xcp_event_id,
                            (var_info.address.1 as i64 - 0x80000000) as i64,
                            cfa
                        );

                        // @@@@ TODO: Create functions instead of constants for relative address encoding
                        // Encode dyn addressing mode A2L/XCP address from offset and event id
                        let offset: i64 = var_info.address.1 as i64 - 0x80000000 + cfa;
                        if offset < -(McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64)
                            || offset > (McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK as i64 - McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64)
                        {
                            warn!(
                                "Variable '{}' skipped, has offset {} which does not fit the XCP dynamic addressing mode range",
                                var_name, offset
                            );
                            continue; // skip this variable
                        }

                        (((offset + McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64) as u64) & McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK as u64)
                            | ((event.id as u64) << McAddress::XCP_ADDR_EXT_DYN_OFFSET_BITS)
                    } else {
                        debug!("Variable '{}' skipped, could not find event for dyn addressing mode", var_name);
                        continue; // skip this variable
                    }
                }
                // @@@@ TODO: Handle other address extensions
                else {
                    debug!("Variable '{}' skipped, has unsupported address extension {:#x}", var_name, mem_addr_ext);
                    continue; // skip this variable
                };

                // Check if the absolute address is in a calibration segment
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
                    let addr_ext = if segment_relative && mem_addr_ext == 0 {
                        1 // set to absolute addressing mode
                    } else {
                        mem_addr_ext
                    };
                    (McObjectType::Measurement, McAddress::new_a2l_with_event(xcp_event_id, mem_addr as u32, addr_ext))
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
                            info!(
                                "Add {} for {}: addr = {}:0x{:08x}",
                                if object_type == McObjectType::Characteristic { "characteristic" } else { "measurement" },
                                a2l_name,
                                mem_addr_ext,
                                mem_addr
                            );
                            if verbose >= 2 {
                                info!("{}", type_info);
                            }
                            let dim_type = self.get_dim_type(reg, type_info, object_type);
                            let res = reg.instance_list.add_instance(a2l_name.clone(), dim_type, McSupportData::new(object_type), mc_addr);
                            match res {
                                Ok(_) => {
                                    if verbose >= 1 {
                                        info!(
                                            "  Registered variable '{}' with type '{}', size = {}, event id = {}",
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
}
