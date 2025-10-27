# bintool - XCPlite BIN File Tool

A command-line tool for XCPlite `.BIN` persistency files:
- **Inspect**: View BIN file structure and calibration data
- **Convert**: Extract calibration data to Intel-Hex format
- **Update**: Apply Intel-Hex data back to BIN files

Persistency features in XCPlite are enabled with OPTION_CAL_PERSISTENCE.  

Disclaimer: This is an AI generated tool. Don't use it for production. Improper use may corrupt calibration data.  

## Overview

This tool provides comprehensive management of XCPlite binary persistency files:
- **BIN to HEX**: Extract calibration segment data to Intel-Hex format
- **HEX to BIN**: Update calibration segment data from Intel-Hex files
- **Dump/Inspect**: View file structure, segments, and hex dumps

The tool uses a hardcoded 32-bit segment relative addressing scheme: `Address = 0x80000000 | (segment_index << 16)` which is default in XCPlite.  
Absolute addressing (OPTION_CAL_SEGMENTS_ABS in XCPlite main_cfg.h) is not supported yet.  
There is no check for the correct addressing mode.  
For safety, it is essential to use the EPK version segment. This can be enabled with OPTION_CAL_SEGMENT_EPK.  



## Usage

### Quick Start (Convenient Syntax)

**Convert BIN to HEX** (output filename auto-derived):
```bash
bintool input.bin
# Creates: input.hex
```

**Inspect BIN file contents**:
```bash
bintool input.bin --dump
bintool input.bin --dump --verbose  # With hex dump
```

**Apply HEX data back to BIN**:
```bash
bintool input.bin --apply-hex changes.hex
```

### Detailed Usage

#### Mode 1: BIN to HEX Conversion

Convert a BIN file to Intel-Hex format:

```bash
bintool [BIN_FILE] [OPTIONS]
bintool --bin <INPUT.bin> --hex <OUTPUT.hex> [OPTIONS]  # Old style
```

If output filename is not specified, it's automatically derived from input (changes .bin to .hex).

#### Mode 2: HEX to BIN Update

Apply Intel-Hex data to update calibration segments in a BIN file:

```bash
bintool <FILE.bin> --apply-hex <INPUT.hex> [OPTIONS]
```

#### Mode 3: Dump BIN File Contents

Inspect BIN file structure without converting:

```bash
bintool <FILE.bin> --dump [--verbose]
```

Shows header information and calibration segments. With `--verbose`, displays hex dump of all calibration data.

### Arguments

- `[BIN_FILE]` - Input .BIN file path (positional argument, or use `--bin`)
- `-b, --bin <FILE>` - Input .BIN file path (alternative to positional)
- `-o, --hex <FILE>` - Output .HEX file path (optional, auto-derived from input if omitted)
- `--apply-hex <FILE>` - Apply Intel-Hex file data to update the BIN file (HEX to BIN mode)
- `-d, --dump` - Dump BIN file header and segment information (does not convert)
- `-v, --verbose` - Enable verbose output (with `--dump`, shows hex data)
- `-h, --help` - Display help information
- `-V, --version` - Display version information

**Note**: Options `--hex`, `--apply-hex`, and `--dump` are mutually exclusive.

### Examples

**BIN to HEX conversion (simple):**
```bash
bintool myproject_v1.0.bin           # Creates myproject_v1.0.hex
bintool myproject_v1.0.bin -o out.hex  # Explicit output name
```

**BIN to HEX conversion (old style):**
```bash
bintool --bin myproject_v1.0.bin --hex output.hex
bintool -b myproject_v1.0.bin -o output.hex -v
```

**Dump BIN file contents:**
```bash
bintool myproject_v1.0.bin --dump              # Header and segments only
bintool myproject_v1.0.bin --dump --verbose   # With hex dump of data
bintool myproject_v1.0.bin -d -v              # Short form
```

**HEX to BIN update:**
```bash
bintool myproject_v1.0.bin --apply-hex calibration.hex
bintool -b myproject_v1.0.bin --apply-hex calibration.hex -v
```

## Suggestions for improvement

- ~~Add a command line option to just dump a BIN file~~ ✅ **DONE** - Added `--dump` option
- Extend the file header format to define the addressing scheme used (segment relative or absolute)
- Extend the segment header with the absolute address of the default page 
- Abstract the addressing scheme used  


# BINTOOL Architecture

## Key Concepts
- XCP upload = FROM target, download = TO target (this typically gets confused by AI tools)
- Address: 0x80000000 | (segment_index << 16) hardcoded, no address information in the segment header, this should be changed in a future version
- BIN files: timestamp in name (cpp_demo_v10_21_01_18.bin)

## Implementation
- Location: tools/bintool/
- Rust with #[repr(C, packed)] for C ABI
- Structs: BinHeaderRaw (58B), EventDescriptorRaw (28B), CalSegDescriptorRaw (24B)

## Validation
- EPK must match if present
- Segment data must be complete
- Atomic three-phase updates


## Building

