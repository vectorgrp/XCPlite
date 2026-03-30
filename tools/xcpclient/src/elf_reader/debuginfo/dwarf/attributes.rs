// Taken from Github repository a2ltool by DanielT

use super::{DebugDataReader, UnitList};
use gimli::{DebugAddrBase, DebuggingInformationEntry, EndianSlice, RunTimeEndian, UnitHeader};

type SliceType<'a> = EndianSlice<'a, RunTimeEndian>;
type OptionalAttribute<'data> = Option<gimli::AttributeValue<SliceType<'data>>>;

// try to get the attribute of the type attrtype for the DIE
pub(crate) fn get_attr_value<'unit>(entry: &DebuggingInformationEntry<'_, 'unit, SliceType, usize>, attrtype: gimli::DwAt) -> OptionalAttribute<'unit> {
    entry.attr_value(attrtype).unwrap_or(None)
}

// get a name as a String from a DW_AT_name attribute
pub(crate) fn get_name_attribute(
    entry: &DebuggingInformationEntry<SliceType, usize>,
    dwarf: &gimli::Dwarf<EndianSlice<RunTimeEndian>>,
    unit_header: &gimli::UnitHeader<EndianSlice<RunTimeEndian>>,
) -> Result<String, String> {
    let name_attr = get_attr_value(entry, gimli::constants::DW_AT_name).ok_or_else(|| "failed to get name attribute".to_string())?;
    match name_attr {
        gimli::AttributeValue::String(slice) => {
            if let Ok(utf8string) = slice.to_string() {
                // could not demangle, but successfully converted the slice to utf8
                return Ok(utf8string.to_owned());
            }
            Err(format!("could not decode {slice:#?} as a utf-8 string"))
        }
        gimli::AttributeValue::DebugStrRef(str_offset) => {
            match dwarf.debug_str.get_str(str_offset) {
                Ok(slice) => {
                    if let Ok(utf8string) = slice.to_string() {
                        // could not demangle, but successfully converted the slice to utf8
                        return Ok(utf8string.to_owned());
                    }
                    Err(format!("could not decode {slice:#?} as a utf-8 string"))
                }
                Err(err) => Err(err.to_string()),
            }
        }
        gimli::AttributeValue::DebugStrOffsetsIndex(index) => {
            let unit = dwarf.unit(*unit_header).map_err(|_| "failed to get name attribute (invalid unit header)".to_string())?;
            let offset = dwarf
                .debug_str_offsets
                .get_str_offset(unit.encoding().format, unit.str_offsets_base, index)
                .map_err(|_| "failed to get name attribute (invalid debug_str_offsets index)".to_string())?;
            match dwarf.debug_str.get_str(offset) {
                Ok(slice) => {
                    if let Ok(utf8string) = slice.to_string() {
                        // could not demangle, but successfully converted the slice to utf8
                        return Ok(utf8string.to_owned());
                    }
                    Err(format!("could not decode {slice:#?} as a utf-8 string"))
                }
                Err(err) => Err(err.to_string()),
            }
        }
        gimli::AttributeValue::DebugLineStrRef(line_str_offset) => {
            match dwarf.debug_line_str.get_str(line_str_offset) {
                Ok(slice) => {
                    if let Ok(utf8string) = slice.to_string() {
                        // could not demangle, but successfully converted the slice to utf8
                        return Ok(utf8string.to_owned());
                    }
                    Err(format!("could not decode {slice:#?} as a utf-8 string"))
                }
                Err(err) => Err(err.to_string()),
            }
        }
        _ => Err(format!("invalid name attribute type {name_attr:#?}")),
    }
}

// get a type reference as an offset relative to the start of .debug_info from a DW_AT_type attribute
// it the type reference is a UnitRef (relative to the unit header) it will be converted first
pub(crate) fn get_typeref_attribute(entry: &DebuggingInformationEntry<SliceType, usize>, unit: &UnitHeader<SliceType>) -> Result<usize, String> {
    let type_attr = get_attr_value(entry, gimli::constants::DW_AT_type).ok_or_else(|| "failed to get type reference attribute".to_string())?;
    match type_attr {
        gimli::AttributeValue::UnitRef(unitoffset) => Ok(unitoffset.to_debug_info_offset(unit).unwrap().0),
        gimli::AttributeValue::DebugInfoRef(infooffset) => Ok(infooffset.0),
        gimli::AttributeValue::DebugTypesRef(_typesig) => {
            // .debug_types was added in DWARF v4 and removed again in v5.
            // silently ignore references to the .debug_types section
            // this is unlikely to matter as few compilers ever bothered with .debug_types
            // (for example gcc supports this, but support is only enabled if the user requests this explicitly)
            Err("unsupported reference to a .debug_types entry (Dwarf 4)".to_string())
        }
        _ => Err(format!("unsupported type reference: {type_attr:#?}")),
    }
}

