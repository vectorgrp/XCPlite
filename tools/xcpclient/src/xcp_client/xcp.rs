#[allow(unused_imports)]
use log::{debug, error, info, trace, warn};

use bytes::{BufMut, BytesMut};

//--------------------------------------------------------------------------------------------------------------------------------------------------

// XCP command response codes
pub const CRC_CMD_OK: u8 = 0x00;
pub const CRC_CMD_SYNCH: u8 = 0x00;
pub const CRC_CMD_PENDING: u8 = 0x01;
pub const CRC_CMD_IGNORED: u8 = 0x02;
pub const CRC_CMD_BUSY: u8 = 0x10;
pub const CRC_DAQ_ACTIVE: u8 = 0x11;
pub const CRC_PGM_ACTIVE: u8 = 0x12;
pub const CRC_CMD_UNKNOWN: u8 = 0x20;
pub const CRC_CMD_SYNTAX: u8 = 0x21;
pub const CRC_OUT_OF_RANGE: u8 = 0x22;
pub const CRC_WRITE_PROTECTED: u8 = 0x23;
pub const CRC_ACCESS_DENIED: u8 = 0x24;
pub const CRC_ACCESS_LOCKED: u8 = 0x25;
pub const CRC_PAGE_NOT_VALID: u8 = 0x26;
pub const CRC_MODE_NOT_VALID: u8 = 0x27;
pub const CRC_SEGMENT_NOT_VALID: u8 = 0x28;
pub const CRC_SEQUENCE: u8 = 0x29;
pub const CRC_DAQ_CONFIG: u8 = 0x2A;
pub const CRC_MEMORY_OVERFLOW: u8 = 0x30;
pub const CRC_GENERIC: u8 = 0x31;
pub const CRC_VERIFY: u8 = 0x32;
pub const CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE: u8 = 0x33;
pub const CRC_SUBCMD_UNKNOWN: u8 = 0x34;
pub const CRC_TIMECORR_STATE_CHANGE: u8 = 0x35;

//--------------------------------------------------------------------------------------------------------------------------------------------------
// XCP commands

// XCP command codes
pub const CC_CONNECT: u8 = 0xFF;
pub const CC_DISCONNECT: u8 = 0xFE;
pub const CC_SHORT_DOWNLOAD: u8 = 0xED;
pub const CC_SYNC: u8 = 0xFC;
pub const CC_GET_COMM_MODE_INFO: u8 = 0xFB;
pub const CC_GET_ID: u8 = 0xFA;
pub const CC_SET_MTA: u8 = 0xF6;
pub const CC_UPLOAD: u8 = 0xF5;
pub const CC_SHORT_UPLOAD: u8 = 0xF4;
pub const CC_USER: u8 = 0xF1;
pub const CC_DOWNLOAD: u8 = 0xF0;
pub const CC_NOP: u8 = 0xC1;
pub const CC_SET_CAL_PAGE: u8 = 0xEB;
pub const CC_GET_CAL_PAGE: u8 = 0xEA;
pub const CC_GET_PAGE_PROCESSOR_INFO: u8 = 0xE9;
pub const CC_GET_SEGMENT_INFO: u8 = 0xE8;
pub const CC_GET_PAGE_INFO: u8 = 0xE7;
pub const CC_SET_SEGMENT_MODE: u8 = 0xE6;
pub const CC_GET_SEGMENT_MODE: u8 = 0xE5;
pub const CC_COPY_CAL_PAGE: u8 = 0xE4;
pub const CC_CLEAR_DAQ_LIST: u8 = 0xE3;
pub const CC_SET_DAQ_PTR: u8 = 0xE2;
pub const CC_WRITE_DAQ: u8 = 0xE1;
pub const CC_SET_DAQ_LIST_MODE: u8 = 0xE0;
pub const CC_GET_DAQ_LIST_MODE: u8 = 0xDF;
pub const CC_START_STOP_DAQ_LIST: u8 = 0xDE;
pub const CC_START_STOP_SYNCH: u8 = 0xDD;
pub const CC_GET_DAQ_CLOCK: u8 = 0xDC;
pub const CC_READ_DAQ: u8 = 0xDB;
pub const CC_GET_DAQ_PROCESSOR_INFO: u8 = 0xDA;
pub const CC_GET_DAQ_RESOLUTION_INFO: u8 = 0xD9;
pub const CC_GET_DAQ_LIST_INFO: u8 = 0xD8;
pub const CC_GET_DAQ_EVENT_INFO: u8 = 0xD7;
pub const CC_FREE_DAQ: u8 = 0xD6;
pub const CC_ALLOC_DAQ: u8 = 0xD5;
pub const CC_ALLOC_ODT: u8 = 0xD4;
pub const CC_ALLOC_ODT_ENTRY: u8 = 0xD3;
pub const CC_TIME_CORRELATION_PROPERTIES: u8 = 0xC6;
pub const CC_GET_VERSION: u8 = 0xC0;

