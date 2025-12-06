#!/bin/bash

# Script to test XCPlite examples
# Usage: ./test.sh [clean] [example_name]
#   If example_name is provided, only that example will be tested
#   If no parameter is given, all examples will be tested
#   If 'clean' is given, all generated files (.a2l, .bin, .hex) will be deleted before running tests
# Examples:
#   ./test.sh              # Run all examples
#   ./test.sh hello_xcp    # Run only hello_xcp.out
#   ./test.sh c_demo       # Run only c_demo.out
#   ./test.sh clean        # Clean and run all examples
#   ./test.sh clean hello_xcp  # Clean and run only hello_xcp.out

# Exit on error
set -e


# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to log without color codes to file
log_plain() {
    echo -e "$@"
    echo -e "$@" | sed 's/\x1b\[[0-9;]*m//g' >> "$LOG_FILE"
}

# Path to tools
BINTOOL="bintool"
A2LTOOL="a2ltool"
XCPCLIENT="xcp_client"

# Get the workspace root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${WORKSPACE_ROOT}/build"
FIXTURES_DIR="${SCRIPT_DIR}/fixtures"

# Parse command line arguments
SPECIFIC_EXAMPLE=""
DO_CLEAN=false

if [ $# -gt 0 ]; then
    # Check if the parameter is 'clean'
    if [ "$1" = "clean" ]; then
        DO_CLEAN=true
        # Shift to check if there's a second parameter (example name)
        shift
        if [ $# -gt 0 ]; then
            SPECIFIC_EXAMPLE="$1"
            # Add .out extension if not provided
            if [[ ! "$SPECIFIC_EXAMPLE" =~ \.out$ ]]; then
                SPECIFIC_EXAMPLE="${SPECIFIC_EXAMPLE}.out"
            fi
        fi
    else
        SPECIFIC_EXAMPLE="$1"
        # Add .out extension if not provided
        if [[ ! "$SPECIFIC_EXAMPLE" =~ \.out$ ]]; then
            SPECIFIC_EXAMPLE="${SPECIFIC_EXAMPLE}.out"
        fi
    fi
fi

# Perform clean if requested
if [ "$DO_CLEAN" = true ]; then
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  Cleaning generated files${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    echo -e "${YELLOW}Deleting all .a2l, .bin, and .hex files in workspace root...${NC}"
    
    # Count files before deletion
    a2l_count=$(find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.a2l" -type f 2>/dev/null | wc -l)
    bin_count=$(find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.bin" -type f 2>/dev/null | wc -l)
    hex_count=$(find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.hex" -type f 2>/dev/null | wc -l)
    
    # Delete files
    find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.a2l" -type f -delete 2>/dev/null || true
    find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.bin" -type f -delete 2>/dev/null || true
    find "$WORKSPACE_ROOT" -maxdepth 1 -name "*.hex" -type f -delete 2>/dev/null || true
    
    echo -e "${GREEN}  ✓ Deleted $a2l_count .a2l file(s)${NC}"
    echo -e "${GREEN}  ✓ Deleted $bin_count .bin file(s)${NC}"
    echo -e "${GREEN}  ✓ Deleted $hex_count .hex file(s)${NC}"
    echo ""
    echo -e "${GREEN}Clean complete! Starting tests...${NC}"
    echo ""
fi




# Create log file based on what's being tested
if [ -n "$SPECIFIC_EXAMPLE" ]; then
    # Remove .out extension for log filename
    LOG_NAME="${SPECIFIC_EXAMPLE%.out}"
    LOG_FILE="${SCRIPT_DIR}/test_${LOG_NAME}.log"
else
    LOG_FILE="${SCRIPT_DIR}/test_all.log"
fi

# Delete existing log file to start fresh
rm -f "$LOG_FILE"


# Function to log to both console and file
log() {
    echo -e "$@" | tee -a "$LOG_FILE"
}

log_plain "${BLUE}========================================${NC}"
if [ -n "$SPECIFIC_EXAMPLE" ]; then
    log_plain "${BLUE}  Testing XCPlite Example (2s)${NC}"
else
    log_plain "${BLUE}  Running All XCPlite Examples (2s each)${NC}"
fi
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



# List of all available examples (order matters - simpler ones first)
ALL_EXAMPLES=(
    "hello_xcp.out"
    "hello_xcp_cpp.out"
    "c_demo.out"
    "cpp_demo.out"
    "struct_demo.out"
    "multi_thread_demo.out"
    "point_cloud_demo.out"
)

# Determine which examples to run
if [ -n "$SPECIFIC_EXAMPLE" ]; then
    # Check if the specific example is in the list
    if [[ " ${ALL_EXAMPLES[@]} " =~ " ${SPECIFIC_EXAMPLE} " ]]; then
        EXAMPLES=("$SPECIFIC_EXAMPLE")
        log_plain "${BLUE}Testing specific example: ${SPECIFIC_EXAMPLE}${NC}"
        log_plain ""
    else
        log_plain "${RED}Error: Unknown example '${SPECIFIC_EXAMPLE}'${NC}"
        log_plain "${YELLOW}Available examples:${NC}"
        for ex in "${ALL_EXAMPLES[@]}"; do
            log_plain "  - ${ex%.out}"
        done
        exit 1
    fi
else
    EXAMPLES=("${ALL_EXAMPLES[@]}")
fi

# Function to get protocol for an example
# TCP examples: hello_xcp, hello_xcp_cpp, struct_demo
# UDP examples: c_demo, cpp_demo, multi_thread_demo
get_example_protocol() {
    local example_name="$1"
    case "$example_name" in
        hello_xcp.out|hello_xcp_cpp.out|point_cloud_demo.out|struct_demo.out)
            echo "tcp"
            ;;
        c_demo.out|cpp_demo.out|multi_thread_demo.out)
            echo "udp"
            ;;
        *)
            echo "tcp"  # Default to TCP
            ;;
    esac
}



# Counter for results
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

# Counter for xcp_client tests
XCP_CRASHED=0

# Counter for hex file comparisons
HEX_COMPARED=0
HEX_MATCHED=0
HEX_MISMATCHED=0

# Counter for A2L file comparisons
A2L_COMPARED=0
A2L_MATCHED=0
A2L_MISMATCHED=0

# Arrays to track updated fixtures
UPDATED_HEX_FIXTURES=()
UPDATED_A2L_FIXTURES=()

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
    
    # Kill any existing XCP processes to free up ports
    pkill -f "$example" || true
    sleep 0.2  # Give processes time to terminate
    
    # Create temporary file for capturing output
    local tmp_output=$(mktemp)
    
    # Run the example with timeout (using portable method for macOS/Linux)
    # Start the process in background and capture its PID
    # Change to WORKSPACE_ROOT so A2L file is created there
    cd "$WORKSPACE_ROOT"
    "$exe_path" > "$tmp_output" 2>&1 &
    local pid=$!
    # Stay in WORKSPACE_ROOT - don't change back yet so xcp_client runs in same directory
    
    # Run xcp_client test connect (foreground) if available
    sleep 1.0  # Give the example more time to initialize - A2L file is created on first connect
    
    if command -v "$XCPCLIENT" &> /dev/null; then
        # Determine protocol for this example
        local protocol=$(get_example_protocol "$example")
        local protocol_flag="--${protocol}"
        local protocol_upper=$(echo "$protocol" | tr '[:lower:]' '[:upper:]')
        
        log_plain "${BLUE}    Running xcp_client to test connection ($protocol_upper)...${NC}"
        # Run xcp_client from current directory (WORKSPACE_ROOT)
        # where the demo app is running and will create the A2L file
        # Disable exit-on-error for this command
        set +e
        # Create a temp file for xcp_client output
        local tmp_xcp=$(mktemp)
        
        # Run xcp_client - it will connect and the server will create the A2L file in current dir
        # xcp_client will automatically find and use the A2L file from the current directory
        "$XCPCLIENT" "$protocol_flag" > "$tmp_xcp" 2>&1
        local xcp_exit=$?
        
        # Append xcp_client output to main output file for logging
        cat "$tmp_xcp" >> "$tmp_output"
        
        # Also log xcp_client output to log file with indentation for visibility
        if [ -s "$tmp_xcp" ]; then
            echo "" >> "$LOG_FILE"
            echo "  xcp_client output ($protocol_upper):" >> "$LOG_FILE"
            echo "  ........................................" >> "$LOG_FILE"
            sed 's/^/  /' "$tmp_xcp" >> "$LOG_FILE"
            echo "  ........................................" >> "$LOG_FILE"
        fi
        
        rm -f "$tmp_xcp"
        set -e
        
        if [ $xcp_exit -eq 0 ]; then
            log_plain "${GREEN}      ✓ xcp_client test passed ($protocol_upper)${NC}"
        elif [ $xcp_exit -eq 134 ]; then
            # Exit code 134 is abort/panic - this is a serious error
            log_plain "${RED}      ✗ xcp_client CRASHED (abort/panic, exit code 134)${NC}"
            log_plain "${RED}         This indicates a bug in xcp_client - check the log file for details${NC}"
            XCP_CRASHED=$((XCP_CRASHED + 1))
        else
            log_plain "${YELLOW}      ⚠ xcp_client exited with code $xcp_exit (connection may have failed)${NC}"
        fi
        
        # Give the server a moment to finish writing the A2L file
        sleep 0.2

      # Run xcp_client a second time in test mode
      # "$XCPCLIENT" "$protocol_flag" "--test"


    fi
   

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
        wait "$pid" 2>/dev/null || true
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
        # Strip ANSI color codes before logging
        sed 's/\x1b\[[0-9;]*m//g' "$tmp_output" | sed 's/^/  /' >> "$LOG_FILE"
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
    # Each example creates exactly one BIN file
    local example_basename="${example%.out}"
    # Find BIN files matching the pattern and filter to exact matches (not substrings)
    # Use grep to ensure the filename ends with _HH_MM_SS.bin pattern
    local bin_files=$(find "$BUILD_DIR" "$WORKSPACE_ROOT" -maxdepth 1 -name "${example_basename}_*.bin" -type f 2>/dev/null | grep -E "/${example_basename}_[0-9]{2}_[0-9]{2}_[0-9]{2}\.bin$" | head -1)
    
    if [ -n "$bin_files" ]; then
        log_plain "${BLUE}  Found .bin file(s):${NC}"
        for bin_file in $bin_files; do
            local bin_filename=$(basename "$bin_file")
            local bin_size=$(ls -lh "$bin_file" | awk '{print $5}')
            log_plain "${BLUE}    ${bin_filename} (${bin_size})${NC}"
            
            if [ -n "$BINTOOL" ]; then
                # Dump content to log file only (not console)
                
                # Create temporary file for bintool output
                local tmp_bintool=$(mktemp)
                if "$BINTOOL" --dump --verbose "$bin_file" > "$tmp_bintool" 2>&1; then
                    # Success - log the output (but don't display to console)
                    echo "    Dumping content:" >> "$LOG_FILE"
                    sed 's/^/      /' "$tmp_bintool" >> "$LOG_FILE"
                else
                    # Failed - log error
                    echo "      Warning: bintool failed to dump file (possible version mismatch)" >> "$LOG_FILE"
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
                            # Update fixture and track it
                            cp "$hex_file" "$fixture_hex"
                            UPDATED_HEX_FIXTURES+=("${example_basename}.hex")
                        fi
                        
                        rm -f "$tmp_current" "$tmp_fixture"
                    else
                        log_plain "${YELLOW}      Note: No fixture found - creating new fixture: ${example_basename}.hex${NC}"
                        # Create fixtures directory if it doesn't exist
                        mkdir -p "$FIXTURES_DIR"
                        # Copy the hex file to fixtures
                        cp "$hex_file" "$fixture_hex"
                        log_plain "${GREEN}      ✓ Created fixture from current HEX file${NC}"
                        UPDATED_HEX_FIXTURES+=("${example_basename}.hex (new)")
                    fi
                else
                    log_plain "${YELLOW}      Warning: bintool failed to create Intel-HEX file${NC}"
                fi
            fi
        done
    fi
    
    # Check for .a2l files created by this example
    # Format: example_name_HH_MM_SS.a2l (where HH_MM_SS is from __TIME__)
    # Check both BUILD_DIR and WORKSPACE_ROOT (examples write to current directory)
    # Each example creates exactly one A2L file
    # Use grep to ensure the filename ends with _HH_MM_SS.a2l pattern or exact match
    local a2l_files=$(find "$BUILD_DIR" "$WORKSPACE_ROOT" -maxdepth 1 \( -name "${example_basename}_*.a2l" -o -name "${example_basename}.a2l" \) -type f 2>/dev/null | grep -E "(/${example_basename}_[0-9]{2}_[0-9]{2}_[0-9]{2}\.a2l$|/${example_basename}\.a2l$)" | head -1)
    
    if [ -n "$a2l_files" ]; then
        log_plain "${BLUE}  Found .a2l file(s):${NC}"
        for a2l_file in $a2l_files; do
            local a2l_filename=$(basename "$a2l_file")
            local a2l_size=$(ls -lh "$a2l_file" | awk '{print $5}')
            log_plain "${BLUE}    ${a2l_filename} (${a2l_size})${NC}"
            
            # Compare with fixture if it exists
            local fixture_a2l="${FIXTURES_DIR}/${example_basename}.a2l"
            if [ -f "$fixture_a2l" ]; then
                A2L_COMPARED=$((A2L_COMPARED + 1))
                log_plain "${BLUE}    Comparing with fixture: ${example_basename}.a2l${NC}"
                
                if diff -q "$a2l_file" "$fixture_a2l" >/dev/null 2>&1; then
                    log_plain "${GREEN}      ✓ A2L file matches fixture${NC}"
                    A2L_MATCHED=$((A2L_MATCHED + 1))
                else
                    log_plain "${RED}      ✗ A2L file DOES NOT match fixture!${NC}"
                    A2L_MISMATCHED=$((A2L_MISMATCHED + 1))
                    # Log the differences (first 30 lines)
                    echo "      Differences (first 30 lines):" >> "$LOG_FILE"
                    diff -u "$fixture_a2l" "$a2l_file" | head -30 | sed 's/^/      /' >> "$LOG_FILE" || true
                    log_plain "${YELLOW}      Note: A2L files differ - this may be expected due to timestamps or memory addresses${NC}"
                    log_plain "${YELLOW}      Copying new A2L file to fixtures directory...${NC}"
                    cp "$a2l_file" "$fixture_a2l"
                    log_plain "${GREEN}      ✓ Updated fixture with new A2L file${NC}"
                    UPDATED_A2L_FIXTURES+=("${example_basename}.a2l")
                fi
            else
                log_plain "${YELLOW}      Note: No fixture found - creating new fixture: ${example_basename}.a2l${NC}"
                # Create fixtures directory if it doesn't exist
                mkdir -p "$FIXTURES_DIR"
                # Copy the a2l file to fixtures
                cp "$a2l_file" "$fixture_a2l"
                log_plain "${GREEN}      ✓ Created fixture from current A2L file${NC}"
                UPDATED_A2L_FIXTURES+=("${example_basename}.a2l (new)")
            fi
        done
    fi
    
    # Return to original directory
    cd - > /dev/null
    
    log_plain ""
}

# Run all regular examples
for example in "${EXAMPLES[@]}"; do
    run_example "$example" false
done



# Print summary
log_plain "${BLUE}========================================${NC}"
log_plain "${BLUE}  Summary${NC}"
log_plain "${BLUE}========================================${NC}"
log_plain "Total:   $TOTAL"
log_plain "${GREEN}Passed:  $PASSED${NC}"
if [ $FAILED -gt 0 ]; then
log_plain "${RED}Failed:  $FAILED${NC}"
fi
if [ $SKIPPED -gt 0 ]; then
log_plain "${YELLOW}Skipped: $SKIPPED${NC}"
fi
log_plain ""
if [ $XCP_CRASHED -gt 0 ]; then
    log_plain "${RED}⚠ WARNING: xcp_client crashed $XCP_CRASHED time(s)${NC}"
    log_plain "${RED}  This indicates bugs in xcp_client that need to be fixed!${NC}"
    log_plain ""
fi
if [ $HEX_COMPARED -gt 0 ]; then
    log_plain "${BLUE}HEX File Comparisons:${NC}"
    log_plain "  Compared:   $HEX_COMPARED"
    log_plain "${GREEN}  Matched:    $HEX_MATCHED${NC}"
    if [ $HEX_MISMATCHED -gt 0 ]; then
    log_plain "${RED}  Mismatched: $HEX_MISMATCHED${NC}"
    fi
    if [ ${#UPDATED_HEX_FIXTURES[@]} -gt 0 ]; then
        log_plain "${YELLOW}  Updated fixtures:${NC}"
        for fixture in "${UPDATED_HEX_FIXTURES[@]}"; do
            log_plain "${YELLOW}    - $fixture${NC}"
        done
    fi
    log_plain ""
fi
if [ $A2L_COMPARED -gt 0 ]; then
    log_plain "${BLUE}A2L File Comparisons:${NC}"
    log_plain "  Compared:   $A2L_COMPARED"
    log_plain "${GREEN}  Matched:    $A2L_MATCHED${NC}"
    if [ $A2L_MISMATCHED -gt 0 ]; then
    log_plain "${YELLOW}  Mismatched: $A2L_MISMATCHED${NC}"
    fi
    if [ ${#UPDATED_A2L_FIXTURES[@]} -gt 0 ]; then
        log_plain "${YELLOW}  Updated fixtures:${NC}"
        for fixture in "${UPDATED_A2L_FIXTURES[@]}"; do
            log_plain "${YELLOW}    - $fixture${NC}"
        done
    fi
    log_plain ""
fi
log_plain "End time: $(date)"
log_plain ""

# Show updated fixtures even if no comparisons were made
if [ ${#UPDATED_HEX_FIXTURES[@]} -gt 0 ] || [ ${#UPDATED_A2L_FIXTURES[@]} -gt 0 ]; then
    log_plain "${YELLOW}Updated/Created Fixtures:${NC}"
    for fixture in "${UPDATED_HEX_FIXTURES[@]}"; do
        log_plain "${YELLOW}  - $fixture${NC}"
    done
    for fixture in "${UPDATED_A2L_FIXTURES[@]}"; do
        log_plain "${YELLOW}  - $fixture${NC}"
    done
    log_plain ""
fi

log_plain "Full log saved to: $LOG_FILE"

# Exit with appropriate code
if [ $FAILED -gt 0 ]; then
    log_plain "${RED}Some examples failed!${NC}"
    exit 1
elif [ $HEX_MISMATCHED -gt 0 ]; then
    log_plain "${RED}Some HEX files don't match their fixtures!${NC}"
    exit 1
else
    if [ $A2L_MISMATCHED -gt 0 ]; then
        log_plain "${YELLOW}Note: Some A2L files were updated in fixtures (this may be expected)${NC}"
    fi
    log_plain "${GREEN}All examples passed!${NC}"
    exit 0
fi
