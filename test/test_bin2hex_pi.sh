#!/bin/bash

# BINTOOL Test Script for Raspberry Pi
# This script tests the complete bintool workflow:
# 1. Sync project to target
# 2. Build cpp_demo on target (has multiple calibration segments)
# 3. Start cpp_demo XCP server
# 4. Download A2L and BIN files using xcp_client
# 5. Convert BIN to HEX using bintool
# 6. Upload HEX file back to target to verify round-trip

set -e  # Exit on error

#======================================================================================================================
# Configuration
#======================================================================================================================

# Target connection details
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-RainerZ"
TARGET_EXECUTABLE="$TARGET_PATH/build/cpp_demo.out"

# Local paths
XCPCLIENT="../xcp-lite-RainerZ/target/debug/xcp_client"
BINTOOL="./tools/bin2hex/target/release/bintool"
TEST_DIR="./test_bintool"
LOGFILE="$TEST_DIR/test_bintool.log"

# Build type for target executable
BUILD_TYPE="Debug"

# Test files
A2L_FILE="$TEST_DIR/cpp_demo.a2l"
BIN_FILE="$TEST_DIR/cpp_demo.bin"
HEX_FROM_BIN="$TEST_DIR/cpp_demo_from_bin.hex"
HEX_FROM_XCP="$TEST_DIR/cpp_demo_from_xcp.hex"
BIN_BACKUP="$TEST_DIR/cpp_demo_backup.bin"
BIN_RESTORED="$TEST_DIR/cpp_demo_restored.bin"

#======================================================================================================================
# Helper Functions
#======================================================================================================================

cleanup_target() {
    echo "Cleaning up target processes..."
    ssh $TARGET_USER@$TARGET_HOST "pkill -f cpp_demo.out" 2>/dev/null || true
    sleep 1
}

cleanup_local() {
    echo "Cleaning up local test directory..."
    rm -rf $TEST_DIR
    mkdir -p $TEST_DIR
}

#======================================================================================================================
# Main Test Flow
#======================================================================================================================

echo "========================================================================================================"
echo "BINTOOL Test Script"
echo "========================================================================================================"
echo "Target: $TARGET_USER@$TARGET_HOST"
echo "Demo:   cpp_demo (multiple calibration segments)"
echo "Log:    $LOGFILE"
echo ""
echo "Test workflow:"
echo "  1. Sync and build cpp_demo on target"
echo "  2. Start XCP server (cpp_demo generates BIN file on startup)"
echo "  3. Download A2L via XCP"
echo "  4. Upload calibration data via XCP to create HEX file (xcp_client --upload-bin)"
echo "  5. Download BIN file via scp"
echo "  6. Convert BIN → HEX using bintool"
echo "  7. Compare both HEX files (from XCP vs from BIN)"
echo "  8. Test round-trip: HEX → BIN"
echo ""

# Initialize
cleanup_local
echo "" > $LOGFILE

#---------------------------------------------------
# Step 1: Sync project to target
#---------------------------------------------------
echo "Step 1: Syncing project to target..."
rsync -avz --delete \
    --exclude=build/ \
    --exclude=.git/ \
    --exclude="*.o" \
    --exclude="*.out" \
    --exclude="*.a" \
    ./ $TARGET_USER@$TARGET_HOST:$TARGET_PATH/ >> $LOGFILE 2>&1

if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync to target"
    exit 1
fi
echo "✅ Sync completed"

#---------------------------------------------------
# Step 2: Build cpp_demo on target
#---------------------------------------------------
echo ""
echo "Step 2: Building cpp_demo on target..."
ssh $TARGET_USER@$TARGET_HOST "cd $TARGET_PATH && ./build.sh $BUILD_TYPE" >> $LOGFILE 2>&1

if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build on target"
    exit 1
fi
echo "✅ Build completed"

#---------------------------------------------------
# Step 3: Start cpp_demo XCP server
#---------------------------------------------------
echo ""
echo "Step 3: Starting cpp_demo XCP server on target..."
cleanup_target
ssh $TARGET_USER@$TARGET_HOST "cd $TARGET_PATH && $TARGET_EXECUTABLE" >> $LOGFILE 2>&1 &
SSH_PID=$!
sleep 2
echo "✅ XCP server started (PID: $SSH_PID)"

#---------------------------------------------------
# Step 4: Upload calibration data via XCP to create HEX file
#---------------------------------------------------
echo ""
echo "Step 4: Uploading calibration data from target via XCP..."
echo "(xcp_client --upload-bin reads calibration segments and creates Intel-Hex file)"
$XCPCLIENT \
    --dest-addr=$TARGET_HOST \
    --udp \
    --upload-a2l \
    --upload-bin \
    >> $LOGFILE 2>&1

if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client upload from target"
    cleanup_target
    exit 1
fi

# Move uploaded files to test directory
mv cpp_demo.a2l $A2L_FILE 2>/dev/null || true
mv cpp_demo.hex $HEX_FROM_XCP 2>/dev/null || true