//--------------------------------------------------------------------------------------------------------------------------------------------------
// XCP protocol definitions

// XCP id types
pub const IDT_ASCII: u8 = 0;
pub const IDT_ASAM_NAME: u8 = 1;
pub const IDT_ASAM_PATH: u8 = 2;
pub const IDT_ASAM_URL: u8 = 3;
pub const IDT_ASAM_UPLOAD: u8 = 4;
pub const IDT_ASAM_EPK: u8 = 5;
pub const IDT_VECTOR_MAPNAMES: u8 = 0xDB;
pub const IDT_VECTOR_GET_A2LOBJECTS_FROM_ECU: u8 = 0xA2;
pub const IDT_VECTOR_ELF_UPLOAD: u8 = 0xA3;

// XCP get/set calibration page mode
pub const CAL_PAGE_MODE_ECU: u8 = 0x01;
pub const CAL_PAGE_MODE_XCP: u8 = 0x02;

//--------------------------------------------------------------------------------------------------------------------------------------------------
// XCP error type

pub const ERROR_CMD_TIMEOUT: u8 = 0xF0;
pub const ERROR_TL_HEADER: u8 = 0xF1;
pub const ERROR_A2L: u8 = 0xF2;
pub const ERROR_LIMIT: u8 = 0xF3;
pub const ERROR_ODT_SIZE: u8 = 0xF4;
pub const ERROR_TASK_TERMINATED: u8 = 0xF5;
pub const ERROR_SESSION_TERMINATION: u8 = 0xF6;
pub const ERROR_TYPE_MISMATCH: u8 = 0xF7;
pub const ERROR_REGISTRY_EXISTS: u8 = 0xF8;
pub const ERROR_GENERIC: u8 = 0xF9;
pub const ERROR_NOT_FOUND: u8 = 0xFA;

#[derive(Default)]
pub struct XcpError {
    code: u8,
    cmd: u8,
}

impl XcpError {
    pub fn new(code: u8, cmd: u8) -> XcpError {
        XcpError { code, cmd }
    }
    pub fn get_error_code(&self) -> u8 {
        self.code
    }
}

impl std::fmt::Display for XcpError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        let cmd: XcpCommand = From::from(self.cmd);
        match self.code {
            ERROR_CMD_TIMEOUT => {
                write!(f, "{cmd:?}: Command response timeout")
            }
            ERROR_TASK_TERMINATED => {
                write!(f, "Client task terminated")
            }
            ERROR_SESSION_TERMINATION => {
                write!(f, "Session terminated by XCP server")
            }
            ERROR_TL_HEADER => {
                write!(f, "Transport layer header error")
            }
            ERROR_GENERIC => {
                write!(f, "Generic error")
            }
            ERROR_A2L => {
                write!(f, "A2L file error")
            }
            ERROR_REGISTRY_EXISTS => {
                write!(f, "Registry already exists")
            }
            ERROR_LIMIT => {
                write!(f, "Calibration value limit exceeded")
            }
            ERROR_NOT_FOUND => {
                write!(f, "Measurement or calibration variable not found")
            }
            ERROR_ODT_SIZE => {
                write!(f, "ODT max size exceeded")
            }
            CRC_CMD_SYNCH => {
                write!(f, "SYNCH")
            }
            CRC_CMD_PENDING => {
                write!(f, "XCP command PENDING")
            }
            CRC_CMD_IGNORED => {
                write!(f, "{cmd:?}: XCP command IGNORED")
            }
            CRC_CMD_BUSY => {
                write!(f, "{cmd:?}: XCP command BUSY")
            }
            CRC_DAQ_ACTIVE => {
                write!(f, "{cmd:?}: XCP DAQ ACTIVE")
            }
            CRC_PGM_ACTIVE => {
                write!(f, "{cmd:?}: XCP PGM ACTIVE")
            }
            CRC_CMD_UNKNOWN => {
                write!(f, "Unknown XCP command: {cmd:?} ")
            }
            CRC_CMD_SYNTAX => {
                write!(f, "{cmd:?}: XCP command SYNTAX")
            }
            CRC_OUT_OF_RANGE => {
                write!(f, "{cmd:?}: Parameter out of range")
            }
            CRC_WRITE_PROTECTED => {
                write!(f, "{cmd:?}: Write protected")
            }
            CRC_ACCESS_DENIED => {
                write!(f, "{cmd:?}: Access denied")
            }
            CRC_ACCESS_LOCKED => {
                write!(f, "{cmd:?}: Access locked")
            }
            CRC_PAGE_NOT_VALID => {
                write!(f, "{cmd:?}: Invalid page")
            }
            CRC_MODE_NOT_VALID => {
                write!(f, "{cmd:?}: Invalide mode")
            }
            CRC_SEGMENT_NOT_VALID => {
                write!(f, "{cmd:?}: Invalid segment")
            }
            CRC_SEQUENCE => {
                write!(f, "{cmd:?}: Wrong sequence")
            }
            CRC_DAQ_CONFIG => {
                write!(f, "{cmd:?}: DAQ configuration error")
            }
            CRC_MEMORY_OVERFLOW => {
                write!(f, "{cmd:?}: Memory overflow")
            }
            CRC_GENERIC => {
                write!(f, "{cmd:?}: XCP generic error")
            }
            CRC_VERIFY => {
                write!(f, "{cmd:?}: Verify failed")
            }
            CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE => {
                write!(f, "{cmd:?}: Resource temporary not accessible")
            }
            CRC_SUBCMD_UNKNOWN => {
                write!(f, "{cmd:?}: Unknown sub command")
            }
            CRC_TIMECORR_STATE_CHANGE => {
                write!(f, "{cmd:?}: Time correlation state change")
            }
            _ => {
                write!(f, "{cmd:?}: XCP error code = 0x{:0X}", self.code)
            }
        }
    }
}

