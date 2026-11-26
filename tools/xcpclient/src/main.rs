//-----------------------------------------------------------------------------
// xcpclient - XCP test tool
//
// - Connect to XCP on Ethernet servers via TCP or UDP
// - Upload A2L or ELF files from XCP servers (GET_ID command)
// - Create complete A2L files from ELF debug information the XCP server event and memory segment information
// - Create A2L templates from the XCP server event and memory segment information
// - Read and write calibration variables (CAL)
// - Configure and acquire measurement data (DAQ)
// - List available variables and parameters with regex patterns
// - Execute test sequences
//
// xcpclient --help
//-----------------------------------------------------------------------------

use std::io::{IsTerminal, Write};
use std::net::Ipv4Addr;
use std::process::ExitCode;
use std::{error::Error, sync::Arc};

use figment::{
    Figment,
    providers::{Format, Toml},
};
use parking_lot::Mutex;

mod xcp_client;
use xcp_client::*;

mod xcp_test_executor;

mod a2l_fix;

mod daq_writer;
use daq_writer::{DaqDecoder, DaqWriter};

mod console_writer;
use console_writer::ConsoleWriter;

mod csv_writer;
use csv_writer::CsvWriter;

mod perfetto_writer;
use perfetto_writer::{PerfettoWriter, TraceAuto};

mod mdf_writer;
use mdf_writer::MdfWriter;
mod mcap_writer;
use mcap_writer::McapWriter;

use xcp_registry::McA2mlMode;
use xcp_registry::McEvent;
use xcp_test_executor::test_executor;

pub mod elf_reader;
use elf_reader::{ElfFilter, ElfReader};

pub mod bin_reader;

//-----------------------------------------------------------------------------
// Command line arguments

use clap::Parser;

//------------------------------------------------------------------------
// --default-event value

/// Value of --default-event: an event id or an event name.
/// A name follows the C identifier rules and is resolved to an event id against the registry event list once it is complete.
#[derive(Debug, Clone, PartialEq)]
enum DefaultEvent {
    Id(u16),
    Name(String),
}

impl DefaultEvent {
    /// Resolve to an event id
    /// An id is taken as it is, a name is looked up in the registry event list and is an error when not found
    fn resolve(&self, reg: &xcp_registry::Registry) -> Result<u16, String> {
        match self {
            DefaultEvent::Id(id) => Ok(*id),
            DefaultEvent::Name(name) => reg
                .event_list
                .iter()
                .find(|e| e.get_unique_name(reg) == name.as_str() || (e.index == 0 && e.get_name() == name.as_str()))
                .map(|e| e.get_id())
                .ok_or_else(|| {
                    let known: Vec<String> = reg.event_list.iter().map(|e| format!("{} ({})", e.get_unique_name(reg), e.get_id())).collect();
                    format!("Default event '{}' not found in the event list [{}]", name, known.join(", "))
                }),
        }
    }
}

impl std::fmt::Display for DefaultEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DefaultEvent::Id(id) => write!(f, "{}", id),
            DefaultEvent::Name(name) => write!(f, "{}", name),
        }
    }
}

impl std::str::FromStr for DefaultEvent {
    type Err = String;

    /// An unsigned integer is an event id, a C identifier is an event name, anything else is an error
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.trim();
        if let Ok(id) = s.parse::<u16>() {
            return Ok(DefaultEvent::Id(id));
        }
        let mut chars = s.chars();
        let is_identifier = matches!(chars.next(), Some(c) if c == '_' || c.is_ascii_alphabetic()) && chars.all(|c| c == '_' || c.is_ascii_alphanumeric());
        if is_identifier {
            Ok(DefaultEvent::Name(s.to_string()))
        } else {
            Err(format!("'{}' is neither an event id (0..65535) nor an event name (C identifier)", s))
        }
    }
}

/// Config file: accept an integer (event id) or a string (event id or event name)
impl<'de> serde::Deserialize<'de> for DefaultEvent {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        #[derive(serde::Deserialize)]
        #[serde(untagged)]
        enum Raw {
            Id(u16),
            Text(String),
        }
        match Raw::deserialize(deserializer)? {
            Raw::Id(id) => Ok(DefaultEvent::Id(id)),
            Raw::Text(s) => s.parse().map_err(serde::de::Error::custom),
        }
    }
}

#[derive(Parser, Debug)]
#[command(name = "xcpclient")]
#[command(about = concat!("XCP client v", env!("CARGO_PKG_VERSION"), " for testing XCP servers and managing A2L and HEX files"))]
#[command(long_about = concat!("XCP client v", env!("CARGO_PKG_VERSION"), " for testing XCP servers and managing A2L and HEX files"))]
#[command(version)]
struct Args {
    // --config
    /// Load arguments from a TOML config file. Command-line arguments take precedence.
    #[arg(long, default_value = "")]
    config: String,

    // -l --log-level
    /// Program flow log level (Off=0, Error=1, Warn=2, Info=3, Debug=4, Trace=5)
    #[arg(long, default_value_t = 3)]
    log_level: u8,

    // -v --verbose
    /// ELF parsing log verbosity level
    #[arg(long, default_value_t = 0)]
    verbose: usize,

    // -d --dest_addr
    /// XCP server address (IP address or IP:port). If port is omitted, uses --port parameter.
    #[arg(long, default_value = "127.0.0.1")]
    dest_addr: String,

    // --port
    /// XCP server port number (used when --dest-addr doesn't include port).
    #[arg(long, default_value_t = 5555)]
    port: u16,

    // --bind-addr
    /// Bind address (IP address or IP:port). If port is omitted, system assigns an available port.
    #[arg(long, default_value = "0.0.0.0")]
    bind_addr: String,

    // --baud-rate
    /// Baud rate for XCP communication (only applicable for certain protocols).
    #[arg(long, default_value_t = 115200)]
    baud_rate: u32,

    // --tcp
    /// Use TCP for XCP communication..
    #[arg(long, default_value_t = false)]
    tcp: bool,
    // --udp
    /// Use UDP for XCP communication
    #[arg(long, default_value_t = false)]
    udp: bool,
    // --sxi
    /// Use SxI for XCP communication
    #[arg(long, default_value_t = false)]
    sxi: bool,

    // --connect-mode
    /// XCP connect mode
    #[arg(long, default_value_t = 0)]
    connect_mode: u8,

    // --offline
    /// Force offline mode (no network communication), communication parameters are used to create A2L file.
    #[arg(long, default_value_t = false)]
    offline: bool,

    // --a2l
    /// Specify and overide the name of the A2L file name.
    /// If not specified, The A2L file name is read from the XCP server.
    #[arg(long, default_value = "")]
    a2l: String,

    // --upload-a2l
    /// Upload A2L file from XCP server.
    /// Requires that the XCP server supports GET_ID A2L upload.
    #[arg(long, default_value_t = false)]
    upload_a2l: bool,

    // --create-a2l
    /// Build an A2L file template from XCP server information about events and memory segments.
    /// Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands.
    /// Insert all visible measurement and calibration variables from ELF file if specified with --elf or --upload-elf.
    #[arg(long, default_value_t = false)]
    create_a2l: bool,

    // --create-a2l-template
    /// Build a minimal A2L template from XCP server event and memory segment information only.
    /// No variables or types are registered; the result is a skeleton A2L file.
    /// Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands.
    #[arg(long, default_value_t = false)]
    create_a2l_template: bool,

    // --fix-a2l
    /// Correct the event ids and calibration segment numbers of the given A2L file with XCP server information and rewrite it, the original is kept as <file>.a2l.bak.
    /// Only for A2L files created by xcpclient (--elf and --create-a2l), typically generated offline without section registration.
    /// Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands.
    #[arg(long, default_value_t = false)]
    fix_a2l: bool,

