//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module mcap_writer — Writes DAQ measurement data to an MCAP file (xcpclient --mcap <file>)
//
// MCAP (https://mcap.dev) is a container for timestamped messages, the format of the messages is described by a schema in a
// serialization format of the user's choice. The payload of the DAQ packets is written as it was received, as a message in the
// ros1 serialization format: ros1 serializes the fields of a message packed (no alignment) and little endian, fixed size arrays have
// no length prefix. So a ros1 message definition describes the binary layout of the payload exactly, gaps are uint8[n] padding fields.
//
//   One channel per DAQ list, its topic is the event name, its schema (encoding ros1msg) describes the payload of the DAQ list
//   Message: log_time = publish_time = DAQ timestamp in ns, data = payload (XcpDaqList::size bytes) as received
//
// Values are raw values, MCAP has no conversions or units. They are in the comments of the message definition and in the channel
// metadata ("<field>.unit", "<field>.factor", "<field>.offset", "<field>.labels", "<field>.name" is the name of the signal).
// The ros1 field names are the signal names with all characters except letters, digits and '_' replaced by '_'.
//
// The messages are written in chunks of 1 MB, compressed with zstd. The summary (index and statistics) is written at measurement stop,
// if xcpclient is killed, the last chunk is lost and the file has no summary, `mcap recover` repairs it.

use std::collections::{BTreeMap, HashSet};
use std::fmt::Write as _;
use std::io::{self, Seek, Write};
use std::time::{SystemTime, UNIX_EPOCH};

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use mcap::records::{MessageHeader, Metadata};
use mcap::{Compression, WriteOptions, Writer};

use crate::daq_writer::{DaqLayout, DaqSample, DaqWriter};
use crate::xcp_client::{A2lType, A2lTypeEncoding, OdtEntry, XcpDaqList};

const MESSAGE_ENCODING: &str = "ros1";
const SCHEMA_ENCODING: &str = "ros1msg";
const SCHEMA_PACKAGE: &str = "xcp";