impl std::fmt::Debug for XcpError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "XcpError 0x{:02X} - {}", self.code, self)
    }
}

impl std::error::Error for XcpError {}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// XCP command enum

#[derive(Debug)]
pub enum XcpCommand {
    Connect = CC_CONNECT as isize,
    Disconnect = CC_DISCONNECT as isize,
    SetMta = CC_SET_MTA as isize,
    ShortUpload = CC_SHORT_UPLOAD as isize,
    Upload = CC_UPLOAD as isize,
    ShortDownload = CC_SHORT_DOWNLOAD as isize,
    Download = CC_DOWNLOAD as isize,
    User = CC_USER as isize,
    Sync = CC_SYNC as isize,
    Nop = CC_NOP as isize,
    GetId = CC_GET_ID as isize,
    SetCalPage = CC_SET_CAL_PAGE as isize,
    GetCalPage = CC_GET_CAL_PAGE as isize,
    GetPageProcessorInfo = CC_GET_PAGE_PROCESSOR_INFO as isize,
    GetSegmentInfo = CC_GET_SEGMENT_INFO as isize,
    GetPageInfo = CC_GET_PAGE_INFO as isize,
    SetSegmentMode = CC_SET_SEGMENT_MODE as isize,
    GetSegmentMode = CC_GET_SEGMENT_MODE as isize,
    CopyCalPage = CC_COPY_CAL_PAGE as isize,
    ClearDaqList = CC_CLEAR_DAQ_LIST as isize,
    SetDaqPtr = CC_SET_DAQ_PTR as isize,
    WriteDaq = CC_WRITE_DAQ as isize,
    SetDaqListMode = CC_SET_DAQ_LIST_MODE as isize,
    GetDaqListMode = CC_GET_DAQ_LIST_MODE as isize,
    StartStopDaqList = CC_START_STOP_DAQ_LIST as isize,
    StartStopSynch = CC_START_STOP_SYNCH as isize,
    GetDaqClock = CC_GET_DAQ_CLOCK as isize,
    ReadDaq = CC_READ_DAQ as isize,
    GetDaqProcessorInfo = CC_GET_DAQ_PROCESSOR_INFO as isize,
    GetDaqResolutionInfo = CC_GET_DAQ_RESOLUTION_INFO as isize,
    GetDaqListInfo = CC_GET_DAQ_LIST_INFO as isize,
    GetDaqEventInfo = CC_GET_DAQ_EVENT_INFO as isize,
    FreeDaq = CC_FREE_DAQ as isize,
    AllocDaq = CC_ALLOC_DAQ as isize,
    AllocOdt = CC_ALLOC_ODT as isize,
    AllocOdtEntry = CC_ALLOC_ODT_ENTRY as isize,
    TimeCorrelationProperties = CC_TIME_CORRELATION_PROPERTIES as isize,
}

