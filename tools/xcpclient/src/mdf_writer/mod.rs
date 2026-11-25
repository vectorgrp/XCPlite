//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module mdf_writer — Writes DAQ measurement data to an ASAM MDF 4.1 file (xcpclient --mdf <file>)
//
// The payload of the DAQ packets is written as it was received, the MDF metadata describes its binary layout. Only the DAQ header
// and the 32 bit timestamp are replaced by a record header with the DAQ list number and the extended 64 bit timestamp, the 32 bit
// timestamp wraps around after 4.29s with 1ns resolution.
//
// File layout (unsorted MDF: all DAQ lists in one data group, the records are told apart by the record id)
//
//   ID  HD  FH  MD  TX...  SI  { TX CC CN ... CG } per DAQ list  DG  DT [records ...]
//
//   One channel group (CG) per DAQ list, its record id is the DAQ list number
//   Record:  daq (u16) | time (i64) | payload (XcpDaqList::size bytes)
//            └ record id  └ master channel, raw timestamp ticks since the measurement start, converted to s
//                                     └ signals at 8 + OdtEntry::offset (byte offsets do not include the record id)
//
// All metadata is known at measurement start and written first, the DT block is the last block and grows to the end of the file.
// The file is written as unfinalized MDF ("UnFinMF ", cycle counts and DT length not valid), MDF tools can repair it, if xcpclient
// does not stop the measurement. At measurement stop, the cycle counts and the DT length are set and the file is marked finalized.

mod blocks;
use blocks::*;

use std::collections::HashMap;
use std::io::{self, Seek, SeekFrom, Write};
use std::time::{SystemTime, UNIX_EPOCH};

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use crate::daq_writer::{DaqLayout, DaqSample, DaqWriter};
use crate::xcp_client::{A2lTypeEncoding, OdtEntry};

// Record header: record id (DAQ list number) and timestamp
const RECORD_ID_SIZE: usize = 2;
const RECORD_TIME_SIZE: usize = 8;
const RECORD_HEADER_SIZE: usize = RECORD_ID_SIZE + RECORD_TIME_SIZE;

pub struct MdfWriter<W: Write + Seek + Send> {
    writer: W,
    ecu_name: String,
    started: bool,
    start_ticks: u64,          // Raw timestamp of the measurement start, time 0 in the file
    payload_sizes: Vec<usize>, // Payload size of each DAQ list
    cg_positions: Vec<u64>,    // File position of the CG block of each DAQ list
    cycle_counts: Vec<u64>,    // Number of records of each DAQ list
    dt_position: u64,          // File position of the DT block
    data_length: u64,          // Length of the records in the DT block
}

impl MdfWriter<io::BufWriter<std::fs::File>> {
    /// Create an MDF file
    pub fn create(filename: &str) -> io::Result<Self> {
        Ok(MdfWriter::new(io::BufWriter::with_capacity(1024 * 1024, std::fs::File::create(filename)?)))
    }
}

impl<W: Write + Seek + Send> MdfWriter<W> {
    pub fn new(writer: W) -> Self {
        MdfWriter {
            writer,
            ecu_name: "ECU".to_string(),
            started: false,
            start_ticks: 0,
            payload_sizes: Vec::new(),
            cg_positions: Vec::new(),
            cycle_counts: Vec::new(),
            dt_position: 0,
            data_length: 0,
        }
    }

    /// Set the name of the ECU, the acquisition source of all channel groups
    pub fn with_ecu_name(mut self, name: &str) -> Self {
        if !name.is_empty() {
            self.ecu_name = name.to_string();
        }
        self
    }

    /// Get the writer back, for the tests
    pub fn into_inner(self) -> W {
        self.writer
    }

    // Data type and bit count of a signal
    fn data_type(entry: &OdtEntry) -> (u8, u32) {
        let bits = 8 * entry.a2l_type.size as u32;
        match (entry.a2l_type.encoding, entry.a2l_type.size) {
            (A2lTypeEncoding::Unsigned, 1 | 2 | 4 | 8) => (CN_VAL_UNSIGNED_LE, bits),
            (A2lTypeEncoding::Signed, 1 | 2 | 4 | 8) => (CN_VAL_SIGNED_LE, bits),
            (A2lTypeEncoding::Float, 4 | 8) => (CN_VAL_REAL_LE, bits),
            _ => (CN_VAL_BYTE_ARRAY, bits),
        }
    }