// get the address of a variable from a DW_AT_location attribute
// The DW_AT_location contains an Exprloc expression that allows the address to be calculated
// in complex ways, so the expression must be evaluated in order to get the address
pub(crate) fn get_location_attribute(
    debug_data_reader: &DebugDataReader,
    entry: &DebuggingInformationEntry<SliceType, usize>,
    encoding: gimli::Encoding,
    current_unit: usize,
) -> Option<(u8, u64)> {
    let loc_attr = get_attr_value(entry, gimli::constants::DW_AT_location)?;
    match loc_attr {
        gimli::AttributeValue::Exprloc(expression) => evaluate_exprloc(debug_data_reader, expression, encoding, current_unit),
        gimli::AttributeValue::LocationListsRef(offset) => evaluate_location_list(debug_data_reader, offset, encoding, current_unit),
        _ => {
            log::error!("get_location_attribute: Unexpected location attribute type: {loc_attr:#?}");
            None
        }
    }
}

// get the address offset of a struct member from a DW_AT_data_member_location attribute
pub(crate) fn get_data_member_location_attribute(
    debug_data_reader: &DebugDataReader,
    entry: &DebuggingInformationEntry<SliceType, usize>,
    encoding: gimli::Encoding,
    current_unit: usize,
) -> Option<u64> {
    let loc_attr = get_attr_value(entry, gimli::constants::DW_AT_data_member_location)?;
    match loc_attr {
        gimli::AttributeValue::Exprloc(expression) => {
            if let Some((addr_ext, addr)) = evaluate_exprloc(debug_data_reader, expression, encoding, current_unit) {
                Some(addr)
            } else {
                None
            }
        }
        gimli::AttributeValue::Udata(val) => Some(val),
        gimli::AttributeValue::Data1(val) => Some(u64::from(val)),
        gimli::AttributeValue::Data2(val) => Some(u64::from(val)),
        gimli::AttributeValue::Data4(val) => Some(u64::from(val)),
        gimli::AttributeValue::Data8(val) => Some(val),
        other => {
            log::warn!("unexpected data_member_location attribute: {other:?}");
            None
        }
    }
}

// get the element size stored in the DW_AT_byte_size attribute
pub(crate) fn get_byte_size_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let byte_size_attr = get_attr_value(entry, gimli::constants::DW_AT_byte_size)?;
    match byte_size_attr {
        gimli::AttributeValue::Sdata(byte_size) => Some(byte_size as u64),
        gimli::AttributeValue::Udata(byte_size) => Some(byte_size),
        gimli::AttributeValue::Data1(byte_size) => Some(u64::from(byte_size)),
        gimli::AttributeValue::Data2(byte_size) => Some(u64::from(byte_size)),
        gimli::AttributeValue::Data4(byte_size) => Some(u64::from(byte_size)),
        gimli::AttributeValue::Data8(byte_size) => Some(byte_size),
        _ => None,
    }
}

// get the encoding of a variable from the DW_AT_encoding attribute
pub(crate) fn get_encoding_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<gimli::DwAte> {
    let encoding_attr = get_attr_value(entry, gimli::constants::DW_AT_encoding)?;
    if let gimli::AttributeValue::Encoding(enc) = encoding_attr { Some(enc) } else { None }
}

// get the upper bound of an array from the DW_AT_upper_bound attribute
pub(crate) fn get_lower_bound_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let lbound_attr = get_attr_value(entry, gimli::constants::DW_AT_lower_bound)?;
    match lbound_attr {
        gimli::AttributeValue::Sdata(lbound) => Some(lbound as u64),
        gimli::AttributeValue::Udata(lbound) => Some(lbound),
        gimli::AttributeValue::Data1(lbound) => Some(u64::from(lbound)),
        gimli::AttributeValue::Data2(lbound) => Some(u64::from(lbound)),
        gimli::AttributeValue::Data4(lbound) => Some(u64::from(lbound)),
        gimli::AttributeValue::Data8(lbound) => Some(lbound),
        _ => None,
    }
}

