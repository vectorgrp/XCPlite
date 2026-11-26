//--------------------------------------------------------------------------------------------------------------------------------------------------
// Module console_writer — Prints DAQ measurement data to the console (xcpclient --mea without output file)
//
// One line per DAQ packet with event, DAQ list, lost packets, timestamp and the time since the last packet of the DAQ list,
// then one line per signal with its physical value and unit.

use std::io::{self, Write};

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use crate::daq_writer::{DaqLayout, DaqSample, DaqWriter};

pub struct ConsoleWriter<W: Write + Send> {
    writer: W,
    last_time_ns: Vec<u64>, // Last timestamp of each DAQ list
}

impl ConsoleWriter<io::Stdout> {
    pub fn stdout() -> Self {
        ConsoleWriter::new(io::stdout())
    }
}

impl<W: Write + Send> ConsoleWriter<W> {
    pub fn new(writer: W) -> Self {
        ConsoleWriter { writer, last_time_ns: Vec::new() }
    }
}

impl<W: Write + Send> DaqWriter for ConsoleWriter<W> {
    fn name(&self) -> &'static str {
        "Console"
    }

    fn start(&mut self, layout: &DaqLayout, start_ns: u64) -> io::Result<()> {
        self.last_time_ns = vec![start_ns; layout.daq_lists.len()];
        Ok(())
    }

    fn write(&mut self, s: &DaqSample) -> io::Result<()> {
        let last = self.last_time_ns.get_mut(s.daq as usize).ok_or_else(|| io::Error::other("unknown DAQ list"))?;
        let delta_us = s.time_ns.saturating_sub(*last) / 1000;
        *last = s.time_ns;

        let w = &mut self.writer;
        match &s.daq_list.event_name {
            Some(name) => writeln!(
                w,
                "EVENT: {} (id={}, daq={}), lost={}, t={}ns (+{}us)",
                name, s.daq_list.event_id, s.daq, s.lost, s.time_ns, delta_us
            )?,
            None => writeln!(w, "EVENT: id={}, daq={}, lost={}, t={}ns (+{}us)", s.daq_list.event_id, s.daq, s.lost, s.time_ns, delta_us)?,
        }
        for entry in &s.daq_list.entries {
            // Physical value: factor and offset applied, the label of an enum value
            let value = match entry.decode(s.data) {
                Some(value) => value.to_string(),
                None => {
                    warn!("{}: {} not supported", entry.name, entry.a2l_type);
                    String::new()
                }
            };
            let unit = entry.conversion.unit();
            if unit.is_empty() {
                writeln!(w, " {} = {}", entry.name, value)?;
            } else {
                writeln!(w, " {} = {} {}", entry.name, value, unit)?;
            }
        }
        Ok(())
    }

    fn stop(&mut self, _stop_ns: u64) -> io::Result<()> {
        self.writer.flush()
    }
}
