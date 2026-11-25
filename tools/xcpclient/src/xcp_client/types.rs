//--------------------------------------------------------------------------------------------------------------------------------------------------
// types.rs — A2L address/type descriptors and calibration/measurement object wrappers

#![allow(dead_code)]

use xcp_registry::{McSupportData, McValueType};

use super::XcpClient;

//--------------------------------------------------------------------------------------------------------------------------------------------------
// A2L address

#[derive(Debug, Clone, Copy)]
pub struct A2lAddr {
    pub ext: u8,
    pub addr: u32,
    pub event: Option<u16>,
}

impl std::fmt::Display for A2lAddr {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if let Some(event) = self.event {
            write!(f, "{}:0x{:08X} event {}", self.ext, self.addr, event)
        } else {
            write!(f, "{}:0x{:08X}", self.ext, self.addr)
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// A2L type encoding and descriptor

#[derive(Debug, Clone, Copy)]
pub enum A2lTypeEncoding {
    Signed,
    Unsigned,
    Float,
    Blob,
}

impl From<&McValueType> for A2lTypeEncoding {
    fn from(value_type: &McValueType) -> A2lTypeEncoding {
        match value_type {
            McValueType::Bool | McValueType::Ubyte | McValueType::Uword | McValueType::Ulong | McValueType::Ulonglong => A2lTypeEncoding::Unsigned,
            McValueType::Sbyte | McValueType::Sword | McValueType::Slong | McValueType::Slonglong => A2lTypeEncoding::Signed,
            McValueType::Float32Ieee | McValueType::Float64Ieee => A2lTypeEncoding::Float,
            _ => A2lTypeEncoding::Blob,
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct A2lType {
    pub size: usize,
    pub encoding: A2lTypeEncoding,
}

impl std::fmt::Display for A2lType {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self.encoding {
            A2lTypeEncoding::Signed => write!(f, "{} byte signed", self.size),
            A2lTypeEncoding::Unsigned => write!(f, "{} byte unsigned", self.size),
            A2lTypeEncoding::Float => write!(f, "{} byte float", self.size),
            A2lTypeEncoding::Blob => write!(f, "{} byte blob", self.size),
        }
    }
}

impl A2lType {
    /// Decode a raw value from little endian bytes
    /// Returns None for a blob or an unsupported size
    pub fn decode(&self, bytes: &[u8]) -> Option<A2lValue> {
        if bytes.len() < self.size || self.size == 0 || self.size > 8 {
            return None;
        }
        let mut buf = [0u8; 8];
        buf[..self.size].copy_from_slice(&bytes[..self.size]);
        let value = u64::from_le_bytes(buf);
        match (self.encoding, self.size) {
            (A2lTypeEncoding::Unsigned, _) => Some(A2lValue::Unsigned(value)),
            (A2lTypeEncoding::Signed, 1) => Some(A2lValue::Signed(value as u8 as i8 as i64)),
            (A2lTypeEncoding::Signed, 2) => Some(A2lValue::Signed(value as u16 as i16 as i64)),
            (A2lTypeEncoding::Signed, 4) => Some(A2lValue::Signed(value as u32 as i32 as i64)),
            (A2lTypeEncoding::Signed, 8) => Some(A2lValue::Signed(value as i64)),
            // The shortest decimal representation of a f32 is kept ("0.1", not "0.10000000149011612")
            (A2lTypeEncoding::Float, 4) => Some(A2lValue::Float(f32::from_bits(value as u32).to_string().parse().unwrap_or(f64::NAN))),
            (A2lTypeEncoding::Float, 8) => Some(A2lValue::Float(f64::from_bits(value))),
            _ => None,
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// A2L value and conversion from raw to physical values

/// A raw or physical value
/// A physical value is a number with a unit or the label of an enum value
#[derive(Debug, Clone, PartialEq)]
pub enum A2lValue {
    Signed(i64),
    Unsigned(u64),
    Float(f64),
    Label(String),
}

impl A2lValue {
    /// Get the value as a number, None for a label
    pub fn as_f64(&self) -> Option<f64> {
        match self {
            A2lValue::Signed(v) => Some(*v as f64),
            A2lValue::Unsigned(v) => Some(*v as f64),
            A2lValue::Float(v) => Some(*v),
            A2lValue::Label(_) => None,
        }
    }

    /// Get the label of an enum value, None for a number
    pub fn as_label(&self) -> Option<&str> {
        match self {
            A2lValue::Label(s) => Some(s),
            _ => None,
        }
    }
}

impl std::fmt::Display for A2lValue {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            A2lValue::Signed(v) => write!(f, "{}", v),
            A2lValue::Unsigned(v) => write!(f, "{}", v),
            A2lValue::Float(v) => write!(f, "{}", v),
            A2lValue::Label(s) => write!(f, "{}", s),
        }
    }
}

/// Conversion of a raw value to a physical value
///
/// Created from the metadata of a registry instance (McSupportData): factor, offset and unit.
/// The unit of an enum is a list of pairs `<int> "<label>"` separated by spaces (`0 "OFF" 1 "ON"`), it is not a physical unit.
///
/// - Enum: The physical value is the label of the raw value, a raw value without a label is not converted
/// - Linear, when there is a factor or an offset: physical = raw * factor + offset, the result is a float
/// - Otherwise the physical value is the raw value, integers are not converted to float
#[derive(Debug, Clone, Default, PartialEq)]
pub struct A2lConversion {
    factor: Option<f64>,
    offset: Option<f64>,
    unit: String,
    enum_labels: Option<Vec<(i64, String)>>,
}

impl From<&McSupportData> for A2lConversion {
    fn from(mc_support_data: &McSupportData) -> A2lConversion {
        A2lConversion::new(mc_support_data.get_factor(), mc_support_data.get_offset(), mc_support_data.get_unit())
    }
}

impl A2lConversion {
    /// Create a conversion from factor, offset and unit
    /// A unit in enum format makes it an enum conversion, factor and offset are ignored then
    pub fn new(factor: Option<f64>, offset: Option<f64>, unit: &str) -> A2lConversion {
        match Self::parse_enum_unit(unit) {
            Some(enum_labels) => A2lConversion {
                factor: None,
                offset: None,
                unit: String::new(),
                enum_labels: Some(enum_labels),
            },
            None => A2lConversion {
                factor: factor.filter(|f| *f != 1.0),
                offset: offset.filter(|o| *o != 0.0),
                unit: unit.to_string(),
                enum_labels: None,
            },
        }
    }

    /// Parse a unit string in enum format, one or more pairs `<int> "<label>"` separated by spaces
    /// Returns None, if the string is not in enum format
    fn parse_enum_unit(unit: &str) -> Option<Vec<(i64, String)>> {
        let mut rest = unit.trim();
        let mut pairs = Vec::new();
        while !rest.is_empty() {
            let sep = rest.find(' ')?;
            let value = rest[..sep].parse::<i64>().ok()?;
            rest = rest[sep..].trim_start().strip_prefix('"')?;
            let end = rest.find('"')?;
            pairs.push((value, rest[..end].to_string()));
            rest = rest[end + 1..].trim_start();
        }
        if pairs.is_empty() { None } else { Some(pairs) }
    }

    /// Factor of a linear conversion, None if there is none (1.0) or for an enum
    pub fn factor(&self) -> Option<f64> {
        self.factor
    }

    /// Offset of a linear conversion, None if there is none (0.0) or for an enum
    pub fn offset(&self) -> Option<f64> {
        self.offset
    }

    /// Physical unit, empty if there is none or for an enum
    pub fn unit(&self) -> &str {
        &self.unit
    }

    /// True for an enum conversion
    pub fn is_enum(&self) -> bool {
        self.enum_labels.is_some()
    }

    /// Labels of an enum conversion
    pub fn enum_labels(&self) -> Option<&[(i64, String)]> {
        self.enum_labels.as_deref()
    }

    /// Convert a raw value to the physical value
    pub fn convert(&self, raw: A2lValue) -> A2lValue {
        if let Some(enum_labels) = &self.enum_labels {
            let key = match raw {
                A2lValue::Signed(v) => Some(v),
                A2lValue::Unsigned(v) => i64::try_from(v).ok(),
                _ => None,
            };
            return match key.and_then(|k| enum_labels.iter().find(|(v, _)| *v == k)) {
                Some((_, label)) => A2lValue::Label(label.clone()),
                None => raw,
            };
        }
        if self.factor.is_some() || self.offset.is_some() {
            if let Some(v) = raw.as_f64() {
                return A2lValue::Float(v * self.factor.unwrap_or(1.0) + self.offset.unwrap_or(0.0));
            }
        }
        raw
    }
}

#[derive(Debug, Clone, Copy)]
pub struct A2lLimits {
    pub lower: f64,
    pub upper: f64,
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// Calibration object

#[derive(Debug, Clone, Copy)]
pub struct XcpCalibrationObjectHandle(pub(super) usize);

impl XcpCalibrationObjectHandle {
    pub fn get_name(self, xcp_client: &mut XcpClient) -> &str {
        xcp_client.get_calibration_object(self).get_name()
    }
    pub fn get_a2l_addr(self, xcp_client: &mut XcpClient) -> A2lAddr {
        xcp_client.get_calibration_object(self).get_a2l_addr()
    }
    pub fn get_a2l_type(self, xcp_client: &mut XcpClient) -> A2lType {
        xcp_client.get_calibration_object(self).get_a2l_type()
    }
}

#[derive(Debug)]
pub struct XcpClientCalibrationObject {
    pub(super) name: String,
    pub(super) a2l_addr: A2lAddr,
    pub(super) get_type: A2lType,
    pub(super) a2l_limits: A2lLimits,
    pub(super) value: Vec<u8>,
}

impl XcpClientCalibrationObject {
    pub fn new(name: &str, a2l_addr: A2lAddr, get_type: A2lType, a2l_limits: A2lLimits) -> XcpClientCalibrationObject {
        XcpClientCalibrationObject {
            name: name.to_string(),
            a2l_addr,
            get_type,
            a2l_limits,
            value: Vec::new(),
        }
    }

    pub fn get_name(&self) -> &str {
        &self.name
    }
    pub fn get_a2l_type(&self) -> A2lType {
        self.get_type
    }
    pub fn get_a2l_addr(&self) -> A2lAddr {
        self.a2l_addr
    }
    pub fn set_value(&mut self, bytes: &[u8]) {
        self.value = bytes.to_vec();
    }
    pub fn get_value(&mut self) -> &[u8] {
        &self.value
    }
    pub fn get_value_u64(&self) -> u64 {
        let mut value = 0u64;
        for i in (0..self.get_type.size).rev() {
            value <<= 8;
            value += self.value[i] as u64;
        }
        value
    }
    pub fn get_value_i64(&self) -> i64 {
        let size = self.get_type.size;
        let mut value: i64 = if self.value[size - 1] & 0x80 != 0 { -1 } else { 0 };
        for i in (0..size).rev() {
            value <<= 8;
            assert!(value & 0xFF == 0);
            value |= self.value[i] as i64;
        }
        value
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
// Measurement object

#[derive(Debug, Copy, Clone)]
pub struct XcpMeasurementObjectHandle(pub usize);

impl XcpMeasurementObjectHandle {
    pub fn get_name(self, xcp_client: &mut XcpClient) -> &str {
        xcp_client.get_measurement_object(self).get_name()
    }
    pub fn get_a2l_addr(self, xcp_client: &mut XcpClient) -> A2lAddr {
        xcp_client.get_measurement_object(self).get_a2l_addr()
    }
    pub fn get_a2l_type(self, xcp_client: &mut XcpClient) -> A2lType {
        xcp_client.get_measurement_object(self).get_a2l_type()
    }
}

#[derive(Debug, Clone)]
pub struct XcpClientMeasurementObject {
    pub(super) name: String,
    pub a2l_addr: A2lAddr,
    pub a2l_type: A2lType,
    pub conversion: A2lConversion,
    pub daq: u16,
    pub odt: u8,
    pub offset: u16,
}

impl XcpClientMeasurementObject {
    pub fn new(name: &str, a2l_addr: A2lAddr, a2l_type: A2lType, conversion: A2lConversion) -> XcpClientMeasurementObject {
        XcpClientMeasurementObject {
            name: name.to_string(),
            a2l_addr,
            a2l_type,
            conversion,
            daq: 0,
            odt: 0,
            offset: 0,
        }
    }
    pub fn get_name(&self) -> &str {
        &self.name
    }
    pub fn get_a2l_addr(&self) -> A2lAddr {
        self.a2l_addr
    }
    pub fn get_a2l_type(&self) -> A2lType {
        self.a2l_type
    }
    pub fn get_conversion(&self) -> &A2lConversion {
        &self.conversion
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn a2l_type(size: usize, encoding: A2lTypeEncoding) -> A2lType {
        A2lType { size, encoding }
    }

    #[test]
    fn test_a2l_type_decode() {
        assert_eq!(a2l_type(1, A2lTypeEncoding::Unsigned).decode(&[0xFF]), Some(A2lValue::Unsigned(255)));
        assert_eq!(a2l_type(1, A2lTypeEncoding::Signed).decode(&[0xFF]), Some(A2lValue::Signed(-1)));
        assert_eq!(a2l_type(2, A2lTypeEncoding::Signed).decode(&[0xFE, 0xFF, 0x55]), Some(A2lValue::Signed(-2)));
        assert_eq!(
            a2l_type(4, A2lTypeEncoding::Unsigned).decode(&[0x78, 0x56, 0x34, 0x12]),
            Some(A2lValue::Unsigned(0x12345678))
        );
        assert_eq!(a2l_type(8, A2lTypeEncoding::Unsigned).decode(&u64::MAX.to_le_bytes()), Some(A2lValue::Unsigned(u64::MAX)));
        assert_eq!(a2l_type(8, A2lTypeEncoding::Signed).decode(&i64::MIN.to_le_bytes()), Some(A2lValue::Signed(i64::MIN)));
        assert_eq!(a2l_type(4, A2lTypeEncoding::Float).decode(&0.1f32.to_le_bytes()), Some(A2lValue::Float(0.1)));
        assert_eq!(a2l_type(8, A2lTypeEncoding::Float).decode(&(-2.5f64).to_le_bytes()), Some(A2lValue::Float(-2.5)));
        assert_eq!(a2l_type(4, A2lTypeEncoding::Unsigned).decode(&[0, 0]), None); // too short
        assert_eq!(a2l_type(16, A2lTypeEncoding::Blob).decode(&[0; 16]), None);
    }

    #[test]
    fn test_a2l_conversion() {
        // No conversion, the type of the raw value is kept, also for values a f64 can not represent
        let c = A2lConversion::new(None, None, "");
        assert_eq!(c.convert(A2lValue::Unsigned(u64::MAX)), A2lValue::Unsigned(u64::MAX));
        assert_eq!(c.convert(A2lValue::Signed(-5)).to_string(), "-5");
        assert_eq!(c.unit(), "");
        assert!(!c.is_enum());

        // Unit only, factor 1 and offset 0 are no conversion
        let c = A2lConversion::new(Some(1.0), Some(0.0), "Volt");
        assert_eq!(c.convert(A2lValue::Unsigned(7)), A2lValue::Unsigned(7));
        assert_eq!(c.unit(), "Volt");

        // Linear
        let c = A2lConversion::new(Some(0.5), Some(-10.0), "km/h");
        assert_eq!(c.convert(A2lValue::Unsigned(100)), A2lValue::Float(40.0));
        assert_eq!(c.convert(A2lValue::Signed(-4)), A2lValue::Float(-12.0));
        assert_eq!(c.convert(A2lValue::Float(1.0)), A2lValue::Float(-9.5));
        let c = A2lConversion::new(None, Some(1.5), "");
        assert_eq!(c.convert(A2lValue::Unsigned(1)).to_string(), "2.5");

        // Enum, a value without a label and a float are not converted
        let c = A2lConversion::new(Some(2.0), None, r#"0 "none" 1 "calc" 2 "stand by" -1 "error""#);
        assert!(c.is_enum());
        assert_eq!(c.unit(), "");
        assert_eq!(c.enum_labels().unwrap().len(), 4);
        assert_eq!(c.convert(A2lValue::Unsigned(1)), A2lValue::Label("calc".into()));
        assert_eq!(c.convert(A2lValue::Unsigned(2)).as_label(), Some("stand by"));
        assert_eq!(c.convert(A2lValue::Signed(-1)).to_string(), "error");
        assert_eq!(c.convert(A2lValue::Unsigned(3)), A2lValue::Unsigned(3));
        assert_eq!(c.convert(A2lValue::Unsigned(u64::MAX)), A2lValue::Unsigned(u64::MAX));
        assert_eq!(c.convert(A2lValue::Float(1.0)), A2lValue::Float(1.0));

        // Not in enum format, a physical unit
        for unit in ["m/s", "1 2", r#"0 "a" 1"#, r#"0 "a" x "b""#, r#"0 "a"#, "0", r#""a""#] {
            let c = A2lConversion::new(None, None, unit);
            assert!(!c.is_enum(), "{}", unit);
            assert_eq!(c.unit(), unit);
        }
    }

    #[test]
    fn test_a2l_conversion_from_support_data() {
        use xcp_registry::McObjectType;
        let c = A2lConversion::from(&McSupportData::new(McObjectType::Measurement).set_linear(0.1, 2.0, "Volt"));
        assert_eq!(c.convert(A2lValue::Unsigned(10)), A2lValue::Float(3.0));
        assert_eq!(c.unit(), "Volt");
        let c = A2lConversion::from(&McSupportData::new(McObjectType::Measurement).set_unit(r#"0 "OFF" 1 "ON""#));
        assert_eq!(c.convert(A2lValue::Unsigned(1)), A2lValue::Label("ON".into()));
    }
}
