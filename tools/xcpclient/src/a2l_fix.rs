//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module a2l_fix
// Correct the event ids and calibration segment numbers of an A2L file with the information of the XCP server (--fix-a2l)
//
// Without section registration (OPTION_SECTION_REGISTRATION) the target numbers events and calibration segments in the order of their
// creation at runtime, an A2L file generated offline from the ELF file has placeholder event ids and assumed segment numbers.
// The numbers are not only in the event and segment definitions, XCPlite encodes them in addresses as well:
//   - Dynamic (event relative) addressing, address extension 2..15: the upper 10 bits of the address are the event id (XCP_DYN_ADDR_EVENT_BITS)
//   - Calibration segment relative addressing: address 0x80000000 | segment number << 16 | offset (XcpAddrEncodeSegNumber)
// The registry of xcp_registry does not update these addresses for instances loaded from an A2L file, this is done here.

use std::collections::HashMap;
use std::error::Error;
use std::net::Ipv4Addr;
use std::path::Path;

use log::{info, warn};
use xcp_registry::{McA2mlMode, McAddress, Registry};

// XCPlite dynamic addressing, see src/xcp_cfg.h
const XCP_ADDR_EXT_DYN_FIRST: u8 = 0x02;
const XCP_ADDR_EXT_DYN_LAST: u8 = 0x0F;
const XCP_DYN_ADDR_EVENT_MASK: u16 = 0x3FF;
const XCP_DYN_ADDR_OFFSET_BITS: u32 = 22;
const XCP_DYN_ADDR_OFFSET_MASK: u32 = 0x003F_FFFF;

// XCPlite calibration segment relative addressing, see src/xcp_cfg.h
const XCP_ADDR_SEG_FLAG: u32 = 0x8000_0000;
const XCP_ADDR_SEG_NUMBER_MASK: u32 = 0x7FFF;

// The title comment xcpclient writes into the first lines of an A2L file it creates, see main.rs
const XCPCLIENT_TITLE: &str = "Created by xcp_client";

// Was the A2L file created by xcpclient (--create-a2l)? Only these files are corrected by --fix-a2l: the registry reads the A2L
// features which xcpclient writes, a file of another A2L creator would lose information when it is written again
pub fn is_created_by_xcpclient(a2l_text: &str) -> bool {
    a2l_text.lines().take(3).any(|line| line.contains(XCPCLIENT_TITLE))
}

// The XCPlite addressing scheme (A2L PROJECT_NO XCPLITE__<scheme>) of an A2L file, None if the file was not written for XCPlite
pub fn get_project_no(a2l_text: &str) -> Option<String> {
    let pos = a2l_text.find("PROJECT_NO")?;
    let project_no = a2l_text[pos + "PROJECT_NO".len()..].split_whitespace().next()?;
    Some(project_no.to_string())
}

// The XCP on Ethernet transport layer of an A2L file (protocol, address, port) from IF_DATA XCP_ON_UDP_IP or XCP_ON_TCP_IP
// Example: /begin XCP_ON_UDP_IP 0x0104 5555 ADDRESS "127.0.0.1" /end XCP_ON_UDP_IP
fn get_eth_transport_layer(a2l_text: &str) -> Option<(&'static str, Ipv4Addr, u16)> {
    for (keyword, protocol) in [("XCP_ON_UDP_IP", "UDP"), ("XCP_ON_TCP_IP", "TCP")] {
        if let Some(pos) = a2l_text.find(&format!("/begin {keyword}")) {
            let mut tokens = a2l_text[pos..].split_whitespace().skip(3); // /begin XCP_ON_xxx_IP version
            let port = tokens.next()?.parse::<u16>().ok()?;
            if tokens.next()? != "ADDRESS" {
                return None;
            }
            let addr = tokens.next()?.trim_matches('"').parse::<Ipv4Addr>().ok()?;
            return Some((protocol, addr, port));
        }
    }
    None
}

// Address extension of calibration segment relative addressing for an XCPlite addressing scheme (XCPLITE__CASDD: 0, XCPLITE__ACSDD: 1)
// The first letter is the meaning of address extension 0, the second of address extension 1, 'C' is calibration segment relative
fn get_seg_addr_ext(project_no: Option<&str>) -> Option<u8> {
    let scheme = project_no?.strip_prefix("XCPLITE__")?;
    scheme.chars().take(2).position(|c| c == 'C').map(|i| i as u8)
}