    // --embed-aml
    /// Embed the XCP A2ML description into an A2L file created by xcpclient (--elf, --create-a2l, --create-a2l-template), instead of including XCP_104.aml.
    /// The A2L file is self-contained, XCP_104.aml is not needed next to it. --fix-a2l keeps an embedded A2ML description.
    #[arg(long, default_value_t = false)]
    embed_aml: bool,

    // --upload-elf
    /// Upload ELF file from XCP server.
    /// Requires that the XCP server supports proprietary GET_ID ELF upload command.
    #[arg(long, default_value_t = false)]
    upload_elf: bool,

    // ---elf
    /// Specify the name of an ELF file, create an A2L file from ELF debug information.
    /// If connected to a XCP server, events and memory segments will be extracted from the XCP server.
    /// ELF files with DWARF debug information only (Linux, QNX, embedded targets), macOS Mach-O executables are not supported.
    #[arg(long, default_value = "")]
    elf: String,

    // --elf-unit-limit
    /// Parse only compilations units <= n.
    #[arg(long, default_value_t = usize::MAX)]
    elf_unit_limit: usize,

    // --elf-unit-limit-min
    /// Parse only compilations units >= n.
    #[arg(long, default_value_t = 0)]
    elf_unit_limit_min: usize,

    // --elf-var-filter
    /// Regex pattern to filter variable names when registering from an ELF file.
    /// Only variables whose names match the pattern are included in the A2L output.
    /// If not specified (or empty), all variables are registered.
    #[arg(long, default_value = "")]
    elf_var_filter: String,

    // --elf-skip-no-metadata
    /// Skip variables without any metadata (XCP_UNIT / XCP_LIMITS / XCP_COMMENT) when creating an A2L file from an ELF file.
    /// Only variables that have at least one metadata annotation are included in the A2L output.
    #[arg(long, default_value_t = false)]
    elf_skip_no_metadata: bool,

    // --elf-unit-filter
    /// Regex pattern to filter variables by their compilation unit (source file) name.
    /// Only variables defined in compilation units whose name matches are included in the A2L output.
    /// If not specified (or empty), variables from all compilation units are registered.
    #[arg(long, default_value = "")]
    elf_unit_filter: String,

    // --bin
    /// Specify the pathname of a binary file (Intel-HEX) for calibration parameter segment data
    #[arg(long, default_value = "")]
    bin: String,

    // --upload-bin
    /// Upload all calibration segments working page data from target and store into a binary file.
    /// Requires that the XCP server supports GET_ID A2L upload.
    /// A .bin file is an XCPlite persistence file with the event list and the context list (conv.contexts in the A2L file), a .hex file contains the segment data only.
    #[arg(long, default_value_t = false)]
    upload_bin: bool,

    // --download-bin
    /// Download all calibration segments working page data in a binary file to the target.
    #[arg(long, default_value_t = false)]
    download_bin: bool,

    // --list-mea
    /// Lists all specified measurement variables (regex) found in the A2L file.
    #[arg(long, default_value = "")]
    list_mea: String,

    // --mea
    /// Specify variable names for DAQ measurement (list), may be list of names separated by space or single regular expressions (e.g. ".*").
    #[arg(long, value_delimiter = ' ', num_args = 1..)]
    mea: Vec<String>,

    // --default-event
    /// Event for variables without a fixed event (global variables and static variables in functions without an event trigger), given by event id or event name.
    /// Used for their DAQ measurement and assigned to them as default event when an A2L file is created from an ELF file.
    /// An event name is looked up in the event list (from the XCP server, the ELF file or the A2L file), xcpclient aborts when it is not found.
    /// If not specified, such variables get no event and can not be measured with xcpclient.
    #[arg(long, value_name = "ID|NAME")]
    default_event: Option<DefaultEvent>,

    // --time
    /// Time limit measurement duration to n s. 0 means infinite.
    #[arg(long, default_value_t = 0)]
    time: u64,

    // --csv
    /// Save measurement data to a CSV file. If not specified, data is printed to the console.
    /// CSV format: time_ns,daq,name,value,unit  (one row per measurement sample).
    /// Values are physical values (factor and offset applied), the value of an enum is its label.
    #[arg(long, default_value = "")]
    csv: String,

    // --mdf
    /// Save measurement data to an ASAM MDF 4.1 file. One channel group per DAQ list, the payload of the DAQ packets is stored as received.
    /// Values are raw values, the MDF file contains the conversions (factor and offset, enum labels) and units.
    /// May be combined with --csv and --perfetto.
    #[arg(long, default_value = "", value_name = "FILE")]
    mdf: String,

    // --mcap
    /// Save measurement data to an MCAP file (https://mcap.dev). One channel per DAQ list, the topic is the event name, the payload of the
    /// DAQ packets is stored as received, as ros1 messages. Values are raw values, MCAP has no conversions or units, they are in the
    /// comments of the message definitions and in the channel metadata. May be combined with --csv, --mdf and --perfetto.
    #[arg(long, default_value = "", value_name = "FILE")]
    mcap: String,

    // --perfetto
    /// Save measurement data to a trace file for Perfetto (Chrome Trace Event JSON format, open it with https://ui.perfetto.dev).
    /// Spans (DaqSpan) are slices in a track per context and thread, numeric signals are counter tracks, enum signals are state tracks,
    /// span links (xcp::LinkSpan) are flows (arrows) from the producer span to the consumer span.
    /// The span and link signals of the contexts needed for the trace are added to the measurement, see --trace-auto.
    /// May be combined with --csv, the added signals are not written to the CSV file.
    #[arg(long, default_value = "", value_name = "FILE")]
    perfetto: String,

    // --trace-auto
    /// Trace signals added to the measurement with --perfetto: off (none, only hints which signals are missing),
    /// context (the spans of the contexts of the measured signals), links (also the span links of these contexts and, if there are any,
    /// the spans of all contexts as possible producers), all (the spans and span links of all contexts).
    /// The context of a signal is the prefix of its name or of the name of its event (<context>.<name>).
    #[arg(long, default_value = "links", value_parser = ["off", "context", "links", "all"], value_name = "LEVEL")]
    trace_auto: String,

    // --list-cal
    /// Lists all specified calibration variables (regex) found in the A2L file
    #[arg(long, default_value = "")]
    list_cal: String,

    // --cal
    /// Set calibration variable to a value (format: "variable_name value").
    #[arg(long, value_names = ["NAME", "VALUE"], num_args = 2)]
    cal: Vec<String>,

    /// --test
    /// Execute a test sequence on the XCP server.
    /// If there is a calibration segment named params (see daq_test, c_demo), a calibration consistency test will be performed.
    #[arg(long, default_value_t = false)]
    test: bool,

    // --yes
    /// Automatically confirm EPK mismatch and other safety warnings. Use in scripts to suppress interactive prompts.
    #[arg(long, short = 'y', default_value_t = false)]
    yes: bool,
}

//----------------------------------------------------------------------------------------------
// Logging

#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

trait ToLogLevelFilter {
    fn to_log_level_filter(self) -> log::LevelFilter;
}

impl ToLogLevelFilter for u8 {
    fn to_log_level_filter(self) -> log::LevelFilter {
        match self {
            0 => log::LevelFilter::Off,
            1 => log::LevelFilter::Error,
            2 => log::LevelFilter::Warn,
            3 => log::LevelFilter::Info,
            4 => log::LevelFilter::Debug,
            5 => log::LevelFilter::Trace,
            _ => log::LevelFilter::Warn,
        }
    }
}

//------------------------------------------------------------------------
// Measurement data output
// The DAQ decoder hands the DAQ packets to writers, one for each output format, see module daq_writer

