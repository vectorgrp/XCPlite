//-----------------------------------------------------------------------------
// xcpclient - XCP test tool
//
// - Connect to XCP on Ethernet servers via TCP or UDP
// - Upload A2L files from XCP servers (GET_ID command)
// - Create complete A2L files from ELF debug information the XCP server event and memory segment information
// - Create A2L templates from the XCP server event and memory segment information
// - Read and write calibration variables (CAL)
// - Configure and acquire measurement data (DAQ)
// - List available variables and parameters with regex patterns
// - Execute test sequences
//
// xcpclient --help
//-----------------------------------------------------------------------------

use std::net::Ipv4Addr;
use std::{error::Error, sync::Arc};

use parking_lot::Mutex;

mod xcp_client;
use xcp_client::*;

mod xcp_test_executor;

use xcp_lite::registry::McEvent;
use xcp_test_executor::test_executor;

pub mod elf_reader;
use elf_reader::ElfReader;

pub mod bin_reader;

//-----------------------------------------------------------------------------
// Command line arguments

use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "xcpclient")]
#[command(about = concat!("XCP client v", env!("CARGO_PKG_VERSION"), " for testing XCP servers and managing A2L and HEX files"))]
#[command(long_about = concat!("XCP client v", env!("CARGO_PKG_VERSION"), " for testing XCP servers and managing A2L and HEX files.

This tool can:
- Connect to XCP on Ethernet servers via TCP or UDP and show information about the XCP protocol and the target ECU
- Upload A2L files from XCP servers (GET_ID command)
- Create A2L files from ELF/DWARF debug information including event and memory segment information obtained from the XCP server
- Create A2L file templates for from a XCPlite ELF/DWARF
- Fix A2L files with event and memory segment information from the XCP server
- Read and write calibration variables (CAL)
- Upload (from target) and download (to target) binary files (Intel-HEX) with calibration segment data
- List available measurement variables and parameters with regex patterns
- Test data acquisition (DAQ)
- Execute test sequences

Examples:
  xcpclient --tcp --dest-addr 192.168.1.100 --port 5555 --upload-a2l
  xcpclient --list-mea \"sensor.*\" --list-cal \"param.*\"
  xcpclient --cal variable_name 42.5
  xcpclient --mea \".*temperature.*\" --time 10
  xcpclient --elf myprogram.elf --create-a2l
  xcpclient --download-bin --bin test.hex
  xcpclient --upload-bin --bin target_data.hex
  xcpclient --elf myprogram.elf --create-a2l --offline --a2l my_a2l_file.a2l
  xcpclient --test"))]
#[command(version)]
struct Args {
    // -l --log-level
    /// Log level (Off=0, Error=1, Warn=2, Info=3, Debug=4, Trace=5)
    #[arg(long, default_value_t = 3)]
    log_level: u8,

    // -v --verbose
    /// Verbose output
    /// Enables additional output when reading ELF files and creating A2L files.
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

    // --tcp
    /// Use TCP for XCP communication..
    #[arg(long, default_value_t = false)]
    tcp: bool,
    // --udp
    /// Use UDP for XCP communication
    #[arg(long, default_value_t = false)]
    udp: bool,

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
    /// Insert all visible measurement and calibration variables from ELF file if specified with --elf.
    #[arg(long, default_value_t = false)]
    create_a2l: bool,

    // --fix-a2l
    /// Update the given A2L file with XCP server information about events and memory segments.
    /// Requires that the XCP server supports the GET_EVENT_INFO and GET_SEGMENT_INFO commands.
    #[arg(long, default_value_t = false)]
    fix_a2l: bool,

        // --upload-elf
    /// Upload ELF file from XCP server.
    /// Requires that the XCP server supports proprietary GET_ID ELF upload command.
    #[arg(long, default_value_t = false)]
    upload_elf: bool,

    // ---elf
    /// Specify the name of an ELF file, create an A2L file from ELF debug information.
    /// If connected to a XCP server, events and memory segments will be extracted from the XCP server.
    #[arg(long, default_value = "")]
    elf: String,

    // --elf-unit-limit
    /// Parse only compilations units <= n.
    #[arg(long, default_value_t = usize::MAX)]
    elf_unit_limit: usize,

