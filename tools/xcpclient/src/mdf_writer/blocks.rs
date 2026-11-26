//--------------------------------------------------------------------------------------------------------------------------------------------------
// blocks.rs — Serialization of the MDF4 blocks used by the MDF writer
//
// Minimal subset of ASAM MDF 4.1: ID, HD, FH, MD, TX, DG, CG, SI, CN, CC and DT blocks.
// All values little endian. Every block starts at a position which is a multiple of 8, a block is padded with zeros to a multiple of
// 8 bytes (the padding belongs to the block). A link is the absolute file position of the block it points to, 0 is NIL.
//
// Block layout: header (id "##XX", reserved u32, length u64, link count u64), links (u64 each), data

/// NIL link
pub const NIL: u64 = 0;

// ID block
pub const ID_LENGTH: usize = 64;
pub const ID_FILE_FINALIZED: &[u8; 8] = b"MDF     ";
pub const ID_FILE_UNFINALIZED: &[u8; 8] = b"UnFinMF ";
pub const ID_FILE_OFFSET: u64 = 0;
pub const ID_UNFIN_FLAGS_OFFSET: u64 = 60;
pub const ID_UNFIN_FLAG_INVAL_CYCLE_COUNT_CG: u16 = 1 << 0; // Cycle counts of the CG blocks are not valid
pub const ID_UNFIN_FLAG_INVAL_LEN_LAST_DT: u16 = 1 << 2; // Length of the last DT block is not valid, it extends to the end of the file

// Block header
pub const BLOCK_HEADER_LENGTH: u64 = 24;
pub const BLOCK_LENGTH_OFFSET: u64 = 8; // Offset of the length in the block header

// CG block: offset of cg_cycle_count, behind 6 links and cg_record_id
pub const CG_CYCLE_COUNT_OFFSET: u64 = BLOCK_HEADER_LENGTH + 6 * 8 + 8;

// CN types, sync types and data types
pub const CN_TYPE_VALUE: u8 = 0;
pub const CN_TYPE_MASTER: u8 = 2;
pub const CN_SYNC_NONE: u8 = 0;
pub const CN_SYNC_TIME: u8 = 1;
pub const CN_VAL_UNSIGNED_LE: u8 = 0;
pub const CN_VAL_SIGNED_LE: u8 = 2;
pub const CN_VAL_REAL_LE: u8 = 4;
pub const CN_VAL_BYTE_ARRAY: u8 = 10;
pub const CN_FLAG_DISCRETE_VALUES: u32 = 1 << 6;

// CC types
pub const CC_LINEAR: u8 = 1;
pub const CC_VALUE_TO_TEXT: u8 = 7;

// SI types
pub const SI_TYPE_ECU: u8 = 1;
pub const SI_BUS_NONE: u8 = 0;

//--------------------------------------------------------------------------------------------------------------------------------------------------

/// Channel description for a CN block
pub struct Channel {
    pub cn_type: u8,
    pub sync_type: u8,
    pub data_type: u8,
    pub byte_offset: u32, // Offset in the record, behind the record id
    pub bit_count: u32,
    pub flags: u32,
    pub name: u64,       // TX block
    pub source: u64,     // SI block
    pub conversion: u64, // CC block
    pub unit: u64,       // TX block
    pub next: u64,       // Next CN block
}

/// Builds the metadata blocks of an MDF4 file in memory
/// Positions are absolute file positions, the buffer starts at file position 0
pub struct BlockBuffer {
    buf: Vec<u8>,
}

impl BlockBuffer {
    pub fn new() -> BlockBuffer {
        BlockBuffer {
            buf: Vec::with_capacity(64 * 1024),
        }
    }

    /// Current position, where the next block starts
    pub fn pos(&self) -> u64 {
        self.buf.len() as u64
    }

    pub fn into_inner(self) -> Vec<u8> {
        self.buf
    }