// Apply the event id and calibration segment number mappings (A2L -> target) to a registry loaded from an A2L file
// Events, calibration segments and the XCPlite specific encoding of event ids and segment numbers in the addresses of the instances
pub fn apply_mappings(reg: &mut Registry, event_mapping: &HashMap<u16, u16>, seg_mapping: &HashMap<u16, u16>, project_no: Option<&str>) {
    let seg_addr_ext = get_seg_addr_ext(project_no);

    for event in &mut reg.event_list {
        if let Some(new_id) = event_mapping.get(&event.get_id()) {
            event.set_id(*new_id);
        }
    }

    for seg in reg.cal_seg_list.iter_mut() {
        if let Some(&new_index) = seg_mapping.get(&seg.get_index()) {
            seg.set_index(new_index);
            seg.set_number(u8::try_from(new_index).ok());
            // In calibration segment relative addressing mode, the address of the memory segment contains the segment number
            if seg_addr_ext == Some(0) && seg.addr & XCP_ADDR_SEG_FLAG != 0 {
                seg.addr = XCP_ADDR_SEG_FLAG | ((new_index as u32) << 16) | (seg.addr & 0xFFFF);
            }
        }
    }

    for instance in reg.instance_list.iter_mut() {
        let address = instance.address;
        if !address.get_addr_mode().is_a2l() {
            // Calibration segment relative (McAddrMode::Cal) addresses are calculated from the segment index when the A2L file is written
            if address.is_event_relative() {
                warn!("Instance '{}' has an event relative address, its event id is not corrected", instance.name);
            }
            continue;
        }
        let (addr_ext, addr) = address.get_raw_a2l_addr();
        let old_event = address.get_event_id();
        let new_event = old_event.map(|id| *event_mapping.get(&id).unwrap_or(&id));
        let mut new_addr = addr;

        // Event id in the address of dynamic addressing
        if (XCP_ADDR_EXT_DYN_FIRST..=XCP_ADDR_EXT_DYN_LAST).contains(&addr_ext) {
            let addr_event = (addr >> XCP_DYN_ADDR_OFFSET_BITS) as u16;
            // The event of the address is the event of the instance (IF_DATA), otherwise find it in the mapping by its 10 bit id
            let new_addr_event = match old_event {
                Some(id) if id & XCP_DYN_ADDR_EVENT_MASK == addr_event => new_event,
                Some(id) => {
                    warn!(
                        "Instance '{}': the event id {} in the address 0x{:08X} does not match its event {}, address not corrected",
                        instance.name, addr_event, addr, id
                    );
                    None
                }
                None => event_mapping.iter().find(|(k, _)| **k & XCP_DYN_ADDR_EVENT_MASK == addr_event).map(|(_, v)| *v),
            };
            if let Some(id) = new_addr_event {
                new_addr = ((id & XCP_DYN_ADDR_EVENT_MASK) as u32) << XCP_DYN_ADDR_OFFSET_BITS | (addr & XCP_DYN_ADDR_OFFSET_MASK);
            }
        }
        // Segment number in the address of calibration segment relative addressing
        else if Some(addr_ext) == seg_addr_ext && addr & XCP_ADDR_SEG_FLAG != 0 {
            let number = ((addr >> 16) & XCP_ADDR_SEG_NUMBER_MASK) as u16;
            if let Some(&new_number) = seg_mapping.get(&number) {
                new_addr = XCP_ADDR_SEG_FLAG | ((new_number as u32) << 16) | (addr & 0xFFFF);
            }
        }

        if new_addr != addr || new_event != old_event {
            instance.address = match new_event {
                Some(id) => McAddress::new_a2l_with_event(id, new_addr, addr_ext),
                None => McAddress::new_a2l(new_addr, addr_ext),
            };
        }
    }
}