// Create the writers for the measurement data output, without an output file the data is printed to the console
// contexts are the known contexts and trace_signals the signals added to the measurement for --perfetto (see add_trace_signals)
fn create_daq_writers(
    csv_filename: &str,
    mdf_filename: &str,
    mcap_filename: &str,
    perfetto_filename: &str,
    ecu_name: &str,
    contexts: &[String],
    trace_signals: &[String],
) -> Result<Vec<Box<dyn DaqWriter>>, Box<dyn Error>> {
    let mut writers: Vec<Box<dyn DaqWriter>> = Vec::new();
    if !csv_filename.is_empty() {
        let writer = CsvWriter::create(csv_filename).map_err(|e| format!("Failed to create CSV file '{}': {}", csv_filename, e))?;
        info!("CSV output to file: {}", csv_filename);
        // The trace signals are not written to the CSV file
        writers.push(Box::new(writer.with_skip(trace_signals)));
    }
    if !mdf_filename.is_empty() {
        let writer = MdfWriter::create(mdf_filename).map_err(|e| format!("Failed to create MDF file '{}': {}", mdf_filename, e))?;
        info!("MDF output to file: {}", mdf_filename);
        writers.push(Box::new(writer.with_ecu_name(ecu_name)));
    }
    if !mcap_filename.is_empty() {
        let writer = McapWriter::create(mcap_filename).map_err(|e| format!("Failed to create MCAP file '{}': {}", mcap_filename, e))?;
        info!("MCAP output to file: {}", mcap_filename);
        writers.push(Box::new(writer.with_ecu_name(ecu_name)));
    }
    if !perfetto_filename.is_empty() {
        let mut writer = PerfettoWriter::create(perfetto_filename).map_err(|e| format!("Failed to create Perfetto trace file '{}': {}", perfetto_filename, e))?;
        info!("Perfetto trace output to file: {}", perfetto_filename);
        writer.set_ecu_name(ecu_name);
        writer.set_contexts(contexts);
        writers.push(Box::new(writer));
    }
    if writers.is_empty() {
        writers.push(Box::new(ConsoleWriter::stdout()));
    }
    Ok(writers)
}

//------------------------------------------------------------------------
// Trace signals for --perfetto
// Adds the span and link signals needed for the trace to the measurement list, at the level given with --trace-auto
// Returns the measurement list, the known contexts (each is a process in the Perfetto trace) and the added signals

fn add_trace_signals(xcp_client: &XcpClient, trace_auto: &str, mut list: Vec<String>) -> (Vec<String>, Vec<String>, Vec<String>) {
    let level: TraceAuto = trace_auto.parse().unwrap_or(TraceAuto::Links);
    let measurements: Vec<(String, Option<String>)> = xcp_client
        .find_measurements(".*")
        .into_iter()
        .map(|name| {
            let event = xcp_client.measurement_event_name(&name);
            (name, event)
        })
        .collect();
    let contexts = perfetto_writer::find_contexts(measurements.iter().map(|(n, _)| n.as_str()));
    if contexts.is_empty() {
        return (list, contexts, Vec::new());
    }

    let added = perfetto_writer::trace_signals(level, &measurements, &list);
    if level == TraceAuto::Off {
        let missing = perfetto_writer::trace_signals(TraceAuto::Links, &measurements, &list);
        if !missing.is_empty() {
            warn!(
                "Perfetto trace: the spans and span links of the contexts are not measured, add them with --trace-auto links or measure them: {}",
                missing.join(" ")
            );
        }
    } else if !added.is_empty() {
        info!("Perfetto trace: {} trace signals added (--trace-auto {}): {}", added.len(), trace_auto, added.join(" "));
    }
    list.extend(added.iter().cloned());
    (list, contexts, added)
}

//------------------------------------------------------------------------
// Handle incoming SERV_TEXT data
// Prints the text to the console

#[derive(Debug, Clone, Copy)]
struct ServTextDecoder;

impl ServTextDecoder {
    pub fn new() -> ServTextDecoder {
        ServTextDecoder {}
    }
}

impl XcpTextDecoder for ServTextDecoder {
    // Handle incomming text data from XCP server
    fn decode(&self, data: &[u8]) {
        print!("[SERV_TEXT] ");
        let mut j = 0;
        while j < data.len() {
            print!("{}", data[j] as char);
            j += 1;
        }
    }
}

//------------------------------------------------------------------------
//  XCP client

