#!/bin/bash

# Script to run all XCPlite examples for 2 seconds each
# Usage: ./run_all.sh

# Exit on error
set -e

# Get the workspace root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${WORKSPACE_ROOT}/build"
FIXTURES_DIR="${SCRIPT_DIR}/fixtures"

# Create log file with timestamp
TIMESTAMP=$(date "+%Y%m%d_%H%M%S")
LOG_FILE="${SCRIPT_DIR}/run_all_${TIMESTAMP}.log"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to log to both console and file
log() {
    echo -e "$@" | tee -a "$LOG_FILE"
}

# Function to log without color codes to file
log_plain() {
    echo -e "$@"
    echo -e "$@" | sed 's/\x1b\[[0-9;]*m//g' >> "$LOG_FILE"
}

log_plain "${BLUE}========================================${NC}"
log_plain "${BLUE}  Running All XCPlite Examples (2s each)${NC}"
log_plain "${BLUE}========================================${NC}"
log_plain ""
log_plain "Log file: $LOG_FILE"
log_plain "Start time: $(date)"
log_plain ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    log_plain "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    log_plain "Please run CMake build first"
    exit 1
fi

# Path to bintool (prefer release build if available)
BINTOOL="${WORKSPACE_ROOT}/tools/bintool/target/release/bintool"
if [ ! -x "$BINTOOL" ]; then
    BINTOOL="${WORKSPACE_ROOT}/tools/bintool/target/debug/bintool"
fi

if [ ! -x "$BINTOOL" ]; then
    log_plain "${YELLOW}Warning: bintool not found. Will skip .bin file analysis.${NC}"
    log_plain "${YELLOW}Build it with: cd tools/bintool && cargo build --release${NC}"
    BINTOOL=""
fi

# Clean up all .bin and .hex files before starting
log_plain "${BLUE}Cleaning up old .bin and .hex files...${NC}"
find "$BUILD_DIR" -name "*.bin" -type f -delete 2>/dev/null || true
find "$BUILD_DIR" -name "*.hex" -type f -delete 2>/dev/null || true
find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.bin" -type f -delete 2>/dev/null || true
find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.hex" -type f -delete 2>/dev/null || true
log_plain ""

# List of examples to run (order matters - simpler ones first)
EXAMPLES=(
    "hello_xcp.out"
    "hello_xcp_cpp.out"
    "c_demo.out"
    "cpp_demo.out"
    "struct_demo.out"
    "multi_thread_demo.out"
    
)

# Optional examples
OPTIONAL_EXAMPLES=(
    
    "no_a2l_demo.out"
)

# Counter for results
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

# Counter for hex file comparisons
HEX_COMPARED=0
HEX_MATCHED=0
HEX_MISMATCHED=0

