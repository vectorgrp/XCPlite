// Taken from Github repository a2ltool by DanielT
//
// Readers for the DWARF attributes this tool needs. Each function reads one attribute (DW_AT_xxx) of a debug info entry
// and converts it to a plain Rust value.
//
// Why every getter matches on several AttributeValue variants: DWARF separates the meaning of an attribute (DW_AT_byte_size)
// from its storage format, the "form" (DW_FORM_data1, DW_FORM_data2, DW_FORM_udata, ...). The compiler picks the form which
// takes the least space, so a byte size of 4 may arrive as Data1(4) from one compiler and as Udata(4) from another. gimli
// hands the value over as the AttributeValue enum variant of its form, the getters accept every form which makes sense.
//
// Kinds of values which occur here:
//   constants     Data1/2/4/8, Udata, Sdata: sizes, offsets, enumerator values, bit positions
//   strings       String (inline), DebugStrRef (offset into .debug_str), DebugStrOffsetsIndex (DWARF 5 index, resolved
//                 through the .debug_str_offsets table of the unit), DebugLineStrRef (.debug_line_str, file names)
//   references    UnitRef (offset relative to the unit), DebugInfoRef (offset relative to .debug_info): the type of a
//                 variable, the declaration a definition belongs to, the original of an inlined copy
//   addresses     Addr (a plain address), DebugAddrIndex (DWARF 5 index into .debug_addr)
//   expressions   Exprloc: a location expression, a small program which computes where a variable lives, see evaluate_exprloc.
//                 LocationListsRef / DebugLocListsIndex: a list of (address range, expression) pairs for variables whose
//                 location changes during the function (optimized code)

use super::{DebugDataReader, UnitList};
use gimli::{DebugAddrBase, DebuggingInformationEntry, EndianSlice, RunTimeEndian, UnitHeader};

type SliceType<'a> = EndianSlice<'a, RunTimeEndian>;
type OptionalAttribute<'data> = Option<gimli::AttributeValue<SliceType<'data>>>;

// Start address of a function: DW_AT_low_pc is a direct address (DW_FORM_addr) or, in DWARF 5, an index into the address table
// of the unit (DW_FORM_addrx, Clang and split DWARF), which the caller resolves with resolve_index (Dwarf::address), it is only
// called for the index form. name is the function name for the log message. Returns None if the attribute is missing or unresolved
pub(crate) fn get_low_pc_attribute<R: gimli::Reader>(
    entry: &DebuggingInformationEntry<R>,
    name: &str,
    resolve_index: impl FnOnce(gimli::DebugAddrIndex<R::Offset>) -> gimli::Result<u64>,
) -> Option<u64> {
    match entry.attr_value(gimli::constants::DW_AT_low_pc)? {
        gimli::AttributeValue::Addr(addr) => Some(addr),
        gimli::AttributeValue::DebugAddrIndex(index) => match resolve_index(index) {
            Ok(addr) => Some(addr),
            Err(e) => {
                log::warn!("Function '{}': DW_AT_low_pc address index {:?} not resolved: {}", name, index, e);
                None
            }
        },
        other => {
            log::warn!("Function '{}': unsupported form {:?} of DW_AT_low_pc, the function start address is unknown", name, other);
            None
        }
    }
}

// The DW_AT_producer of a compilation unit: the compiler, its version and its command line options
pub(crate) fn get_producer_attribute(
    entry: &DebuggingInformationEntry<SliceType, usize>,
    dwarf: &gimli::Dwarf<EndianSlice<RunTimeEndian>>,
    unit_header: &gimli::UnitHeader<EndianSlice<RunTimeEndian>>,
) -> Result<String, String> {
    let producer_attr = get_attr_value(entry, gimli::constants::DW_AT_producer).ok_or_else(|| "failed to get producer attribute".to_string())?;
    decode_string_attribute(producer_attr, dwarf, unit_header)
}

// try to get the attribute of the type attrtype for the DIE
pub(crate) fn get_attr_value<'data>(entry: &DebuggingInformationEntry<SliceType<'data>, usize>, attrtype: gimli::DwAt) -> OptionalAttribute<'data> {
    entry.attr_value(attrtype)
}

