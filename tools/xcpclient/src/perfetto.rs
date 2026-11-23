//--------------------------------------------------------------------------------------------------------------------------------------------------
// perfetto.rs — Export of DAQ measurement data as a trace file for Perfetto (xcpclient --perfetto <file>)
//
// The file is written in the Chrome Trace Event JSON format, which the Perfetto UI (https://ui.perfetto.dev) opens directly.
//
// Mapping
//   Perfetto process       A context (xcp::Context, for example "SigGen1"). Signals which do not belong to a context are in a
//                          process named after the ECU.
//   Perfetto thread        A thread which executed spans in the context ("thread 1", "thread 2", ...), from the measurement
//                          <context>.thread (OPTION_DAQ_SPAN_THREAD). Without this signal there is one track "spans" per context.
//   Slice                  A span (DaqSpan), from the begin and the end sample on the spans event of the context
//   Counter track          A numeric signal, physical value, shown by Perfetto as "<name> [<unit>]"
//   State track            An enum signal, one slice per state, named with the enum label
//
// Spans
//   The spans event of a context is found by the names of its signals: <context>.span (enum), <context>.level, <context>.span_dt and
//   optionally <context>.thread, all in the same DAQ list. Each sample is the state after a span begin or end: the current span and
//   the nesting level. A stack per context and thread is kept, a sample with level = depth + 1 begins a span, level = depth - 1 ends
//   the innermost span. Any other level (lost samples, measurement started inside a span, several threads in a context without the
//   thread signal) resynchronises the stack to the level of the sample and is counted.
//   A span is written when it ends, as complete event with begin and duration. Spans which are still open at the end of the
//   measurement end at the last timestamp of their track.
//
// Span links
//   The links event of a context (xcp::LinkSpan) is found by the names of its signals: <context>.link_context (enum conv.contexts)
//   and <context>.link_time, optionally <context>.link_span, <context>.link_thread and <context>.link_consumer_thread. Each sample is
//   a link from a producer span to the consumer span in which the sample was taken. It is written as a flow (an arrow in Perfetto)
//   in the flow format v1: a flow start "s" on the track of the producer context at the producer timestamp link_time, and a flow end
//   "f" with "bp":"e" on the track of the consumer at the time of the sample. Perfetto binds each of them to the innermost slice
//   which encloses its timestamp on that track. Nothing is kept per span: a link is resolved when the producer context is in the
//   measurement and the producer timestamp is not before the measurement start, otherwise it is counted and dropped.
//
// Timestamps are written relative to the start of the measurement, in microseconds with 3 decimals (ns resolution).

use std::collections::HashMap;
use std::io::Write;

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use crate::xcp_client::{A2lValue, OdtEntry, XcpDaqList};

// Process id of the signals which do not belong to a context, the contexts follow
// Above the thread ids, Perfetto marks a thread with the id of its process as main thread
const PID_ECU: u32 = 10000;

// Thread id of the spans track of a context without thread signal
const TID_SPANS: u32 = 1;

// Thread ids of the state tracks of enum signals start here, below are the thread indices (1..255)
const TID_STATE_FIRST: u32 = 1000;

// Name of a span the begin of which has not been seen
const UNKNOWN_SPAN: &str = "(unknown)";

//--------------------------------------------------------------------------------------------------------------------------------------------------

// What to do with an ODT entry
#[derive(Debug)]
enum SignalKind {
    Skip,                                                       // Part of a span record
    Counter { pid: u32, name: String, series: String },         // Numeric signal, Perfetto names the track "<name> <series>"
    State { pid: u32, tid: u32, state: Option<(String, u64)> }, // Enum signal, with the current state and its begin
}

// The spans event of a context
#[derive(Debug)]
struct SpanList {
    pid: u32,
    context: String,
    span: usize,           // Index of the ODT entry <context>.span
    level: usize,          // Index of the ODT entry <context>.level
    thread: Option<usize>, // Index of the ODT entry <context>.thread
}

// The links event of a context
#[derive(Debug)]
struct LinkList {
    pid: u32,                       // Process of the consumer context
    context: usize,                 // Index of the ODT entry <context>.link_context, the producer context
    time: usize,                    // Index of the ODT entry <context>.link_time, the producer timestamp in DAQ timestamp ticks
    span: Option<usize>,            // Index of the ODT entry <context>.link_span, the producer span
    thread: Option<usize>,          // Index of the ODT entry <context>.link_thread, the producer thread
    consumer_thread: Option<usize>, // Index of the ODT entry <context>.link_consumer_thread, the consumer thread
}

#[derive(Debug, Default)]
struct DaqList {
    spans: Option<SpanList>,
    links: Option<LinkList>,
    signals: Vec<SignalKind>, // One per ODT entry
}

// The open spans of a context and thread, innermost last
#[derive(Debug, Default)]
struct SpanTrack {
    stack: Vec<(String, u64)>, // Span name and begin
    last: u64,                 // Last timestamp
}