async fn xcp_client(args: Args, protocol: &'static str, dest_addr: std::net::SocketAddr, local_addr: std::net::SocketAddr, baud_rate: u32) -> Result<(), Box<dyn Error>> {
    // Destructure args
    let Args {
        verbose,
        connect_mode,
        offline,
        a2l: a2l_filename,
        upload_a2l,
        create_a2l,
        create_a2l_template,
        fix_a2l,
        embed_aml,
        elf: elf_filename,
        upload_elf,
        elf_unit_limit_min,
        elf_unit_limit,
        elf_var_filter,
        elf_skip_no_metadata,
        elf_unit_filter,
        bin: bin_filename,
        upload_bin,
        download_bin,
        list_cal,
        list_mea,
        mea: measurement_list,
        default_event,
        time,
        cal: cal_args,
        csv: csv_filename,
        mdf: mdf_filename,
        mcap: mcap_filename,
        perfetto: perfetto_filename,
        trace_auto,
        yes,
        ..
    } = args;
    let measurement_duration_ms = time * 1000;

    // Create xcp_client
    let mut xcp_client = XcpClient::new(protocol, dest_addr, local_addr, baud_rate);
    if let Some(event) = &default_event {
        info!("Default event {} for variables without a fixed event", event);
    }
    // Default event id, resolved from --default-event once the registry event list is complete
    let mut default_event_id: Option<u16> = None;

    // Target ECU name (from GET_ID)
    let mut ecu_name = String::new();

    // A2L name (from GET_ID)
    let mut a2l_name = String::new();
    // EPK reported by the target, empty when offline or unsupported
    let mut ecu_epk = String::new();

    // Calibration segment relative addressing mode
    let mut segment_relative = false;

    // DAQ decoder, the measurement data writers are set at measurement start
    let mut daq_decoder_handle: Option<Arc<Mutex<DaqDecoder>>> = None;

    // Execute main logic and capture the result
    let result = async {
        //----------------------------------------------------------------
        // Connect the XCP on ETH server if required
        let go_online = !offline && (protocol == "TCP" || protocol == "UDP" || upload_a2l || upload_elf || !measurement_list.is_empty() || !cal_args.is_empty());
        if go_online {
            // Connect to the XCP server
            // Print protocol information
            info!("XCP Connect using {}", protocol);
            let daq_decoder = Arc::new(Mutex::new(DaqDecoder::new()));
            daq_decoder_handle = Some(Arc::clone(&daq_decoder));
            match xcp_client.connect(connect_mode, Arc::clone(&daq_decoder), ServTextDecoder::new()).await {
                Ok(_) => {
                    info!("Connected to XCP server at {}", dest_addr);
                }
                Err(e) => {
                    error!("Failed to connect to XCP server at {}, Error: {}", dest_addr, e);
                    return Err("Failed to connect to XCP server".into());
                }
            }
            xcp_client.log_connect_info();
            assert!((xcp_client.comm_mode_basic() & 0x07) == 0); // Address granularity != 1 and motorola format not supported

            info!("Reading target ECU information via XCP GET_ID commands:");

            // Get target ECU name
            let res = xcp_client.get_id(xcp::IDT_ASCII).await;
            match res {
                Ok((_, Some(id))) => {
                    ecu_name = id;
                    info!("  GET_ID IDT_ASCII = {}", ecu_name);
                }
                Err(e) => {
                    error!("GET_ID IDT_ASCII failed, Error: {}", e);
                }
                _ => {
                    panic!("Empty string");
                }
            };

            // Get A2L name
            let res = xcp_client.get_id(xcp::IDT_ASAM_NAME).await;
            match res {
                Ok((_, Some(id))) => {
                    a2l_name = id;
                    info!("  GET_ID IDT_ASAM_NAME = {}", a2l_name);
                }
                Err(e) => {
                    error!("GET_ID IDT_ASAM_NAME failed, Error: {}", e);
                }
                _ => {
                    panic!("Empty string");
                }
            };

            // Get EPK
            let res = xcp_client.get_id(xcp::IDT_ASAM_EPK).await;
            match res {
                Ok((_, Some(id))) => {
                    info!("  GET_ID IDT_EPK = {}", id);
                    ecu_epk = id;
                }
                Err(e) => {
                    warn!("GET_ID IDT_ASAM_EPK failed, Error: {}", e);
                }
                _ => {
                    panic!("Empty string");
                }
            };
        } // go online

        //----------------------------------------------------------------
        // Create a new empty A2L registry
        let mut reg = xcp_registry::Registry::new();
        if !ecu_name.is_empty() {
            reg.application.set_info(ecu_name.clone(), "-", 0);
        }
        reg.set_flatten_typedefs_mode(false);
        reg.set_prefix_names_mode(false);
        reg.set_a2ml_mode(if embed_aml { McA2mlMode::Embed } else { McA2mlMode::Include });

        // Set A2L file path to given command line argument 'a2l' or to GET_ID IDT_ASAM_NAME or IDT_ASCII
        let (mut a2l_path, a2l_asam_name) = if !a2l_filename.is_empty() {
            log::info!("Using A2L file name from command line argument: {}", a2l_filename);
            let a2l_filename = if !a2l_filename.ends_with(".a2l") {
                // Add .a2l extension
                format!("{}.a2l", a2l_filename)
            } else {
                a2l_filename
            };
            (std::path::Path::new(&a2l_filename).to_path_buf(), false)
        } else if !a2l_name.is_empty() {
            log::info!("Using A2L file name from XCP server GET_ID ASAM_NAME: {}", a2l_name);
            let a2l_filename = format!("{}.a2l", a2l_name);
            (std::path::Path::new(&a2l_filename).to_path_buf(), true) // from GET_ID ASAM_NAME
        } else if !ecu_name.is_empty() {
            log::info!("Using A2L file name from XCP server GET_ID ASCII: {}", ecu_name);
            let a2l_filename = format!("{}.a2l", ecu_name);
            (std::path::Path::new(&a2l_filename).to_path_buf(), false) // from GET_ID ASCII
        } else {
            return Err("^No A2L file name specified, use --a2l commandline parameter".into());
        };
        info!("A2L path: {}", a2l_path.display());

        //----------------------------------------------------------------
        // Upload A2L
        // From XCP server and load it into the xcp_client registry
        if upload_a2l {
            // Don't overwrite existing A2L file generated by a XCP demo running in the same directory
            if a2l_path.exists() && a2l_asam_name && a2l_path == std::path::Path::new(&a2l_name).with_extension("a2l") {
                // Generate a different file name for the uploaded A2L file
                let new_a2l_path = a2l_path
                    .with_file_name(format!("{}_upload", a2l_path.file_stem().unwrap().to_str().unwrap()))
                    .with_extension("a2l");
                warn!(
                    "The A2L file '{}' already exists and matches the XCP server A2L file name (from GET_ID ASAM_NAME), uploading to {}",
                    a2l_path.display(),
                    new_a2l_path.display()
                );
                a2l_path = new_a2l_path;
            }

            // Upload A2L file
            let res = xcp_client.upload_a2l_into_registry(&a2l_path, &mut reg).await;
            if let Err(e) = res {
                error!("A2L upload failed, Error: {}", e);
                return Err("A2L upload failed".into());
            }
        }
        //
        //----------------------------------------------------------------
        // Create A2L file
        // If an ELF file is available, create an A2L file from the XCP server information and the ELF file
        // If option create-a2l is specified and no ELF file, create a A2L template from XCP server information
        // Read segment and event information obtained from the XCP server into registry
        // Add measurement and calibration variables from ELF file if specified
        // Addressing scheme may be XCPLITE__ACSDD or XCPLITE__CASDD depending on the target configuration
        else if create_a2l || create_a2l_template || !elf_filename.is_empty() || upload_elf {
            let mode = if create_a2l_template {
                if !elf_filename.is_empty() || upload_elf {
                    "target XCP event/segment and ELF/DWARF event/segment information (template, no variables), online mode"
                } else {
                    "target XCP event/segment information only (template, no variables), online mode"
                }
            } else if xcp_client.is_connected() {
                if !elf_filename.is_empty() || upload_elf {
                    "target XCP event/segment and ELF/DWARF variable and type information, online mode"
                } else {
                    "target XCP event/segment information only, online mode"
                }
            } else {
                "ELF/DWARF information only,  offline mode"
            };
            info!("Generate A2L file {} with {} ", a2l_path.display(), mode);

            // Set registry XCP transport layer information for A2L file
            // TCP or UDP: set IP address and port number
            if protocol == "TCP" || protocol == "UDP" {
                let addr = dest_addr.ip();
                let ipv4_addr = match addr {
                    std::net::IpAddr::V4(v4) => v4,
                    std::net::IpAddr::V6(_) => Ipv4Addr::new(127, 0, 0, 1),
                };
                let port: u16 = dest_addr.port();
                reg.set_xcp_eth_params(protocol, ipv4_addr, port);
            }
            // SxI protocol: set baud rate
            else if protocol == "SxI" {
                reg.set_xcp_sxi_params(baud_rate);
            }

            // If there is an ECU online, get event and segment information via XCP
            if xcp_client.is_connected() {
                xcp_client.get_event_segment_info(&mut reg).await?;
            }

            // Read ELF/DWARF information for events, segments and (unless --create-a2l-template) variables
            // Events and calibration segments found in the ELF file, must match the XCP server information if present
            // If not, they are created, but with dummy event id and segment number, which has to be fixed later !!!
            if !elf_filename.is_empty() || upload_elf {
                // Upload ELF file
                if upload_elf {
                    if elf_filename.is_empty() {
                        error!("ELF file name must be specified with --elf when using --upload-elf");
                        return Err("ELF file name not specified".into());
                    }
                    info!("Upload ELF file: {}", elf_filename);
                    let res = xcp_client.upload_elf_file(&elf_filename).await;
                    if let Err(e) = res {
                        error!("ELF upload failed, Error: {}", e);
                        return Err("ELF upload failed".into());
                    }
                }

                // The compilation unit and variable name selection, built and validated once here and then carried by the ELF reader
                let elf_filter = ElfFilter::new(elf_unit_limit_min, elf_unit_limit, &elf_unit_filter, &elf_var_filter)?;

                // Read ELF file and DWARF debug information, compilation unit number may be limited to reduce processing time and memory needed
                info!("Reading ELF file: {}", elf_filename);
                let elf_reader = ElfReader::new(&elf_filename, verbose, elf_filter).map_err(|e| format!("Failed to read ELF file '{}': {}", elf_filename, e))?;
                elf_reader.log_compilers();

                if verbose > 0 {
                    elf_reader.debug_data.print_debug_info(verbose);
                }

                // Detect addressing scheme for calibration segments
                // Get target signature (CASDD, ACSDD, ...) from ELF file if available
                // true - addr_ext==0 is segment relative addressing, addr_ext==1 is absolute addressing
                // false - addr_ext==0 is absolute addressing, addr_ext==1 is segment relative addressing
                info!("===============================================================");
                segment_relative = {
                    let signature = elf_reader.get_target_signature();
                    if let Some(sig) = signature {
                        info!("Target signature found: {}", sig);
                        if sig.starts_with("C") { true } else { false }
                    } else {
                        warn!("No target signature found in ELF file");
                        false
                    }
                };
                info!(
                    "Using {} addressing for calibration segments",
                    if segment_relative { "segment relative" } else { "absolute" }
                );

                // Register events from event creation markers (evt__name) in the code
                elf_reader.register_events(&mut reg, verbose)?;

                // Register segments from segment creation markers (calseg__name) found in the code
                elf_reader.register_segments(&mut reg, segment_relative, verbose)?;

                // Get the EPK string from debug_data and set it in the registry application version information, if available
                // After the segments, ADDR_EPK depends on the addressing mode and the epk segment
                elf_reader.register_epk_addr_info(&mut reg, segment_relative, verbose);

                // Find event triggers in the code by their trigger markers (trg__mode__name) and register their location (compilation unit, function, CFA offset)
                elf_reader.register_event_locations(&mut reg, verbose)?;

                // Register all accessible variables and their types
                // Skipped in --create-a2l-template mode; events and segments are still registered above
                if !create_a2l_template {
                    // The event list is complete now (XCP server and ELF file), resolve the default event name to its id
                    if let Some(event) = &default_event {
                        default_event_id = Some(event.resolve(&reg)?);
                    }
                    elf_reader.register_variables(&mut reg, segment_relative, verbose, default_event_id)?;
                    // Register the captured local variables of the event triggers (DaqTriggerEventCapture)
                    elf_reader.register_captures(&mut reg, verbose)?;
                    // Apply metadata (XCP_UNIT / XCP_LIMITS / XCP_COMMENT) from the xcp_meta ELF section
                    elf_reader.register_metadata(&mut reg, verbose)?;
                    // Optionally remove all variables without any metadata (XCP_UNIT / XCP_LIMITS / XCP_COMMENT) from the registry
                    if elf_skip_no_metadata {
                        let before = reg.instance_list.len();
                        reg.instance_list.retain(|inst| inst.get_mc_support_data().has_metadata());
                        let removed = before - reg.instance_list.len();
                        if verbose > 0 || removed > 0 {
                            info!("Removed {} variables without metadata ({} remaining)", removed, reg.instance_list.len());
                        }
                    }
                }
            }

            // Write the registry to A2L file
            if !a2l_path.as_os_str().is_empty() {
                info!("===============================================================");
                if a2l_path.exists() {
                    warn!("Overwriting existing A2L file: {}", a2l_path.display());
                }
                if ecu_name.is_empty() {
                    ecu_name = "project_name".into();
                }
                let title_info = format!("Created by xcp_client with {} - {}", mode, chrono::Utc::now().format("%Y-%m-%d %H:%M:%S"));
                reg.write_a2l(
                    &a2l_path,
                    title_info.as_str(),
                    &ecu_name,
                    "",
                    &ecu_name,
                    if segment_relative { "XCPLITE__CASDD" } else { "XCPLITE__ACSDD" },
                    true,
                )
                .unwrap();
                info!("Created A2L with file: {} {}", a2l_path.display(), mode);
            }

            // The A2L file keeps the typedef structure, but xcp_client supports only basic type instances (see load_a2l_file_into_registry):
            // flatten the registry like loading an A2L file does, so --list-cal, --cal and --mea work the same as with --a2l
            reg.flatten_typedefs();
        }
        //----------------------------------------------------------------
        // Load A2L from local file
        // If not upload or create option load  A2L from specified file into reg
        // If fix-a2l option is specified, check and correct the A2L file with the XCP server information otherwise just warn about differences
        // Consider command line option --epk-segment
        else {
            info!("===============================================================");
            info!("Load A2L file: {}", a2l_path.display());
            xcp_client
                .load_a2l_file_into_registry(&a2l_path, &mut reg)
                .map_err(|e| format!("Could not load A2L file '{}': {}", a2l_path.display(), e))?;

            // Check the A2L EPK against the EPK reported by the target.
            // The EPK is the firmware version string the A2L was generated from,
            // so a mismatch means this A2L describes different firmware and its
            // addresses may point at the wrong variables.
            if !ecu_epk.is_empty() {
                let a2l_epk = reg.application.get_version();
                if a2l_epk.is_empty() {
                    warn!("A2L file {} has no EPK, cannot verify it matches the target", a2l_path.display());
                } else if a2l_epk != ecu_epk {
                    error!(
                        "EPK mismatch: A2L file '{}' has EPK '{}', target reports EPK '{}'. Addresses may be wrong.",
                        a2l_path.display(),
                        a2l_epk,
                        ecu_epk
                    );
                    if !yes {
                        if std::io::stdin().is_terminal() {
                            eprint!("Continue anyway? [y/N] ");
                            let _ = std::io::stderr().flush();
                            let mut input = String::new();
                            let _ = std::io::stdin().read_line(&mut input);
                            if !input.trim().eq_ignore_ascii_case("y") {
                                return Err("Aborted due to EPK mismatch".into());
                            }
                        } else {
                            return Err("EPK mismatch: aborting. Use --yes to proceed anyway.".into());
                        }
                    }
                } else {
                    info!("EPK '{}' of A2L file and target match", ecu_epk);
                }
            }

            let mut event_mapping: std::collections::HashMap<u16, u16> = std::collections::HashMap::new();
            let mut seg_mapping: std::collections::HashMap<u16, u16> = std::collections::HashMap::new();
            let mut missing_event_ids = 0;

            // Load the event and calibration segment information from target into a temporary registry and check if the given A2L file needs to be corrected
            let mut tmp_reg = xcp_registry::Registry::new();
            xcp_client.get_event_segment_info(&mut tmp_reg).await?;

            // Check events
            if xcp_client.max_events() == 0 {
                warn!("XCP server does not support get event info, skipping event check");
            } else {
                for event in &tmp_reg.event_list {
                    if let Some(e) = reg.event_list.find_event(event.get_name(), 0) {
                        if e.get_id() != event.get_id() {
                            warn!(
                                "Event id of '{}' differs, A2L file {} has id {}, target has id {}",
                                event.get_name(),
                                a2l_path.display(),
                                e.get_id(),
                                event.get_id()
                            );

                            // Create event mapping information in a hash map
                            event_mapping.insert(e.get_id(), event.get_id());
                        }
                    } else {
                        warn!("Event '{}' missing in A2L file {}", event.get_name(), a2l_path.display());
                        missing_event_ids += 1;
                    }
                }
            }

            // Check calibration segments
            if xcp_client.max_segments() == 0 {
                warn!("XCP server does not support get segment info, skipping calibration segment check");
            } else {
                for seg in &tmp_reg.cal_seg_list {
                    if let Some(s) = reg.cal_seg_list.find_cal_seg(seg.get_name()) {
                        if s.get_index() != seg.get_index() {
                            warn!(
                                "Calibration segment index of '{}' differs, A2L file {} has index {}, target has index {}",
                                seg.get_name(),
                                a2l_path.display(),
                                s.get_index(),
                                seg.get_index()
                            );

                            // Create segment mapping information in a hash map
                            seg_mapping.insert(s.get_index(), seg.get_index());
                        }
                    } else {
                        error!("Calibration segment '{}' missing in A2L file {}", seg.get_name(), a2l_path.display());
                    }
                }
            }

            // Fix the registry used in this run and rewrite the A2L file
            // In the event list and calibration segment list, and in the addresses of all instances with XCPlite event relative
            // or calibration segment relative addressing, which contain the event id or segment number
            if fix_a2l {
                // Only an A2L file generated by xcpclient is corrected, the event ids and segment numbers of an A2L file generated offline
                // from the ELF file without section registration. Any other A2L file is rejected, it might lose information when rewritten
                let a2l_text = std::fs::read_to_string(&a2l_path).map_err(|e| format!("Could not read A2L file '{}': {}", a2l_path.display(), e))?;
                if !a2l_fix::is_created_by_xcpclient(&a2l_text) {
                    return Err(format!(
                        "--fix-a2l: A2L file '{}' was not created by xcpclient, only A2L files created with --elf and --create-a2l can be corrected. \
                         Create the A2L file with --elf and --create-a2l while connected to the XCP server instead",
                        a2l_path.display()
                    )
                    .into());
                }
                if !event_mapping.is_empty() {
                    info!("Event mapping information:");
                    for (k, v) in &event_mapping {
                        info!("  {} -> {}", k, v);
                    }
                }
                if !seg_mapping.is_empty() {
                    info!("Calibration segment mapping information:");
                    for (k, v) in &seg_mapping {
                        info!("  {} -> {}", k, v);
                    }
                }
                let project_no = a2l_fix::get_project_no(&a2l_text);
                a2l_fix::apply_mappings(&mut reg, &event_mapping, &seg_mapping, project_no.as_deref());

                if missing_event_ids > 0 {
                    if missing_event_ids == 1 {
                        // Add a dummy event
                        match reg.event_list.add_event(McEvent::new("async", 0, 0, 0)) {
                            Ok(_) => {
                                warn!("XCPlite async event added");
                            }
                            Err(e) => warn!("Failed to add event 'async', Error: {}", e),
                        }
                    } else {
                        warn!("A2L file {} is missing {} event ids, please correct manually", a2l_path.display(), missing_event_ids);
                    }
                }

                if event_mapping.is_empty() && seg_mapping.is_empty() {
                    info!("Event ids and calibration segment numbers of A2L file {} match the target", a2l_path.display());
                } else {
                    let tl_params = match dest_addr.ip() {
                        std::net::IpAddr::V4(addr) if protocol == "TCP" || protocol == "UDP" => Some((protocol, addr, dest_addr.port())),
                        _ => None,
                    };
                    a2l_fix::rewrite_a2l_file(&a2l_path, &event_mapping, &seg_mapping, tl_params, embed_aml)
                        .map_err(|e| format!("Could not rewrite A2L file '{}': {}", a2l_path.display(), e))?;
                }
            } else if !event_mapping.is_empty() || !seg_mapping.is_empty() {
                warn!("A2L file {} differs from target, use automatic correction (--fix-a2l)", a2l_path.display());
                info!("events: {:?}", event_mapping);
                info!("segments: {:?}", seg_mapping);
            }
        } // load  A2L from specified file

        // Resolve the default event against the final event list, if not done yet, and use it for the measurement of variables without a fixed event
        if let Some(event) = &default_event
            && default_event_id.is_none()
        {
            default_event_id = Some(event.resolve(&reg)?);
        }
        if let (Some(DefaultEvent::Name(name)), Some(id)) = (&default_event, default_event_id) {
            info!("Default event '{}' resolved to event id {}", name, id);
        }
        xcp_client.set_default_event(default_event_id);

        // Assign the new registry to xcp_client
        xcp_client.set_registry(reg);

        // Check the status of all calibration segments and goto working page
        if xcp_client.is_connected() {
            xcp_client.init_calibration_segments().await?;
        }

        // Print all known calibration objects and get their current value
        if !list_cal.is_empty() {
            println!();
            let cal_objects = xcp_client.find_characteristics(list_cal.as_str());
            info!("===============================================================");
            println!("Calibration variables:");
            if !cal_objects.is_empty() {
                for name in &cal_objects {
                    {
                        let h: XcpCalibrationObjectHandle = xcp_client.create_calibration_object(name).await?;
                        match xcp_client.get_calibration_object(h).get_a2l_type().encoding {
                            A2lTypeEncoding::Signed => {
                                let o = xcp_client.get_calibration_object(h);
                                print!(" {} {}:{:08X}", o.get_name(), o.get_a2l_addr().ext, o.get_a2l_addr().addr);
                                if xcp_client.is_connected() {
                                    let v = xcp_client.get_value_i64(h);
                                    print!(" ={}", v);
                                }
                            }
                            A2lTypeEncoding::Unsigned => {
                                let o = xcp_client.get_calibration_object(h);
                                print!(" {} {}:{:08X} ", o.get_name(), o.get_a2l_addr().ext, o.get_a2l_addr().addr);
                                if xcp_client.is_connected() {
                                    let v = xcp_client.get_value_u64(h);
                                    print!(" = {}", v);
                                }
                            }
                            A2lTypeEncoding::Float => {
                                let o = xcp_client.get_calibration_object(h);
                                print!(" {} {}:{:08X}", o.get_name(), o.get_a2l_addr().ext, o.get_a2l_addr().addr);
                                if xcp_client.is_connected() {
                                    let v = xcp_client.get_value_f64(h);
                                    print!(" = {}", v);
                                }
                            }
                            A2lTypeEncoding::Blob => {
                                print!(" {} = [...]", name);
                            }
                        }
                    }
                    println!();
                }
                println!();
            } else {
                println!(" None");
            }
        }

        // Upload or Download calibration segment data
        // (Note: In XCP, Upload means from target to file, Download means from file to target)
        if upload_bin || download_bin {
            let path = if bin_filename.is_empty() {
                // No filename given, build HEX file path from ECU name
                std::path::Path::new(&ecu_name).with_extension("hex")
            } else {
                std::path::Path::new(&bin_filename).to_path_buf()
            };
            if !xcp_client.is_connected() {
                return Err("XCP client not connected, cannot upload or download calibration segment data".into());
            }
            if upload_bin {
                // The context list of the target is the enum conversion table conv.contexts in the A2L file
                let contexts = std::fs::read_to_string(&a2l_path)
                    .ok()
                    .and_then(|a2l_text| bin_reader::parse_a2l_context_table(&a2l_text))
                    .unwrap_or_default();
                if !contexts.is_empty() {
                    info!("{} contexts found in A2L file {}", contexts.len(), a2l_path.display());
                }
                xcp_client.save_calibration_segments_to_file(&path, &contexts).await?;
            } else {
                xcp_client.load_calibration_segments_from_file(&path).await?;
            }
        }

        // Set calibration variable
        if xcp_client.is_connected() && !cal_args.is_empty() {
            if cal_args.len() != 2 {
                return Err("Calibration command requires exactly 2 arguments: variable name and value".into());
            }

            // Parse the value as a double
            let var_name = &cal_args[0];
            let value_str = &cal_args[1];
            let value: f64 = value_str.parse().map_err(|_| format!("Failed to parse '{}' as a double value", value_str))?;
            info!("Setting calibration variable '{}' to {}", var_name, value);

            // Create calibration object
            let handle = xcp_client
                .create_calibration_object(var_name)
                .await
                .map_err(|e| format!("Failed to create calibration object for '{}': {}", var_name, e))?;

            // Set the value
            xcp_client
                .set_value_f64(handle, value)
                .await
                .map_err(|e| format!("Failed to set value for '{}': {}", var_name, e))?;

            info!("Successfully set '{}' = {}", var_name, value);
            println!("Ok");
        }

        // Print all known measurement objects
        if !list_mea.is_empty() {
            println!();
            let mea_objects = xcp_client.find_measurements(&list_mea);
            info!("===============================================================");
            println!("Measurement variables:");
            if !mea_objects.is_empty() {
                for name in &mea_objects {
                    if let Some(h) = xcp_client.create_measurement_object(name) {
                        let o = xcp_client.get_measurement_object(h);
                        println!(" {} {} {}", o.get_name(), o.get_a2l_addr(), o.get_a2l_type());
                    }
                }
                println!();
            } else {
                println!(" None");
            }
        }

        // Measurement
        if xcp_client.is_connected() && !measurement_list.is_empty() {
            // Create list of measurement variable names
            let list = if measurement_list.len() == 1 {
                // Regular expression
                xcp_client.find_measurements(measurement_list[0].as_str())
            } else {
                // Just a list of names given on the command line
                measurement_list
            };
            // Trace signals for --perfetto
            let (list, contexts, trace_signals) = if perfetto_filename.is_empty() {
                (list, Vec::new(), Vec::new())
            } else {
                add_trace_signals(&xcp_client, &trace_auto, list)
            };
            if list.is_empty() {
                warn!("No measurement variables found");
            }
            // Start measurement
            else {
                // Create measurement objects for all names in the list
                // Multi dimensional objects not supported yet
                info!("===============================================================");
                info!("Measurement list:");
                for name in &list {
                    if let Some(o) = xcp_client.create_measurement_object(name) {
                        info!(r#"  {}: {}"#, o.0, name);
                    }
                }

                // Measurement data output
                let writers = create_daq_writers(&csv_filename, &mdf_filename, &mcap_filename, &perfetto_filename, &ecu_name, &contexts, &trace_signals)?;
                if let Some(daq_decoder) = &daq_decoder_handle {
                    daq_decoder.lock().set_writers(writers);
                }

                // Measure
                // 32 bit DAQ timestamp will overflow after 4.2s
                let start_time = tokio::time::Instant::now();
                xcp_client.start_measurement().await?;

                // Periodic statistics reporter — prints event and data rate once per second.
                // Reported at log level Info, independent of --verbose (which only controls per-sample content dumps).
                let stats_decoder = xcp_client.get_daq_decoder();
                let deadline = if measurement_duration_ms > 0 {
                    Some(tokio::time::Instant::now() + std::time::Duration::from_millis(measurement_duration_ms))
                } else {
                    println!("Press Ctrl-C to stop measurement...");
                    None
                };
                let mut ticker = tokio::time::interval(std::time::Duration::from_secs(1));
                ticker.tick().await; // consume the immediate first tick, report rates from t+1s
                let mut last_report = tokio::time::Instant::now();
                let mut last_events = 0usize;
                let mut last_bytes = 0usize;
                let mut last_lost = 0usize;
                loop {
                    tokio::select! {
                        _ = ticker.tick() => {
                            if let Some(ref d) = stats_decoder {
                                let (events, bytes, lost) = {
                                    let g = d.lock();
                                    (g.get_event_count(), g.get_byte_count(), g.get_lost_count())
                                };
                                let now = tokio::time::Instant::now();
                                let dt = now.duration_since(last_report).as_secs_f64();
                                if dt > 0.0 {
                                    if lost > last_lost {
                                        warn!(
                                            "Measurement: {:.0} event/s, {:.3} Mbyte/s, {} packets lost",
                                            (events - last_events) as f64 / dt,
                                            (bytes - last_bytes) as f64 / dt / 1_000_000.0,
                                            lost - last_lost
                                        );
                                    } else {
                                        info!(
                                            "Measurement: {:.0} event/s, {:.3} Mbyte/s",
                                            (events - last_events) as f64 / dt,
                                            (bytes - last_bytes) as f64 / dt / 1_000_000.0
                                        );
                                    }
                                }
                                last_report = now;
                                last_events = events;
                                last_bytes = bytes;
                                last_lost = lost;
                            }
                        }
                        _ = async {
                            match deadline {
                                Some(dl) => tokio::time::sleep_until(dl).await,
                                None => std::future::pending::<()>().await,
                            }
                        } => {
                            break;
                        }
                        _ = tokio::signal::ctrl_c(), if deadline.is_none() => {
                            println!("\nStopping measurement...");
                            break;
                        }
                    }
                }

                xcp_client.stop_measurement().await?;
                let elapsed_time = start_time.elapsed().as_micros();

                // Print statistics from DAQ decoder
                {
                    let daq_decoder = xcp_client.get_daq_decoder();
                    if let Some(daq_decoder) = daq_decoder {
                        let daq_decoder = daq_decoder.lock();
                        let event_count = daq_decoder.get_event_count();
                        let byte_count = daq_decoder.get_byte_count();
                        let lost_count = daq_decoder.get_lost_count();
                        info!(
                            "Measurement done, {} events, {:.0} event/s, {:.3} Mbytes/s, {} packets lost",
                            event_count,
                            event_count as f64 * 1_000_000.0 / elapsed_time as f64,
                            byte_count as f64 / elapsed_time as f64,
                            lost_count
                        );
                        if lost_count > 0 {
                            warn!(
                                "{} of {} DAQ packets lost ({:.3}%), in the transmit queue of the XCP server or in the UDP receive buffer",
                                lost_count,
                                event_count + lost_count,
                                lost_count as f64 * 100.0 / (event_count + lost_count) as f64
                            );
                        }
                    }
                }
            }
        }

        Ok::<(), Box<dyn Error>>(())
    }
    .await;

    // Always disconnect, regardless of whether the logic succeeded or failed
    if xcp_client.is_connected() {
        xcp_client.disconnect().await?;
        info!("XCP Disconnected");
    }

    // Return the result from the main logic (preserving any errors that occurred)
    result
}

//------------------------------------------------------------------------
// Helper function to parse destination address with flexible port handling

fn parse_dest_addr(dest_addr: &str, default_port: u16) -> Result<std::net::SocketAddr, Box<dyn Error>> {
    // Try to parse as a complete socket address first (IP:port)
    if let Ok(addr) = dest_addr.parse::<std::net::SocketAddr>() {
        return Ok(addr);
    }

    // If that fails, try to parse as just an IP address and add the default port
    if let Ok(ip) = dest_addr.parse::<std::net::IpAddr>() {
        return Ok(std::net::SocketAddr::new(ip, default_port));
    }

    // If both fail, return an error
    Err(format!("Invalid destination address: '{}'. Expected format: 'IP' or 'IP:port'", dest_addr).into())
}

//------------------------------------------------------------------------
// Config file support

/// Mirrors all Args fields as Option<T>.
/// Fields present in the TOML file are Some(_); absent fields are None and left unchanged.
#[derive(serde::Deserialize, Default)]
#[serde(default)]
struct ConfigFile {
    log_level: Option<u8>,
    verbose: Option<usize>,
    dest_addr: Option<String>,
    port: Option<u16>,
    bind_addr: Option<String>,
    baud_rate: Option<u32>,
    tcp: Option<bool>,
    udp: Option<bool>,
    sxi: Option<bool>,
    connect_mode: Option<u8>,
    offline: Option<bool>,
    a2l: Option<String>,
    upload_a2l: Option<bool>,
    create_a2l: Option<bool>,
    create_a2l_template: Option<bool>,
    fix_a2l: Option<bool>,
    embed_aml: Option<bool>,
    upload_elf: Option<bool>,
    elf: Option<String>,
    elf_unit_limit: Option<usize>,
    elf_unit_limit_min: Option<usize>,
    elf_var_filter: Option<String>,
    elf_unit_filter: Option<String>,
    bin: Option<String>,
    upload_bin: Option<bool>,
    download_bin: Option<bool>,
    list_mea: Option<String>,
    mea: Option<Vec<String>>,
    default_event: Option<DefaultEvent>,
    time: Option<u64>,
    csv: Option<String>,
    mdf: Option<String>,
    mcap: Option<String>,
    perfetto: Option<String>,
    trace_auto: Option<String>,
    list_cal: Option<String>,
    cal: Option<Vec<String>>,
    elf_skip_no_metadata: Option<bool>,
    test: Option<bool>,
    yes: Option<bool>,
}

/// Apply config file values to args.
/// Only fields that are present in the config file (Some) and were NOT explicitly
/// set on the command line are overwritten.
fn merge_config(matches: &clap::ArgMatches, config: ConfigFile, args: &mut Args) {
    use clap::parser::ValueSource;
    macro_rules! apply {
        ($field:ident) => {
            if let Some(v) = config.$field {
                if matches.value_source(stringify!($field)) != Some(ValueSource::CommandLine) {
                    args.$field = v;
                }
            }
        };
    }
    apply!(log_level);
    apply!(verbose);
    apply!(dest_addr);
    apply!(port);
    apply!(bind_addr);
    apply!(baud_rate);
    apply!(tcp);
    apply!(udp);
    apply!(sxi);
    apply!(connect_mode);
    apply!(offline);
    apply!(a2l);
    apply!(upload_a2l);
    apply!(create_a2l);
    apply!(create_a2l_template);
    apply!(fix_a2l);
    apply!(embed_aml);
    apply!(upload_elf);
    apply!(elf);
    apply!(elf_unit_limit);
    apply!(elf_unit_limit_min);
    apply!(elf_var_filter);
    apply!(elf_unit_filter);
    apply!(bin);
    apply!(upload_bin);
    apply!(download_bin);
    apply!(list_mea);
    apply!(mea);
    // default_event is an Option<DefaultEvent> argument, a value from the config file wraps into Some
    if let Some(v) = config.default_event
        && matches.value_source("default_event") != Some(ValueSource::CommandLine)
    {
        args.default_event = Some(v);
    }
    apply!(time);
    apply!(csv);
    apply!(mdf);
    apply!(mcap);
    apply!(perfetto);
    apply!(trace_auto);
    apply!(list_cal);
    apply!(cal);
    apply!(elf_skip_no_metadata);
    apply!(test);
    apply!(yes);
}

//------------------------------------------------------------------------
// Main function

// Exit status is 1 on any error, so scripts can detect failures
#[tokio::main]
async fn main() -> ExitCode {
    use clap::{CommandFactory, FromArgMatches};

    // Parse command line arguments
    let mut matches = Args::command().get_matches();
    let mut args = Args::from_arg_matches(&mut matches).unwrap();

    // Load and merge config file if --config was specified
    // Logging is not initialized yet, the log level may come from the config file
    if !args.config.is_empty() {
        match Figment::new().merge(Toml::file(&args.config)).extract::<ConfigFile>() {
            Ok(config) => merge_config(&matches, config, &mut args),
            Err(e) => {
                eprintln!("Error: Invalid config file '{}': {}", args.config, e);
                return ExitCode::FAILURE;
            }
        }
    }

    // Initialize logging
    let log_level = args.log_level.to_log_level_filter();
    env_logger::Builder::new()
        .target(env_logger::Target::Stdout)
        .filter_level(log_level)
        .format_timestamp(None)
        .format_module_path(false)
        .format_target(false)
        .init();

    info!(
        "xcp_client - version {}.{}.{}",
        env!("CARGO_PKG_VERSION_MAJOR"),
        env!("CARGO_PKG_VERSION_MINOR"),
        env!("CARGO_PKG_VERSION_PATCH")
    );

    debug!("{:#?}", args);

    // Run the XCP client or the test executor
    match run(args).await {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            error!("{}", e);
            ExitCode::FAILURE
        }
    }
}