// Decode a string attribute value in any of its forms, see the module comment. The DWARF 5 indexed form needs the
// str_offsets_base of the unit, which is why a Unit is constructed from the header for that case only
fn decode_string_attribute(
    attr: gimli::AttributeValue<SliceType>,
    dwarf: &gimli::Dwarf<EndianSlice<RunTimeEndian>>,
    unit_header: &gimli::UnitHeader<EndianSlice<RunTimeEndian>>,
) -> Result<String, String> {
    match attr {
        gimli::AttributeValue::String(slice) => {
            if let Ok(utf8string) = slice.to_string() {
                return Ok(utf8string.to_owned());
            }
            Err(format!("could not decode {slice:#?} as a utf-8 string"))
        }
        gimli::AttributeValue::DebugStrRef(str_offset) => match dwarf.debug_str.get_str(str_offset) {
            Ok(slice) => {
                if let Ok(utf8string) = slice.to_string() {
                    return Ok(utf8string.to_owned());
                }
                Err(format!("could not decode {slice:#?} as a utf-8 string"))
            }
            Err(err) => Err(err.to_string()),
        },
        gimli::AttributeValue::DebugStrOffsetsIndex(index) => {
            let unit = dwarf
                .unit(*unit_header)
                .map_err(|_| "failed to decode string attribute (invalid unit header)".to_string())?;
            let offset = dwarf
                .debug_str_offsets
                .get_str_offset(unit.encoding().format, unit.str_offsets_base, index)
                .map_err(|_| "failed to decode string attribute (invalid debug_str_offsets index)".to_string())?;
            match dwarf.debug_str.get_str(offset) {
                Ok(slice) => {
                    if let Ok(utf8string) = slice.to_string() {
                        return Ok(utf8string.to_owned());
                    }
                    Err(format!("could not decode {slice:#?} as a utf-8 string"))
                }
                Err(err) => Err(err.to_string()),
            }
        }
        gimli::AttributeValue::DebugLineStrRef(line_str_offset) => match dwarf.debug_line_str.get_str(line_str_offset) {
            Ok(slice) => {
                if let Ok(utf8string) = slice.to_string() {
                    return Ok(utf8string.to_owned());
                }
                Err(format!("could not decode {slice:#?} as a utf-8 string"))
            }
            Err(err) => Err(err.to_string()),
        },
        _ => Err(format!("invalid string attribute type {attr:#?}")),
    }
}

// get a name as a String from a DW_AT_name attribute
pub(crate) fn get_name_attribute(
    entry: &DebuggingInformationEntry<SliceType, usize>,
    dwarf: &gimli::Dwarf<EndianSlice<RunTimeEndian>>,
    unit_header: &gimli::UnitHeader<EndianSlice<RunTimeEndian>>,
) -> Result<String, String> {
    let name_attr = get_attr_value(entry, gimli::constants::DW_AT_name).ok_or_else(|| "failed to get name attribute".to_string())?;
    decode_string_attribute(name_attr, dwarf, unit_header)
}