    // --bin
    /// Specify the pathname of a binary file (Intel-HEX) for calibration parameter segment data
    #[arg(long, default_value = "")]
    bin: String,

    // --upload-bin
    /// Upload all calibration segments working page data from target and store into a binary file.
    /// Requires that the XCP server supports GET_ID A2L upload.
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

    // --time
    /// Time limit measurement duration to n s. 0 means infinite.
    #[arg(long, default_value_t = 0)]
    time: u64,

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
    #[arg(long, default_value_t = false)]
    test: bool,
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

//-----------------------------------------------------------------------------
// Test (--test) settings

const TEST_CAL: xcp_test_executor::TestModeCal = xcp_test_executor::TestModeCal::Cal; // Execute calibration tests: Cal or None
const TEST_DAQ: xcp_test_executor::TestModeDaq = xcp_test_executor::TestModeDaq::Daq; // Execute measurement tests: Daq or None
const TEST_DURATION_MS: u64 = 5000;

//------------------------------------------------------------------------
// Handle incoming DAQ data
// Prints the decoded data to the console

const MAX_EVENT: usize = 64;

#[derive(Debug)]
struct DaqDecoder {
    daq_odt_entries: Option<Vec<Vec<OdtEntry>>>,
    timestamp_resolution: u64,
    daq_header_size: u8,
    event_count: usize,
    byte_count: usize,
    daq_timestamp: [u64; MAX_EVENT],
    verbose: usize,
}

impl DaqDecoder {
    pub fn new(verbose: usize) -> DaqDecoder {
        DaqDecoder {
            daq_odt_entries: None,
            timestamp_resolution: 0,
            daq_header_size: 0,
            event_count: 0,
            byte_count: 0,
            daq_timestamp: [0; MAX_EVENT],
            verbose,
        }
    }
}

impl XcpDaqDecoder for DaqDecoder {
    // Set start time and init
    fn start(&mut self, daq_odt_entries: Vec<Vec<OdtEntry>>, timestamp: u64) {
        // Init
        self.daq_odt_entries = Some(daq_odt_entries);
        self.event_count = 0;
        self.byte_count = 0;
        for t in self.daq_timestamp.iter_mut() {
            *t = timestamp;
        }
    }

    fn stop(&mut self) {}

    // Set timestamp resolution
    fn set_daq_properties(&mut self, timestamp_resolution: u64, daq_header_size: u8) {
        self.daq_header_size = daq_header_size;
        self.timestamp_resolution = timestamp_resolution;
    }