// Select protocol and addresses, run the XCP client or the test executor
async fn run(args: Args) -> Result<(), Box<dyn Error>> {
    // Protocol, IP addresses, port, baudrate
    let dest_addr: std::net::SocketAddr = parse_dest_addr(&args.dest_addr, args.port)?;
    let local_addr: std::net::SocketAddr = parse_dest_addr(&args.bind_addr, 0)?;
    let mut baud_rate = args.baud_rate;
    let protocol = if args.tcp {
        "TCP"
    } else if args.udp {
        "UDP"
    } else if args.sxi {
        if baud_rate == 0 {
            baud_rate = 115200;
        }
        "SxI"
    } else {
        warn!("No protocol specified, defaulting to TCP");
        "TCP"
    };

    // Run the test executor if --test is specified
    if args.test {
        const TEST_CAL: xcp_test_executor::TestModeCal = xcp_test_executor::TestModeCal::Cal; // Execute calibration tests: Cal or None
        const TEST_DAQ: xcp_test_executor::TestModeDaq = xcp_test_executor::TestModeDaq::DaqCmd; // Execute measurement tests: DaqCmd, Daq or None
        if !test_executor(protocol, dest_addr, local_addr, TEST_CAL, TEST_DAQ).await {
            return Err("XCP test failed".into());
        }
    }
    // Run the XCP client
    else {
        xcp_client(args, protocol, dest_addr, local_addr, baud_rate)
            .await
            .map_err(|e| format!("XCP client error: {}", e))?;
    }

    Ok(())
}