```bash
cd tools/bintool
cargo build --release
# Optional, install to ~/.cargo/bin/bintool or use the compiled binary will be in `target/release/bintool`
cargo install --path .  
```





## BIN File Format

The tool reads the binary format defined in `XCPlite/src/persistency.c`:

### Header (58 bytes)
- Signature: 16 bytes - "XCPLITE__BINARY"
- Version: 2 bytes - 0x0100
- EPK: 32 bytes - null-terminated string
- Event Count: 2 bytes
- CalSeg Count: 2 bytes
- Reserved: 4 bytes

### Event Descriptors (28 bytes each)
Events are skipped during conversion (not included in hex output).

### Calibration Segment Descriptors (24 bytes each)
- Name: 16 bytes - null-terminated string
- Size: 2 bytes - size in bytes
- Index: 2 bytes
- Reserved: 4 bytes

Followed immediately by the calibration data (size bytes).

## Dump Feature

The `--dump` option allows inspection of BIN file contents without conversion:

```bash
bintool input.bin --dump              # Header and segment info
bintool input.bin --dump --verbose   # With hex dump of data
```

### Output Format

**Without `--verbose`:**
- File signature and version
- EPK (firmware identifier)
- Event and calibration segment counts
- Per-segment: Name, Index, Size, XCP Address

**With `--verbose`:**
- All of the above, plus:
- Hex dump of calibration data
- 16 bytes per line
- Address offsets (0000, 0010, etc.)
- ASCII representation (printable chars or '.')

### Example Output

```
========================================
BIN File: cpp_demo.bin
========================================

HEADER:
  Signature:    XCPLITE__BINARY
  Version:      0x0100
  EPK:          v10_21_01_18
  Event Count:  4
  CalSeg Count: 4

CALIBRATION SEGMENTS:

Segment 0:
  Name:    epk
  Index:   0
  Size:    12 bytes
  Address: 0x80000000 (XCP address)

  Data (hex dump):
    0000:  76 31 30 5F 32 31 5F 30  31 5F 31 38              |v10_21_01_18|
```

## Intel-Hex Output

The tool generates Intel-Hex files with:
- Extended Linear Address records for 32-bit addressing
- Data records (32 bytes per record)
- Calibration segments placed at: `0x80000000 | (segment_index << 16)`
- End-of-file record

### Address Mapping

Each calibration segment is placed at a unique address based on its index:

| Segment Index | Address      | Example       |
|--------------|--------------|---------------|
| 0            | 0x80000000   | epk           |
| 1            | 0x80010000   | kParameters   |
| 2            | 0x80020000   | SigGen1       |
| 3            | 0x80030000   | SigGen2       |

Formula: `Address = 0x80000000 | (index << 16)`

## HEX to BIN Update Safety Features

When updating a BIN file from Intel-Hex data (`--apply-hex`), the tool enforces strict validation:

### 1. EPK Segment Protection
If the first segment is named "epk", its size and content must exactly match between BIN and HEX files. This prevents incompatible firmware updates.

### 2. Complete Segment Coverage
HEX data must completely cover each BIN segment. Partial segment updates are rejected to prevent calibration data corruption.
- HEX segment size must be ≥ BIN segment size
- If HEX has more data, only the required bytes are written

### 3. Atomic Updates
All validations occur **before** any file modifications:
1. **Phase 1**: Read all segment descriptors
2. **Phase 2**: Validate all segments completely
3. **Phase 3**: Only if all validations pass, write the data

This ensures the original BIN file remains completely unchanged if any validation fails.

## Dependencies

- [clap](https://crates.io/crates/clap) - Command-line argument parsing
- [ihex](https://crates.io/crates/ihex) - Intel-Hex file format support
- [thiserror](https://crates.io/crates/thiserror) - Error handling

## Use Cases

### 1. Inspect BIN File Structure
```bash
# Quick overview
bintool myapp.bin --dump

# Detailed analysis with hex dump
bintool myapp.bin --dump --verbose
```

### 2. Backup and Restore Calibration Data
```bash
# Backup calibration to HEX format
bintool myapp.bin -o backup.hex

# Restore calibration from HEX (if EPK matches)
bintool myapp.bin --apply-hex backup.hex
```

### 3. Transfer Calibration Between Systems
```bash
# On development system: export calibration
bintool dev_calibration.bin

# On production system: import calibration (with validation)
bintool prod_firmware.bin --apply-hex dev_calibration.hex
```

### 4. Version Control for Calibration Data
Store `.hex` files in version control (human-readable, diff-friendly) instead of binary `.bin` files.

## Error Handling

The tool provides clear error messages for common issues:

- **Invalid signature**: Not an XCPlite BIN file
- **EPK mismatch**: EPK segment content differs between BIN and HEX
- **Incomplete data**: HEX segment doesn't fully cover BIN segment
- **Invalid format**: Corrupted or malformed files

All errors prevent file modification, ensuring data integrity.



## License

MIT License - See LICENSE file in the project root.

## Author

Rainer Zaiser

