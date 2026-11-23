//--------------------------------------------------------------------------------------------------------------------------------------------------
// daq.rs — DAQ measurement object management and start/stop

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::error::Error;

use super::*;

// Limits of the DAQ setup
const MAX_DAQ_COUNT: usize = 1024; // DAQ lists
const MAX_ODT_ENTRY_COUNT: usize = 0x7B; // ODT entries in an ODT
const DAQ_TIMESTAMP_SIZE: usize = 4; // Size of the timestamp in the first ODT of a DAQ list

// Signals of the span record of a context: <context>.span, <context>.level, <context>.span_dt and <context>.thread
const SPAN_RECORD_SUFFIXES: [&str; 4] = ["span", "level", "span_dt", "thread"];

/// A block of memory which is measured with one ODT entry
/// The signals in a block are adjacent in memory
#[derive(Debug, Clone, Copy, PartialEq)]
struct DaqBlock {
    ext: u8,
    addr: u32,
    size: usize,
}

/// A DAQ list with its ODT entries and signals
#[derive(Debug, PartialEq)]
struct DaqListPlan {
    event: u16,
    blocks: Vec<DaqBlock>,        // ODT entries
    signals: Vec<(usize, usize)>, // Measurement object index and offset in the payload of the DAQ packet, sorted by index
    size: usize,                  // Payload size in bytes
}

/// Distribute the measurement objects to DAQ lists and ODT entries
/// Returns the DAQ lists, sorted by event id
///
/// Signals of an event which are adjacent in memory (same address extension, no gap) are merged to a block, which is measured
/// with one ODT entry of up to max_odt_entry_size bytes (limit of the XCP server). This saves DAQ memory and copy operations in
/// the XCP server. A signal can not be split, it has to fit into one ODT entry and into one ODT.
///
/// There is only one ODT per DAQ list. An event with more ODT entries than fit into one ODT (max_odt_entry_count entries,
/// max_odt_size bytes) gets as many DAQ lists as needed. The XCP server samples all DAQ lists of an event with the same timestamp.
///
/// The signals are in address order in the DAQ packet, the span record of a context is moved to the front, because it has to be
/// in one DAQ list (it is decoded as a whole). The order of the signals in a DAQ list is the order of the measurement objects.
fn split_daq_lists(objects: &[XcpClientMeasurementObject], max_odt_entry_count: usize, max_odt_entry_size: usize, max_odt_size: usize) -> Result<Vec<DaqListPlan>, String> {
    // Signal size, a signal can not be split over several ODT entries, ODTs or DAQ lists
    for m in objects {
        let size = m.a2l_type.size;
        if size > max_odt_entry_size {
            return Err(format!(
                "Measurement object {} is too large: {} bytes, a signal can not be split and the XCP server supports {} bytes per signal (ODT entry)",
                m.name, size, max_odt_entry_size
            ));
        }
        if size > max_odt_size {
            return Err(format!(
                "Measurement object {} is too large: {} bytes, a signal can not be split and a DAQ packet of the XCP server holds {} bytes (MAX_DTO without header and timestamp)",
                m.name, size, max_odt_size
            ));
        }
    }
    let max_block_size = max_odt_entry_size.min(max_odt_size);

    // Events, sorted by event id
    let mut events: Vec<u16> = objects.iter().filter_map(|m| m.a2l_addr.event).collect();
    events.sort_unstable();
    events.dedup();

    let mut daq_lists: Vec<DaqListPlan> = Vec::new();
    for event in events {
        let mut indices: Vec<usize> = (0..objects.len()).filter(|&i| objects[i].a2l_addr.event == Some(event)).collect();

        // Sort by address, the span record of a context to the front
        let contexts: Vec<&str> = indices.iter().filter_map(|&i| objects[i].name.strip_suffix(".span")).collect();
        let is_span_record = |i: usize| {
            contexts.iter().any(|context| {
                objects[i]
                    .name
                    .strip_prefix(context)
                    .and_then(|s| s.strip_prefix('.'))
                    .is_some_and(|s| SPAN_RECORD_SUFFIXES.contains(&s))
            })
        };
        indices.sort_by_key(|&i| (!is_span_record(i), objects[i].a2l_addr.ext, objects[i].a2l_addr.addr, i));

        // Merge adjacent signals to blocks
        // A block does not cross a 32 KiB boundary of the address: the address may be an encoded value with a signed offset
        // in its lower bits (XCPlite event relative addressing), where consecutive addresses are not adjacent in memory
        let mut blocks: Vec<(DaqBlock, Vec<usize>)> = Vec::new();
        for i in indices {
            let A2lAddr { ext, addr, .. } = objects[i].a2l_addr;
            let size = objects[i].a2l_type.size;
            if let Some((block, signals)) = blocks.last_mut() {
                let end = addr as u64 + size as u64;
                if block.ext == ext
                    && block.addr as u64 + block.size as u64 == addr as u64
                    && block.size + size <= max_block_size
                    && end > 0
                    && (block.addr as u64 >> 15) == ((end - 1) >> 15)
                    && is_span_record(signals[0]) == is_span_record(i)
                {
                    block.size += size;
                    signals.push(i);
                    continue;
                }
            }
            blocks.push((DaqBlock { ext, addr, size }, vec![i]));
        }

        // Fill the DAQ lists with the blocks
        let new_daq_list = || DaqListPlan {
            event,
            blocks: Vec::new(),
            signals: Vec::new(),
            size: 0,
        };
        let mut daq_list = new_daq_list();
        for (block, signals) in blocks {
            if daq_list.blocks.len() >= max_odt_entry_count || daq_list.size + block.size > max_odt_size {
                daq_lists.push(std::mem::replace(&mut daq_list, new_daq_list()));
            }
            for i in signals {
                daq_list.signals.push((i, daq_list.size + (objects[i].a2l_addr.addr - block.addr) as usize));
            }
            daq_list.blocks.push(block);
            daq_list.size += block.size;
        }
        daq_lists.push(daq_list);
    }
    for daq_list in &mut daq_lists {
        daq_list.signals.sort_unstable();
    }

    if daq_lists.len() > MAX_DAQ_COUNT {
        return Err(format!("Too many DAQ lists needed: {}, limit is {}", daq_lists.len(), MAX_DAQ_COUNT));
    }
    Ok(daq_lists)
}