    // Build the metadata blocks and the DT block header
    fn metadata(&mut self, layout: &DaqLayout, time_ns: u64) -> Vec<u8> {
        let mut b = BlockBuffer::new();
        let mut texts: HashMap<String, u64> = HashMap::new(); // Units and labels are written once
        let mut tx = |b: &mut BlockBuffer, s: &str| *texts.entry(s.to_string()).or_insert_with(|| b.tx(s));

        b.id_block("xcpclien");
        let hd = b.hd(NIL, NIL, NIL, time_ns);
        let fh_comment = b.md(&format!(
            "<FHcomment>\r\n<TX>Created by xcpclient</TX>\r\n<tool_id>xcpclient</tool_id>\r\n<tool_vendor>Vector Informatik GmbH</tool_vendor>\r\n<tool_version>{}</tool_version>\r\n</FHcomment>\r\n",
            env!("CARGO_PKG_VERSION")
        ));
        let fh = b.fh(fh_comment, time_ns);
        b.set_link(hd, 1, fh);
        let hd_comment = b.tx(&format!("XCP measurement of {}", self.ecu_name));
        b.set_link(hd, 5, hd_comment);
        let source_name = b.tx(&self.ecu_name);
        let source_path = b.tx("XCP");
        let source = b.si(source_name, source_path, SI_TYPE_ECU, SI_BUS_NONE);

        // Master channel, shared blocks
        let time_name = b.tx("time");
        let time_unit = b.tx("s");
        let time_conversion = b.cc_linear(layout.timestamp_resolution_ns as f64 / 1e9, 0.0, NIL);

        // Channels of each DAQ list, the master channel first
        let mut cn_first = Vec::with_capacity(layout.daq_lists.len());
        for daq_list in &layout.daq_lists {
            let mut next = NIL;
            for entry in daq_list.entries.iter().rev() {
                let name = b.tx(&entry.name);
                let (data_type, bit_count) = Self::data_type(entry);
                let mut flags = 0;
                let conversion = if let Some(labels) = entry.conversion.enum_labels() {
                    flags |= CN_FLAG_DISCRETE_VALUES;
                    let values: Vec<f64> = labels.iter().map(|(v, _)| *v as f64).collect();
                    let label_texts: Vec<u64> = labels.iter().map(|(_, l)| tx(&mut b, l)).collect();
                    b.cc_value_to_text(&values, &label_texts)
                } else if entry.conversion.factor().is_some() || entry.conversion.offset().is_some() {
                    b.cc_linear(entry.conversion.factor().unwrap_or(1.0), entry.conversion.offset().unwrap_or(0.0), NIL)
                } else {
                    NIL
                };
                let unit = match entry.conversion.unit() {
                    "" => NIL,
                    unit => tx(&mut b, unit),
                };
                next = b.cn(&Channel {
                    cn_type: CN_TYPE_VALUE,
                    sync_type: CN_SYNC_NONE,
                    data_type,
                    byte_offset: (RECORD_TIME_SIZE + entry.offset as usize) as u32,
                    bit_count,
                    flags,
                    name,
                    source: NIL,
                    conversion,
                    unit,
                    next,
                });
            }
            cn_first.push(b.cn(&Channel {
                cn_type: CN_TYPE_MASTER,
                sync_type: CN_SYNC_TIME,
                data_type: CN_VAL_SIGNED_LE,
                byte_offset: 0,
                bit_count: 8 * RECORD_TIME_SIZE as u32,
                flags: 0,
                name: time_name,
                source: NIL,
                conversion: time_conversion,
                unit: time_unit,
                next,
            }));
        }

        // Channel groups, linked in the order of the DAQ lists
        self.cg_positions = vec![NIL; layout.daq_lists.len()];
        let mut next = NIL;
        for (daq, daq_list) in layout.daq_lists.iter().enumerate().rev() {
            let acq_name = match &daq_list.event_name {
                Some(name) => b.tx(name),
                None => b.tx(&format!("event {}", daq_list.event_id)),
            };
            let data_bytes = (RECORD_TIME_SIZE + daq_list.size) as u32;
            next = b.cg(next, cn_first[daq], acq_name, source, daq as u64, data_bytes);
            self.cg_positions[daq] = next;
        }

        let dg = b.dg(next, NIL, RECORD_ID_SIZE as u8);
        b.set_link(hd, 0, dg);
        self.dt_position = b.dt_header();
        b.set_link(dg, 2, self.dt_position);
        b.into_inner()
    }