//------------------------------------------------------------------------
// Tests

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_event_parse() {
        assert_eq!("3".parse::<DefaultEvent>(), Ok(DefaultEvent::Id(3)));
        assert_eq!(" 65535 ".parse::<DefaultEvent>(), Ok(DefaultEvent::Id(65535)));
        assert_eq!("mainloop".parse::<DefaultEvent>(), Ok(DefaultEvent::Name("mainloop".into())));
        assert_eq!("_task_1".parse::<DefaultEvent>(), Ok(DefaultEvent::Name("_task_1".into())));
        assert!("65536".parse::<DefaultEvent>().is_err()); // not a u16, not an identifier
        assert!("-1".parse::<DefaultEvent>().is_err());
        assert!("1abc".parse::<DefaultEvent>().is_err());
        assert!("main loop".parse::<DefaultEvent>().is_err());
        assert!("main-loop".parse::<DefaultEvent>().is_err());
        assert!("".parse::<DefaultEvent>().is_err());
    }

    #[test]
    fn test_default_event_resolve() {
        let mut reg = xcp_registry::Registry::new();
        reg.event_list.add_event(McEvent::new("mainloop", 0, 7, 0)).unwrap();
        reg.event_list.add_event(McEvent::new("task", 1, 8, 0)).unwrap();
        reg.event_list.add_event(McEvent::new("task", 2, 9, 0)).unwrap();

        assert_eq!(DefaultEvent::Id(42).resolve(&reg), Ok(42)); // ids are not checked here
        assert_eq!(DefaultEvent::Name("mainloop".into()).resolve(&reg), Ok(7));
        assert_eq!(DefaultEvent::Name("task_2".into()).resolve(&reg), Ok(9)); // multi instance event by unique name
        assert!(DefaultEvent::Name("task".into()).resolve(&reg).is_err()); // ambiguous, only exists with index > 0
        assert!(DefaultEvent::Name("unknown".into()).resolve(&reg).is_err());
    }

    #[test]
    fn test_default_event_config_file() {
        #[derive(serde::Deserialize, Default)]
        #[serde(default)]
        struct Cfg {
            default_event: Option<DefaultEvent>,
        }
        let parse = |text: &str| Figment::new().merge(Toml::string(text)).extract::<Cfg>().map_err(|e| e.to_string());
        assert_eq!(parse("default_event = 3").unwrap().default_event, Some(DefaultEvent::Id(3)));
        assert_eq!(parse("default_event = \"3\"").unwrap().default_event, Some(DefaultEvent::Id(3)));
        assert_eq!(parse("default_event = \"mainloop\"").unwrap().default_event, Some(DefaultEvent::Name("mainloop".into())));
        assert!(parse("default_event = \"main loop\"").is_err());
        assert_eq!(parse("").unwrap().default_event, None);
    }
}
