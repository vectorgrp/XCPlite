//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module daq_writer — Output of DAQ measurement data
//
// The DAQ decoder (DaqDecoder) receives the DAQ packets from the XCP client receive task. It parses the DAQ header, extends the
// 32 bit timestamps to 64 bit, checks the packets and keeps the statistics. Each packet is handed over unchanged to a list of
// writers (DaqWriter), one for each output format: console, CSV, Perfetto, ...
//
// A writer gets the binary layout of the DAQ packets at measurement start (DaqLayout) and then each packet as it was received
// (DaqSample): the complete packet with DAQ header and timestamp, and the payload with the signals. A writer may decode the signals
// (OdtEntry::decode) or store the packets as they are, a binary file format which describes the layout needs no copy or rearrangement.
//
// Adding an output format
//   - Implement DaqWriter in a module <format>_writer
//   - Writer specific settings (file name, ECU name, ...) are parameters of its constructor
//   - Create it in create_daq_writers in main.rs
//
// DAQ packet layout (one ODT per DAQ list, so every packet has a timestamp)
//
//   header_size 4 (DAQ_HDR_ODT_FIL_DAQW):  ODT (u8), fill (u8), DAQ (u16), timestamp (u32), payload
//   header_size 2 (DAQ_HDR_ODT_DAQB):      ODT (u8), DAQ (u8),              timestamp (u32), payload
//
//   All values little endian. The payload has the signals of the DAQ list (XcpDaqList::entries) at their offsets, its size is
//   XcpDaqList::size. The transport layer may add fill bytes, the packet may be longer than the layout says.

use std::io;

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use crate::xcp_client::{DAQ_TIMESTAMP_SIZE, DaqTimestampExtender, XcpDaqDecoder, XcpDaqList};

//--------------------------------------------------------------------------------------------------------------------------------------------------
// DaqLayout

/// Binary layout of the DAQ packets of a measurement
#[derive(Debug)]
pub struct DaqLayout {
    /// Size of the DAQ header: 2 (ODT, DAQ as u8) or 4 (ODT, fill, DAQ as u16)
    pub header_size: usize,
    /// Size of the raw timestamp behind the DAQ header
    pub timestamp_size: usize,
    /// ns per raw timestamp tick
    pub timestamp_resolution_ns: u64,
    /// DAQ lists, the index is the DAQ list number in the DAQ header
    pub daq_lists: Vec<XcpDaqList>,
}

impl DaqLayout {
    pub fn new(header_size: usize, timestamp_resolution_ns: u64, daq_lists: Vec<XcpDaqList>) -> DaqLayout {
        DaqLayout {
            header_size,
            timestamp_size: DAQ_TIMESTAMP_SIZE,
            timestamp_resolution_ns,
            daq_lists,
        }
    }

    /// Offset of the raw timestamp in a DAQ packet
    pub fn timestamp_offset(&self) -> usize {
        self.header_size
    }

    /// Offset of the payload in a DAQ packet, the ODT entry offsets are relative to it
    pub fn payload_offset(&self) -> usize {
        self.header_size + self.timestamp_size
    }