// Rewrite the A2L file with the event ids and calibration segment numbers of the target
// The file is loaded again without flattening the typedefs, so its structure is kept, the original is kept as <file>.bak
// Only for A2L files created by xcpclient, see is_created_by_xcpclient
// The transport layer parameters of the A2L file are kept, if the file has none, the ones of the current connection (tl_params) are used
// The A2ML description is embedded if requested (embed_aml) or if it was embedded in the original file, otherwise XCP_104.aml is included
pub fn rewrite_a2l_file(
    a2l_path: &Path,
    event_mapping: &HashMap<u16, u16>,
    seg_mapping: &HashMap<u16, u16>,
    tl_params: Option<(&'static str, Ipv4Addr, u16)>,
    embed_aml: bool,
) -> Result<(), Box<dyn Error>> {
    let a2l_text = std::fs::read_to_string(a2l_path)?;
    assert!(is_created_by_xcpclient(&a2l_text));
    let project_no = get_project_no(&a2l_text);

    // Same registry modes as for the creation of the A2L file, the typedefs and names are written as they are
    let mut reg = Registry::new();
    reg.set_flatten_typedefs_mode(false);
    reg.set_prefix_names_mode(false);
    reg.set_a2ml_mode(if embed_aml || a2l_text.contains("/begin A2ML") { McA2mlMode::Embed } else { McA2mlMode::Include });
    reg.load_a2l(&a2l_path, false, true, false, false)?;
    apply_mappings(&mut reg, event_mapping, seg_mapping, project_no.as_deref());

    // The registry does not read the transport layer from the A2L file, the IF_DATA XCP of the module is only written with it
    match get_eth_transport_layer(&a2l_text).or(tl_params) {
        Some((protocol, addr, port)) => reg.set_xcp_eth_params(protocol, addr, port),
        None => warn!("A2L file {}: no XCP on Ethernet transport layer found, the IF_DATA XCP of the module is not written", a2l_path.display()),
    }

    let backup_path = a2l_path.with_extension("a2l.bak");
    std::fs::copy(a2l_path, &backup_path)?;

    let name = reg.application.get_name().to_string();
    let title = format!(
        "{} with event ids and calibration segment numbers corrected from the XCP server (--fix-a2l) - {}",
        XCPCLIENT_TITLE,
        chrono::Utc::now().format("%Y-%m-%d %H:%M:%S")
    );
    reg.write_a2l(&a2l_path, &title, &name, "", &name, project_no.as_deref().unwrap_or("XCPLITE__ACSDD"), true)?;
    info!("A2L file {} rewritten with the event ids and segment numbers of the target, the original is {}", a2l_path.display(), backup_path.display());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use xcp_registry::{McDimType, McInstance, McSupportData, McValueType};

    fn instance(reg: &Registry, name: &str) -> McAddress {
        reg.instance_list.iter().find(|i| i.name == name).unwrap().address
    }

    #[test]
    fn test_seg_addr_ext() {
        assert_eq!(get_seg_addr_ext(Some("XCPLITE__CASDD")), Some(0));
        assert_eq!(get_seg_addr_ext(Some("XCPLITE__ACSDD")), Some(1));
        assert_eq!(get_seg_addr_ext(Some("XCPLITE__AXSDD")), None);
        assert_eq!(get_seg_addr_ext(Some("OTHER")), None);
        assert_eq!(get_seg_addr_ext(None), None);
    }

    #[test]
    fn test_eth_transport_layer() {
        let text = r#"/begin XCP_ON_UDP_IP 0x0104 5555 ADDRESS "192.168.0.206" /end XCP_ON_UDP_IP"#;
        assert_eq!(get_eth_transport_layer(text), Some(("UDP", Ipv4Addr::new(192, 168, 0, 206), 5555)));
        assert_eq!(get_eth_transport_layer("no transport layer"), None);
    }

    #[test]
    fn test_apply_mappings() {
        let mut reg = Registry::new();
        let dim_type = McDimType::new(McValueType::Ulong, 1, 1);
        let support = || McSupportData::new(xcp_registry::McObjectType::Measurement);
        // Stack variable of event 0xFFFE (placeholder), offset 0xFFDA, address extension 2
        let stack = McAddress::new_a2l_with_event(0xFFFE, 0xFF80_FFDA, 2);
        reg.instance_list.push(McInstance::new("stack", dim_type.clone(), support(), stack));
        // Calibration parameter in segment 1, offset 4, calibration segment relative address extension 0 (CASDD)
        reg.instance_list.push(McInstance::new("cal", dim_type.clone(), support(), McAddress::new_a2l(0x8001_0004, 0)));
        // Absolute address, unchanged
        reg.instance_list.push(McInstance::new("abs", dim_type, support(), McAddress::new_a2l_with_event(0xFFFE, 0x8001_0004, 1)));

        let event_mapping = HashMap::from([(0xFFFE, 3)]);
        let seg_mapping = HashMap::from([(1, 2)]);
        apply_mappings(&mut reg, &event_mapping, &seg_mapping, Some("XCPLITE__CASDD"));

        assert_eq!(instance(&reg, "stack").get_raw_a2l_addr(), (2, (3 << 22) | 0xFFDA));
        assert_eq!(instance(&reg, "stack").get_event_id(), Some(3));
        assert_eq!(instance(&reg, "cal").get_raw_a2l_addr(), (0, 0x8002_0004));
        assert_eq!(instance(&reg, "abs").get_raw_a2l_addr(), (1, 0x8001_0004));
        assert_eq!(instance(&reg, "abs").get_event_id(), Some(3));
    }
}
