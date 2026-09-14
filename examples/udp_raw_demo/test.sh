#!/bin/bash

# A2L file creator for the udp_raw_demo example project

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# The script syncs the example project to the target, builds it there, runs it with XCP on Ethernet,
# downloads the ELF file and the A2L file to the local machine. 
# Prerequisites:
# - The target must be reachable via SSH and have rsync installed
# - The local machine must have rsync and scp installed
# - The local machine must have xcpclient installed


#======================================================================================================================
# Parameters
#======================================================================================================================

LOGFILE="$REPO_ROOT/examples/udp_raw_demo/CANape/udp_raw_demo.log"
#LOGFILE='/dev/stdout'
#LOGFILE="/dev/null"

# A2L file path on local machine
A2LFILE="$REPO_ROOT/examples/udp_raw_demo/CANape/udp_raw_demo.a2l"

# ELF file path on local machine
ELFFILE="$REPO_ROOT/examples/udp_raw_demo/CANape/udp_raw_demo.elf"

# Build type for target executable: Release, RelWithDebInfo or Debug
# RelWithDebInfo is default to demonstrate operation with with -O1 and NDEBUG
# Optimization level >= -O1 keeps variables in registers whenever possible, so these local variables cannot be measured
# Debug mode is the least efficient but keeps all variables and stack frames intact
BUILD_TYPE="RelWithDebInfo"
# -O0
#BUILD_TYPE="Debug"
# -O2 no debug symbols
#BUILD_TYPE="Release"

# Run a simple test calibration and measurement
TEST=true
#TEST=false


# Target connection details
#TARGET_USER="parallels"
#TARGET_HOST="10.211.55.4"
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-Test"
TARGET_BUILD_DIR="build-raw"
TARGET_BINARY="udp_raw_demo"
TARGET_IP="192.168.0.220"
TARGET_PORT="5555"

# Path to xcpclient tool executable (assuming cargo installed it to ~/.cargo/bin)
XCPCLIENT="xcpclient"


#======================================================================================================================
# Sync Target, Build Application on Target, Download ELF, Start ECU, ...
#======================================================================================================================


echo "========================================================================================================"
echo "A2L file creator for the udp_raw_demo example project"
echo "========================================================================================================"

mkdir -p "$(dirname "$LOGFILE")"
echo "Logging to $LOGFILE enabled"
echo "" > "$LOGFILE"


#======================================================================================================================
# Sync target
#======================================================================================================================

# Sync target
echo "Sync target ..."            
rsync -avz --delete \
    --include='/build.sh' \
    --include='/CMakeLists.txt' \
    --include='/cmake/***' \
    --include='/inc/***' \
    --include='/src/***' \
    --include='/examples/' \
    --include='/examples/udp_raw_demo/***' \
    --include='/examples/udp_raw_demo_cpp/***' \
    --exclude='*' \
    "$REPO_ROOT/" "$TARGET_USER@$TARGET_HOST:$TARGET_PATH/" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync with target"
    exit 1
fi


#======================================================================================================================
# Build on target 
#======================================================================================================================

echo "Build executable on Target ..."
ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_PATH && ./build.sh $BUILD_TYPE raw examples" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build on target"
    exit 1
fi




#======================================================================================================================
# Upload ELF and create A2L file
# Create the  A2L from ELF file with xcpclient tool
#======================================================================================================================

# Download the target executable for the local A2L generation process
#echo "Downloading ELF file from target $TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY to $ELFFILE ..."
#scp "$TARGET_USER@$TARGET_HOST:$TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY" "$ELFFILE" 1> /dev/null
#if [ $? -ne 0 ]; then
#    echo "❌ FAILED: Download $TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY"
#    exit 1
#fi

#echo ""
#echo "========================================================================================================"
#echo "Creating A2L file from XCPlite ELF file ..."
#echo "========================================================================================================"
#echo ""
#echo "Command: $XCPCLIENT --log-level=3 --verbose=0 --dest-addr=$TARGET_HOST --udp --offline --elf \"$ELFFILE\" --create-a2l --a2l \"$A2LFILE\""
#$XCPCLIENT --log-level=3 --verbose=0 --dest-addr=$TARGET_HOST --udp --offline --elf "$ELFFILE" --create-a2l --a2l "$A2LFILE" >> "$LOGFILE"
#if [ $? -ne 0 ]; then
#    echo "❌ FAILED: xcpclient returned error"
#    exit 1
#fi

#echo ""
#echo "✅ SUCCESS:"
#echo "Created a new A2L file $A2LFILE"
#echo ""


#======================================================================================================================
# Enable raw socket access for the target executable (requires root privileges)
# sudo setcap cap_net_raw+ep ./build-raw/udp_raw_demo
#======================================================================================================================

ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_PATH && sudo setcap cap_net_raw+ep ./$TARGET_BUILD_DIR/$TARGET_BINARY" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: setcap cap_net_raw+ep $TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY"
    exit 1
fi



#======================================================================================================================
# Test
#======================================================================================================================

if [ "$TEST" = true ]; then

# Start the target executable in the background
ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_PATH && ./$TARGET_BUILD_DIR/$TARGET_BINARY" &
SSH_PID=$!

# Wait until the target is actually serving, instead of a fixed sleep. It has to open the raw
# socket, read the interface MAC and bind, which takes longer than a normal socket bind, and a
# failure here (address in use, missing capability, wrong interface) would otherwise only show up
# as an xcpclient timeout further down.
# Note: pgrep/pkill -x matches the process NAME. Do NOT use -f here: it matches the full command
# line, and the ssh command line on the target contains "$TARGET_BINARY" itself, so -f would match
# the ssh session as well and terminate it.
echo "Waiting for $TARGET_BINARY to come up on $TARGET_IP ..."
ssh "$TARGET_USER@$TARGET_HOST" "for i in \$(seq 1 20); do pgrep -x $TARGET_BINARY > /dev/null && exit 0; sleep 0.5; done; exit 1"
if [ $? -ne 0 ]; then
    echo "❌ FAILED: $TARGET_BINARY is not running on the target"
    echo "   Check the interface name, that $TARGET_IP is free and not owned by the target kernel,"
    echo "   and that setcap cap_net_raw+ep was applied."
    exit 1
fi

echo "========================================================================================================"
echo "Connect and upload A2L"
echo "List measurements and calibrations"
TEST_FAILED=0
$XCPCLIENT --log-level=3 --dest-addr=$TARGET_IP:$TARGET_PORT --udp --upload-a2l --a2l "$A2LFILE" --list-mea . --list-cal .
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcpclient connect, A2L upload or variable listing"
    TEST_FAILED=1
fi

if [ $TEST_FAILED -eq 0 ]; then
echo "========================================================================================================"
echo "Test measurement"
echo "========================================================================================================"
$XCPCLIENT --log-level=2 --dest-addr=$TARGET_IP:$TARGET_PORT --udp --a2l "$A2LFILE"  --mea counter --time 2 --verbose 2
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcpclient measurement"
    TEST_FAILED=1
fi
fi

# Stop the target executable
# -x matches the process name exactly. -f would also match this very ssh command line, because it
# contains the binary name, and would terminate the ssh session instead of (or as well as) the demo.
ssh "$TARGET_USER@$TARGET_HOST" "pkill -x $TARGET_BINARY"
wait "$SSH_PID" 2>/dev/null

if [ $TEST_FAILED -ne 0 ]; then
    exit 1
fi
echo "✅ SUCCESS: udp_raw_demo test passed"

fi