// get the upper bound of an array from the DW_AT_upper_bound attribute
pub(crate) fn get_upper_bound_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let ubound_attr = get_attr_value(entry, gimli::constants::DW_AT_upper_bound)?;
    match ubound_attr {
        gimli::AttributeValue::Sdata(ubound) => Some(ubound as u64),
        gimli::AttributeValue::Udata(ubound) => Some(ubound),
        gimli::AttributeValue::Data1(ubound) => Some(u64::from(ubound)),
        gimli::AttributeValue::Data2(ubound) => Some(u64::from(ubound)),
        gimli::AttributeValue::Data4(ubound) => Some(u64::from(ubound)),
        gimli::AttributeValue::Data8(ubound) => Some(ubound),
        _ => None,
    }
}

// get the upper bound of an array from the DW_AT_upper_bound attribute
pub(crate) fn get_count_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let count_attr = get_attr_value(entry, gimli::constants::DW_AT_count)?;
    match count_attr {
        gimli::AttributeValue::Sdata(count) => Some(count as u64),
        gimli::AttributeValue::Udata(count) => Some(count),
        gimli::AttributeValue::Data1(count) => Some(u64::from(count)),
        gimli::AttributeValue::Data2(count) => Some(u64::from(count)),
        gimli::AttributeValue::Data4(count) => Some(u64::from(count)),
        gimli::AttributeValue::Data8(count) => Some(count),
        _ => None,
    }
}

// get the byte stride of an array from the DW_AT_upper_bound attribute
// this attribute is only present if the stride is different from the element size
pub(crate) fn get_byte_stride_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let stride_attr = get_attr_value(entry, gimli::constants::DW_AT_byte_stride)?;
    match stride_attr {
        gimli::AttributeValue::Sdata(stride) => Some(stride as u64),
        gimli::AttributeValue::Udata(stride) => Some(stride),
        gimli::AttributeValue::Data1(stride) => Some(u64::from(stride)),
        gimli::AttributeValue::Data2(stride) => Some(u64::from(stride)),
        gimli::AttributeValue::Data4(stride) => Some(u64::from(stride)),
        gimli::AttributeValue::Data8(stride) => Some(stride),
        _ => None,
    }
}

// get the const value of an enumerator from the DW_AT_const_value attribute
pub(crate) fn get_const_value_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<i64> {
    let constval_attr = get_attr_value(entry, gimli::constants::DW_AT_const_value)?;
    match constval_attr {
        gimli::AttributeValue::Sdata(value) => Some(value),
        gimli::AttributeValue::Udata(value) => Some(value as i64),
        gimli::AttributeValue::Data1(bit_offset) => Some(i64::from(bit_offset)),
        gimli::AttributeValue::Data2(bit_offset) => Some(i64::from(bit_offset)),
        gimli::AttributeValue::Data4(bit_offset) => Some(i64::from(bit_offset)),
        gimli::AttributeValue::Data8(bit_offset) => Some(bit_offset as i64),
        _ => None,
    }
}

// get the bit size of a variable from the DW_AT_bit_size attribute
// this attribute is only present if the variable is in a bitfield
pub(crate) fn get_bit_size_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let bit_size_attr = get_attr_value(entry, gimli::constants::DW_AT_bit_size)?;
    if let gimli::AttributeValue::Udata(bit_size) = bit_size_attr {
        Some(bit_size)
    } else {
        None
    }
}

// get the bit offset of a variable from the DW_AT_bit_offset attribute
// this attribute is only present if the variable is in a bitfield
pub(crate) fn get_bit_offset_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let data_bit_offset_attr = get_attr_value(entry, gimli::constants::DW_AT_bit_offset)?;
    // DW_AT_bit_offset: up to Dwarf 3
    // DW_AT_data_bit_offset: Dwarf 4 and following
    match data_bit_offset_attr {
        gimli::AttributeValue::Sdata(bit_offset) => Some(bit_offset as u64),
        gimli::AttributeValue::Udata(bit_offset) => Some(bit_offset),
        gimli::AttributeValue::Data1(bit_offset) => Some(u64::from(bit_offset)),
        gimli::AttributeValue::Data2(bit_offset) => Some(u64::from(bit_offset)),
        gimli::AttributeValue::Data4(bit_offset) => Some(u64::from(bit_offset)),
        gimli::AttributeValue::Data8(bit_offset) => Some(bit_offset),
        _ => None,
    }
}