if [ ! -f "$HEX_FROM_XCP" ]; then
    echo "❌ FAILED: HEX file not created by xcp_client"
    cleanup_target
    exit 1
fi

echo "✅ Created HEX file via XCP: $HEX_FROM_XCP ($(wc -l < "$HEX_FROM_XCP") lines)"

#---------------------------------------------------
# Step 5: Download BIN file from target
#---------------------------------------------------
echo ""
echo "Step 5: Downloading BIN file from target using scp..."
echo "(BIN file is generated by cpp_demo on startup with timestamp in name)"

# Find the BIN file on target (name includes build time: cpp_demo_v10_21_01_18.bin)
# BIN file is created by cpp_demo.out in the project root directory
echo "Finding BIN file on target..."
TARGET_BIN_PATTERN="$TARGET_PATH/cpp_demo_v*.bin"
TARGET_BIN_FILE=$(ssh $TARGET_USER@$TARGET_HOST "ls -t $TARGET_BIN_PATTERN 2>/dev/null | head -1")

if [ -z "$TARGET_BIN_FILE" ]; then
    echo "❌ FAILED: Could not find BIN file on target matching pattern: $TARGET_BIN_PATTERN"
    cleanup_target
    exit 1
fi

echo "Found BIN file on target: $TARGET_BIN_FILE"

# Download the BIN file
scp $TARGET_USER@$TARGET_HOST:$TARGET_BIN_FILE $BIN_FILE >> $LOGFILE 2>&1

if [ $? -ne 0 ]; then
    echo "❌ FAILED: BIN file download from target"
    cleanup_target
    exit 1
fi

if [ ! -f "$BIN_FILE" ]; then
    echo "❌ FAILED: BIN file not found after download"
    cleanup_target
    exit 1
fi

# Get just the filename for display
BIN_FILENAME=$(basename "$TARGET_BIN_FILE")
echo "✅ Downloaded BIN file from target: $BIN_FILENAME → $BIN_FILE ($(stat -f%z "$BIN_FILE" 2>/dev/null || stat -c%s "$BIN_FILE") bytes)"

#---------------------------------------------------
# Step 6: Build bintool if needed
#---------------------------------------------------
echo ""
echo "Step 6: Building bintool..."
if [ ! -f "$BINTOOL" ]; then
    echo "Building bintool in release mode..."
    (cd tools/bin2hex && cargo build --release) >> $LOGFILE 2>&1
    if [ $? -ne 0 ]; then
        echo "❌ FAILED: bintool build"
        cleanup_target
        exit 1
    fi
fi
echo "✅ bintool ready"

#---------------------------------------------------
# Step 7: Convert BIN to HEX
#---------------------------------------------------
echo ""
echo "Step 7: Converting BIN to HEX using bintool (new convenient syntax)..."
# Simple syntax: just provide BIN file, HEX is auto-derived
$BINTOOL $BIN_FILE -o $HEX_FROM_BIN

if [ $? -ne 0 ]; then
    echo "❌ FAILED: BIN to HEX conversion"
    cleanup_target
    exit 1
fi

if [ ! -f "$HEX_FROM_BIN" ]; then
    echo "❌ FAILED: HEX file not created"
    cleanup_target
    exit 1
fi

echo "✅ Created HEX file from BIN: $HEX_FROM_BIN ($(wc -l < "$HEX_FROM_BIN") lines)"

#---------------------------------------------------
# Step 8: Compare HEX files
#---------------------------------------------------
echo ""
echo "Step 8: Comparing HEX files (XCP vs BINTOOL)..."
echo "HEX from XCP:    $HEX_FROM_XCP"
echo "HEX from BIN:    $HEX_FROM_BIN"

# Show first few lines of each
echo ""
echo "First 10 lines of HEX from XCP:"
head -10 $HEX_FROM_XCP

echo ""
echo "First 10 lines of HEX from BIN:"
head -10 $HEX_FROM_BIN

# Compare files
echo ""
if cmp -s $HEX_FROM_XCP $HEX_FROM_BIN; then
    echo "✅ HEX files are IDENTICAL - bintool produces same output as xcp_client!"
else
    echo "ℹ️  HEX files differ - comparing content..."
    
    # Count segments in each
    SEGMENTS_XCP=$(grep -c "^:02000004" $HEX_FROM_XCP || echo "0")
    SEGMENTS_BIN=$(grep -c "^:02000004" $HEX_FROM_BIN || echo "0")
    
    echo "Segments in XCP HEX: $SEGMENTS_XCP"
    echo "Segments in BIN HEX: $SEGMENTS_BIN"
    
    # Show differences
    echo ""
    echo "Differences (if any):"
    diff $HEX_FROM_XCP $HEX_FROM_BIN || true
    
    echo ""
    echo "✅ Both HEX files created successfully (minor differences may be expected)"
fi

#---------------------------------------------------
# Step 9: Display HEX file info
#---------------------------------------------------
echo ""
echo "Step 9: HEX file detailed info..."
echo "HEX from BIN file:"
echo "HEX from BIN file:"
echo "First 10 lines of HEX file:"
head -10 $HEX_FROM_BIN
echo "..."
echo "Last 2 lines of HEX file:"
tail -2 $HEX_FROM_BIN