pub struct PerfettoWriter<W: Write> {
    writer: W,
    ecu_name: String,
    first: bool, // No event written yet
    start: u64,  // Timestamp of the measurement start in ns
    daq_lists: Vec<DaqList>,
    span_tracks: HashMap<(u32, u32), SpanTrack>, // by pid and tid
    span_count: u64,
    resync_count: u64,
    context_pids: HashMap<String, u32>, // Process of each context with a span record, by context name
    timestamp_resolution: u64,          // ns per DAQ timestamp tick, for the producer timestamps of the span links
    flow_count: u64,
    unresolved_link_count: u64,
}

impl PerfettoWriter<std::io::BufWriter<std::fs::File>> {
    /// Create a trace file
    pub fn create(filename: &str) -> std::io::Result<Self> {
        Ok(PerfettoWriter::new(std::io::BufWriter::new(std::fs::File::create(filename)?)))
    }
}

impl<W: Write> PerfettoWriter<W> {
    pub fn new(writer: W) -> Self {
        PerfettoWriter {
            writer,
            ecu_name: "ECU".to_string(),
            first: true,
            start: 0,
            daq_lists: Vec::new(),
            span_tracks: HashMap::new(),
            span_count: 0,
            resync_count: 0,
            context_pids: HashMap::new(),
            timestamp_resolution: 1,
            flow_count: 0,
            unresolved_link_count: 0,
        }
    }

    /// Set the resolution of the DAQ timestamps in ns per tick, needed to convert the producer timestamps of span links
    pub fn set_timestamp_resolution(&mut self, ns_per_tick: u64) {
        self.timestamp_resolution = ns_per_tick.max(1);
    }

    /// Set the name of the process for the signals which do not belong to a context
    pub fn set_ecu_name(&mut self, name: &str) {
        if !name.is_empty() {
            self.ecu_name = name.to_string();
        }
    }

    /// Number of spans written
    pub fn span_count(&self) -> u64 {
        self.span_count
    }

    /// Number of span samples which did not fit to the open spans
    pub fn resync_count(&self) -> u64 {
        self.resync_count
    }

    /// Number of span links written as flows
    pub fn flow_count(&self) -> u64 {
        self.flow_count
    }

    /// Number of span links which could not be resolved (producer context not measured, or captured before the measurement start)
    pub fn unresolved_link_count(&self) -> u64 {
        self.unresolved_link_count
    }