    // Write a u64 at a file position
    fn patch(&mut self, pos: u64, data: &[u8]) -> io::Result<()> {
        self.writer.seek(SeekFrom::Start(pos))?;
        self.writer.write_all(data)
    }
}

impl<W: Write + Seek + Send> DaqWriter for MdfWriter<W> {
    fn name(&self) -> &'static str {
        "MDF"
    }

    fn start(&mut self, layout: &DaqLayout, start_ns: u64) -> io::Result<()> {
        if layout.daq_lists.len() > u16::MAX as usize {
            return Err(io::Error::other("too many DAQ lists for a 2 byte record id"));
        }
        let time_ns = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_nanos() as u64);
        self.start_ticks = start_ns / layout.timestamp_resolution_ns.max(1);
        self.payload_sizes = layout.daq_lists.iter().map(|d| d.size).collect();
        self.cycle_counts = vec![0; layout.daq_lists.len()];
        self.data_length = 0;
        let metadata = self.metadata(layout, time_ns);
        self.writer.write_all(&metadata)?;
        self.started = true;
        Ok(())
    }

    fn write(&mut self, s: &DaqSample) -> io::Result<()> {
        let daq = s.daq as usize;
        let size = self.payload_sizes[daq];
        let payload = s.data.get(..size).ok_or_else(|| io::Error::other("DAQ packet shorter than its layout"))?;
        // Timestamp relative to the measurement start, a sample may be slightly older than the start
        let time = s.timestamp.wrapping_sub(self.start_ticks) as i64;
        let mut header = [0u8; RECORD_HEADER_SIZE];
        header[..RECORD_ID_SIZE].copy_from_slice(&s.daq.to_le_bytes());
        header[RECORD_ID_SIZE..].copy_from_slice(&time.to_le_bytes());
        self.writer.write_all(&header)?;
        self.writer.write_all(payload)?;
        self.cycle_counts[daq] += 1;
        self.data_length += (RECORD_HEADER_SIZE + size) as u64;
        Ok(())
    }

    fn stop(&mut self, _stop_ns: u64) -> io::Result<()> {
        if !self.started {
            return Ok(());
        }
        self.started = false;
        self.writer.flush()?;
        let end = self.writer.stream_position()?;

        // Finalize: cycle counts, DT length, ID
        for daq in 0..self.cg_positions.len() {
            let count = self.cycle_counts[daq];
            self.patch(self.cg_positions[daq] + CG_CYCLE_COUNT_OFFSET, &count.to_le_bytes())?;
        }
        let dt_length = BLOCK_HEADER_LENGTH + self.data_length;
        self.patch(self.dt_position + BLOCK_LENGTH_OFFSET, &dt_length.to_le_bytes())?;
        self.patch(ID_FILE_OFFSET, ID_FILE_FINALIZED)?;
        self.patch(ID_UNFIN_FLAGS_OFFSET, &[0u8; 4])?;
        self.writer.seek(SeekFrom::Start(end))?;
        self.writer.flush()?;

        let records: u64 = self.cycle_counts.iter().sum();
        info!("MDF file: {} records of {} DAQ lists, {} bytes", records, self.cycle_counts.len(), end);
        Ok(())
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::xcp_client::{A2lAddr, A2lConversion, A2lType, XcpDaqList};
    use std::io::Cursor;

    // Minimal reader for the tests

    struct Block<'a> {
        id: &'a [u8],
        links: Vec<u64>,
        data: &'a [u8],
    }

    fn u64_at(buf: &[u8], pos: usize) -> u64 {
        u64::from_le_bytes(buf[pos..pos + 8].try_into().unwrap())
    }

    fn block(buf: &[u8], pos: u64) -> Block<'_> {
        let pos = pos as usize;
        assert_eq!(pos % 8, 0, "block not aligned");
        assert_eq!(&buf[pos..pos + 2], b"##");
        let length = u64_at(buf, pos + 8) as usize;
        let link_count = u64_at(buf, pos + 16) as usize;
        let links = (0..link_count).map(|i| u64_at(buf, pos + 24 + 8 * i)).collect();
        Block {
            id: &buf[pos + 2..pos + 4],
            links,
            data: &buf[pos + 24 + 8 * link_count..pos + length],
        }
    }

    fn text(buf: &[u8], pos: u64) -> String {
        let b = block(buf, pos);
        assert_eq!(b.id, b"TX");
        let end = b.data.iter().position(|&c| c == 0).unwrap();
        String::from_utf8(b.data[..end].to_vec()).unwrap()
    }

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
                        entry("counter", 2, A2lTypeEncoding::Unsigned, 0, A2lConversion::default()),
                        entry("temperature", 4, A2lTypeEncoding::Float, 4, A2lConversion::new(Some(2.0), Some(-1.0), "°C")),
                    ],
                    size: 8,
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

    // Write a measurement, the packets have a 4 byte DAQ header, a 32 bit timestamp and fill bytes
    fn measure(stop: bool) -> Vec<u8> {
        let layout = layout();
        let mut w = MdfWriter::new(Cursor::new(Vec::new())).with_ecu_name("demo");
        w.start(&layout, 1000 * 10).unwrap(); // start at tick 1000
        let samples: [(u16, u64, Vec<u8>); 3] = [
            (0, 1001, [7u8, 0, 0xEE, 0xEE].into_iter().chain(1.5f32.to_le_bytes()).collect()),
            (1, 1002, vec![1]),
            (0, 999, [8u8, 0, 0xEE, 0xEE].into_iter().chain(2.5f32.to_le_bytes()).collect()),
        ];
        for (daq, timestamp, payload) in &samples {
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
        if stop {
            w.stop(1002 * 10).unwrap();
        }
        w.writer.flush().unwrap();
        w.into_inner().into_inner()
    }

    #[test]
    fn test_mdf_writer() {
        let buf = measure(true);

        // ID block: finalized MDF 4.10
        assert_eq!(&buf[0..8], b"MDF     ");
        assert_eq!(&buf[8..16], b"4.10    ");
        assert_eq!(u16::from_le_bytes([buf[28], buf[29]]), 410);
        assert_eq!(&buf[60..64], &[0, 0, 0, 0]);

        // HD block at 64, FH with comment
        let hd = block(&buf, 64);
        assert_eq!(hd.id, b"HD");
        let fh = block(&buf, hd.links[1]);
        assert_eq!(fh.id, b"FH");
        assert_eq!(block(&buf, fh.links[1]).id, b"MD");
        assert_eq!(text(&buf, hd.links[5]), "XCP measurement of demo");

        // DG with 2 byte record id
        let dg = block(&buf, hd.links[0]);
        assert_eq!(dg.id, b"DG");
        assert_eq!(dg.data[0], 2);

        // Channel groups
        let mut cgs = Vec::new();
        let mut pos = dg.links[1];
        while pos != NIL {
            let cg = block(&buf, pos);
            assert_eq!(cg.id, b"CG");
            pos = cg.links[0];
            cgs.push(cg);
        }
        assert_eq!(cgs.len(), 2);
        let record_id = |cg: &Block| u64_at(cg.data, 0);
        let cycle_count = |cg: &Block| u64_at(cg.data, 8);
        let data_bytes = |cg: &Block| u32::from_le_bytes(cg.data[24..28].try_into().unwrap());
        assert_eq!((record_id(&cgs[0]), cycle_count(&cgs[0]), data_bytes(&cgs[0])), (0, 2, 16));
        assert_eq!((record_id(&cgs[1]), cycle_count(&cgs[1]), data_bytes(&cgs[1])), (1, 1, 9));
        assert_eq!(text(&buf, cgs[0].links[2]), "mainloop");
        assert_eq!(text(&buf, cgs[1].links[2]), "event 7");
        let si = block(&buf, cgs[0].links[3]);
        assert_eq!(text(&buf, si.links[0]), "demo");

        // Channels of the first group: name, type, data type, byte offset, bit count
        let mut channels = Vec::new();
        let mut pos = cgs[0].links[1];
        while pos != NIL {
            let cn = block(&buf, pos);
            assert_eq!(cn.id, b"CN");
            let byte_offset = u32::from_le_bytes(cn.data[4..8].try_into().unwrap());
            let bit_count = u32::from_le_bytes(cn.data[8..12].try_into().unwrap());
            channels.push((text(&buf, cn.links[2]), cn.data[0], cn.data[2], byte_offset, bit_count, cn.links[4], cn.links[6]));
            pos = cn.links[0];
        }
        assert_eq!(channels.len(), 3);
        assert_eq!(channels[0].0, "time");
        assert_eq!((channels[0].1, channels[0].2, channels[0].3, channels[0].4), (CN_TYPE_MASTER, CN_VAL_SIGNED_LE, 0, 64));
        assert_eq!(text(&buf, channels[0].6), "s");
        let time_cc = block(&buf, channels[0].5);
        assert_eq!(time_cc.data[0], CC_LINEAR);
        assert_eq!(f64::from_le_bytes(time_cc.data[32..40].try_into().unwrap()), 1e-8);
        assert_eq!(
            (channels[1].0.as_str(), channels[1].2, channels[1].3, channels[1].4, channels[1].5),
            ("counter", CN_VAL_UNSIGNED_LE, 8, 16, NIL)
        );
        assert_eq!(
            (channels[2].0.as_str(), channels[2].2, channels[2].3, channels[2].4),
            ("temperature", CN_VAL_REAL_LE, 12, 32)
        );
        assert_eq!(text(&buf, channels[2].6), "°C");
        let cc = block(&buf, channels[2].5);
        assert_eq!(cc.data[0], CC_LINEAR);
        let cc_val = |cc: &Block, i: usize| f64::from_le_bytes(cc.data[24 + 8 * i..32 + 8 * i].try_into().unwrap());
        assert_eq!((cc_val(&cc, 0), cc_val(&cc, 1)), (-1.0, 2.0)); // offset, factor

        // Value to text conversion of the second group
        let time_cn = block(&buf, cgs[1].links[1]);
        let state = block(&buf, time_cn.links[0]);
        assert_eq!(text(&buf, state.links[2]), "state");
        let cc = block(&buf, state.links[4]);
        assert_eq!(cc.data[0], CC_VALUE_TO_TEXT);
        assert_eq!(cc.links.len(), 4 + 3);
        assert_eq!((text(&buf, cc.links[4]), text(&buf, cc.links[5]), cc.links[6]), ("OFF".to_string(), "ON".to_string(), NIL));
        assert_eq!((cc_val(&cc, 0), cc_val(&cc, 1)), (0.0, 1.0));

        // DT block: the records, payload as received without fill bytes
        let dt_pos = dg.links[2] as usize;
        assert_eq!(&buf[dt_pos..dt_pos + 4], b"##DT");
        let dt_length = u64_at(&buf, dt_pos + 8) as usize;
        assert_eq!(dt_pos + dt_length, buf.len());
        let records = &buf[dt_pos + 24..];
        let mut expected = Vec::new();
        expected.extend([0, 0]);
        expected.extend(1i64.to_le_bytes());
        expected.extend([7, 0, 0xEE, 0xEE]);
        expected.extend(1.5f32.to_le_bytes());
        expected.extend([1, 0]);
        expected.extend(2i64.to_le_bytes());
        expected.extend([1]);
        expected.extend([0, 0]);
        expected.extend((-1i64).to_le_bytes()); // older than the start
        expected.extend([8, 0, 0xEE, 0xEE]);
        expected.extend(2.5f32.to_le_bytes());
        assert_eq!(records, expected.as_slice());
    }

    #[test]
    fn test_mdf_writer_unfinalized() {
        let buf = measure(false);
        assert_eq!(&buf[0..8], b"UnFinMF ");
        assert_eq!(u16::from_le_bytes([buf[60], buf[61]]), ID_UNFIN_FLAG_INVAL_CYCLE_COUNT_CG | ID_UNFIN_FLAG_INVAL_LEN_LAST_DT);
    }
}