impl XcpClient {
    //------------------------------------------------------------------------
    // XcpMeasurementObject, XcpMeasurementObjectHandle (index pointer to XcpMeasurementObject)

    /// Create a measurement object by name from the registry
    /// name may be a regular expression matching exactly one measurement
    pub fn create_measurement_object(&mut self, name: &str) -> Option<XcpMeasurementObjectHandle> {
        let registry = self.registry.as_ref().unwrap();
        match registry.instance_list.get_instance(name, xcp_registry::McObjectType::Measurement, None) {
            None => {
                debug!("Measurement {} not found", name);
                None
            }
            Some(instance) => {
                let (ext, addr) = instance.get_address().get_a2l_addr(registry);
                // Measurement objects without a fixed event (global variables) are measured with the default event, if specified
                let event = instance.event_id().or(self.default_event);
                if event.is_none() {
                    log::warn!(
                        "Measurement object {} has no event and no default event is specified (--default-event), addr = {}:0x{:0X}",
                        name,
                        ext,
                        addr
                    );
                    return None;
                }
                let a2l_addr: A2lAddr = A2lAddr { ext, addr, event };
                let a2l_type: A2lType = A2lType {
                    size: instance.value_size(),
                    encoding: instance.value_type().into(),
                };
                let conversion = A2lConversion::from(instance.get_mc_support_data());
                let o = XcpClientMeasurementObject::new(name, a2l_addr, a2l_type, conversion);
                debug!("Create measurement object {}: addr = {:08X} type = {:?}", name, a2l_addr.addr, a2l_type);
                debug!("-> {:?} ", o);
                self.measurement_object_list.push(o);
                Some(XcpMeasurementObjectHandle(self.measurement_object_list.len() - 1))
            }
        }
    }

    pub fn get_measurement_object(&self, handle: XcpMeasurementObjectHandle) -> &XcpClientMeasurementObject {
        &self.measurement_object_list[handle.0]
    }

