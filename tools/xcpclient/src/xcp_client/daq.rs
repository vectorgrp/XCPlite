//--------------------------------------------------------------------------------------------------------------------------------------------------
// daq.rs — DAQ measurement object management and start/stop

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::error::Error;

use super::*;

// Limits of the DAQ setup
const MAX_DAQ_COUNT: usize = 1024; // DAQ lists
const MAX_ODT_ENTRY_COUNT: usize = 0x7B; // ODT entries (signals) in an ODT
const MAX_ODT_ENTRY_SIZE: usize = 255; // Size of an ODT entry (signal) in bytes
const DAQ_TIMESTAMP_SIZE: usize = 4; // Size of the timestamp in the first ODT of a DAQ list

// Signals of the span record of a context: <context>.span, <context>.level, <context>.span_dt and <context>.thread
const SPAN_RECORD_SUFFIXES: [&str; 4] = ["span", "level", "span_dt", "thread"];

/// Distribute the measurement objects to DAQ lists
/// Returns the event id and the measurement object indices of each DAQ list, sorted by event id
///
/// There is only one ODT per DAQ list. An event with more signals than fit into one ODT (max_odt_entry_count signals,
/// max_odt_size bytes) gets as many DAQ lists as needed. The XCP server samples all DAQ lists of an event with the same timestamp.
/// The order of the signals of an event is kept, with one exception: when an event is split, the span record of a context is
/// moved to the front, because it has to be in one DAQ list (it is decoded as a whole).
fn split_daq_lists(objects: &[XcpClientMeasurementObject], max_odt_entry_count: usize, max_odt_size: usize) -> Result<Vec<(u16, Vec<usize>)>, String> {
    // Signal size
    let max_size = MAX_ODT_ENTRY_SIZE.min(max_odt_size);
    for m in objects {
        if m.a2l_type.size > max_size {
            return Err(format!(
                "Measurement object {} is too large: {} bytes, limit is {} bytes",
                m.name, m.a2l_type.size, max_size
            ));
        }
    }

    // Events, sorted by event id
    let mut events: Vec<u16> = objects.iter().filter_map(|m| m.a2l_addr.event).collect();
    events.sort_unstable();
    events.dedup();

    let mut daq_lists: Vec<(u16, Vec<usize>)> = Vec::new();
    for event in events {
        let mut indices: Vec<usize> = (0..objects.len()).filter(|&i| objects[i].a2l_addr.event == Some(event)).collect();

        // Span record to the front, if the event does not fit into one DAQ list
        let size: usize = indices.iter().map(|&i| objects[i].a2l_type.size).sum();
        if indices.len() > max_odt_entry_count || size > max_odt_size {
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
            indices.sort_by_key(|&i| !is_span_record(i)); // stable
        }

        // Fill the DAQ lists in order
        let mut entries: Vec<usize> = Vec::new();
        let mut odt_size: usize = 0;
        for i in indices {
            let size = objects[i].a2l_type.size;
            if entries.len() >= max_odt_entry_count || odt_size + size > max_odt_size {
                daq_lists.push((event, std::mem::take(&mut entries)));
                odt_size = 0;
            }
            entries.push(i);
            odt_size += size;
        }
        daq_lists.push((event, entries));
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
        let daq_list_plan = split_daq_lists(&self.measurement_object_list, MAX_ODT_ENTRY_COUNT, max_odt_size)?;
        let daq_count: u16 = daq_list_plan.len() as u16;
        debug!("daq count = {}", daq_count);
        let mut daq_lists: Vec<XcpDaqList> = Vec::with_capacity(daq_list_plan.len());

        // Alloc the DAQ lists
        self.free_daq().await?;
        self.alloc_daq(daq_count).await?;
        debug!("alloc_daq count={}", daq_count);

        // Alloc one ODT for each DAQ list
        // Restriction: Only one ODT per DAQ list supported, an event with more signals has more than one DAQ list
        for daq in 0..daq_count {
            self.alloc_odt(daq, 1).await?;
            debug!("Alloc daq={}, odt_count={}", daq, 1);
        }

        // Alloc ODT entries (signal count) for each ODT/DAQ list
        for daq in 0..daq_count {
            let odt_entry_count = daq_list_plan[daq as usize].1.len();
            self.alloc_odt_entries(daq, 0, odt_entry_count as u8).await?;
            debug!("Alloc odt_entries: daq={}, odt={}, odt_entry_count={}", daq, 0, odt_entry_count);
        }

        // Create all ODT entries for each DAQ list and store information for the DAQ decoder
        for daq in 0..daq_count {
            let event = daq_list_plan[daq as usize].0;
            let odt = 0; // Only one odt per daq list supported

            // Create ODT entries for this daq list
            let mut odt_entries = Vec::new();
            let mut odt_size: u16 = 0;
            self.set_daq_ptr(daq, odt, 0).await?;
            for &i in &daq_list_plan[daq as usize].1 {
                let m = &mut self.measurement_object_list[i];
                let a2l_addr = m.a2l_addr;
                let a2l_type: A2lType = m.a2l_type;
                m.daq = daq;
                m.odt = odt;
                m.offset = odt_size + 6;

                debug!(
                    "WRITE_DAQ {} daq={}, odt={},  type={:?}, size={}, ext={}, addr=0x{:08X}, offset={}",
                    m.name,
                    daq,
                    odt,
                    a2l_type.encoding,
                    a2l_type.size,
                    a2l_addr.ext,
                    a2l_addr.addr,
                    odt_size + 6
                );

                odt_entries.push(OdtEntry {
                    name: m.name.clone(),
                    a2l_type,
                    a2l_addr,
                    conversion: m.conversion.clone(),
                    offset: odt_size,
                });

                // Sizes are within the limits, see split_daq_lists
                self.write_daq(a2l_addr.ext, a2l_addr.addr, a2l_type.size as u8).await?;
                odt_size += a2l_type.size as u16;
            }

            // Event name from the registry event list, for the DAQ decoder
            daq_lists.push(XcpDaqList {
                event_id: event,
                event_name: self.event_name(event),
                entries: odt_entries,
            });
        }

        // Set DAQ list events
        for daq in 0..daq_count {
            let event = daq_list_plan[daq as usize].0;
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

    fn object(name: &str, event: u16, size: usize) -> XcpClientMeasurementObject {
        let a2l_addr = A2lAddr {
            ext: 0,
            addr: 0,
            event: Some(event),
        };
        let a2l_type = A2lType {
            size,
            encoding: A2lTypeEncoding::Unsigned,
        };
        XcpClientMeasurementObject::new(name, a2l_addr, a2l_type, A2lConversion::default())
    }

    #[test]
    fn test_split_daq_lists() {
        // One DAQ list per event, sorted by event id, signal order kept
        let objects = vec![object("b1", 7, 4), object("a1", 2, 8), object("b2", 7, 1)];
        assert_eq!(split_daq_lists(&objects, 123, 1016).unwrap(), vec![(2, vec![1]), (7, vec![0, 2])]);

        // Split by signal count
        let objects: Vec<_> = (0..250).map(|i| object(&format!("s{}", i), 3, 1)).collect();
        let daq_lists = split_daq_lists(&objects, 123, 1016).unwrap();
        assert_eq!(daq_lists.iter().map(|d| (d.0, d.1.len())).collect::<Vec<_>>(), vec![(3, 123), (3, 123), (3, 4)]);
        assert_eq!(daq_lists.iter().flat_map(|d| d.1.clone()).collect::<Vec<_>>(), (0..250).collect::<Vec<_>>());

        // Split by size: 8+8 fits exactly, the 4 byte signal starts a new DAQ list
        let objects = vec![object("a", 1, 8), object("b", 1, 8), object("c", 1, 4), object("d", 1, 8)];
        assert_eq!(split_daq_lists(&objects, 123, 16).unwrap(), vec![(1, vec![0, 1]), (1, vec![2, 3])]);

        // The span record of a context is moved to the front when the event is split
        let objects = vec![
            object("x", 1, 8),
            object("ctx.span", 1, 1),
            object("y", 1, 8),
            object("ctx.level", 1, 1),
            object("ctx.span_dt", 1, 4),
            object("ctx.other", 1, 4),
        ];
        assert_eq!(split_daq_lists(&objects, 123, 16).unwrap(), vec![(1, vec![1, 3, 4, 0]), (1, vec![2, 5])]);
        // Not split, order kept
        assert_eq!(split_daq_lists(&objects, 123, 1016).unwrap(), vec![(1, vec![0, 1, 2, 3, 4, 5])]);

        // Signal too large
        assert!(split_daq_lists(&[object("big", 1, 256)], 123, 1016).is_err());
        assert!(split_daq_lists(&[object("big", 1, 32)], 123, 16).is_err());

        // Too many DAQ lists
        let objects: Vec<_> = (0..MAX_DAQ_COUNT + 1).map(|i| object(&format!("s{}", i), 1, 1)).collect();
        assert!(split_daq_lists(&objects, 1, 1016).is_err());
        assert!(split_daq_lists(&objects[..MAX_DAQ_COUNT], 1, 1016).is_ok());
    }
}