fn mcap_error(e: mcap::McapError) -> io::Error {
    io::Error::other(e)
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// ros1 message definitions

// ros1 type of a signal, a type which is not a basic type is a byte array
fn ros1_type(t: &A2lType) -> String {
    match (t.encoding, t.size) {
        (A2lTypeEncoding::Unsigned, 1) => "uint8".to_string(),
        (A2lTypeEncoding::Unsigned, 2) => "uint16".to_string(),
        (A2lTypeEncoding::Unsigned, 4) => "uint32".to_string(),
        (A2lTypeEncoding::Unsigned, 8) => "uint64".to_string(),
        (A2lTypeEncoding::Signed, 1) => "int8".to_string(),
        (A2lTypeEncoding::Signed, 2) => "int16".to_string(),
        (A2lTypeEncoding::Signed, 4) => "int32".to_string(),
        (A2lTypeEncoding::Signed, 8) => "int64".to_string(),
        (A2lTypeEncoding::Float, 4) => "float32".to_string(),
        (A2lTypeEncoding::Float, 8) => "float64".to_string(),
        _ => format!("uint8[{}]", t.size),
    }
}

// Legal ros1 name: a letter, then letters, digits and '_'
fn ros1_name(name: &str) -> String {
    let mut s: String = name.chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' { c } else { '_' }).collect();
    if !s.starts_with(|c: char| c.is_ascii_alphabetic()) {
        s.insert(0, 'v');
    }
    s
}

// Make a name unique by appending _2, _3, ...
fn unique_name(names: &mut HashSet<String>, name: String) -> String {
    let mut unique = name.clone();
    let mut i = 2;
    while names.contains(&unique) {
        unique = format!("{}_{}", name, i);
        i += 1;
    }
    names.insert(unique.clone());
    unique
}

// Text for a comment or a metadata value, without line breaks
fn one_line(s: &str) -> String {
    s.chars().map(|c| if c.is_control() { ' ' } else { c }).collect()
}

// Enum labels in the A2L unit notation: 0 "OFF" 1 "ON"
fn labels_text(labels: &[(i64, String)]) -> String {
    labels.iter().map(|(v, l)| format!("{} \"{}\"", v, l)).collect::<Vec<_>>().join(" ")
}

/// ros1 message definition of the payload of a DAQ list and the channel metadata with the conversions and units
/// The fields are the signals ordered by offset, gaps are uint8[n] padding fields, the message has exactly XcpDaqList::size bytes
/// A signal which overlaps another one or exceeds the payload can not be described, it is skipped with a warning
fn ros1_message_definition(daq: usize, daq_list: &XcpDaqList) -> (String, BTreeMap<String, String>) {
    let mut definition = String::new();
    let mut metadata = BTreeMap::new();
    let mut names = HashSet::new();
    let mut pad = 0;
    let mut add_padding = |definition: &mut String, names: &mut HashSet<String>, size: usize| {
        let name = unique_name(names, format!("pad{}", pad));
        pad += 1;
        let _ = writeln!(definition, "uint8[{}] {}", size, name);
    };

    let event = daq_list.event_name.clone().unwrap_or_else(|| format!("event {}", daq_list.event_id));
    let _ = writeln!(
        definition,
        "# XCP DAQ list {}, event {} (id {}), {} bytes",
        daq,
        one_line(&event),
        daq_list.event_id,
        daq_list.size
    );
    metadata.insert("daq".to_string(), daq.to_string());
    metadata.insert("event".to_string(), event);
    metadata.insert("event_id".to_string(), daq_list.event_id.to_string());

    let mut entries: Vec<&OdtEntry> = daq_list.entries.iter().collect();
    entries.sort_by_key(|e| e.offset);
    let mut pos = 0;
    for entry in entries {
        let offset = entry.offset as usize;
        if offset < pos || offset + entry.a2l_type.size > daq_list.size {
            warn!(
                "MCAP: {} at offset {} can not be described in the message definition of DAQ list {}, skipped",
                entry.name, offset, daq
            );
            continue;
        }
        if offset > pos {
            add_padding(&mut definition, &mut names, offset - pos);
        }
        pos = offset + entry.a2l_type.size;

        let field = unique_name(&mut names, ros1_name(&entry.name));
        let mut comment = one_line(&entry.name);
        metadata.insert(format!("{}.name", field), one_line(&entry.name));
        let conversion = &entry.conversion;
        if let Some(labels) = conversion.enum_labels() {
            let labels = one_line(&labels_text(labels));
            let _ = write!(comment, " {}", labels);
            metadata.insert(format!("{}.labels", field), labels);
        } else {
            if conversion.factor().is_some() || conversion.offset().is_some() {
                let factor = conversion.factor().unwrap_or(1.0);
                let offset = conversion.offset().unwrap_or(0.0);
                let _ = write!(comment, " phys = {} * raw + {}", factor, offset);
                metadata.insert(format!("{}.factor", field), factor.to_string());
                metadata.insert(format!("{}.offset", field), offset.to_string());
            }
            if !conversion.unit().is_empty() {
                let unit = one_line(conversion.unit());
                let _ = write!(comment, " [{}]", unit);
                metadata.insert(format!("{}.unit", field), unit);
            }
        }
        let _ = writeln!(definition, "{} {}  # {}", ros1_type(&entry.a2l_type), field, comment);
    }
    if daq_list.size > pos {
        add_padding(&mut definition, &mut names, daq_list.size - pos);
    }
    (definition, metadata)
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// McapWriter

pub struct McapWriter<W: Write + Seek + Send> {
    writer: Option<Writer<W>>, // None after stop
    finished: Option<W>,       // The underlying writer after stop
    ecu_name: String,
    channels: Vec<u16>,        // MCAP channel id of each DAQ list
    payload_sizes: Vec<usize>, // Payload size of each DAQ list
    sequences: Vec<u32>,       // Message counter of each DAQ list
}

impl McapWriter<io::BufWriter<std::fs::File>> {
    /// Create an MCAP file
    pub fn create(filename: &str) -> io::Result<Self> {
        McapWriter::new(io::BufWriter::with_capacity(1024 * 1024, std::fs::File::create(filename)?))
    }
}

impl<W: Write + Seek + Send> McapWriter<W> {
    /// Writes the MCAP magic and header
    pub fn new(writer: W) -> io::Result<Self> {
        let options = WriteOptions::new()
            .compression(Some(Compression::Zstd))
            .library(format!("xcpclient {}, {}", env!("CARGO_PKG_VERSION"), mcap::LIBRARY_IDENTIFIER));
        Ok(McapWriter {
            writer: Some(options.create(writer).map_err(mcap_error)?),
            finished: None,
            ecu_name: "ECU".to_string(),
            channels: Vec::new(),
            payload_sizes: Vec::new(),
            sequences: Vec::new(),
        })
    }

    /// Set the name of the ECU, in the metadata record "xcp"
    pub fn with_ecu_name(mut self, name: &str) -> Self {
        if !name.is_empty() {
            self.ecu_name = name.to_string();
        }
        self
    }

    /// Get the underlying writer back after stop, for the tests
    pub fn into_inner(mut self) -> Option<W> {
        self.finished.take()
    }

    fn writer(&mut self) -> io::Result<&mut Writer<W>> {
        self.writer.as_mut().ok_or_else(|| io::Error::other("MCAP file already finished"))
    }
}

impl<W: Write + Seek + Send> DaqWriter for McapWriter<W> {
    fn name(&self) -> &'static str {
        "MCAP"
    }

    fn start(&mut self, layout: &DaqLayout, start_ns: u64) -> io::Result<()> {
        let time_ns = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_nanos() as u64);
        let metadata = Metadata {
            name: "xcp".to_string(),
            metadata: BTreeMap::from([
                ("ecu".to_string(), self.ecu_name.clone()),
                ("tool".to_string(), format!("xcpclient {}", env!("CARGO_PKG_VERSION"))),
                ("start_time_ns".to_string(), start_ns.to_string()),
                ("start_unix_time_ns".to_string(), time_ns.to_string()),
                ("timestamp_resolution_ns".to_string(), layout.timestamp_resolution_ns.to_string()),
            ]),
        };
        self.writer()?.write_metadata(&metadata).map_err(mcap_error)?;

        let mut topics = HashSet::new();
        let mut schema_names = HashSet::new();
        self.channels.clear();
        for (daq, daq_list) in layout.daq_lists.iter().enumerate() {
            let event = daq_list.event_name.clone().unwrap_or_else(|| format!("event_{}", daq_list.event_id));
            let topic = unique_name(&mut topics, format!("/{}", event));
            let schema_name = unique_name(&mut schema_names, format!("{}/{}", SCHEMA_PACKAGE, ros1_name(&event)));
            let (definition, metadata) = ros1_message_definition(daq, daq_list);
            debug!("MCAP: topic {}, schema {}:\n{}", topic, schema_name, definition);
            let w = self.writer()?;
            let schema = w.add_schema(&schema_name, SCHEMA_ENCODING, definition.as_bytes()).map_err(mcap_error)?;
            let channel = w.add_channel(schema, &topic, MESSAGE_ENCODING, &metadata).map_err(mcap_error)?;
            self.channels.push(channel);
        }
        self.payload_sizes = layout.daq_lists.iter().map(|d| d.size).collect();
        self.sequences = vec![0; layout.daq_lists.len()];
        Ok(())
    }

    fn write(&mut self, s: &DaqSample) -> io::Result<()> {
        let daq = s.daq as usize;
        let (Some(&channel_id), Some(&size)) = (self.channels.get(daq), self.payload_sizes.get(daq)) else {
            return Err(io::Error::other("unknown DAQ list"));
        };
        let payload = s.data.get(..size).ok_or_else(|| io::Error::other("DAQ packet shorter than its layout"))?;
        let sequence = self.sequences[daq];
        self.sequences[daq] = sequence.wrapping_add(1);
        let header = MessageHeader {
            channel_id,
            sequence,
            log_time: s.time_ns,
            publish_time: s.time_ns,
        };
        self.writer()?.write_to_known_channel(&header, payload).map_err(mcap_error)
    }

    fn stop(&mut self, _stop_ns: u64) -> io::Result<()> {
        let Some(mut writer) = self.writer.take() else {
            return Ok(());
        };
        let summary = writer.finish().map_err(mcap_error)?;
        let mut inner = writer.into_inner();
        inner.flush()?;
        if let Some(stats) = summary.stats {
            info!(
                "MCAP file: {} messages on {} channels, {} chunks",
                stats.message_count, stats.channel_count, stats.chunk_count
            );
        }
        self.finished = Some(inner);
        Ok(())
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::xcp_client::{A2lAddr, A2lConversion};
    use std::io::Cursor;

    fn entry(name: &str, size: usize, encoding: A2lTypeEncoding, offset: u16, conversion: A2lConversion) -> OdtEntry {
        OdtEntry {
            name: name.to_string(),
            a2l_type: A2lType { size, encoding },
            a2l_addr: A2lAddr { ext: 0, addr: 0, event: Some(0) },
            conversion,
            offset,
        }
    }

    fn layout() -> DaqLayout {
        DaqLayout::new(
            4,
            10, // 10ns per tick
            vec![
                XcpDaqList {
                    event_id: 5,
                    event_name: Some("mainloop".to_string()),
                    entries: vec![
                        entry("temp.value", 4, A2lTypeEncoding::Float, 4, A2lConversion::new(Some(2.0), Some(-1.0), "°C")),
                        entry("counter", 2, A2lTypeEncoding::Unsigned, 0, A2lConversion::default()),
                        entry("_raw", 3, A2lTypeEncoding::Blob, 8, A2lConversion::default()),
                    ],
                    size: 12,
                },
                XcpDaqList {
                    event_id: 7,
                    event_name: None,
                    entries: vec![entry("state", 1, A2lTypeEncoding::Unsigned, 0, A2lConversion::new(None, None, r#"0 "OFF" 1 "ON""#))],
                    size: 1,
                },
            ],
        )
    }

    #[test]
    fn test_ros1_name() {
        assert_eq!(ros1_name("Mainloop.value1"), "Mainloop_value1");
        assert_eq!(ros1_name("a[2].b"), "a_2__b");
        assert_eq!(ros1_name("_x"), "v_x");
        assert_eq!(ros1_name("1x"), "v1x");
        let mut names = HashSet::new();
        assert_eq!(unique_name(&mut names, "a".to_string()), "a");
        assert_eq!(unique_name(&mut names, "a".to_string()), "a_2");
        assert_eq!(unique_name(&mut names, "a".to_string()), "a_3");
    }

    #[test]
    fn test_ros1_message_definition() {
        let layout = layout();
        let (definition, metadata) = ros1_message_definition(0, &layout.daq_lists[0]);
        assert_eq!(
            definition,
            "# XCP DAQ list 0, event mainloop (id 5), 12 bytes\n\
             uint16 counter  # counter\n\
             uint8[2] pad0\n\
             float32 temp_value  # temp.value phys = 2 * raw + -1 [°C]\n\
             uint8[3] v_raw  # _raw\n\
             uint8[1] pad1\n"
        );
        assert_eq!(metadata["temp_value.name"], "temp.value");
        assert_eq!(metadata["temp_value.factor"], "2");
        assert_eq!(metadata["temp_value.offset"], "-1");
        assert_eq!(metadata["temp_value.unit"], "°C");
        assert_eq!(metadata["event"], "mainloop");
        let (definition, metadata) = ros1_message_definition(1, &layout.daq_lists[1]);
        assert_eq!(definition, "# XCP DAQ list 1, event event 7 (id 7), 1 bytes\nuint8 state  # state 0 \"OFF\" 1 \"ON\"\n");
        assert_eq!(metadata["state.labels"], "0 \"OFF\" 1 \"ON\"");

        // Overlapping signal is skipped
        let daq_list = XcpDaqList {
            event_id: 0,
            event_name: None,
            entries: vec![
                entry("a", 4, A2lTypeEncoding::Unsigned, 0, A2lConversion::default()),
                entry("b", 2, A2lTypeEncoding::Unsigned, 2, A2lConversion::default()),
                entry("c", 2, A2lTypeEncoding::Unsigned, 4, A2lConversion::default()),
            ],
            size: 6,
        };
        let (definition, _) = ros1_message_definition(0, &daq_list);
        assert_eq!(definition.lines().skip(1).collect::<Vec<_>>(), ["uint32 a  # a", "uint16 c  # c"]);
    }

    #[test]
    fn test_mcap_writer() {
        let layout = layout();
        let mut w = McapWriter::new(Cursor::new(Vec::new())).unwrap().with_ecu_name("demo");
        w.start(&layout, 1000 * 10).unwrap();
        let payload0: Vec<u8> = [7u8, 0, 0xEE, 0xEE].into_iter().chain(1.5f32.to_le_bytes()).chain([1, 2, 3, 0xEE]).collect();
        let samples: [(u16, u64, Vec<u8>); 3] = [(0, 1001, payload0.clone()), (1, 1002, vec![1]), (0, 1003, payload0.clone())];
        for (daq, timestamp, payload) in &samples {
            // DAQ header, 32 bit timestamp, payload, fill bytes
            let mut packet = vec![0, 0xAA];
            packet.extend(daq.to_le_bytes());
            packet.extend((*timestamp as u32).to_le_bytes());
            packet.extend(payload);
            packet.extend([0, 0, 0]);
            let sample = DaqSample {
                daq: *daq,
                daq_list: &layout.daq_lists[*daq as usize],
                lost: 0,
                timestamp: *timestamp,
                time_ns: timestamp * 10,
                packet: &packet,
                data: &packet[8..],
            };
            w.write(&sample).unwrap();
        }
        w.stop(1003 * 10).unwrap();
        w.stop(1003 * 10).unwrap(); // Second stop is ignored
        let buf = w.into_inner().unwrap().into_inner();

        // Summary
        let summary = mcap::Summary::read(&buf).unwrap().unwrap();
        let stats = summary.stats.unwrap();
        assert_eq!((stats.message_count, stats.channel_count, stats.metadata_count), (3, 2, 1));
        assert_eq!((stats.message_start_time, stats.message_end_time), (10010, 10030));

        // Messages: payload as received, without fill bytes
        let messages: Vec<_> = mcap::MessageStream::new(&buf).unwrap().map(|m| m.unwrap()).collect();
        assert_eq!(messages.len(), 3);
        let m = &messages[0];
        assert_eq!(
            (m.channel.topic.as_str(), m.channel.message_encoding.as_str(), m.sequence, m.log_time),
            ("/mainloop", "ros1", 0, 10010)
        );
        assert_eq!(m.data.as_ref(), payload0.as_slice());
        let schema = m.channel.schema.as_ref().unwrap();
        assert_eq!((schema.name.as_str(), schema.encoding.as_str()), ("xcp/mainloop", "ros1msg"));
        assert!(std::str::from_utf8(&schema.data).unwrap().contains("float32 temp_value"));
        let m = &messages[1];
        assert_eq!(
            (m.channel.topic.as_str(), m.sequence, m.log_time, m.data.as_ref()),
            ("/event_7", 0, 10020, [1u8].as_slice())
        );
        assert_eq!(m.channel.metadata["state.labels"], "0 \"OFF\" 1 \"ON\"");
        assert_eq!(messages[2].sequence, 1);
    }
}