    /// Unique name of the fixed event of a measurement object, None if it has none or the event is not in the registry event list
    pub fn measurement_event_name(&self, name: &str) -> Option<String> {
        let registry = self.registry.as_ref()?;
        let instance = registry.instance_list.get_instance(name, xcp_registry::McObjectType::Measurement, None)?;
        self.event_name(instance.event_id()?)
    }

    //------------------------------------------------------------------------
    // DAQ event names

    /// Unique name of an event from the registry event list (with instance index), None if the event is not in the list
    fn event_name(&self, event: u16) -> Option<String> {
        self.registry
            .as_ref()
            .and_then(|reg| reg.event_list.find_event_id(event).map(|e| e.get_unique_name(reg).into_owned()))
    }

    //------------------------------------------------------------------------
    // DAQ init, start, stop

    /// Get clock resolution in ns
    pub fn get_timestamp_resolution(&self) -> u64 {
        self.timestamp_resolution_ns
    }

    /// Start DAQ
    pub async fn start_measurement(&mut self) -> Result<(), Box<dyn Error>> {
        debug!("Start measurement");

        // Distribute the signals to DAQ lists, an event may need more than one
        // Limits are checked here, before the DAQ setup of the XCP server is changed
        // Payload of the ODT: MAX_DTO without the DAQ header and the timestamp
        let max_odt_size = (self.max_dto_size as usize).saturating_sub(self.daq_header_size as usize + DAQ_TIMESTAMP_SIZE);
        let daq_list_plan = split_daq_lists(&self.measurement_object_list, MAX_ODT_ENTRY_COUNT, self.max_odt_entry_size as usize, max_odt_size)?;
        let daq_count: u16 = daq_list_plan.len() as u16;
        debug!("daq count = {}", daq_count);
        let mut daq_lists: Vec<XcpDaqList> = Vec::with_capacity(daq_list_plan.len());

        // Alloc the DAQ lists
        self.free_daq().await?;
        self.alloc_daq(daq_count).await?;
        debug!("alloc_daq count={}", daq_count);

        // Alloc one ODT for each DAQ list
        // Restriction: Only one ODT per DAQ list supported, an event with more ODT entries has more than one DAQ list
        for daq in 0..daq_count {
            self.alloc_odt(daq, 1).await?;
            debug!("Alloc daq={}, odt_count={}", daq, 1);
        }

        // Alloc ODT entries (block count) for each ODT/DAQ list
        for daq in 0..daq_count {
            let odt_entry_count = daq_list_plan[daq as usize].blocks.len();
            self.alloc_odt_entries(daq, 0, odt_entry_count as u8).await?;
            debug!("Alloc odt_entries: daq={}, odt={}, odt_entry_count={}", daq, 0, odt_entry_count);
        }

        // Create all ODT entries for each DAQ list and store information for the DAQ decoder
        for daq in 0..daq_count {
            let plan = &daq_list_plan[daq as usize];
            let odt = 0; // Only one odt per daq list supported

            // Create the ODT entries for this daq list, one for each block of adjacent signals
            // Sizes are within the limits, see split_daq_lists
            self.set_daq_ptr(daq, odt, 0).await?;
            for block in &plan.blocks {
                debug!("WRITE_DAQ daq={}, odt={}, ext={}, addr=0x{:08X}, size={}", daq, odt, block.ext, block.addr, block.size);
                self.write_daq(block.ext, block.addr, block.size as u8).await?;
            }

            // Signals of this daq list, for the DAQ decoder
            let mut odt_entries = Vec::with_capacity(plan.signals.len());
            for &(i, offset) in &plan.signals {
                let m = &mut self.measurement_object_list[i];
                m.daq = daq;
                m.odt = odt;
                m.offset = offset as u16 + 6;
                debug!(
                    "Signal {} daq={}, odt={}, type={:?}, size={}, offset={}",
                    m.name, daq, odt, m.a2l_type.encoding, m.a2l_type.size, m.offset
                );
                odt_entries.push(OdtEntry {
                    name: m.name.clone(),
                    a2l_type: m.a2l_type,
                    a2l_addr: m.a2l_addr,
                    conversion: m.conversion.clone(),
                    offset: offset as u16,
                });
            }

            // Event name from the registry event list, for the DAQ decoder
            daq_lists.push(XcpDaqList {
                event_id: plan.event,
                event_name: self.event_name(plan.event),
                entries: odt_entries,
            });
        }

        // Set DAQ list events
        for daq in 0..daq_count {
            let event = daq_list_plan[daq as usize].event;
            self.set_daq_list_mode(daq, event).await?;
            debug!("Set event: daq={}, event={}", daq, event);
        }

        // Select and prepare all DAQ lists
        for daq in 0..daq_count {
            self.select_daq_list(daq).await?;
        }
        self.prepare_selected_daq_lists().await?;

        // Reset the DAQ decoder and set measurement start time
        let daq_clock = self.get_daq_clock_raw().await?;
        self.daq_decoder.as_ref().unwrap().lock().start(daq_lists, daq_clock);

        // Send running=true throught the DAQ control channel to the receive task
        self.task_control.running = true;
        self.tx_task_control.as_ref().unwrap().send(self.task_control).await.unwrap();

        // Start DAQ
        self.start_selected_daq_lists().await?;

        Ok(())
    }

