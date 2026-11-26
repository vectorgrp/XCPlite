//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module csv_writer — Writes DAQ measurement data to a CSV file (xcpclient --csv <file>)
//
// Format: time_ns,daq,name,value,unit  (one row per signal and DAQ packet)
// Values are physical values (factor and offset applied), the value of an enum is its label.

use std::collections::HashSet;
use std::io::{self, Write};

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use crate::daq_writer::{DaqLayout, DaqSample, DaqWriter};

// Quote a CSV field, if it contains a separator, a quote or a line break
fn csv_field(s: &str) -> std::borrow::Cow<'_, str> {
    if s.contains([',', '"', '\n', '\r']) {
        std::borrow::Cow::Owned(format!("\"{}\"", s.replace('"', "\"\"")))
    } else {
        std::borrow::Cow::Borrowed(s)
    }
}

pub struct CsvWriter<W: Write + Send> {
    writer: W,
    skip: HashSet<String>, // Signals not written
}

impl CsvWriter<io::BufWriter<std::fs::File>> {
    /// Create a CSV file
    pub fn create(filename: &str) -> io::Result<Self> {
        Ok(CsvWriter::new(io::BufWriter::new(std::fs::File::create(filename)?)))
    }
}

impl<W: Write + Send> CsvWriter<W> {
    pub fn new(writer: W) -> Self {
        CsvWriter { writer, skip: HashSet::new() }
    }

    /// Signals which are measured but not written, for example the trace signals added for --perfetto
    pub fn with_skip(mut self, names: &[String]) -> Self {
        self.skip = names.iter().cloned().collect();
        self
    }

    /// Get the writer back, for the tests
    pub fn into_inner(self) -> W {
        self.writer
    }
}

impl<W: Write + Send> DaqWriter for CsvWriter<W> {
    fn name(&self) -> &'static str {
        "CSV"
    }

    fn start(&mut self, _layout: &DaqLayout, _start_ns: u64) -> io::Result<()> {
        writeln!(self.writer, "time_ns,daq,name,value,unit")
    }

    fn write(&mut self, s: &DaqSample) -> io::Result<()> {
        for entry in s.daq_list.entries.iter().filter(|e| !self.skip.contains(&e.name)) {
            let value = match entry.decode(s.data) {
                Some(value) => value.to_string(),
                None => {
                    warn!("{}: {} not supported", entry.name, entry.a2l_type);
                    String::new()
                }
            };
            writeln!(
                self.writer,
                "{},{},{},{},{}",
                s.time_ns,
                s.daq,
                entry.name,
                csv_field(&value),
                csv_field(entry.conversion.unit())
            )?;
        }
        Ok(())
    }

    fn stop(&mut self, _stop_ns: u64) -> io::Result<()> {
        self.writer.flush()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::xcp_client::{A2lAddr, A2lConversion, A2lType, A2lTypeEncoding, OdtEntry, XcpDaqList};

    #[test]
    fn test_csv_field() {
        assert_eq!(csv_field("1.5"), "1.5");
        assert_eq!(csv_field("stand by"), "stand by");
        assert_eq!(csv_field("a,b"), "\"a,b\"");
        assert_eq!(csv_field("a\"b"), "\"a\"\"b\"");
    }

    #[test]
    fn test_csv_writer() {
        let entry = |name: &str, offset: u16, unit: &str| OdtEntry {
            name: name.to_string(),
            a2l_type: A2lType {
                size: 1,
                encoding: A2lTypeEncoding::Unsigned,
            },
            a2l_addr: A2lAddr { ext: 0, addr: 0, event: Some(0) },
            conversion: A2lConversion::new(None, None, unit),
            offset,
        };
        let layout = DaqLayout::new(
            4,
            1,
            vec![XcpDaqList {
                event_id: 0,
                event_name: None,
                entries: vec![entry("a", 0, "m,s"), entry("b", 1, r#"0 "OFF" 1 "ON""#), entry("skipped", 2, "")],
                size: 3,
            }],
        );
        let mut w = CsvWriter::new(Vec::new()).with_skip(&["skipped".to_string()]);
        w.start(&layout, 0).unwrap();
        let data = [7u8, 1, 9];
        let sample = DaqSample {
            daq: 0,
            daq_list: &layout.daq_lists[0],
            lost: 0,
            timestamp: 100,
            time_ns: 100,
            packet: &data,
            data: &data,
        };
        w.write(&sample).unwrap();
        w.stop(100).unwrap();
        assert_eq!(String::from_utf8(w.into_inner()).unwrap(), "time_ns,daq,name,value,unit\n100,0,a,7,\"m,s\"\n100,0,b,ON,\n");
    }
}