# Function to run an example
run_example() {
    local example=$1
    local is_optional=$2
    local exe_path="${BUILD_DIR}/${example}"
    
    TOTAL=$((TOTAL + 1))
    
    log_plain "${YELLOW}Running: ${example}${NC}"
    
    # Check if executable exists
    if [ ! -f "$exe_path" ]; then
        if [ "$is_optional" = true ]; then
            log_plain "${YELLOW}  Skipped: ${example} not found (optional)${NC}"
            SKIPPED=$((SKIPPED + 1))
        else
            log_plain "${RED}  Failed: ${example} not found${NC}"
            FAILED=$((FAILED + 1))
        fi
        log_plain ""
        return
    fi
    
    # Check if executable is runnable
    if [ ! -x "$exe_path" ]; then
        log_plain "${RED}  Failed: ${example} is not executable${NC}"
        FAILED=$((FAILED + 1))
        log_plain ""
        return
    fi
    
    # Create temporary file for capturing output
    local tmp_output=$(mktemp)
    
    # Run the example with timeout (using portable method for macOS/Linux)
    # Start the process in background and capture its PID
    "$exe_path" > "$tmp_output" 2>&1 &
    local pid=$!
    
    # Wait for up to 2 seconds for the process to complete
    local counter=0
    local max_count=20  # 20 * 0.1s = 2s
    local process_exited=false
    
    while [ $counter -lt $max_count ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            process_exited=true
            break
        fi
        sleep 0.1
        counter=$((counter + 1))
    done
    
    if [ "$process_exited" = true ]; then
        # Process exited before timeout - check exit code
        wait "$pid" 2>/dev/null
        EXIT_CODE=$?
        if [ $EXIT_CODE -eq 0 ]; then
            log_plain "${GREEN}  Passed: ${example} completed successfully${NC}"
            PASSED=$((PASSED + 1))
        else
            log_plain "${RED}  Failed: ${example} crashed with exit code $EXIT_CODE${NC}"
            FAILED=$((FAILED + 1))
        fi
    else
        # Process is still running after 2s - kill it (this is expected/success)
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        log_plain "${GREEN}  Passed: ${example} ran for 2s and was terminated${NC}"
        PASSED=$((PASSED + 1))
    fi
    
    # Wait for kernel to flush buffered output to temp file
    # When a process is killed abruptly without signal handler, buffered output may not be flushed
    # Give the system time to flush buffers and sync the file
    sleep 0.5
    
    # Log the example output (always log, even if empty)
    echo "" >> "$LOG_FILE"
    echo "  Output from ${example}:" >> "$LOG_FILE"
    echo "  ----------------------------------------" >> "$LOG_FILE"
    
    # Check if output file exists and has content
    if [ -f "$tmp_output" ] && [ -s "$tmp_output" ]; then
        sed 's/^/  /' "$tmp_output" >> "$LOG_FILE"
    else
        local file_size=0
        if [ -f "$tmp_output" ]; then
            file_size=$(stat -f%z "$tmp_output" 2>/dev/null || stat -c%s "$tmp_output" 2>/dev/null || echo 0)
        fi
        echo "  (no output captured - file size: ${file_size} bytes)" >> "$LOG_FILE"
    fi
    echo "  ----------------------------------------" >> "$LOG_FILE"
    rm -f "$tmp_output"
    
    # Check for .bin files created by this example
    # Format: example_name_HH_MM_SS.bin (where HH_MM_SS is from __TIME__)
    # Check both BUILD_DIR and WORKSPACE_ROOT (examples write to current directory)
    local example_basename="${example%.out}"
    local bin_files=$(find "$BUILD_DIR" "$WORKSPACE_ROOT" -maxdepth 1 -name "${example_basename}_*.bin" -type f 2>/dev/null)
    
    if [ -n "$bin_files" ]; then
        log_plain "${BLUE}  Found .bin file(s):${NC}"
        for bin_file in $bin_files; do
            local bin_filename=$(basename "$bin_file")
            local bin_size=$(ls -lh "$bin_file" | awk '{print $5}')
            log_plain "${BLUE}    ${bin_filename} (${bin_size})${NC}"
            
            if [ -n "$BINTOOL" ]; then
                log_plain "${BLUE}    Dumping content:${NC}"
                
                # Create temporary file for bintool output
                local tmp_bintool=$(mktemp)
                if "$BINTOOL" --dump --verbose "$bin_file" > "$tmp_bintool" 2>&1; then
                    # Success - display and log the output
                    cat "$tmp_bintool" | sed 's/^/      /' | tee -a "$LOG_FILE"
                else
                    # Failed - show error
                    log_plain "${YELLOW}      Warning: bintool failed to dump file (possible version mismatch)${NC}"
                    echo "      bintool error output:" >> "$LOG_FILE"
                    sed 's/^/      /' "$tmp_bintool" >> "$LOG_FILE"
                fi
                rm -f "$tmp_bintool"
                
                # Create Intel-HEX file
                local hex_file="${bin_file%.bin}.hex"
                log_plain "${BLUE}    Creating Intel-HEX file: $(basename "$hex_file")${NC}"
                if "$BINTOOL" --hex "$hex_file" "$bin_file" 2>&1 | tee -a "$LOG_FILE" | grep -q "Conversion complete"; then
                    local hex_size=$(ls -lh "$hex_file" | awk '{print $5}')
                    log_plain "${GREEN}      Successfully created Intel-HEX file (${hex_size})${NC}"
                    # Log a sample of the hex file (first 10 lines)
                    echo "      Intel-HEX content (first 10 lines):" >> "$LOG_FILE"
                    head -n 10 "$hex_file" | sed 's/^/      /' >> "$LOG_FILE"
                    
                    # Compare with fixture if it exists
                    local fixture_hex="${FIXTURES_DIR}/${example_basename}.hex"
                    if [ -f "$fixture_hex" ]; then
                        HEX_COMPARED=$((HEX_COMPARED + 1))
                        log_plain "${BLUE}    Comparing with fixture: ${example_basename}.hex${NC}"
                        
                        # Create temporary files without the informational header lines
                        local tmp_current=$(mktemp)
                        local tmp_fixture=$(mktemp)
                        
                        # Skip the first few lines (conversion messages) and compare actual hex content
                        grep "^:" "$hex_file" > "$tmp_current" 2>/dev/null || true
                        grep "^:" "$fixture_hex" > "$tmp_fixture" 2>/dev/null || true
                        
                        if diff -q "$tmp_current" "$tmp_fixture" >/dev/null 2>&1; then
                            log_plain "${GREEN}      ✓ HEX file matches fixture${NC}"
                            HEX_MATCHED=$((HEX_MATCHED + 1))
                        else
                            log_plain "${RED}      ✗ HEX file DOES NOT match fixture!${NC}"
                            HEX_MISMATCHED=$((HEX_MISMATCHED + 1))
                            # Log the differences
                            echo "      Differences:" >> "$LOG_FILE"
                            diff -u "$tmp_fixture" "$tmp_current" | head -20 | sed 's/^/      /' >> "$LOG_FILE" || true
                        fi
                        
                        rm -f "$tmp_current" "$tmp_fixture"
                    else
                        log_plain "${YELLOW}      Note: No fixture found - creating new fixture: ${example_basename}.hex${NC}"
                        # Create fixtures directory if it doesn't exist
                        mkdir -p "$FIXTURES_DIR"
                        # Copy the hex file to fixtures
                        cp "$hex_file" "$fixture_hex"
                        log_plain "${GREEN}      ✓ Created fixture from current HEX file${NC}"
                    fi
                else
                    log_plain "${YELLOW}      Warning: bintool failed to create Intel-HEX file${NC}"
                fi
            fi
        done
    fi
    
    log_plain ""
}

# Run all regular examples
for example in "${EXAMPLES[@]}"; do
    run_example "$example" false
done

# Run optional examples
for example in "${OPTIONAL_EXAMPLES[@]}"; do
    run_example "$example" true
done

# Print summary
log_plain "${BLUE}========================================${NC}"
log_plain "${BLUE}  Summary${NC}"
log_plain "${BLUE}========================================${NC}"
log_plain "Total:   $TOTAL"
log_plain "${GREEN}Passed:  $PASSED${NC}"
log_plain "${RED}Failed:  $FAILED${NC}"
log_plain "${YELLOW}Skipped: $SKIPPED${NC}"
log_plain ""
if [ $HEX_COMPARED -gt 0 ]; then
    log_plain "${BLUE}HEX File Comparisons:${NC}"
    log_plain "  Compared:   $HEX_COMPARED"
    log_plain "${GREEN}  Matched:    $HEX_MATCHED${NC}"
    log_plain "${RED}  Mismatched: $HEX_MISMATCHED${NC}"
    log_plain ""
fi
log_plain "End time: $(date)"
log_plain ""
log_plain "Full log saved to: $LOG_FILE"

# Exit with appropriate code
if [ $FAILED -gt 0 ]; then
    log_plain "${RED}Some examples failed!${NC}"
    exit 1
elif [ $HEX_MISMATCHED -gt 0 ]; then
    log_plain "${RED}Some HEX files don't match their fixtures!${NC}"
    exit 1
else
    log_plain "${GREEN}All examples passed!${NC}"
    exit 0
fi