    /// Stop DAQ
    pub async fn stop_measurement(&mut self) -> Result<(), Box<dyn Error>> {
        debug!("Stop measurement");

        // Stop DAQ
        let res = self.stop_all_daq_lists().await;

        // Send running=false throught the DAQ control channel to the receive task
        self.task_control.running = false;
        self.tx_task_control.as_ref().unwrap().send(self.task_control).await?;

        // Stop the DAQ decoder
        self.daq_decoder.as_ref().unwrap().lock().stop();

        // Clear the measurement object list
        self.measurement_object_list.clear();

        res
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn object(name: &str, event: u16, ext: u8, addr: u32, size: usize) -> XcpClientMeasurementObject {
        let a2l_addr = A2lAddr { ext, addr, event: Some(event) };
        let a2l_type = A2lType {
            size,
            encoding: A2lTypeEncoding::Unsigned,
        };
        XcpClientMeasurementObject::new(name, a2l_addr, a2l_type, A2lConversion::default())
    }

    // Event, ODT entries as (addr, size) and signals as (index, offset) of each DAQ list
    type Plan = Vec<(u16, Vec<(u32, usize)>, Vec<(usize, usize)>)>;
    fn plan(objects: &[XcpClientMeasurementObject], max_odt_entry_count: usize, max_odt_entry_size: usize, max_odt_size: usize) -> Plan {
        split_daq_lists(objects, max_odt_entry_count, max_odt_entry_size, max_odt_size)
            .unwrap()
            .into_iter()
            .map(|d| {
                assert_eq!(d.size, d.blocks.iter().map(|b| b.size).sum::<usize>());
                (d.event, d.blocks.iter().map(|b| (b.addr, b.size)).collect(), d.signals)
            })
            .collect()
    }

    #[test]
    fn test_split_daq_lists() {
        // One DAQ list per event, sorted by event id, signals which are not adjacent are not merged
        let objects = vec![object("b1", 7, 0, 0x100, 4), object("a1", 2, 0, 0x200, 8), object("b2", 7, 0, 0x110, 1)];
        assert_eq!(
            plan(&objects, 123, 248, 1016),
            vec![(2, vec![(0x200, 8)], vec![(1, 0)]), (7, vec![(0x100, 4), (0x110, 1)], vec![(0, 0), (2, 4)])]
        );

        // Adjacent signals are merged to one ODT entry, in address order, the signals of a DAQ list are in object order
        // A gap, an overlap and another address extension start a new ODT entry
        let objects = vec![
            object("y", 1, 0, 0x104, 4),
            object("x", 1, 0, 0x100, 4),
            object("z", 1, 0, 0x108, 2),
            object("gap", 1, 0, 0x10C, 4),
            object("overlap", 1, 0, 0x10C, 2),
            object("ext", 1, 1, 0x110, 4),
        ];
        assert_eq!(
            plan(&objects, 123, 248, 1016),
            vec![(
                1,
                vec![(0x100, 10), (0x10C, 4), (0x10C, 2), (0x110, 4)],
                vec![(0, 4), (1, 0), (2, 8), (3, 10), (4, 14), (5, 16)]
            )]
        );

        // An ODT entry is limited to the maximum ODT entry size, it is cut at a signal boundary
        let objects: Vec<_> = (0..100).map(|i| object(&format!("s{}", i), 3, 0, 0x1000 + i * 4, 4)).collect();
        let daq_lists = plan(&objects, 123, 10, 1016);
        assert_eq!(daq_lists.len(), 1);
        assert_eq!(daq_lists[0].1.len(), 50);
        assert!(daq_lists[0].1.iter().enumerate().all(|(i, b)| *b == (0x1000 + i as u32 * 8, 8)));
        assert_eq!(daq_lists[0].2, (0..100).map(|i| (i, i * 4)).collect::<Vec<_>>());
        // It is limited to the ODT size as well
        assert_eq!(plan(&objects, 123, 248, 16).len(), 25);

        // Split by ODT entry count: 250 signals which are not adjacent
        let objects: Vec<_> = (0..250).map(|i| object(&format!("s{}", i), 3, 0, i * 2, 1)).collect();
        let daq_lists = plan(&objects, 123, 248, 1016);
        assert_eq!(
            daq_lists.iter().map(|d| (d.0, d.1.len(), d.2.len())).collect::<Vec<_>>(),
            vec![(3, 123, 123), (3, 123, 123), (3, 4, 4)]
        );
        assert_eq!(daq_lists.iter().flat_map(|d| d.2.iter().map(|s| s.0)).collect::<Vec<_>>(), (0..250).collect::<Vec<_>>());

        // Split by ODT size: 8+8 fits exactly, the offsets start again in the next DAQ list
        let objects = vec![
            object("a", 1, 0, 0x00, 8),
            object("b", 1, 0, 0x10, 8),
            object("c", 1, 0, 0x20, 4),
            object("d", 1, 0, 0x30, 8),
        ];
        assert_eq!(
            plan(&objects, 123, 248, 16),
            vec![(1, vec![(0x00, 8), (0x10, 8)], vec![(0, 0), (1, 8)]), (1, vec![(0x20, 4), (0x30, 8)], vec![(2, 0), (3, 4)])]
        );

        // The span record of a context is in the first DAQ list and not merged with other signals
        let objects = vec![
            object("x", 1, 0, 0x00, 8),
            object("ctx.span", 1, 0, 0x10, 1),
            object("y", 1, 0, 0x08, 8),
            object("ctx.level", 1, 0, 0x11, 1),
            object("ctx.span_dt", 1, 0, 0x14, 4),
            object("ctx.other", 1, 0, 0x18, 4),
        ];
        assert_eq!(
            plan(&objects, 123, 248, 16),
            vec![
                (1, vec![(0x10, 2), (0x14, 4)], vec![(1, 0), (3, 1), (4, 2)]),
                (1, vec![(0x00, 16)], vec![(0, 0), (2, 8)]),
                (1, vec![(0x18, 4)], vec![(5, 0)])
            ]
        );

        // No merge over a 32 KiB boundary of the address (signed offset of XCPlite event relative addressing)
        let objects = vec![object("a", 1, 2, 0x0005_7FFC, 4), object("b", 1, 2, 0x0005_8000, 4), object("c", 1, 2, 0x0005_8004, 4)];
        assert_eq!(plan(&objects, 123, 248, 1016)[0].1, vec![(0x0005_7FFC, 4), (0x0005_8000, 8)]);

        // Signal too large for an ODT entry (limit of the XCP server) or for the ODT
        assert!(split_daq_lists(&[object("big", 1, 0, 0, 249)], 123, 248, 1016).is_err());
        assert!(split_daq_lists(&[object("big", 1, 0, 0, 248)], 123, 248, 1016).is_ok());
        assert!(split_daq_lists(&[object("big", 1, 0, 0, 32)], 123, 255, 16).is_err());

        // Too many DAQ lists
        let objects: Vec<_> = (0..MAX_DAQ_COUNT as u32 + 1).map(|i| object(&format!("s{}", i), 1, 0, i * 2, 1)).collect();
        assert!(split_daq_lists(&objects, 1, 255, 1016).is_err());
        assert!(split_daq_lists(&objects[..MAX_DAQ_COUNT], 1, 255, 1016).is_ok());
    }
}
