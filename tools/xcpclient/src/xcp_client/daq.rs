//--------------------------------------------------------------------------------------------------------------------------------------------------
// daq.rs — DAQ measurement object management and start/stop

#![allow(dead_code)]

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use std::collections::HashMap;
use std::error::Error;

use super::*;

// Limits of the DAQ setup
const MAX_DAQ_COUNT: usize = 1024; // DAQ lists, one for each event
const MAX_ODT_ENTRY_COUNT: usize = 0x7B; // ODT entries (signals) in an ODT
const MAX_ODT_ENTRY_SIZE: usize = 255; // Size of an ODT entry (signal) in bytes
const DAQ_TIMESTAMP_SIZE: usize = 4; // Size of the timestamp in the first ODT of a DAQ list

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
    // DAQ limits

    /// Unique name of an event from the registry event list (with instance index), None if the event is not in the list
    fn event_name(&self, event: u16) -> Option<String> {
        self.registry
            .as_ref()
            .and_then(|reg| reg.event_list.find_event_id(event).map(|e| e.get_unique_name(reg).into_owned()))
    }

    /// Check the measurement object list against the limits of the DAQ setup, before any DAQ command is sent to the XCP server
    /// event_list are the event ids with their signal count
    /// Restriction: Only one ODT per DAQ list supported yet, so all signals of an event have to fit into one ODT
    fn check_daq_limits(&self, event_list: &[(u16, u16)]) -> Result<(), Box<dyn Error>> {
        if event_list.len() > MAX_DAQ_COUNT {
            return Err(format!(
                "Too many events to measure: {} events, each needs a DAQ list, limit is {} DAQ lists",
                event_list.len(),
                MAX_DAQ_COUNT
            )
            .into());
        }

        // Signal size
        for m in &self.measurement_object_list {
            if m.a2l_type.size > MAX_ODT_ENTRY_SIZE {
                return Err(format!(
                    "Measurement object {} is too large: {} bytes, limit is {} bytes",
                    m.name, m.a2l_type.size, MAX_ODT_ENTRY_SIZE
                )
                .into());
            }
        }

        // Payload of the ODT: MAX_DTO without the DAQ header and the timestamp, which is in the first ODT of a DAQ list
        let max_odt_size = (self.max_dto_size as usize).saturating_sub(self.daq_header_size as usize + DAQ_TIMESTAMP_SIZE);
        for &(event, count) in event_list {
            let label = match self.event_name(event) {
                Some(name) => format!("{} (id={})", name, event),
                None => format!("id={}", event),
            };
            if count as usize > MAX_ODT_ENTRY_COUNT {
                return Err(format!(
                    "ODT overflow: {} signals on event {}, limit is {} signals per event (only one ODT per DAQ list supported)",
                    count, label, MAX_ODT_ENTRY_COUNT
                )
                .into());
            }
            let mut odt_size: usize = 0;
            for m in self.measurement_object_list.iter().filter(|m| m.a2l_addr.event == Some(event)) {
                odt_size += m.a2l_type.size;
                if odt_size > max_odt_size {
                    return Err(format!(
                        "ODT overflow: the signals on event {} need more than {} bytes (MAX_DTO={}), exceeded at {} (only one ODT per DAQ list supported)",
                        label, max_odt_size, self.max_dto_size, m.name
                    )
                    .into());
                }
            }
        }
        Ok(())
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

        // Init
        let signal_count = self.measurement_object_list.len();
        let mut daq_lists: Vec<XcpDaqList> = Vec::with_capacity(8);

        // Store all events in a hashmap (eventnumber, signalcount)
        let mut event_map: HashMap<u16, u16> = HashMap::new();
        let mut min_event: u16 = 0xFFFF;
        let mut max_event: u16 = 0;
        for i in 0..signal_count {
            let event = self.measurement_object_list[i].get_a2l_addr().event.unwrap();
            if event < min_event {
                min_event = event;
            }
            if event > max_event {
                max_event = event;
            }
            let count = event_map.entry(event).or_insert(0);
            *count += 1;
        }
        let event_count: u16 = event_map.len() as u16;
        debug!("event/daq count = {}", event_count);

        // Transform the event hashmap to a sorted array
        let mut event_list: Vec<(u16, u16)> = Vec::new();
        for (event, count) in event_map.into_iter() {
            event_list.push((event, count));
        }
        event_list.sort_by(|a, b| a.0.cmp(&b.0));

        // Check the limits before the DAQ setup of the XCP server is changed
        self.check_daq_limits(&event_list)?;

        // Alloc a DAQ list for each event
        let daq_count: u16 = event_count;
        self.free_daq().await?;
        self.alloc_daq(daq_count).await?;
        debug!("alloc_daq count={}", daq_count);

        // Alloc one ODT for each DAQ list (event)
        // @@@@ TODO Restriction: Only one ODT per DAQ list supported yet
        for daq in 0..daq_count {
            self.alloc_odt(daq, 1).await?;
            debug!("Alloc daq={}, odt_count={}", daq, 1);
        }

        // Alloc ODT entries (signal count) for each ODT/DAQ list
        for daq in 0..daq_count {
            let odt_entry_count = event_list[daq as usize].1;
            self.alloc_odt_entries(daq, 0, odt_entry_count as u8).await?;
            debug!("Alloc odt_entries: daq={}, odt={}, odt_entry_count={}", daq, 0, odt_entry_count);
        }

        // Create all ODT entries for each daq/event list and store information for the DAQ decoder
        for daq in 0..daq_count {
            let event = event_list[daq as usize].0;
            let odt = 0; // Only one odt per daq list supported yet
            let odt_entry_count = self.measurement_object_list.len();

            // Create ODT entries for this daq list
            let mut odt_entries = Vec::new();
            let mut odt_size: u16 = 0;
            self.set_daq_ptr(daq, odt, 0).await?;
            for odt_entry in 0..odt_entry_count {
                let m = &mut self.measurement_object_list[odt_entry];
                let a2l_addr = m.a2l_addr;
                if a2l_addr.event == Some(event) {
                    // Only add signals for the daq list event
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

                    // Sizes are within the limits, see check_daq_limits
                    self.write_daq(a2l_addr.ext, a2l_addr.addr, a2l_type.size as u8).await?;
                    odt_size += a2l_type.size as u16;
                }
            } // odt_entries

            // Event name from the registry event list, for the DAQ decoder
            daq_lists.push(XcpDaqList {
                event_id: event,
                event_name: self.event_name(event),
                entries: odt_entries,
            });
        }

        // Set DAQ list events
        for daq in 0..daq_count {
            let event = event_list[daq as usize].0;
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