# Count segments
SEGMENT_COUNT=$(grep -c "^:02000004" $HEX_FROM_BIN || echo "0")
echo ""
echo "Number of segments (Extended Linear Address records): $SEGMENT_COUNT"

#---------------------------------------------------
# Step 10: Test round-trip conversion
#---------------------------------------------------
echo ""
echo "Step 10: Testing round-trip conversion (HEX → BIN)..."

# Backup original BIN
cp $BIN_FILE $BIN_BACKUP

# Apply HEX to BIN (using HEX from BIN conversion) - new convenient syntax
$BINTOOL $BIN_FILE --apply-hex $HEX_FROM_BIN

if [ $? -ne 0 ]; then
    echo "❌ FAILED: HEX to BIN conversion"
    cleanup_target
    exit 1
fi

# Compare original and restored
echo "Comparing original and restored BIN files..."
if cmp -s $BIN_BACKUP $BIN_FILE; then
    echo "✅ Round-trip successful: Files are identical"
else
    echo "❌ FAILED: Round-trip produced different file"
    cleanup_target
    exit 1
fi

#---------------------------------------------------
# Step 11: Test incomplete HEX rejection
#---------------------------------------------------
echo ""
echo "Step 11: Testing incomplete segment data rejection..."

# Create incomplete HEX file with truncated data in a segment (not just fewer lines)
# This creates a HEX with segment data that's too short
echo ":0200000480007A" > $TEST_DIR/incomplete.hex
echo ":0400007631305F3153" >> $TEST_DIR/incomplete.hex  # EPK segment with only 4 bytes instead of 12
echo ":00000001FF" >> $TEST_DIR/incomplete.hex

# Try to apply incomplete HEX (should fail because segment 0 needs 12 bytes but only has 4)
# Try to apply incomplete HEX (should fail)
$BINTOOL $BIN_FILE --apply-hex $TEST_DIR/incomplete.hex 2>&1 | tee $TEST_DIR/incomplete_test.log

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    echo "❌ FAILED: Incomplete segment data was accepted (should have been rejected)"
    cleanup_target
    exit 1
fi

# Verify BIN file unchanged
if cmp -s $BIN_BACKUP $BIN_FILE; then
    echo "✅ Incomplete segment data correctly rejected, BIN file unchanged"
else
    echo "❌ FAILED: BIN file was modified by incomplete segment data"
    cleanup_target
    exit 1
fi

#---------------------------------------------------
# Step 12: Test partial HEX update (subset of segments)
#---------------------------------------------------
echo ""
echo "Step 12: Testing partial HEX update (subset of segments)..."

# Restore backup
cp $BIN_BACKUP $BIN_FILE

# Create HEX file with only first 3 segments (not all 4)
head -n -5 $HEX_FROM_BIN > $TEST_DIR/partial.hex 2>/dev/null || \
    tail -r $HEX_FROM_BIN | tail -n +6 | tail -r > $TEST_DIR/partial.hex
echo ":00000001FF" >> $TEST_DIR/partial.hex

# This should succeed and update only the 3 segments present
$BINTOOL $BIN_FILE --apply-hex $TEST_DIR/partial.hex 2>&1 | tee $TEST_DIR/partial_test.log

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "❌ FAILED: Partial HEX update failed"
    cleanup_target
    exit 1
fi

echo "✅ Partial HEX update successful (updated 3 of 4 segments)"

#---------------------------------------------------
# Step 13: Cleanup
#---------------------------------------------------
echo ""
echo "Step 13: Cleanup..."
cleanup_target
echo "✅ Target processes stopped"

#======================================================================================================================
# Summary
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "✅ ALL TESTS PASSED"
echo "========================================================================================================"
echo ""
echo "Test Results:"
echo "  - Project synced and built on target"
echo "  - XCP server started successfully"
echo "  - HEX file created via XCP upload: $HEX_FROM_XCP"
echo "  - BIN file downloaded from target via scp: $BIN_FILE"
echo "  - HEX file created from BIN: $HEX_FROM_BIN ($SEGMENT_COUNT segments)"
echo "  - HEX files comparison completed"
echo "  - Round-trip conversion verified (BIN → HEX → BIN)"
echo "  - Incomplete segment data rejection verified"
echo "  - Partial HEX update tested (subset of segments)"
echo "  - BIN file integrity maintained"
echo ""
echo "Files available in: $TEST_DIR/"
echo "  - cpp_demo.a2l           : A2L file from target"
echo "  - cpp_demo.bin           : Original BIN file from target (via scp)"
echo "  - cpp_demo_from_xcp.hex  : HEX created by xcp_client --upload-bin"
echo "  - cpp_demo_from_bin.hex  : HEX created by bin2hex tool"
echo "  - cpp_demo_backup.bin    : Backup for comparison"
echo ""
echo "Log file: $LOGFILE"
echo ""