    // Decode DAQ data
    fn decode(&mut self, lost: u32, buf: &[u8]) {
        let daq: u16;
        let odt: u8;
        let mut timestamp_raw: u32 = 0;
        let data: &[u8];

        // Decode header and raw timestamp
        if self.daq_header_size == 4 {
            daq = (buf[2] as u16) | ((buf[3] as u16) << 8);
            odt = buf[0];
            if odt == 0 {
                timestamp_raw = (buf[4] as u32) | ((buf[4 + 1] as u32) << 8) | ((buf[4 + 2] as u32) << 16) | ((buf[4 + 3] as u32) << 24);
                data = &buf[8..];
            } else {
                data = &buf[4..];
            }
        } else {
            daq = buf[1] as u16;
            odt = buf[0];
            if odt == 0 {
                timestamp_raw = (buf[2] as u32) | ((buf[2 + 1] as u32) << 8) | ((buf[2 + 2] as u32) << 16) | ((buf[2 + 3] as u32) << 24);
                data = &buf[6..];
            } else {
                data = &buf[2..];
            }
        }

        assert!(daq < MAX_EVENT as u16);
        assert!(odt == 0);

        // Decode full 64 bit daq timestamp
        let t_last = self.daq_timestamp[daq as usize];
        let t: u64 = if odt == 0 {
            let tl = (t_last & 0xFFFFFFFF) as u32;
            let mut th = (t_last >> 32) as u32;
            if timestamp_raw < tl {
                th += 1;
            }
            let t = (timestamp_raw as u64) | ((th as u64) << 32);
            if t < t_last {
                warn!("Timestamp of daq {} declining {} -> {}", daq, t_last, t);
            }
            self.daq_timestamp[daq as usize] = t;
            t
        } else {
            t_last
        };

        if self.verbose >= 1 {
            println!("DAQ: lost={}, daq={}, odt={}, t={}ns (+{}us)", lost, daq, odt, t, (t - t_last) / 1000);

            if self.verbose >= 2 {
                // Get daq list
                let daq_list = &self.daq_odt_entries.as_ref().unwrap()[daq as usize];

                // Decode all odt entries
                for odt_entry in daq_list.iter() {
                    let value_size = odt_entry.a2l_type.size;
                    let mut value_offset = odt_entry.offset as usize + value_size - 1;
                    let mut value: u64 = 0;
                    loop {
                        value |= data[value_offset] as u64;
                        if value_offset == odt_entry.offset as usize {
                            break;
                        };
                        value <<= 8;
                        value_offset -= 1;
                    }
                    match odt_entry.a2l_type.encoding {
                        A2lTypeEncoding::Signed => {
                            match value_size {
                                1 => {
                                    let signed_value: i8 = value as u8 as i8;
                                    println!(" {} = {}", odt_entry.name, signed_value);
                                }
                                2 => {
                                    let signed_value: i16 = value as u16 as i16;
                                    println!(" {} = {}", odt_entry.name, signed_value);
                                }
                                4 => {
                                    let signed_value: i32 = value as u32 as i32;
                                    println!(" {} = {}", odt_entry.name, signed_value);
                                }
                                8 => {
                                    let signed_value: i64 = value as i64;
                                    println!(" {} = {}", odt_entry.name, signed_value);
                                }
                                _ => {
                                    warn!("Unsupported signed value size {}", value_size);
                                }
                            };
                        }
                        A2lTypeEncoding::Unsigned => {
                            println!(" {} = {}", odt_entry.name, value);
                        }
                        A2lTypeEncoding::Float => {
                            if odt_entry.a2l_type.size == 4 {
                                // #[allow(clippy::transmute_int_to_float)]
                                // let value: f32 = unsafe { std::mem::transmute(value as u32) };
                                let value: f32 = f32::from_bits(value as u32);

                                println!(" {} = {}", odt_entry.name, value);
                            } else {
                                // #[allow(clippy::transmute_int_to_float)]
                                // let value: f64 = unsafe { std::mem::transmute(value) };
                                let value: f64 = f64::from_bits(value);
                                println!(" {} = {}", odt_entry.name, value);
                            }
                        }
                        A2lTypeEncoding::Blob => {
                            panic!("Blob not supported");
                        }
                    }
                }
            }
        }

        self.byte_count += data.len(); // overall payload byte count
        self.event_count += 1; // overall event count
    }

    fn get_byte_count(&self) -> usize {
        self.byte_count
    }