    fn put_u64(&mut self, v: u64) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }

    /// Overwrite a u64 at an absolute position, to set links to blocks written later
    pub fn patch_u64(&mut self, pos: u64, v: u64) {
        let pos = pos as usize;
        self.buf[pos..pos + 8].copy_from_slice(&v.to_le_bytes());
    }

    /// Set link index of the block at block_pos
    pub fn set_link(&mut self, block_pos: u64, index: usize, target: u64) {
        self.patch_u64(block_pos + BLOCK_HEADER_LENGTH + 8 * index as u64, target);
    }

    /// Append a block, returns its position
    pub fn block(&mut self, id: &[u8; 2], links: &[u64], data: &[u8]) -> u64 {
        debug_assert_eq!(self.buf.len() % 8, 0);
        let pos = self.pos();
        let length = (BLOCK_HEADER_LENGTH as usize + 8 * links.len() + data.len()).next_multiple_of(8);
        self.buf.extend_from_slice(&[b'#', b'#', id[0], id[1], 0, 0, 0, 0]);
        self.put_u64(length as u64);
        self.put_u64(links.len() as u64);
        for &link in links {
            self.put_u64(link);
        }
        self.buf.extend_from_slice(data);
        self.buf.resize(pos as usize + length, 0);
        pos
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------

    /// ID block, at position 0, unfinalized
    pub fn id_block(&mut self, program: &str) {
        assert_eq!(self.pos(), 0);
        let mut id = [0u8; ID_LENGTH];
        id[0..8].copy_from_slice(ID_FILE_UNFINALIZED);
        id[8..16].copy_from_slice(b"4.10    ");
        let mut prog = [b' '; 8];
        for (p, c) in prog.iter_mut().zip(program.bytes()) {
            *p = c;
        }
        id[16..24].copy_from_slice(&prog);
        id[28..30].copy_from_slice(&410u16.to_le_bytes());
        let unfin = ID_UNFIN_FLAG_INVAL_CYCLE_COUNT_CG | ID_UNFIN_FLAG_INVAL_LEN_LAST_DT;
        id[60..62].copy_from_slice(&unfin.to_le_bytes());
        self.buf.extend_from_slice(&id);
    }

    /// TX block with a zero terminated UTF-8 string
    pub fn tx(&mut self, text: &str) -> u64 {
        let mut data = Vec::with_capacity(text.len() + 1);
        data.extend_from_slice(text.as_bytes());
        data.push(0);
        self.block(b"TX", &[], &data)
    }

    /// MD block with a zero terminated XML string
    pub fn md(&mut self, xml: &str) -> u64 {
        let mut data = Vec::with_capacity(xml.len() + 1);
        data.extend_from_slice(xml.as_bytes());
        data.push(0);
        self.block(b"MD", &[], &data)
    }

    /// HD block, links: first DG, first FH, comment
    pub fn hd(&mut self, dg_first: u64, fh_first: u64, comment: u64, start_time_ns: u64) -> u64 {
        let mut data = Vec::with_capacity(32);
        data.extend_from_slice(&start_time_ns.to_le_bytes());
        data.extend_from_slice(&0i16.to_le_bytes()); // tz offset
        data.extend_from_slice(&0i16.to_le_bytes()); // dst offset
        data.push(0); // time flags: UTC, offsets not valid
        data.push(0); // time class: PC
        data.push(0); // flags
        data.push(0); // reserved
        data.extend_from_slice(&0f64.to_le_bytes()); // start angle
        data.extend_from_slice(&0f64.to_le_bytes()); // start distance
        self.block(b"HD", &[dg_first, fh_first, NIL, NIL, NIL, comment], &data)
    }

    /// FH block, the comment is mandatory (MD block with <FHcomment>)
    pub fn fh(&mut self, comment: u64, time_ns: u64) -> u64 {
        let mut data = Vec::with_capacity(16);
        data.extend_from_slice(&time_ns.to_le_bytes());
        data.extend_from_slice(&[0u8; 8]); // tz offset, dst offset, time flags (UTC), reserved
        self.block(b"FH", &[NIL, comment], &data)
    }

    /// DG block, links: next DG, first CG, data, comment
    pub fn dg(&mut self, cg_first: u64, data_block: u64, record_id_size: u8) -> u64 {
        let mut data = [0u8; 8];
        data[0] = record_id_size;
        self.block(b"DG", &[NIL, cg_first, data_block, NIL], &data)
    }

    /// CG block, links: next CG, first CN, acquisition name, acquisition source, first SR, comment
    pub fn cg(&mut self, next: u64, cn_first: u64, acq_name: u64, acq_source: u64, record_id: u64, data_bytes: u32) -> u64 {
        let mut data = Vec::with_capacity(32);
        data.extend_from_slice(&record_id.to_le_bytes());
        data.extend_from_slice(&0u64.to_le_bytes()); // cycle count, set at finalization
        data.extend_from_slice(&0u16.to_le_bytes()); // flags
        data.extend_from_slice(&0u16.to_le_bytes()); // path separator
        data.extend_from_slice(&[0u8; 4]); // reserved
        data.extend_from_slice(&data_bytes.to_le_bytes());
        data.extend_from_slice(&0u32.to_le_bytes()); // invalidation bytes
        self.block(b"CG", &[next, cn_first, acq_name, acq_source, NIL, NIL], &data)
    }

    /// SI block, links: name, path, comment
    pub fn si(&mut self, name: u64, path: u64, si_type: u8, bus_type: u8) -> u64 {
        let data = [si_type, bus_type, 0, 0, 0, 0, 0, 0];
        self.block(b"SI", &[name, path, NIL], &data)
    }

    /// CN block, links: next CN, composition, name, source, conversion, data, unit, comment
    pub fn cn(&mut self, c: &Channel) -> u64 {
        let mut data = Vec::with_capacity(72);
        data.push(c.cn_type);
        data.push(c.sync_type);
        data.push(c.data_type);
        data.push(0); // bit offset
        data.extend_from_slice(&c.byte_offset.to_le_bytes());
        data.extend_from_slice(&c.bit_count.to_le_bytes());
        data.extend_from_slice(&c.flags.to_le_bytes());
        data.extend_from_slice(&0u32.to_le_bytes()); // invalidation bit position
        data.push(0xFF); // precision
        data.push(0); // reserved
        data.extend_from_slice(&0u16.to_le_bytes()); // attachment count
        for _ in 0..6 {
            data.extend_from_slice(&0f64.to_le_bytes()); // value range and limits, not valid
        }
        self.block(b"CN", &[c.next, NIL, c.name, c.source, c.conversion, NIL, c.unit, NIL], &data)
    }

    /// CC block, links: name, unit, comment, inverse, refs
    fn cc(&mut self, cc_type: u8, flags: u16, unit: u64, refs: &[u64], values: &[f64]) -> u64 {
        let mut links = vec![NIL, unit, NIL, NIL];
        links.extend_from_slice(refs);
        let mut data = Vec::with_capacity(24 + 8 * values.len());
        data.push(cc_type);
        data.push(0xFF); // precision
        data.extend_from_slice(&flags.to_le_bytes());
        data.extend_from_slice(&(refs.len() as u16).to_le_bytes());
        data.extend_from_slice(&(values.len() as u16).to_le_bytes());
        data.extend_from_slice(&0f64.to_le_bytes()); // physical range, not valid
        data.extend_from_slice(&0f64.to_le_bytes());
        for v in values {
            data.extend_from_slice(&v.to_le_bytes());
        }
        self.block(b"CC", &links, &data)
    }

    /// Linear conversion: phys = offset + factor * raw
    pub fn cc_linear(&mut self, factor: f64, offset: f64, unit: u64) -> u64 {
        self.cc(CC_LINEAR, 0, unit, &[], &[offset, factor])
    }

    /// Value to text conversion, refs are TX blocks with the labels, the default (raw value without label) is NIL
    pub fn cc_value_to_text(&mut self, values: &[f64], labels: &[u64]) -> u64 {
        debug_assert_eq!(values.len(), labels.len());
        let mut refs = labels.to_vec();
        refs.push(NIL); // default
        self.cc(CC_VALUE_TO_TEXT, 0, NIL, &refs, values)
    }

    /// DT block header, the records follow, the length is set at finalization
    pub fn dt_header(&mut self) -> u64 {
        let pos = self.pos();
        self.buf.extend_from_slice(b"##DT\0\0\0\0");
        self.put_u64(BLOCK_HEADER_LENGTH);
        self.put_u64(0);
        pos
    }
}