    /// Size of the DAQ packet of a DAQ list, without fill bytes of the transport layer
    pub fn packet_size(&self, daq: usize) -> usize {
        self.payload_offset() + self.daq_lists[daq].size
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// DaqSample

/// A DAQ packet, a sample of a DAQ list
#[derive(Debug)]
pub struct DaqSample<'a> {
    /// DAQ list number
    pub daq: u16,
    /// DAQ list
    pub daq_list: &'a XcpDaqList,
    /// Packets lost before this one (transport layer counter)
    pub lost: u32,
    /// 64 bit timestamp in raw ticks, extended from the 32 bit raw timestamp in the packet
    pub timestamp: u64,
    /// 64 bit timestamp in ns
    pub time_ns: u64,
    /// The complete DAQ packet as received, with DAQ header, raw timestamp and maybe fill bytes, see DaqLayout
    pub packet: &'a [u8],
    /// The payload of the DAQ packet, without DAQ header and timestamp
    pub data: &'a [u8],
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// DaqWriter

/// Output of DAQ measurement data in one format
///
/// Called from the receive task of the XCP client, with the DAQ decoder locked. A writer which returns an error is removed, the
/// measurement goes on with the others.
pub trait DaqWriter: Send {
    /// Name of the output format, for log messages
    fn name(&self) -> &'static str;

    /// Measurement start
    /// layout is the binary layout of the DAQ packets, start_ns is the timestamp of the measurement start in ns
    fn start(&mut self, layout: &DaqLayout, start_ns: u64) -> io::Result<()>;

    /// A DAQ packet
    fn write(&mut self, sample: &DaqSample) -> io::Result<()>;

    /// Measurement stop, stop_ns is the newest timestamp of the measurement in ns
    /// Completes and flushes the output
    fn stop(&mut self, stop_ns: u64) -> io::Result<()>;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// DaqDecoder

/// Receives the DAQ packets from the XCP client and hands them to the writers
pub struct DaqDecoder {
    writers: Vec<Box<dyn DaqWriter>>,
    layout: Option<DaqLayout>,
    timestamp_resolution: u64,
    header_size: u8,
    event_count: usize,
    byte_count: usize,
    lost_count: usize,                        // DAQ packets lost, from the gaps of the transport layer counter
    daq_timestamp: Vec<u64>,                  // Last timestamp of each DAQ list, to check for monotonic timestamps
    timestamp_extender: DaqTimestampExtender, // 32 to 64 bit timestamp extension, shared by all DAQ lists
    last_timestamp_ns: u64,                   // Newest timestamp of all DAQ lists
}

impl DaqDecoder {
    pub fn new() -> DaqDecoder {
        DaqDecoder {
            writers: Vec::new(),
            layout: None,
            timestamp_resolution: 1,
            header_size: 4,
            event_count: 0,
            byte_count: 0,
            lost_count: 0,
            daq_timestamp: Vec::new(),
            timestamp_extender: DaqTimestampExtender::new(0),
            last_timestamp_ns: 0,
        }
    }

    /// Set the writers for the next measurement, before the measurement start
    pub fn set_writers(&mut self, writers: Vec<Box<dyn DaqWriter>>) {
        self.writers = writers;
    }

    // Parse the DAQ header and the raw timestamp, returns DAQ list number, ODT number and raw timestamp
    fn parse_header(&self, buf: &[u8]) -> Option<(u16, u8, u32)> {
        let header_size = self.header_size as usize;
        let ts = buf.get(header_size..header_size + DAQ_TIMESTAMP_SIZE)?;
        let timestamp_raw = u32::from_le_bytes([ts[0], ts[1], ts[2], ts[3]]);
        let daq = if header_size == 4 { u16::from_le_bytes([buf[2], buf[3]]) } else { buf[1] as u16 };
        Some((daq, buf[0], timestamp_raw))
    }
}

// Call a writer function for all writers, a writer which fails is removed
fn for_each_writer(writers: &mut Vec<Box<dyn DaqWriter>>, mut f: impl FnMut(&mut dyn DaqWriter) -> io::Result<()>) {
    writers.retain_mut(|w| match f(w.as_mut()) {
        Ok(()) => true,
        Err(e) => {
            error!("{} output failed, stopped: {}", w.name(), e);
            false
        }
    });
}

impl Default for DaqDecoder {
    fn default() -> Self {
        Self::new()
    }
}

impl XcpDaqDecoder for DaqDecoder {
    // Set timestamp resolution and DAQ header size, called at connect
    fn set_daq_properties(&mut self, timestamp_resolution: u64, daq_header_size: u8) {
        self.header_size = daq_header_size;
        self.timestamp_resolution = timestamp_resolution.max(1);
    }

    // Measurement start
    fn start(&mut self, daq_lists: Vec<XcpDaqList>, timestamp: u64) {
        let layout = DaqLayout::new(self.header_size as usize, self.timestamp_resolution, daq_lists);
        self.last_timestamp_ns = timestamp * self.timestamp_resolution;
        self.daq_timestamp = vec![timestamp; layout.daq_lists.len()];
        self.event_count = 0;
        self.byte_count = 0;
        self.lost_count = 0;
        self.timestamp_extender = DaqTimestampExtender::new(timestamp);
        let start_ns = self.last_timestamp_ns;
        for_each_writer(&mut self.writers, |w| w.start(&layout, start_ns));
        self.layout = Some(layout);
    }

    // Measurement stop
    fn stop(&mut self) {
        let stop_ns = self.last_timestamp_ns;
        for_each_writer(&mut self.writers, |w| w.stop(stop_ns));
    }

    // DAQ packet
    fn decode(&mut self, lost: u32, buf: &[u8]) {
        let Some(layout) = &self.layout else {
            return;
        };
        self.lost_count += lost as usize;

        // Only DAQ lists of the measurement setup with a single ODT are expected, ignore anything else
        let (daq, timestamp_raw) = match self.parse_header(buf) {
            Some((daq, 0, t)) if (daq as usize) < layout.daq_lists.len() && buf.len() >= layout.packet_size(daq as usize) => (daq, t),
            Some((daq, odt, _)) => {
                warn!(
                    "Unexpected DAQ packet ignored: daq={}, odt={}, len={}, {} DAQ lists with one ODT configured",
                    daq,
                    odt,
                    buf.len(),
                    layout.daq_lists.len()
                );
                return;
            }
            None => {
                warn!("DAQ packet too short ignored: len={}", buf.len());
                return;
            }
        };

        // Extend to a 64 bit timestamp
        // The extension uses the newest timestamp of all DAQ lists as reference, the order is checked for each DAQ list on its own
        let t_last = self.daq_timestamp[daq as usize];
        let t = self.timestamp_extender.extend(timestamp_raw);
        if t < t_last {
            warn!("Timestamp of daq {} declining {} -> {}", daq, t_last, t);
        }
        self.daq_timestamp[daq as usize] = t;
        let time_ns = t * self.timestamp_resolution;
        self.last_timestamp_ns = self.last_timestamp_ns.max(time_ns);

        let sample = DaqSample {
            daq,
            daq_list: &layout.daq_lists[daq as usize],
            lost,
            timestamp: t,
            time_ns,
            packet: buf,
            data: &buf[layout.payload_offset()..],
        };
        self.byte_count += sample.data.len(); // overall payload byte count
        self.event_count += 1; // overall event count

        for_each_writer(&mut self.writers, |w| w.write(&sample));
    }

    fn get_byte_count(&self) -> usize {
        self.byte_count
    }

    fn get_event_count(&self) -> usize {
        self.event_count
    }

    fn get_lost_count(&self) -> usize {
        self.lost_count
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::xcp_client::{A2lAddr, A2lConversion, A2lType, A2lTypeEncoding, OdtEntry};
    use std::sync::{Arc, Mutex};

    // Records what it gets
    #[derive(Default)]
    struct Recorder {
        log: Arc<Mutex<Vec<String>>>,
        fail_after: Option<usize>,
    }

    impl DaqWriter for Recorder {
        fn name(&self) -> &'static str {
            "Recorder"
        }
        fn start(&mut self, layout: &DaqLayout, start_ns: u64) -> io::Result<()> {
            self.log
                .lock()
                .unwrap()
                .push(format!("start {} {} {}", layout.payload_offset(), layout.packet_size(0), start_ns));
            Ok(())
        }
        fn write(&mut self, s: &DaqSample) -> io::Result<()> {
            let mut log = self.log.lock().unwrap();
            if self.fail_after.is_some_and(|n| log.len() > n) {
                return Err(io::Error::other("full"));
            }
            let value = s.daq_list.entries[0].decode(s.data).unwrap();
            log.push(format!("write {} {} {} {} {}", s.daq, s.lost, s.time_ns, s.packet.len(), value));
            Ok(())
        }
        fn stop(&mut self, stop_ns: u64) -> io::Result<()> {
            self.log.lock().unwrap().push(format!("stop {}", stop_ns));
            Ok(())
        }
    }

    fn daq_list() -> XcpDaqList {
        XcpDaqList {
            event_id: 1,
            event_name: None,
            entries: vec![OdtEntry {
                name: "x".to_string(),
                a2l_type: A2lType {
                    size: 2,
                    encoding: A2lTypeEncoding::Unsigned,
                },
                a2l_addr: A2lAddr { ext: 0, addr: 0, event: Some(1) },
                conversion: A2lConversion::default(),
                offset: 0,
            }],
            size: 2,
        }
    }

    // ODT 0, fill 0xAA, DAQ 0, timestamp, payload, 2 fill bytes of the transport layer
    fn packet(timestamp: u32, value: u16) -> Vec<u8> {
        let mut p = vec![0, 0xAA, 0, 0];
        p.extend(timestamp.to_le_bytes());
        p.extend(value.to_le_bytes());
        p.extend([0, 0]);
        p
    }

    #[test]
    fn test_daq_decoder() {
        let log = Arc::new(Mutex::new(Vec::new()));
        let mut d = DaqDecoder::new();
        d.set_daq_properties(10, 4);
        d.set_writers(vec![Box::new(Recorder {
            log: Arc::clone(&log),
            fail_after: None,
        })]);
        d.start(vec![daq_list()], 0x1_0000_0000);
        d.decode(0, &packet(5, 42));
        d.decode(3, &packet(7, 43));
        d.decode(0, &[1, 0xAA, 0, 0, 0, 0, 0, 0]); // ODT 1, ignored
        d.decode(0, &[0, 0xAA, 1, 0, 0, 0, 0, 0, 0, 0]); // DAQ 1, ignored
        d.decode(0, &[0, 0xAA, 0, 0, 0]); // too short, ignored
        d.stop();
        assert_eq!(
            *log.lock().unwrap(),
            ["start 8 10 42949672960", "write 0 0 42949673010 12 42", "write 0 3 42949673030 12 43", "stop 42949673030"]
        );
        assert_eq!(d.get_event_count(), 2);
        assert_eq!(d.get_byte_count(), 8);
        assert_eq!(d.get_lost_count(), 3);

        // A new measurement resets the lost count
        d.start(vec![daq_list()], 0);
        assert_eq!(d.get_lost_count(), 0);
    }

    #[test]
    fn test_daq_decoder_failing_writer() {
        let log = Arc::new(Mutex::new(Vec::new()));
        let mut d = DaqDecoder::new();
        d.set_daq_properties(1, 4);
        d.set_writers(vec![Box::new(Recorder {
            log: Arc::clone(&log),
            fail_after: Some(1),
        })]);
        d.start(vec![daq_list()], 0);
        for i in 0..4 {
            d.decode(0, &packet(i, i as u16));
        }
        d.stop();
        // Removed after the first error, no stop
        assert_eq!(*log.lock().unwrap(), ["start 8 10 0", "write 0 0 0 12 0"]);
        assert_eq!(d.get_event_count(), 4);
    }
}