// get the bit offset of a variable from the DW_AT_data_bit_offset attribute
// this attribute is only present if the variable is in a bitfield
pub(crate) fn get_data_bit_offset_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<u64> {
    let data_bit_offset_attr = get_attr_value(entry, gimli::constants::DW_AT_data_bit_offset)?;
    // DW_AT_bit_offset: up to Dwarf 3
    // DW_AT_data_bit_offset: Dwarf 4 and following
    match data_bit_offset_attr {
        gimli::AttributeValue::Sdata(bit_offset) => Some(bit_offset as u64),
        gimli::AttributeValue::Udata(bit_offset) => Some(bit_offset),
        gimli::AttributeValue::Data1(bit_offset) => Some(u64::from(bit_offset)),
        gimli::AttributeValue::Data2(bit_offset) => Some(u64::from(bit_offset)),
        gimli::AttributeValue::Data4(bit_offset) => Some(u64::from(bit_offset)),
        gimli::AttributeValue::Data8(bit_offset) => Some(bit_offset),
        _ => None,
    }
}

pub(crate) fn get_specification_attribute<'data, 'abbrev, 'unit>(
    entry: &'data DebuggingInformationEntry<SliceType, usize>,
    unit: &'unit UnitHeader<EndianSlice<'data, RunTimeEndian>>,
    abbrev: &'abbrev gimli::Abbreviations,
) -> Option<DebuggingInformationEntry<'abbrev, 'unit, EndianSlice<'data, RunTimeEndian>, usize>> {
    let specification_attr = get_attr_value(entry, gimli::constants::DW_AT_specification)?;
    match specification_attr {
        gimli::AttributeValue::UnitRef(unitoffset) => unit.entry(abbrev, unitoffset).ok(),
        gimli::AttributeValue::DebugInfoRef(_) => {
            // presumably, a debugger could also generate a DebugInfo ref instead on a UnitRef
            // parsing this would take info that we don't have here, e.g. the unit headers and abbreviations of all units
            // fortunately I have not seen a compiler generate this variation yet
            None
        }
        _ => None,
    }
}

pub(crate) fn get_abstract_origin_attribute<'data, 'abbrev, 'unit>(
    entry: &'data DebuggingInformationEntry<SliceType, usize>,
    unit: &'unit UnitHeader<EndianSlice<'data, RunTimeEndian>>,
    abbrev: &'abbrev gimli::Abbreviations,
) -> Option<DebuggingInformationEntry<'abbrev, 'unit, EndianSlice<'data, RunTimeEndian>, usize>> {
    let origin_attr = get_attr_value(entry, gimli::constants::DW_AT_abstract_origin)?;
    match origin_attr {
        gimli::AttributeValue::UnitRef(unitoffset) => unit.entry(abbrev, unitoffset).ok(),
        _ => None,
    }
}

pub(crate) fn get_addr_base_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<DebugAddrBase> {
    let origin_attr = get_attr_value(entry, gimli::constants::DW_AT_addr_base)?;
    match origin_attr {
        gimli::AttributeValue::DebugAddrBase(addr_base) => Some(addr_base),
        _ => None,
    }
}

// log location list entries for debugging
fn evaluate_location_list(debug_data_reader: &DebugDataReader, offset: gimli::LocationListsOffset, encoding: gimli::Encoding, current_unit: usize) -> Option<(u8, u64)> {
    let (unit_header, _) = &debug_data_reader.units[current_unit];

    // Create a Unit from the UnitHeader
    let unit = match debug_data_reader.dwarf.unit(*unit_header) {
        Ok(unit) => unit,
        Err(e) => {
            log::warn!("LocationList: Failed to create unit: {}", e);
            return None;
        }
    };

    // Get the location list
    let loclists = match debug_data_reader.dwarf.locations(&unit, offset) {
        Ok(loclists) => loclists,
        Err(e) => {
            log::warn!("LocationList: Failed to get location list at offset {:?}: {}", offset, e);
            return None;
        }
    };

    // Print
    log::debug!("LocationList: offset={:?}, entries:", offset);

    let mut addr_ext: u8 = 0xff;
    let mut addr: u64 = 0;

    // Iterate through location list entries
    let mut entry_count = 0;
    let mut loclists_iter = loclists;
    while let Ok(Some(entry)) = loclists_iter.next() {
        entry_count += 1;

        // Log the PC range for this location
        log::debug!("  {}: PC range 0x{:08x}..0x{:08x}", entry_count, entry.range.begin, entry.range.end);

        let expression = entry.data;

        // Print
        let mut evaluation = expression.evaluation(encoding);
        evaluation.set_object_address(0);
        evaluation.set_initial_value(0);
        match evaluation.evaluate() {
            Ok(gimli::EvaluationResult::Complete) => {
                let result = evaluation.result();
                if !result.is_empty() {
                    log::debug!("\tLocation: {:?}", result[0]);
                } else {
                    log::debug!("\tLocation: <empty>");
                }
            }
            Ok(eval_result) => {
                log::debug!("\tLocation: evaluation incomplete: {:?}", eval_result);
            }
            Err(e) => {
                log::debug!("\tLocation: evaluation failed: {}", e);
            }
        }

        // Evaluate the expression to get a measurable (if possible) address
        if let Some(ea) = evaluate_exprloc(debug_data_reader, expression, encoding, current_unit) {
            log::debug!("    Evaluated Address: addr_ext={}, address=0x{:x}", ea.0, ea.1);
            // @@@@ TODO: For now, just return the lowest evaluated valid address extension
            if ea.0 < addr_ext {
                addr_ext = ea.0;
                addr = ea.1;
            }
        }
    }

    if entry_count == 0 || addr_ext == 0xff {
        return None;
    }
    return Some((addr_ext, addr));
}