    fn get_event_count(&self) -> usize {
        self.event_count
    }
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

async fn xcp_client(
    verbose: usize,
    tcp: bool,
    udp: bool,
    dest_addr: std::net::SocketAddr,
    local_addr: std::net::SocketAddr,
    connect_mode: u8,
    offline: bool,
    a2l_filename: String,
    upload_a2l: bool,
    create_a2l: bool,
    fix_a2l: bool,
    elf_filename: String,
    upload_elf: bool,
    elf_idx_unit_limit: usize,
    bin_filename: String,
    upload_bin: bool,
    download_bin: bool,
    list_cal: String,
    list_mea: String,
    measurement_list: Vec<String>,
    measurement_duration_ms: u64,
    cal_args: Vec<String>,
) -> Result<(), Box<dyn Error>> {
    // Create xcp_client
    let mut xcp_client = XcpClient::new(tcp, dest_addr, local_addr);

    // Target ECU name (from GET_ID)
    let mut ecu_name = String::new();

    // A2L name (from GET_ID)
    let mut a2l_name = String::new();

    // Calibration segment relative addressing mode
    let mut segment_relative = false;

    // Execute main logic and capture the result
    let result = async {
        //----------------------------------------------------------------
        // Connect the XCP server if required
        let go_online = !offline && (tcp || udp || upload_a2l || upload_elf || !measurement_list.is_empty() || !cal_args.is_empty());
        if go_online {
            // Connect to the XCP server
            // Print protocol information
            info!("XCP Connect using {}", if tcp { "TCP" } else { "UDP" });
            let daq_decoder = Arc::new(Mutex::new(DaqDecoder::new(verbose))); // print DAQ data if verbose > 0
            match xcp_client.connect(connect_mode, Arc::clone(&daq_decoder), ServTextDecoder::new()).await {
                Ok(_) => {
                    info!("Connected to XCP server at {}", dest_addr);
                }
                Err(e) => {
                    error!("Failed to connect to XCP server at {}, Error: {}", dest_addr, e);
                    return Err("Failed to connect to XCP server".into());
                }
            }
            info!("XCP Protocol Information:");
            info!("  XCP MAX_CTO = {}", xcp_client.max_cto_size);
            info!("  XCP MAX_DTO = {}", xcp_client.max_dto_size);
            info!(
                "  XCP RESOURCES = 0x{:02X} {} {} {} {}",
                xcp_client.resources,
                if (xcp_client.resources & 0x01) != 0 { "CAL" } else { "" },
                if (xcp_client.resources & 0x04) != 0 { "DAQ" } else { "" },
                if (xcp_client.resources & 0x10) != 0 { "PGM" } else { "" },
                if (xcp_client.resources & 0x40) != 0 { "STM" } else { "" }
            );
            info!("  XCP COMM_MODE_BASIC = 0x{:02X}", xcp_client.comm_mode_basic);
            assert!((xcp_client.comm_mode_basic & 0x07) == 0); // Address granularity != 1 and motorola format not supported
            info!("  XCP PROTOCOL_VERSION = 0x{:04X}", xcp_client.protocol_version);
            info!("  XCP TRANSPORT_LAYER_VERSION = 0x{:04X}", xcp_client.transport_layer_version);
            info!("  XCP DRIVER_VERSION = 0x{:02X}", xcp_client.driver_version);
            info!("  XCP MAX_SEGMENTS = {}", xcp_client.max_segments);
            info!("  XCP FREEZE_SUPPORTED = {}", xcp_client.freeze_supported);
            info!("  XCP MAX_EVENTS = {}", xcp_client.max_events);

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
            let _ecu_epk = match res {
                Ok((_, Some(id))) => {
                    info!("  GET_ID IDT_EPK = {}", id);
                    id
                }
                Err(e) => {
                    warn!("GET_ID IDT_ASAM_EPK failed, Error: {}", e);
                    "".into()
                }
                _ => {
                    panic!("Empty string");
                }
            };
        } // go online

        //----------------------------------------------------------------
        // Create a new empty A2L registry
        let mut reg = xcp_lite::registry::Registry::new();
        if !ecu_name.is_empty() {
            reg.application.set_info(ecu_name.clone(), "-", 0);
        }
        reg.set_flatten_typedefs_mode(false);
        reg.set_prefix_names_mode(false);

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
        warn!("A2L path: {}", a2l_path.display());


        //----------------------------------------------------------------
        // Upload A2L
        // From XCP server and load it into the xcp_client registry
        if upload_a2l {
            // Don't overwrite existing A2L file generated by a XCP demo running in the same directory
            if a2l_path.exists() && a2l_asam_name && a2l_path == std::path::Path::new(&a2l_name).with_extension("a2l") {
                // Generate a different file name for the uploaded A2L file
                let new_a2l_path = a2l_path
                    .with_file_name(format!("{}_upload", a2l_path.file_stem().unwrap().to_str().unwrap()))
                    .with_extension(".a2l");
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
        //----------------------------------------------------------------
        // Create A2L file
        // If an ELF file is specified create an A2L file from the XCP server information and the ELF file
        // If option create-a2l is specified and no ELF file, create a A2L template from XCP server information
        // Read segment and event information obtained from the XCP server into registry
        // Add measurement and calibration variables from ELF file if specified
        // Addressing scheme may be XCPLITE__ACSDD or XCPLITE__CASDD depending on the target configuration
        else if create_a2l || !elf_filename.is_empty() {
            let mode = if xcp_client.is_connected() {
                if !elf_filename.is_empty() {
                    "target XCP event/segment and ELF/DWARF variable and type information, online mode"
                } else {
                    "target XCP event/segment information only, online mode"
                }
            } else {
                "ELF/DWARF information only,  offline mode"
            };
            info!("Generate A2L file {} with {} ", a2l_path.display(), mode);

            // Set registry XCP default transport layer informations for A2L file
            let protocol = if tcp { "TCP" } else { "UDP" };
            let addr = dest_addr.ip();
            let ipv4_addr = match addr {
                std::net::IpAddr::V4(v4) => v4,
                std::net::IpAddr::V6(_) => Ipv4Addr::new(127, 0, 0, 1),
            };
            let port: u16 = dest_addr.port();
            reg.set_xcp_params(protocol, ipv4_addr, port);

            // If there is an ECU online, get event and segment information via XCP
            if xcp_client.is_connected() {
                xcp_client.get_event_segment_info(&mut reg).await?;
            }

            // Read binary file if specified and create calibration variables in segments and all global measurement variables
            // Events and calibration segments found in the ELF file, must match the XCP server information if present
            // If not, they are created, but with dummy event id and segment number, which has to be fixed later !!!
            if !elf_filename.is_empty() {
                info!("Reading ELF file: {}", elf_filename);

                // Upload ELF file
                if upload_elf {
                    let res = xcp_client.upload_elf_file(&elf_filename).await;
                    if let Err(e) = res {
                        error!("ELF upload failed, Error: {}", e);
                        return Err("ELF upload failed".into());
                    }
                }

                // Read ELF file and DWARF debug information, compilation unit number may be limited to reduce processing time and memory needed
                let elf_reader = ElfReader::new(&elf_filename, verbose, elf_idx_unit_limit).ok_or(format!("Failed to read ELF file '{}'", elf_filename))?;
                if verbose > 0 {
                    elf_reader.debug_data.print_debug_info(verbose, elf_idx_unit_limit); // print only variables <= compilation unit 0
                }

                // Detect addressing scheme for calibration segments
                // Get target signature (CASDD, ACSDD, ...)from ELF file if available
                // true - addr_ext==0 is segment relative addressing, addr_ext==1 is absolute addressing
                // false - addr_ext==0 is absolute addressing, addr_ext==1 is segment relative addressing
                segment_relative = {
                    let signature = elf_reader.get_target_signature();
                    if let Some(sig) = signature {
                        info!("Target signature: {}", sig);
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

                // Register segments and event creations found in the code
                elf_reader.register_segments_and_events(&mut reg, segment_relative, verbose)?;
                // Find event triggers in the code and register their location (compilation unit, function, CFA offset)
                elf_reader.register_event_locations(&mut reg, verbose)?;
                // Register accessible variables and their types
                elf_reader.register_variables(&mut reg, segment_relative, verbose, elf_idx_unit_limit)?; // register only variables <= compilation unit 0
            }

            // Write the registry to A2L file
            if !a2l_path.as_os_str().is_empty() {
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
        }
        //----------------------------------------------------------------
        // Load A2L from local file
        // If not upload or create option load  A2L from specified file into reg
        // If fix-a2l option is specified, check and correct the A2L file with the XCP server information otherwise just warn about differences
        // Consider command line option --epk-segment
        else {
            info!("Load A2L file: {}", a2l_path.display());
            xcp_client
                .load_a2l_file_into_registry(&a2l_path, &mut reg)
                .map_err(|e| format!("Could not load A2L file '{}'", a2l_path.display()))?;

            let mut event_mapping: std::collections::HashMap<u16, u16> = std::collections::HashMap::new();
            let mut seg_mapping: std::collections::HashMap<u16, u16> = std::collections::HashMap::new();
            let mut missing_event_ids = 0;

            // Load the event and calibration segment information from target into a temporary registry and check if the given A2L file needs to be corrected
            let mut tmp_reg = xcp_lite::registry::Registry::new();
            xcp_client.get_event_segment_info(&mut tmp_reg).await?;

            // Check events
            if xcp_client.max_events == 0 {
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
            if xcp_client.max_segments == 0 {
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

            // Fix the registry
            // In the event list and in the address information of all instance in event relative addressing mode
            if fix_a2l {
                if !event_mapping.is_empty() {
                    info!("Event mapping information:");
                    for (k, v) in &event_mapping {
                        info!("  {} -> {}", k, v);
                    }
                    reg.update_event_mapping(&event_mapping);
                }
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

                // Fix the calibration segment list and the address information of all calibration objects in calibration segment relative addressing mode
                // Update the registry
                if !seg_mapping.is_empty() {
                    info!("Calibration segment mapping information:");
                    for (k, v) in &seg_mapping {
                        info!("  {} -> {}", k, v);
                        reg.update_cal_seg_mapping(&seg_mapping);
                    }
                }
            } else if !event_mapping.is_empty() || !seg_mapping.is_empty() {
                warn!("A2L file {} differs from target, use automatic correction (--fix_a2l)", a2l_path.display());
                info!("events: {:?}", event_mapping);
                info!("segments: {:?}", seg_mapping);
            }
        } // load  A2L from specified file

        // Assign the new registry to xcp_client
        xcp_client.registry = Some(reg);

        // Check the status of all calibration segments and goto working page
        if xcp_client.is_connected() {
            xcp_client.init_calibration_segments().await?;
        }

        // Print all known calibration objects and get their current value
        if !list_cal.is_empty() {
            println!();
            let cal_objects = xcp_client.find_characteristics(list_cal.as_str());
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
                xcp_client.save_calibration_segments_to_file(&path).await?;
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
            if list.is_empty() {
                warn!("No measurement variables found");
            }
            // Start measurement
            else {
                // Create measurement objects for all names in the list
                // Multi dimensional objects not supported yet
                info!("Measurement list:");
                for name in &list {
                    if let Some(o) = xcp_client.create_measurement_object(name) {
                        info!(r#"  {}: {}"#, o.0, name);
                    }
                }

                // Measure
                // 32 bit DAQ timestamp will overflow after 4.2s
                let start_time = tokio::time::Instant::now();
                xcp_client.start_measurement().await?;

                if measurement_duration_ms > 0 {
                    tokio::time::sleep(std::time::Duration::from_millis(measurement_duration_ms)).await;
                } else {
                    println!("Press Ctrl-C to stop measurement...");
                    // Wait for Ctrl-C signal
                    let _ = tokio::signal::ctrl_c().await;
                    println!("\nStopping measurement...");
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
                        info!(
                            "Measurement done, {} events, {:.0} event/s, {:.3} Mbytes/s",
                            event_count,
                            event_count as f64 * 1_000_000.0 / elapsed_time as f64,
                            byte_count as f64 / elapsed_time as f64
                        );
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
// Main function

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    info!("xcp_client");

    // Parse command line arguments
    let args = Args::parse();

    // Initialize logging
    let log_level = args.log_level.to_log_level_filter();
    env_logger::Builder::new()
        .target(env_logger::Target::Stdout)
        .filter_level(log_level)
        .format_timestamp(None)
        .format_module_path(false)
        .format_target(false)
        .init();

    // Parse IP addresses and ports
    let mut tcp = args.tcp;
    let udp = args.udp;
    let dest_addr: std::net::SocketAddr = parse_dest_addr(&args.dest_addr, args.port)?;
    let local_addr: std::net::SocketAddr = parse_dest_addr(&args.bind_addr, 0)?;
    if args.offline {
        info!("XCP client offline mode");
    } else {
        info!("XCP server dest addr: {}", dest_addr);
        info!("XCP client local bind addr: {}", local_addr);
        if !tcp && !udp {
            warn!("No protocol specified, defaulting to TCP");
            tcp = true;
        }
    }

    // Run the test executor if --test is specified
    if args.test {
        test_executor(args.tcp, dest_addr, local_addr, TEST_CAL, TEST_DAQ, TEST_DURATION_MS).await
    }
    // Run the XCP client
    else {
        let res = xcp_client(
            args.verbose,
            tcp,
            udp,
            dest_addr,
            local_addr,
            args.connect_mode,
            args.offline,
            args.a2l,
            args.upload_a2l,
            args.create_a2l,
            args.fix_a2l,
            args.elf,
            args.upload_elf,
            args.elf_unit_limit,
            args.bin,
            args.upload_bin,
            args.download_bin,
            args.list_cal,
            args.list_mea,
            args.mea,
            args.time * 1000,
            args.cal,
        )
        .await;
        if let Err(e) = res {
            error!("XCP client error: {}", e);
        }
    }

    Ok(())
}
