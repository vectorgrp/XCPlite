# bintool - XCPlite BIN File Tool

A command-line tool for XCPlite `.BIN` persistence files.  
Persistence features in XCPlite are enabled with OPTION_CAL_PERSISTENCE.  
For safety, it is essential to use the EPK version segment. This can be enabled with OPTION_CAL_SEGMENT_EPK.  

Disclaimer: This is an AI generated tool. Don't use it for production. Improper use may corrupt calibration data.  

## Overview

This tool provides comprehensive management of XCPlite binary persistence files:
- **BIN to HEX**: Extract calibration segment data to Intel-Hex format
- **HEX to BIN**: Update calibration segment data in a BIN file from Intel-Hex files
- **Dump/Inspect**: View file structure, events, segments, and hex dumps


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



## Building

```bash
cd tools/bintool
cargo build --release
# Optional, install to ~/.cargo/bin/bintool or use the compiled binary will be in `target/release/bintool`
cargo install --path .  
```



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
- End-of-file record



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





## License

MIT License - See LICENSE file in the project root.

## Author

RainerZ