// evaluate an exprloc expression to get a variable address or struct member offset
fn evaluate_exprloc(
    debug_data_reader: &DebugDataReader,
    expression: gimli::Expression<EndianSlice<RunTimeEndian>>,
    encoding: gimli::Encoding,
    current_unit: usize,
) -> Option<(u8, u64)> {
    let mut addr_ext = 0;
    let mut evaluation = expression.evaluation(encoding);
    evaluation.set_object_address(0);
    evaluation.set_initial_value(0);
    evaluation.set_max_iterations(100);
    let mut eval_result = evaluation
        .evaluate()
        .map_err(|e| {
            log::error!("evaluate_exprloc: Initial evaluation failed: {e:?}");
            e
        })
        .ok()?;
    while eval_result != gimli::EvaluationResult::Complete {
        match eval_result {
            // @@@@ TODO Address extensions hardcoded here assuming XCP_LITE_AASDD
            // @@@@ Address extension 0x80 is used to indicate registers, registers are not supported yet
            // @@@@ Address extension 0x81 is used to indicate TLS, TLS is not supported yet
            // @@@@ Address extension 0x82 is error

            // Supported
            gimli::EvaluationResult::RequiresRelocatedAddress(address) => {
                // Global memory
                // Will be resolved with xcp_get_base_address() at runtime
                addr_ext = 0;
                eval_result = evaluation
                    .resume_with_relocated_address(address)
                    .map_err(|e| {
                        log::error!("evaluate_exprloc: resume_with_relocated_address failed: {e:?}");
                        e
                    })
                    .ok()?;
                log::debug!("RequiresRelocatedAddress: resolved with xcp_get_base_address, addr_ext=0");
            }
            gimli::EvaluationResult::RequiresFrameBase => {
                // Stack frame of a function.
                // Use 0x80000000 as a dummy value for now
                // Will be resolved with xcp_get_frame_address() at runtime
                addr_ext = 2;
                eval_result = evaluation
                    .resume_with_frame_base(0x80000000)
                    .map_err(|e| {
                        log::error!("evaluate_exprloc: resume_with_frame_base failed: {e:?}");
                        e
                    })
                    .ok()?;
                log::debug!("RequiresFrameBase: resolved with xcp_get_frame_address, addr_ext=2");
            }
            gimli::EvaluationResult::RequiresIndexedAddress { index, .. } => {
                // DWARF 5: Variable address is stored in the .debug_addr table
                // Need to get DW_AT_addr_base from the compilation unit DIE to locate the address table
                // Will be resolved with xcp_get_base_address() at runtime
                // TODO: Optimize by caching addr_base per unit instead of re-parsing
                let (unit_header, abbrev) = &debug_data_reader.units[current_unit];
                let address_size = unit_header.address_size();
                let mut entries = unit_header.entries(abbrev);
                let (_, entry) = entries.next_dfs().ok()??;
                let base = get_addr_base_attribute(entry)?;
                let addr = debug_data_reader.dwarf.debug_addr.get_address(address_size, base, index).ok()?;
                addr_ext = 0;
                eval_result = evaluation
                    .resume_with_indexed_address(addr)
                    .map_err(|e| {
                        log::error!("evaluate_exprloc: resume_with_indexed_address failed: {e:?}");
                        e
                    })
                    .ok()?;
                log::debug!("RequiresIndexedAddress: resolved from .debug_addr[{:?}], addr_ext=0", index);
            }

            // Error: Not supported
            gimli::EvaluationResult::RequiresRegister { .. } => {
                // the value is relative to a register (e.g. the stack base)
                // this means it cannot be referenced and is not suitable for use in a2l yet
                // @@@@ xcp_client: allow register addresses ????
                addr_ext = 0x80;
                log::debug!("RequiresRegister: expression not evaluated, unsupported, eval_result={eval_result:?}");
                return Some((addr_ext, 0));
            }
            gimli::EvaluationResult::RequiresTls(address) => {
                // Thread local storage address
                // @@@@ xcp_client: allow TLS addresses ????
                addr_ext = 0x81;
                log::debug!("RequiresTls: expression not evaluated, unsupported, eval_result={eval_result:?}");
                return Some((addr_ext, address));
            }
            // @@@@ TODO: Clarifiy if we need to handle RequiresCallFrameCfa
            _other => {
                // there are a lot of other types of address expressions that can only be evaluated by a debugger while a program is running
                // none of these can be handled in the a2lfile use-case.
                addr_ext = 0x82;
                log::debug!("Other: expression not evaluated, unsupported, eval_result={_other:?}");
                return Some((addr_ext, 0));
            }
        };
    }
    let result = evaluation.result();
    if result.len() > 1 {
        log::debug!("evaluate_exprloc: Multiple pieces in evaluation result are not supported yet: {:?}", result);
        return None;
    }
    log::debug!("evaluate_exprloc: Evaluation result: {:?}", result[0]);
    if result.is_empty() {
        log::debug!("evaluate_exprloc: Evaluation result is empty");
        Some((0xFF, 0))
    } else {
        let (addr_ext, address) = match &result[0] {
            gimli::Piece {
                location: gimli::Location::Address { address },
                ..
            } => {
                log::debug!("evaluate_exprloc: Location is an address {}:0x{:08X}", addr_ext, *address);

                (addr_ext, *address)
            }

            gimli::Piece {
                location: gimli::Location::Register { register },
                ..
            } => {
                log::debug!("evaluate_exprloc: Location is a register {:?}", register);
                (0x80, 0)
            }

            gimli::Piece {
                location: gimli::Location::Value { value },
                ..
            } => {
                log::debug!("evaluate_exprloc: Location is a constant value {:?}", value);
                (0x81, value.to_u64(0).unwrap_or(0))
            }

            other => {
                log::debug!("evaluate_exprloc: Location evaluation result not handled  {:?}", other);
                (0xFF, 0)
            }
        };
        Some((addr_ext, address))
    }

    // if let gimli::Piece {
    //     location: gimli::Location::Address { address },
    //     ..
    // } = result[0]
    // {
    //     log::info!("evaluate_exprloc: Address is {}:0x{:x}", addr_ext, address);
    //     Some((addr_ext, address))
    // } else {
    //     log::warn!("evaluate_exprloc: Location is not a measurement address {:?}", result[0]);
    //     None
    // }
}