impl From<u8> for XcpCommand {
    fn from(code: u8) -> Self {
        match code {
            CC_CONNECT => XcpCommand::Connect,
            CC_DISCONNECT => XcpCommand::Disconnect,
            CC_SET_MTA => XcpCommand::SetMta,
            CC_SHORT_DOWNLOAD => XcpCommand::ShortDownload,
            CC_DOWNLOAD => XcpCommand::Download,
            CC_SHORT_UPLOAD => XcpCommand::ShortUpload,
            CC_UPLOAD => XcpCommand::Upload,
            CC_USER => XcpCommand::User,
            CC_SYNC => XcpCommand::Sync,
            CC_NOP => XcpCommand::Nop,
            CC_GET_ID => XcpCommand::GetId,
            CC_SET_CAL_PAGE => XcpCommand::SetCalPage,
            CC_GET_CAL_PAGE => XcpCommand::GetCalPage,
            CC_GET_PAGE_PROCESSOR_INFO => XcpCommand::GetPageProcessorInfo,
            CC_GET_SEGMENT_INFO => XcpCommand::GetSegmentInfo,
            CC_GET_PAGE_INFO => XcpCommand::GetPageInfo,
            CC_SET_SEGMENT_MODE => XcpCommand::SetSegmentMode,
            CC_GET_SEGMENT_MODE => XcpCommand::GetSegmentMode,
            CC_COPY_CAL_PAGE => XcpCommand::CopyCalPage,
            CC_CLEAR_DAQ_LIST => XcpCommand::ClearDaqList,
            CC_SET_DAQ_PTR => XcpCommand::SetDaqPtr,
            CC_WRITE_DAQ => XcpCommand::WriteDaq,
            CC_SET_DAQ_LIST_MODE => XcpCommand::SetDaqListMode,
            CC_GET_DAQ_LIST_MODE => XcpCommand::GetDaqListMode,
            CC_START_STOP_DAQ_LIST => XcpCommand::StartStopDaqList,
            CC_START_STOP_SYNCH => XcpCommand::StartStopSynch,
            CC_GET_DAQ_CLOCK => XcpCommand::GetDaqClock,
            CC_READ_DAQ => XcpCommand::ReadDaq,
            CC_GET_DAQ_PROCESSOR_INFO => XcpCommand::GetDaqProcessorInfo,
            CC_GET_DAQ_RESOLUTION_INFO => XcpCommand::GetDaqResolutionInfo,
            CC_GET_DAQ_LIST_INFO => XcpCommand::GetDaqListInfo,
            CC_GET_DAQ_EVENT_INFO => XcpCommand::GetDaqEventInfo,
            CC_FREE_DAQ => XcpCommand::FreeDaq,
            CC_ALLOC_DAQ => XcpCommand::AllocDaq,
            CC_ALLOC_ODT => XcpCommand::AllocOdt,
            CC_ALLOC_ODT_ENTRY => XcpCommand::AllocOdtEntry,
            CC_TIME_CORRELATION_PROPERTIES => XcpCommand::TimeCorrelationProperties,
            _ => {
                error!("Unknown command code: 0x{:02X}", code);
                panic!("Unknown command code: 0x{:02X}", code);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// Build XCP commands with transport layer header

pub struct XcpCommandBuilder {
    data: BytesMut,
}

impl XcpCommandBuilder {
    pub fn new(command_code: u8) -> XcpCommandBuilder {
        let mut cmd = XcpCommandBuilder {
            data: BytesMut::with_capacity(12),
        };
        cmd.data.put_u16_le(0);
        cmd.data.put_u16_le(0);
        cmd.data.put_u8(command_code);
        cmd
    }
    pub fn add_u8(&mut self, value: u8) -> &mut Self {
        self.data.put_u8(value);
        self
    }

    pub fn add_u8_slice(&mut self, value: &[u8]) -> &mut Self {
        self.data.put_slice(value);
        self
    }

    pub fn add_u16(&mut self, value: u16) -> &mut Self {
        assert!(self.data.len() & 1 == 0, "add_u16: unaligned");
        self.data.put_u16_le(value);
        self
    }

    pub fn add_u32(&mut self, value: u32) -> &mut Self {
        assert!(self.data.len() & 3 == 0, "add_u32: unaligned");
        self.data.put_u32_le(value);
        self
    }

    pub fn build(&mut self) -> &[u8] {
        let mut len: u16 = self.data.len().try_into().unwrap();
        assert!(len >= 5);
        len -= 4;
        self.data[0] = (len & 0xFFu16) as u8;
        self.data[1] = (len >> 8) as u8;
        self.data.as_ref()
    }
}