    /// Get the writer back, for the tests
    pub fn into_inner(self) -> W {
        self.writer
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------
    // Output

    fn json_string(s: &str) -> String {
        let mut out = String::with_capacity(s.len() + 2);
        out.push('"');
        for c in s.chars() {
            match c {
                '"' => out.push_str("\\\""),
                '\\' => out.push_str("\\\\"),
                c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
                c => out.push(c),
            }
        }
        out.push('"');
        out
    }

    // Timestamp or duration in ns as microseconds with 3 decimals
    fn us(ns: u64) -> String {
        format!("{}.{:03}", ns / 1000, ns % 1000)
    }

    fn write_event(&mut self, event: &str) {
        let sep = if self.first { "" } else { ",\n" };
        self.first = false;
        let _ = write!(self.writer, "{}{}", sep, event);
    }

    fn write_process_name(&mut self, pid: u32, name: &str) {
        self.write_event(&format!(
            r#"{{"ph":"M","pid":{},"name":"process_name","args":{{"name":{}}}}}"#,
            pid,
            Self::json_string(name)
        ));
        self.write_event(&format!(r#"{{"ph":"M","pid":{},"name":"process_sort_index","args":{{"sort_index":{}}}}}"#, pid, pid));
    }

    fn write_thread_name(&mut self, pid: u32, tid: u32, name: &str) {
        self.write_event(&format!(
            r#"{{"ph":"M","pid":{},"tid":{},"name":"thread_name","args":{{"name":{}}}}}"#,
            pid,
            tid,
            Self::json_string(name)
        ));
        self.write_event(&format!(
            r#"{{"ph":"M","pid":{},"tid":{},"name":"thread_sort_index","args":{{"sort_index":{}}}}}"#,
            pid, tid, tid
        ));
    }

    // Complete event, begin and end are absolute timestamps in ns
    fn write_slice(&mut self, pid: u32, tid: u32, name: &str, begin: u64, end: u64) {
        let ts = begin.saturating_sub(self.start);
        let dur = end.saturating_sub(begin);
        self.write_event(&format!(
            r#"{{"ph":"X","pid":{},"tid":{},"name":{},"ts":{},"dur":{}}}"#,
            pid,
            tid,
            Self::json_string(name),
            Self::us(ts),
            Self::us(dur)
        ));
    }

    // Flow from a producer span to a consumer span, flow format v1, the timestamps are absolute in ns
    // Each end binds to the innermost slice on its track which encloses its timestamp
    fn write_flow(&mut self, name: &str, from: (u32, u32, u64), to: (u32, u32, u64)) {
        self.flow_count += 1;
        let id = self.flow_count;
        let name = Self::json_string(name);
        let (pid, tid, t) = from;
        let ts = Self::us(t.saturating_sub(self.start));
        self.write_event(&format!(
            r#"{{"ph":"s","pid":{},"tid":{},"name":{},"cat":"span_link","id":{},"ts":{}}}"#,
            pid, tid, name, id, ts
        ));
        let (pid, tid, t) = to;
        let ts = Self::us(t.saturating_sub(self.start));
        self.write_event(&format!(
            r#"{{"ph":"f","pid":{},"tid":{},"name":{},"cat":"span_link","id":{},"ts":{},"bp":"e"}}"#,
            pid, tid, name, id, ts
        ));
    }

    fn write_counter(&mut self, pid: u32, name: &str, series: &str, t: u64, value: f64) {
        if !value.is_finite() {
            return; // Not representable in JSON
        }
        let ts = t.saturating_sub(self.start);
        self.write_event(&format!(
            r#"{{"ph":"C","pid":{},"name":{},"ts":{},"args":{{{}:{}}}}}"#,
            pid,
            Self::json_string(name),
            Self::us(ts),
            Self::json_string(series),
            value
        ));
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------
    // Measurement start

    // Find the span record of a context in a DAQ list
    // Returns the context name and the indices of the ODT entries <context>.span, .level, .span_dt and .thread
    fn find_span_record(odt_entries: &[OdtEntry]) -> Option<(String, usize, usize, usize, Option<usize>)> {
        for (span, entry) in odt_entries.iter().enumerate() {
            let Some(context) = entry.name.strip_suffix(".span") else { continue };
            if !entry.conversion.is_enum() {
                continue;
            }
            let find = |suffix: &str| {
                odt_entries
                    .iter()
                    .position(|e| e.name.strip_prefix(context).and_then(|s| s.strip_prefix('.')) == Some(suffix))
            };
            if let (Some(level), Some(span_dt)) = (find("level"), find("span_dt")) {
                return Some((context.to_string(), span, level, span_dt, find("thread")));
            }
        }
        None
    }

    // Find the link record of a context in a DAQ list
    fn find_link_record(odt_entries: &[OdtEntry]) -> Option<LinkList> {
        for (context, entry) in odt_entries.iter().enumerate() {
            let Some(name) = entry.name.strip_suffix(".link_context") else { continue };
            if !entry.conversion.is_enum() {
                continue;
            }
            let find = |suffix: &str| odt_entries.iter().position(|e| e.name.strip_prefix(name).and_then(|s| s.strip_prefix('.')) == Some(suffix));
            if let Some(time) = find("link_time") {
                return Some(LinkList {
                    pid: PID_ECU,
                    context,
                    time,
                    span: find("link_span"),
                    thread: find("link_thread"),
                    consumer_thread: find("link_consumer_thread"),
                });
            }
        }
        None
    }

    /// Start of the measurement
    /// daq_lists are the signals of each DAQ list, start is the timestamp of the measurement start in ns
    pub fn start(&mut self, daq_lists: &[XcpDaqList], start: u64) {
        let odt_entries: Vec<&[OdtEntry]> = daq_lists.iter().map(|d| d.entries.as_slice()).collect();
        self.start = start;
        self.daq_lists.clear();
        self.span_tracks.clear();
        self.span_count = 0;
        self.resync_count = 0;
        self.flow_count = 0;
        self.unresolved_link_count = 0;
        self.first = true;
        let _ = write!(self.writer, "{{\"displayTimeUnit\":\"ns\",\"traceEvents\":[\n");
        let ecu_name = self.ecu_name.clone();
        self.write_process_name(PID_ECU, &ecu_name);

        // Contexts: the DAQ lists with a span record
        let mut contexts: Vec<(String, u32)> = Vec::new(); // Context name and pid
        let mut span_records = Vec::new();
        for entries in odt_entries.iter().copied() {
            let record = Self::find_span_record(entries);
            if let Some((context, ..)) = &record {
                let pid = PID_ECU + 1 + contexts.len() as u32;
                contexts.push((context.clone(), pid));
                self.write_process_name(pid, context);
            }
            span_records.push(record);
        }
        self.context_pids = contexts.iter().cloned().collect();
        // A signal belongs to the context with the longest name which is a prefix of the signal name
        let pid_of = |name: &str| -> u32 {
            contexts
                .iter()
                .filter(|(context, _)| name.strip_prefix(context.as_str()).is_some_and(|s| s.starts_with('.')))
                .max_by_key(|(context, _)| context.len())
                .map_or(PID_ECU, |(_, pid)| *pid)
        };

        // Signals
        let mut state_tid = TID_STATE_FIRST;
        for (entries, record) in odt_entries.iter().zip(span_records) {
            let mut daq_list = DaqList::default();
            let mut skip: Vec<usize> = Vec::new();
            if let Some((context, span, level, span_dt, thread)) = record {
                let pid = pid_of(&entries[span].name);
                skip.extend([span, level, span_dt]);
                skip.extend(thread);
                if thread.is_none() {
                    info!(
                        "Context {}: no signal {}.thread, the spans of all threads of this context are in one track",
                        context, context
                    );
                    self.write_thread_name(pid, TID_SPANS, "spans");
                }
                daq_list.spans = Some(SpanList {
                    pid,
                    context,
                    span,
                    level,
                    thread,
                });
            }
            if let Some(mut links) = Self::find_link_record(entries) {
                links.pid = pid_of(&entries[links.context].name);
                skip.extend([links.context, links.time]);
                skip.extend(links.span.iter().chain(links.thread.iter()).chain(links.consumer_thread.iter()));
                daq_list.links = Some(links);
            }
            for (i, entry) in entries.iter().enumerate() {
                let pid = pid_of(&entry.name);
                let kind = if skip.contains(&i) {
                    SignalKind::Skip
                } else if entry.conversion.is_enum() {
                    let tid = state_tid;
                    state_tid += 1;
                    self.write_thread_name(pid, tid, &entry.name);
                    SignalKind::State { pid, tid, state: None }
                } else {
                    let unit = entry.conversion.unit();
                    let series = if unit.is_empty() { "value".to_string() } else { format!("[{}]", unit) };
                    SignalKind::Counter {
                        pid,
                        name: entry.name.clone(),
                        series,
                    }
                };
                daq_list.signals.push(kind);
            }
            self.daq_lists.push(daq_list);
        }
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------
    // Samples

    // A sample of the spans event of a context: the current span and nesting level of a thread after a span begin or end
    // tid is the thread index, or TID_SPANS without thread signal (thread_signal false)
    fn span_sample(&mut self, pid: u32, tid: u32, thread_signal: bool, t: u64, span: &str, level: usize) {
        if !self.span_tracks.contains_key(&(pid, tid)) && thread_signal {
            self.write_thread_name(pid, tid, &format!("thread {}", tid));
        }
        let mut track = self.span_tracks.remove(&(pid, tid)).unwrap_or_default();
        track.last = t;
        let depth = track.stack.len();
        if level == depth + 1 {
            // Begin of a span
            track.stack.push((span.to_string(), t));
        } else if level + 1 == depth {
            // End of the innermost span
            let (name, begin) = track.stack.pop().unwrap();
            self.write_slice(pid, tid, &name, begin, t);
            self.span_count += 1;
        } else {
            // Resynchronise to the level of the sample
            // The first samples of a track are not counted, the measurement may have started inside a span
            if depth > 0 || self.span_count > 0 {
                self.resync_count += 1;
            }
            while track.stack.len() > level {
                let (name, begin) = track.stack.pop().unwrap();
                self.write_slice(pid, tid, &name, begin, t);
                self.span_count += 1;
            }
            while track.stack.len() + 1 < level {
                track.stack.push((UNKNOWN_SPAN.to_string(), t));
            }
            if level > 0 {
                if track.stack.len() < level {
                    track.stack.push((span.to_string(), t));
                } else if track.stack.last().is_some_and(|(name, _)| name != span) {
                    // Same level, another span
                    let (name, begin) = track.stack.pop().unwrap();
                    self.write_slice(pid, tid, &name, begin, t);
                    self.span_count += 1;
                    track.stack.push((span.to_string(), t));
                }
            }
        }
        self.span_tracks.insert((pid, tid), track);
    }

    /// A sample of a DAQ list
    /// t is the timestamp in ns, data is the DAQ data without header and timestamp
    pub fn sample(&mut self, daq: usize, t: u64, odt_entries: &[OdtEntry], data: &[u8]) {
        if daq >= self.daq_lists.len() {
            return;
        }

        // Span record
        if let Some(spans) = &self.daq_lists[daq].spans {
            let pid = spans.pid;
            let thread_signal = spans.thread.is_some();
            let span = odt_entries.get(spans.span).and_then(|e| e.decode(data));
            let level = odt_entries.get(spans.level).and_then(|e| e.decode_raw(data)).and_then(|v| v.as_f64());
            let tid = match spans.thread {
                Some(thread) => odt_entries.get(thread).and_then(|e| e.decode_raw(data)).and_then(|v| v.as_f64()).map(|v| v as u32),
                None => Some(TID_SPANS),
            };
            if let (Some(span), Some(level), Some(tid)) = (span, level, tid) {
                // A span id which is not in the enum conversion has no label
                let name = match &span {
                    A2lValue::Label(label) => label.clone(),
                    other => format!("span_{}", other),
                };
                self.span_sample(pid, tid, thread_signal, t, &name, level as usize);
            }
        }

        // Span link: resolve the producer, the flow is written after the borrow of the DAQ list ends
        let link = self.daq_lists[daq].links.as_ref().map(|links| {
            let raw = |index: Option<usize>| -> Option<u64> {
                match odt_entries.get(index?)?.decode_raw(data)? {
                    A2lValue::Unsigned(v) => Some(v),
                    _ => None,
                }
            };
            let consumer = (links.pid, raw(links.consumer_thread).map_or(TID_SPANS, |v| v as u32), t);
            let producer_pid = match odt_entries.get(links.context).and_then(|e| e.decode(data)) {
                Some(A2lValue::Label(context)) => self.context_pids.get(&context).copied(),
                _ => None,
            };
            let producer_tid = raw(links.thread).map_or(TID_SPANS, |v| v as u32);
            let producer_t = raw(Some(links.time)).map(|ticks| ticks.saturating_mul(self.timestamp_resolution));
            let name = match links.span.and_then(|i| odt_entries.get(i)).and_then(|e| e.decode(data)) {
                Some(A2lValue::Label(label)) => label,
                _ => "span_link".to_string(),
            };
            match (producer_pid, producer_t) {
                (Some(pid), Some(producer_t)) if producer_t >= self.start => Some((name, (pid, producer_tid, producer_t), consumer)),
                _ => None,
            }
        });
        match link {
            Some(Some((name, from, to))) => self.write_flow(&name, from, to),
            Some(None) => self.unresolved_link_count += 1,
            None => {}
        }

        // Signals
        for i in 0..self.daq_lists[daq].signals.len() {
            let Some(entry) = odt_entries.get(i) else { break };
            match &self.daq_lists[daq].signals[i] {
                SignalKind::Skip => {}
                SignalKind::Counter { pid, name, series } => {
                    if let Some(value) = entry.decode(data).and_then(|v| v.as_f64()) {
                        let (pid, name, series) = (*pid, name.clone(), series.clone());
                        self.write_counter(pid, &name, &series, t, value);
                    }
                }
                SignalKind::State { pid, tid, state } => {
                    let Some(value) = entry.decode(data) else { continue };
                    let label = value.to_string();
                    if state.as_ref().is_some_and(|(current, _)| *current == label) {
                        continue; // State unchanged
                    }
                    let (pid, tid, previous) = (*pid, *tid, state.clone());
                    if let Some((name, begin)) = previous {
                        self.write_slice(pid, tid, &name, begin, t);
                    }
                    if let SignalKind::State { state, .. } = &mut self.daq_lists[daq].signals[i] {
                        *state = Some((label, t));
                    }
                }
            }
        }
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------
    // Measurement stop

    /// End of the measurement, t is the last timestamp of the measurement in ns
    /// Ends the open spans and states and completes the file
    pub fn stop(&mut self, t: u64) {
        // Open spans end at the last timestamp of their track
        let mut tracks: Vec<((u32, u32), SpanTrack)> = self.span_tracks.drain().collect();
        tracks.sort_by_key(|(key, _)| *key);
        for ((pid, tid), mut track) in tracks {
            while let Some((name, begin)) = track.stack.pop() {
                self.write_slice(pid, tid, &name, begin, track.last);
            }
        }

        // Open states end at the end of the measurement
        let mut states = Vec::new();
        for daq_list in &mut self.daq_lists {
            for signal in &mut daq_list.signals {
                if let SignalKind::State { pid, tid, state } = signal {
                    if let Some((name, begin)) = state.take() {
                        states.push((*pid, *tid, name, begin));
                    }
                }
            }
        }
        for (pid, tid, name, begin) in states {
            self.write_slice(pid, tid, &name, begin, t.max(begin));
        }

        let _ = write!(self.writer, "\n]}}\n");
        let _ = self.writer.flush();

        if self.flow_count > 0 {
            info!("Perfetto trace: {} span links written as flows", self.flow_count);
        }
        if self.unresolved_link_count > 0 {
            warn!(
                "Perfetto trace: {} span links not resolved (spans of the producer context not measured, or captured before the measurement start)",
                self.unresolved_link_count
            );
        }
        if self.resync_count > 0 {
            warn!(
                "Perfetto trace: {} span samples did not fit to the open spans (lost samples, or several threads in a context without the signal <context>.thread, see OPTION_DAQ_SPAN_THREAD)",
                self.resync_count
            );
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::xcp_client::{A2lAddr, A2lConversion, A2lType, A2lTypeEncoding};

    const ECU: u64 = PID_ECU as u64; // Process of the signals without context
    const CTX: u64 = ECU + 1; // Process of the first context

    const SPANS: &str = r#"0 "none" 1 "calc" 2 "clip" 3 "poll""#;

    fn entry(name: &str, size: usize, encoding: A2lTypeEncoding, offset: u16, unit: &str) -> OdtEntry {
        OdtEntry {
            name: name.to_string(),
            a2l_type: A2lType { size, encoding },
            a2l_addr: A2lAddr { ext: 0, addr: 0, event: Some(0) },
            conversion: A2lConversion::new(None, None, unit),
            offset,
        }
    }

    // The span record of a context: span_dt (u32), span (u16), level (u8), thread (u8)
    fn span_entries(context: &str, thread: bool) -> Vec<OdtEntry> {
        let mut entries = vec![
            entry(&format!("{context}.span"), 2, A2lTypeEncoding::Unsigned, 4, SPANS),
            entry(&format!("{context}.level"), 1, A2lTypeEncoding::Unsigned, 6, ""),
            entry(&format!("{context}.span_dt"), 4, A2lTypeEncoding::Unsigned, 0, "ns"),
        ];
        if thread {
            entries.push(entry(&format!("{context}.thread"), 1, A2lTypeEncoding::Unsigned, 7, ""));
        }
        entries
    }

    fn span_data(span: u16, level: u8, thread: u8) -> Vec<u8> {
        let mut data = vec![0u8; 4];
        data.extend(span.to_le_bytes());
        data.extend([level, thread]);
        data
    }

    // Run a measurement and return the trace events
    fn run(odt_entries: Vec<Vec<OdtEntry>>, samples: &[(usize, u64, Vec<u8>)], stop: u64) -> (Vec<serde_json::Value>, u64, u64) {
        let mut writer = PerfettoWriter::new(Vec::new());
        writer.set_ecu_name("demo");
        let daq_lists: Vec<XcpDaqList> = odt_entries
            .into_iter()
            .enumerate()
            .map(|(i, entries)| XcpDaqList {
                event_id: i as u16,
                event_name: None,
                entries,
            })
            .collect();
        writer.start(&daq_lists, 1000);
        for (daq, t, data) in samples {
            writer.sample(*daq, *t, &daq_lists[*daq].entries, data);
        }
        writer.stop(stop);
        let (spans, resyncs) = (writer.span_count(), writer.resync_count());
        let json: serde_json::Value = serde_json::from_slice(&writer.into_inner()).expect("valid JSON");
        (json["traceEvents"].as_array().unwrap().clone(), spans, resyncs)
    }

    // The slices as (pid, tid, name, ts, dur), ts and dur in ns
    fn slices(events: &[serde_json::Value]) -> Vec<(u64, u64, String, u64, u64)> {
        let ns = |v: &serde_json::Value| (v.as_f64().unwrap() * 1000.0).round() as u64;
        events
            .iter()
            .filter(|e| e["ph"] == "X")
            .map(|e| {
                (
                    e["pid"].as_u64().unwrap(),
                    e["tid"].as_u64().unwrap(),
                    e["name"].as_str().unwrap().to_string(),
                    ns(&e["ts"]),
                    ns(&e["dur"]),
                )
            })
            .collect()
    }

    fn metadata_name(events: &[serde_json::Value], kind: &str, pid: u64, tid: Option<u64>) -> Option<String> {
        events
            .iter()
            .find(|e| e["ph"] == "M" && e["name"] == kind && e["pid"] == pid && tid.is_none_or(|tid| e["tid"] == tid))
            .map(|e| e["args"]["name"].as_str().unwrap().to_string())
    }

    #[test]
    fn test_nested_spans() {
        let samples = [
            (0, 2000, span_data(1, 1, 1)), // calc begins
            (0, 2500, span_data(2, 2, 1)), // clip begins
            (0, 2700, span_data(1, 1, 1)), // clip ends
            (0, 3001, span_data(0, 0, 1)), // calc ends
        ];
        let (events, spans, resyncs) = run(vec![span_entries("SigGen1", true)], &samples, 4000);
        assert_eq!(slices(&events), [(CTX, 1, "clip".to_string(), 1500, 200), (CTX, 1, "calc".to_string(), 1000, 1001)]);
        assert_eq!((spans, resyncs), (2, 0));
        assert_eq!(metadata_name(&events, "process_name", ECU, None).as_deref(), Some("demo"));
        assert_eq!(metadata_name(&events, "process_name", CTX, None).as_deref(), Some("SigGen1"));
        assert_eq!(metadata_name(&events, "thread_name", CTX, Some(1)).as_deref(), Some("thread 1"));
        // The signals of the span record are no counters
        assert!(!events.iter().any(|e| e["ph"] == "C"));
    }

    // Two threads in the same context, their spans overlap: one track per thread
    #[test]
    fn test_threads_in_one_context() {
        let samples = [
            (0, 2000, span_data(1, 1, 1)), // thread 1: calc begins
            (0, 2100, span_data(3, 1, 2)), // thread 2: poll begins
            (0, 2200, span_data(2, 2, 1)), // thread 1: clip begins
            (0, 2300, span_data(0, 0, 2)), // thread 2: poll ends
            (0, 2400, span_data(1, 1, 1)), // thread 1: clip ends
            (0, 2500, span_data(0, 0, 1)), // thread 1: calc ends
        ];
        let (events, spans, resyncs) = run(vec![span_entries("SigGen1", true)], &samples, 4000);
        assert_eq!(
            slices(&events),
            [
                (CTX, 2, "poll".to_string(), 1100, 200),
                (CTX, 1, "clip".to_string(), 1200, 200),
                (CTX, 1, "calc".to_string(), 1000, 500)
            ]
        );
        assert_eq!((spans, resyncs), (3, 0));
        assert_eq!(metadata_name(&events, "thread_name", CTX, Some(2)).as_deref(), Some("thread 2"));

        // The same samples without the thread signal: one track, the samples do not fit and are counted
        let (events, _, resyncs) = run(vec![span_entries("SigGen1", false)], &samples, 4000);
        assert!(resyncs > 0);
        assert!(slices(&events).iter().all(|s| s.1 == TID_SPANS as u64));
        assert_eq!(metadata_name(&events, "thread_name", CTX, Some(TID_SPANS as u64)).as_deref(), Some("spans"));
    }

    // Measurement started inside a span, a lost sample, a span without label and a span which is open at the end
    #[test]
    fn test_resync() {
        let samples = [
            (0, 2000, span_data(1, 1, 1)), // clip ended before, calc is the current span: not counted
            (0, 2100, span_data(0, 0, 1)), // calc ends
            (0, 3000, span_data(2, 2, 1)), // calc begin is lost, clip begins
            (0, 3100, span_data(1, 1, 1)), // clip ends
            (0, 3200, span_data(0, 0, 1)), // the unknown span ends
            (0, 4000, span_data(9, 1, 1)), // a span without label begins and does not end
        ];
        let (events, spans, resyncs) = run(vec![span_entries("ctx", true)], &samples, 5000);
        assert_eq!(
            slices(&events),
            [
                (CTX, 1, "calc".to_string(), 1000, 100),
                (CTX, 1, "clip".to_string(), 2000, 100),
                (CTX, 1, UNKNOWN_SPAN.to_string(), 2000, 200),
                (CTX, 1, "span_9".to_string(), 3000, 0),
            ]
        );
        assert_eq!((spans, resyncs), (3, 1));
    }

    // Numeric signals are counters, enum signals are states, both in the process of their context
    #[test]
    fn test_counters_and_states() {
        let odt_entries = vec![
            span_entries("SigGen1", true),
            vec![
                entry("SigGen1.value_", 8, A2lTypeEncoding::Float, 0, "Volt"),
                entry("SigGen1.mode", 1, A2lTypeEncoding::Unsigned, 8, r#"0 "OFF" 1 "ON""#),
            ],
            vec![
                entry("counter", 2, A2lTypeEncoding::Unsigned, 0, ""),
                entry("SigGen10.x", 2, A2lTypeEncoding::Signed, 2, ""),
            ],
        ];
        let value = |v: f64, mode: u8| {
            let mut data = v.to_le_bytes().to_vec();
            data.push(mode);
            data
        };
        let samples = [
            (1, 2000, value(1.5, 0)),
            (1, 3000, value(f64::NAN, 0)), // not written
            (1, 4000, value(-2.0, 1)),
            (2, 4500, vec![7, 0, 0xFF, 0xFF]),
        ];
        let (events, ..) = run(odt_entries, &samples, 6000);
        let counters: Vec<_> = events
            .iter()
            .filter(|e| e["ph"] == "C")
            .map(|e| {
                let (series, value) = e["args"].as_object().unwrap().iter().next().unwrap();
                (
                    e["pid"].as_u64().unwrap(),
                    e["name"].as_str().unwrap(),
                    series.as_str(),
                    e["ts"].as_f64().unwrap(),
                    value.as_f64().unwrap(),
                )
            })
            .collect();
        assert_eq!(
            counters,
            [
                (CTX, "SigGen1.value_", "[Volt]", 1.0, 1.5),
                (CTX, "SigGen1.value_", "[Volt]", 3.0, -2.0),
                (ECU, "counter", "value", 3.5, 7.0),
                (ECU, "SigGen10.x", "value", 3.5, -1.0)
            ]
        );
        // OFF from the first sample to the change, ON until the end of the measurement
        assert_eq!(slices(&events), [(CTX, 1000, "OFF".to_string(), 1000, 2000), (CTX, 1000, "ON".to_string(), 3000, 2000)]);
        assert_eq!(metadata_name(&events, "thread_name", CTX, Some(1000)).as_deref(), Some("SigGen1.mode"));
    }

    // The link record of a context: clock (u64), context (u16), span (u16), thread (u8), consumer_thread (u8)
    fn link_entries(context: &str, thread: bool) -> Vec<OdtEntry> {
        const CONTEXTS: &str = r#"0 "none" 1 "SigGen1" 2 "Mainloop""#;
        let mut entries = vec![
            entry(&format!("{context}.link_context"), 2, A2lTypeEncoding::Unsigned, 8, CONTEXTS),
            entry(&format!("{context}.link_span"), 2, A2lTypeEncoding::Unsigned, 10, SPANS),
            entry(&format!("{context}.link_time"), 8, A2lTypeEncoding::Unsigned, 0, ""),
        ];
        if thread {
            entries.push(entry(&format!("{context}.link_thread"), 1, A2lTypeEncoding::Unsigned, 12, ""));
            entries.push(entry(&format!("{context}.link_consumer_thread"), 1, A2lTypeEncoding::Unsigned, 13, ""));
        }
        entries
    }

    fn link_data(clock: u64, context: u16, span: u16, thread: u8, consumer_thread: u8) -> Vec<u8> {
        let mut data = clock.to_le_bytes().to_vec();
        data.extend(context.to_le_bytes());
        data.extend(span.to_le_bytes());
        data.extend([thread, consumer_thread, 0, 0]);
        data
    }

    // The flows as (ph, pid, tid, name, id, ts in ns)
    fn flows(events: &[serde_json::Value]) -> Vec<(String, u64, u64, String, u64, u64)> {
        events
            .iter()
            .filter(|e| e["ph"] == "s" || e["ph"] == "f")
            .map(|e| {
                (
                    e["ph"].as_str().unwrap().to_string(),
                    e["pid"].as_u64().unwrap(),
                    e["tid"].as_u64().unwrap(),
                    e["name"].as_str().unwrap().to_string(),
                    e["id"].as_u64().unwrap(),
                    (e["ts"].as_f64().unwrap() * 1000.0).round() as u64,
                )
            })
            .collect()
    }

    // A producer span in SigGen1, a consumer span in Mainloop which links to it, and two links which can not be resolved
    #[test]
    fn test_span_links() {
        let odt_entries = vec![span_entries("SigGen1", true), span_entries("Mainloop", true), link_entries("Mainloop", true)];
        let samples = [
            (0, 2000, span_data(1, 1, 1)),          // SigGen1 thread 1: calc begins
            (0, 2600, span_data(0, 0, 1)),          // SigGen1 thread 1: calc ends
            (1, 3000, span_data(3, 1, 2)),          // Mainloop thread 2: poll begins
            (2, 3100, link_data(2300, 1, 1, 1, 2)), // link to calc of SigGen1, captured at 2300
            (2, 3200, link_data(500, 1, 1, 1, 2)),  // captured before the measurement start: not resolved
            (2, 3300, link_data(2300, 0, 1, 1, 2)), // producer context 'none': not resolved
            (1, 3400, span_data(0, 0, 2)),          // Mainloop thread 2: poll ends
        ];
        let mut writer = PerfettoWriter::new(Vec::new());
        let daq_lists: Vec<XcpDaqList> = odt_entries
            .into_iter()
            .enumerate()
            .map(|(i, entries)| XcpDaqList {
                event_id: i as u16,
                event_name: None,
                entries,
            })
            .collect();
        writer.start(&daq_lists, 1000);
        for (daq, t, data) in &samples {
            writer.sample(*daq, *t, &daq_lists[*daq].entries, data);
        }
        writer.stop(4000);
        assert_eq!((writer.flow_count(), writer.unresolved_link_count()), (1, 2));
        let json: serde_json::Value = serde_json::from_slice(&writer.into_inner()).expect("valid JSON");
        let events = json["traceEvents"].as_array().unwrap();
        let (siggen1, mainloop) = (CTX, CTX + 1);
        assert_eq!(
            flows(events),
            [
                ("s".to_string(), siggen1, 1, "calc".to_string(), 1, 1300),
                ("f".to_string(), mainloop, 2, "calc".to_string(), 1, 2100)
            ]
        );
        assert!(events.iter().any(|e| e["ph"] == "f" && e["bp"] == "e"));
        // The signals of the link record are no counters or states
        assert!(!events.iter().any(|e| e["ph"] == "C"));
        assert!(metadata_name(events, "thread_name", mainloop, Some(TID_STATE_FIRST as u64)).is_none());
    }

    // Without the thread signals both ends are on the spans track, the producer timestamp is converted with the timestamp resolution
    #[test]
    fn test_span_links_without_threads() {
        let daq_lists: Vec<XcpDaqList> = [span_entries("SigGen1", false), link_entries("Mainloop", false)]
            .into_iter()
            .enumerate()
            .map(|(i, entries)| XcpDaqList {
                event_id: i as u16,
                event_name: None,
                entries,
            })
            .collect();
        let mut writer = PerfettoWriter::new(Vec::new());
        writer.set_timestamp_resolution(10); // 10 ns per tick
        writer.start(&daq_lists, 1000);
        writer.sample(1, 5000, &daq_lists[1].entries, &link_data(250, 1, 3, 0, 0)); // captured at tick 250 = 2500 ns
        writer.stop(6000);
        let json: serde_json::Value = serde_json::from_slice(&writer.into_inner()).expect("valid JSON");
        assert_eq!(
            flows(json["traceEvents"].as_array().unwrap()),
            [
                ("s".to_string(), CTX, TID_SPANS as u64, "poll".to_string(), 1, 1500),
                ("f".to_string(), ECU, TID_SPANS as u64, "poll".to_string(), 1, 4000)
            ]
        );
    }

    #[test]
    fn test_json_string_and_time() {
        assert_eq!(PerfettoWriter::<Vec<u8>>::json_string("a\"b\\c\n"), r#""a\"b\\c\u000a""#);
        assert_eq!(PerfettoWriter::<Vec<u8>>::us(1_234_567), "1234.567");
        assert_eq!(PerfettoWriter::<Vec<u8>>::us(5), "0.005");
    }
}