// Get a DW_AT_type attribute and return the number of the unit in which the type is located
// as well as the offset of the type relative to the start of .debug_info
// If the attribute is missing, return Ok(None)
pub(crate) fn get_type_attribute(
    entry: &DebuggingInformationEntry<SliceType, usize>,
    unit_list: &UnitList<'_>,
    current_unit: usize,
) -> Result<Option<(usize, gimli::DebugInfoOffset)>, String> {
    match get_attr_value(entry, gimli::constants::DW_AT_type) {
        Some(gimli::AttributeValue::DebugInfoRef(dbginfo_offset)) => {
            if let Some(unit_idx) = unit_list.get_unit(dbginfo_offset.0) {
                Ok(Some((unit_idx, dbginfo_offset)))
            } else {
                Err("invalid debug info ref".to_string())
            }
        }
        Some(gimli::AttributeValue::UnitRef(unit_offset)) => {
            let (unit, _) = &unit_list[current_unit];
            let dbginfo_offset = unit_offset.to_debug_info_offset(unit).unwrap();
            Ok(Some((current_unit, dbginfo_offset)))
        }
        None => Ok(None),
        other => Err(format!("failed to get type attribute: {other:#?}")),
    }
}

// get the DW_AT_declaration attribute
pub(crate) fn get_declaration_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<bool> {
    let decl_attr = get_attr_value(entry, gimli::constants::DW_AT_declaration)?;
    if let gimli::AttributeValue::Flag(flag) = decl_attr { Some(flag) } else { None }
}