// get the mangled (linker) name of a C++ variable or function from the DW_AT_linkage_name attribute, e.g. _ZN13motor_control5inputE
// DW_AT_MIPS_linkage_name is the name of the attribute before it was standardized in DWARF 4, still emitted by some compilers.
// C entities and C++ entities with internal linkage have no linkage name
pub(crate) fn get_linkage_name_attribute(
    entry: &DebuggingInformationEntry<SliceType, usize>,
    dwarf: &gimli::Dwarf<EndianSlice<RunTimeEndian>>,
    unit_header: &gimli::UnitHeader<EndianSlice<RunTimeEndian>>,
) -> Result<String, String> {
    let linkage_attr = get_attr_value(entry, gimli::constants::DW_AT_linkage_name)
        .or_else(|| get_attr_value(entry, gimli::constants::DW_AT_MIPS_linkage_name))
        .ok_or_else(|| "failed to get linkage name attribute".to_string())?;
    decode_string_attribute(linkage_attr, dwarf, unit_header)
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

// get the address of a variable from a DW_AT_location attribute, as (address extension, address), see VarInfo
// The DW_AT_location contains an Exprloc expression that allows the address to be calculated
// in complex ways, so the expression must be evaluated in order to get the address.
// A location list (several expressions, each valid for a range of code addresses) is accepted if all its entries agree
pub(crate) fn get_location_attribute(
    debug_data_reader: &DebugDataReader,
    entry: &DebuggingInformationEntry<SliceType, usize>,
    encoding: gimli::Encoding,
    current_unit: usize,
    name: &str,
) -> Option<(u8, u64)> {
    let loc_attr = get_attr_value(entry, gimli::constants::DW_AT_location)?;
    match loc_attr {
        gimli::AttributeValue::Exprloc(expression) => evaluate_exprloc(debug_data_reader, expression, encoding, current_unit, name),
        gimli::AttributeValue::LocationListsRef(offset) => evaluate_location_list(debug_data_reader, offset, encoding, current_unit, name),
        gimli::AttributeValue::DebugLocListsIndex(index) => {
            // DWARF 5: index into the location list offset table of the unit (DW_FORM_loclistx)
            let (unit_header, _) = &debug_data_reader.units[current_unit];
            let unit = debug_data_reader.dwarf.unit(*unit_header).ok()?;
            match debug_data_reader.dwarf.locations_offset(&unit, index) {
                Ok(offset) => evaluate_location_list(debug_data_reader, offset, encoding, current_unit, name),
                Err(e) => {
                    log::debug!("get_location_attribute: '{name}': location list index {index:?} not resolved: {e}");
                    None
                }
            }
        }
        _ => {
            log::warn!("get_location_attribute: '{name}': unexpected location attribute type: {loc_attr:#?}");
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
    name: &str,
) -> Option<u64> {
    let loc_attr = get_attr_value(entry, gimli::constants::DW_AT_data_member_location)?;
    match loc_attr {
        gimli::AttributeValue::Exprloc(expression) => {
            if let Some((addr_ext, addr)) = evaluate_exprloc(debug_data_reader, expression, encoding, current_unit, name) {
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
            log::warn!("'{name}': unexpected data_member_location attribute: {other:?}");
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

// get the lower bound of an array dimension from the DW_AT_lower_bound attribute (0 for C/C++, the attribute is usually absent)
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

// get the number of elements of an array dimension from the DW_AT_count attribute (clang emits this instead of the upper bound)
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

// get the byte stride of an array from the DW_AT_byte_stride attribute
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

// Follow the DW_AT_specification attribute: the entry is the definition (out of line) of something declared in another entry,
// e.g. the definition of a C++ static class member or a namespace variable. Returns the declaration entry, which holds the
// name, type and scope, while the definition entry holds the location
pub(crate) fn get_specification_attribute<'data>(
    entry: &DebuggingInformationEntry<SliceType<'data>, usize>,
    unit: &UnitHeader<EndianSlice<'data, RunTimeEndian>>,
    abbrev: &gimli::Abbreviations,
) -> Option<DebuggingInformationEntry<EndianSlice<'data, RunTimeEndian>, usize>> {
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

// Follow the DW_AT_abstract_origin attribute: the entry is a concrete copy (inlined or out of line) of a function or of a
// variable of a function which the compiler inlined. Returns the "abstract instance" entry, which holds name and type
pub(crate) fn get_abstract_origin_attribute<'data>(
    entry: &DebuggingInformationEntry<SliceType<'data>, usize>,
    unit: &UnitHeader<EndianSlice<'data, RunTimeEndian>>,
    abbrev: &gimli::Abbreviations,
) -> Option<DebuggingInformationEntry<EndianSlice<'data, RunTimeEndian>, usize>> {
    let origin_attr = get_attr_value(entry, gimli::constants::DW_AT_abstract_origin)?;
    match origin_attr {
        gimli::AttributeValue::UnitRef(unitoffset) => unit.entry(abbrev, unitoffset).ok(),
        _ => None,
    }
}

// get the DW_AT_addr_base attribute of a compilation unit entry: the start of the unit's part of the DWARF 5 .debug_addr table,
// needed to resolve indexed addresses (DW_FORM_addrx / DW_OP_addrx) of the unit
pub(crate) fn get_addr_base_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<DebugAddrBase> {
    let origin_attr = get_attr_value(entry, gimli::constants::DW_AT_addr_base)?;
    match origin_attr {
        gimli::AttributeValue::DebugAddrBase(addr_base) => Some(addr_base),
        _ => None,
    }
}

// Evaluate a location list (a variable whose location depends on the PC, optimized code)
// The trigger point of the event is not known here, so the list is only accepted if all its entries describe the same memory
// location, which is then valid wherever the event is triggered. A variable which is held in a register or in different
// memory locations in parts of the function is not measurable, this is reported as address extension 0x80 like a register
// location of a single expression, so that the address is not looked up in the symbol table by name
fn evaluate_location_list(
    debug_data_reader: &DebugDataReader,
    offset: gimli::LocationListsOffset,
    encoding: gimli::Encoding,
    current_unit: usize,
    name: &str,
) -> Option<(u8, u64)> {
    let (unit_header, _) = &debug_data_reader.units[current_unit];

    // Create a Unit from the UnitHeader
    let unit = match debug_data_reader.dwarf.unit(*unit_header) {
        Ok(unit) => unit,
        Err(e) => {
            log::warn!("LocationList: '{name}': failed to create unit: {}", e);
            return None;
        }
    };

    // Get the location list
    let loclists = match debug_data_reader.dwarf.locations(&unit, offset) {
        Ok(loclists) => loclists,
        Err(e) => {
            log::warn!("LocationList: '{name}': failed to get location list at offset {:?}: {}", offset, e);
            return None;
        }
    };

    // Print
    log::debug!("LocationList: '{name}': offset={:?}, entries:", offset);

    let mut location: Option<(u8, u64)> = None;

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

        // Evaluate the expression, all entries must describe the same memory location
        match evaluate_exprloc(debug_data_reader, expression, encoding, current_unit, name) {
            Some(ea) if ea.0 < 0x80 => {
                log::debug!("    Evaluated Address: addr_ext={}, address=0x{:x}", ea.0, ea.1);
                if location.is_some_and(|l| l != ea) {
                    log::debug!("LocationList: '{name}': entries describe different locations, not measurable");
                    return Some((0x80, 0));
                }
                location = Some(ea);
            }
            _ => {
                log::debug!("LocationList: '{name}': entry is not a memory location, not measurable");
                return Some((0x80, 0));
            }
        }
    }

    // A list without entries has no location, like a missing location attribute
    location
}

// Evaluate an exprloc expression to get a variable address or struct member offset, as (address extension, address)
//
// A DWARF location expression is a small stack machine program (DW_OP_addr 0x2000, DW_OP_fbreg -20, DW_OP_plus_uconst 4, ...)
// which a debugger runs to find a variable. Typical expressions:
//   DW_OP_addr <address>        a global or static variable at a fixed address
//   DW_OP_addrx <index>         the same, DWARF 5, the address is in the .debug_addr table
//   DW_OP_fbreg <offset>        a stack variable, offset relative to the frame base of the function (DW_AT_frame_base)
//   DW_OP_reg<n>                the variable lives in register n, DW_OP_breg<n> <offset>: relative to register n
//   DW_OP_plus_uconst <offset>  member offset inside a struct (DW_AT_data_member_location)
//
// gimli evaluates the program and stops whenever it needs something only the running program knows (the frame base, a
// register value, a relocated address): evaluate() returns a RequiresXxx result, the caller supplies the value with
// resume_with_xxx() and the evaluation continues until Complete. This tool has no running program, so it supplies:
//   RequiresRelocatedAddress    the address itself (no relocation in a linked executable): address extension 0
//   RequiresFrameBase           the dummy frame base 0x80000000, so the result is 0x80000000 + offset: address extension 2,
//                               register_variables subtracts the dummy again and combines the offset with the event trigger
//   RequiresIndexedAddress      the address from the .debug_addr table of the unit: address extension 0
//   anything else               not measurable, reported with the internal address extensions 0x80 (register), 0x81 (thread
//                               local), 0x82 (other), no further evaluation
// The result is a list of "pieces" (a variable may be split over several locations), only a single piece is supported
fn evaluate_exprloc(
    debug_data_reader: &DebugDataReader,
    expression: gimli::Expression<EndianSlice<RunTimeEndian>>,
    encoding: gimli::Encoding,
    current_unit: usize,
    name: &str,
) -> Option<(u8, u64)> {
    let mut addr_ext = 0;
    let mut evaluation = expression.evaluation(encoding);
    evaluation.set_object_address(0);
    evaluation.set_initial_value(0);
    evaluation.set_max_iterations(100);
    let mut eval_result = evaluation
        .evaluate()
        .map_err(|e| {
            log::debug!("evaluate_exprloc: Initial evaluation failed: {e:?}");
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
                        log::debug!("evaluate_exprloc: resume_with_relocated_address failed: {e:?}");
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
                        log::debug!("evaluate_exprloc: resume_with_frame_base failed: {e:?}");
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
                let entry = entries.next_dfs().ok()??;
                let base = get_addr_base_attribute(entry)?;
                let addr = debug_data_reader.dwarf.debug_addr.get_address(address_size, base, index).ok()?;
                addr_ext = 0;
                eval_result = evaluation
                    .resume_with_indexed_address(addr)
                    .map_err(|e| {
                        log::debug!("evaluate_exprloc: resume_with_indexed_address failed: {e:?}");
                        e
                    })
                    .ok()?;
                log::debug!("RequiresIndexedAddress: resolved from .debug_addr[{:?}], addr_ext=0", index);
            }

            // Error: Not supported
            gimli::EvaluationResult::RequiresRegister { register, .. } => {
                // DW_OP_breg<N> <offset>: the location is (register N's runtime value + offset), i.e. an address, not a
                // plain register value - but we have no runtime register value to resolve it with.
                // this means it cannot be referenced and is not suitable for use in a2l yet
                // @@@@ xcp_client: allow register addresses ????
                addr_ext = 0x80;
                if super::stack_pointer_registers(debug_data_reader.architecture).contains(&register.0) {
                    // Seen with Clang: some locals are located directly as "DW_OP_breg<sp> <offset>" instead of going
                    // through DW_AT_frame_base/DW_OP_fbreg like GCC does (RequiresFrameBase, handled above). The offset
                    // here is relative to the live stack pointer, NOT to the frame base (they differ by the function's
                    // stack frame size), so it must not be reported with the frame-base address extension (2) - that
                    // would silently produce a wrong address. Needs its own address extension / resolution, not yet implemented.
                    log::warn!(
                        "RequiresRegister: '{name}': variable location is stack-pointer-relative (DW_OP_breg{} <offset>), not frame-base-relative; \
                         not measurable yet, eval_result={eval_result:?}",
                        register.0
                    );
                } else {
                    log::warn!("RequiresRegister: '{name}': expression not evaluated, unsupported, eval_result={eval_result:?}");
                }
                return Some((addr_ext, 0));
            }
            gimli::EvaluationResult::RequiresTls(address) => {
                // Thread local storage address
                // @@@@ xcp_client: allow TLS addresses ????
                addr_ext = 0x81;
                log::warn!("RequiresTls: '{name}': expression not evaluated, unsupported, eval_result={eval_result:?}");
                return Some((addr_ext, address));
            }
            // @@@@ TODO: Clarifiy if we need to handle RequiresCallFrameCfa
            _other => {
                // there are a lot of other types of address expressions that can only be evaluated by a debugger while a program is running
                // none of these can be handled in the a2lfile use-case.
                addr_ext = 0x82;
                log::warn!("Other: '{name}': expression not evaluated, unsupported, eval_result={_other:?}");
                return Some((addr_ext, 0));
            }
        };
    }

    let result = evaluation.result();
    if result.len() > 1 {
        log::warn!("evaluate_exprloc: '{name}': multiple pieces in evaluation result are not supported yet: {:?}", result);
        return None;
    }
    log::debug!("evaluate_exprloc: '{name}': evaluation result: {:?}", result[0]);
    if result.is_empty() {
        log::warn!("evaluate_exprloc: '{name}': evaluation result is empty");
        Some((0xFF, 0))
    } else {
        let (addr_ext, address) = match &result[0] {
            gimli::Piece {
                location: gimli::Location::Address { address },
                ..
            } => {
                log::debug!("evaluate_exprloc: '{name}': location is an address {}:0x{:08X}", addr_ext, *address);
                (addr_ext, *address)
            }

            gimli::Piece {
                location: gimli::Location::Register { register },
                ..
            } => {
                log::warn!("evaluate_exprloc: '{name}': location is a register {:?}", register);
                (0x80, 0)
            }

            gimli::Piece {
                location: gimli::Location::Value { value },
                ..
            } => {
                log::warn!("evaluate_exprloc: '{name}': location is a constant value {:?}", value);
                (0x81, value.to_u64(0).unwrap_or(0))
            }

            other => {
                log::warn!("evaluate_exprloc: '{name}': location evaluation result not handled {:?}", other);
                (0xFF, 0)
            }
        };
        Some((addr_ext, address))
    }
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

// get the DW_AT_declaration attribute: true if the entry only declares something which is defined elsewhere (a struct
// declared but not defined in this unit, a static class member, an extern variable)
pub(crate) fn get_declaration_attribute(entry: &DebuggingInformationEntry<SliceType, usize>) -> Option<bool> {
    let decl_attr = get_attr_value(entry, gimli::constants::DW_AT_declaration)?;
    if let gimli::AttributeValue::Flag(flag) = decl_attr { Some(flag) } else { None }
}
