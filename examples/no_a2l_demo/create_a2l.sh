# !/bin/bash

# A2L file creator for the no_a2l_demo example project

# The script syncs the example project to the target, builds it there, runs it with XCP on Ethernet,
# downloads the ELF file to the local machine and creates an A2L file either with xcp_client or with a2ltool
# Prerequisites:
# - The target must be reachable via SSH and have rsync installed
# - The local machine must have rsync and scp installed
# - The local machine must have xcp_client and a2ltool compiled and available


#======================================================================================================================
# Parameters
#======================================================================================================================

LOGFILE="examples/no_a2l_demo/CANape/no_a2l_demo.log"
#LOGFILE='/dev/stdout'
#LOGFILE="/dev/null"

# Target A2L name to generate
# The A2L file is generated in the local machine CANape project directory (examples/no_a2l_demo/CANape/)
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo.a2l"
A2LFILE_TEMPLATE="examples/no_a2l_demo/CANape/no_a2l_demo.template.a2l"

# Path to ELF file in local machine CANape project folder
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo"

# Build type for target executable: Release, RelWithDebInfo or Debug
# RelWithDebInfo is default to demonstrate operation with with -O1 and NDEBUG
BUILD_TYPE="RelWithDebInfo"
# -O0
#BUILD_TYPE="Debug"
# -O2 no debug symbols
#BUILD_TYPE="Release"

# Release build will of course not work because debug symbols are removed

# Optimization level >= -O1 keeps variables in registers whenever possible, so local variables cannot be measured in any case
# The most efficient solution to keep local variables measurable is to use the DaqCapture macro, another option is mark the variable with volatile ot with the provided macro XCP_MEA
# Debug mode is the least efficient but keeps all variables and stack frames intact
# So far, the solution works well with -O1


# Connect to the target system possible
#ECU_ONLINE=true
ECU_ONLINE=true

# Connect to the target XCP server possible
# Start the XCP server on the target in background and stop it after A2L creation
#XCP_ONLINE=true
XCP_ONLINE=true

# Use the offline A2L creator and then update the A2L with the online A2L updater when the ECU is online
OFFLINE_A2L_CREATION_AND_FIX=false
#OFFLINE_A2L_CREATION_AND_FIX=true

# Target connection details
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-RainerZ/build/no_a2l_demo"

# Path to a2ltool executable
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to xcp_client tool executable
XCPCLIENT="../xcp-lite-RainerZ/target/debug/xcp_client"


# Run a simple measurement with the created A2L file to verify that measurements and characteristics are working
#TEST=true
TEST=false

#======================================================================================================================
# Sync Target, Build Application on Target, Download ELF, Start ECU, ...
#======================================================================================================================


echo "========================================================================================================"
echo "A2L file creator for the no_a2l_demo example project"
echo "========================================================================================================"

echo "Logging to $LOGFILE enabled"
echo "" > $LOGFILE

#---------------------------------------------------
if [ $ECU_ONLINE == true ]; then

# Sync target
echo "Sync target ..."            
rsync -avz --delete --exclude=build/ --exclude=.git/ --exclude="*.o" --exclude="*.a" ./ rainer@192.168.0.206:~/XCPlite-RainerZ/ 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync with target"
    exit 1
fi


# Build on target
echo "Build executable on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && ./build.sh $BUILD_TYPE" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build on target"
    exit 1
fi


# Download the target executable for the local A2L generation process
echo "Downloading executable from target $TARGET_PATH to $ELFFILE ..."
scp $TARGET_USER@$TARGET_HOST:$TARGET_PATH $ELFFILE 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Download $TARGET_PATH"
    exit 1
fi
cp $ELFFILE ../xcp-lite-RainerZ/fixtures/no_a2l_demo


# Run target executable with XCP on Ethernet in background#
if [ $XCP_ONLINE == true ]; then
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo" 1> /dev/null
echo ""
echo "Starting executable $TARGET_PATH on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
SSH_PID=$!
sleep 2
fi

fi
# ECU_ONLINE
#---------------------------------------------------


#======================================================================================================================
# Create A2L file
#======================================================================================================================

# Create a A2L template offline with xcp_client by uploading memory segments and events via XCP

#---------------------------------------------------
# One step
if [ $OFFLINE_A2L_CREATION_AND_FIX == false ]; then

echo ""
echo "========================================================================================================"
echo "Creating A2L file in online mode via XCP event and segment information and from XCPlite ELF file ..."
echo "========================================================================================================"
echo ""
$XCPCLIENT --log-level=3  --verbose=5 --dest-addr=$TARGET_HOST --tcp --elf $ELFFILE  --a2l $A2LFILE --create-a2l >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi

#---------------------------------------------------
# Two step
else


echo ""
echo "========================================================================================================"
echo "Creating A2L file template from XCPlite ELF file ..."
echo "========================================================================================================"
echo ""
$XCPCLIENT --log-level=3 --verbose=0 --dest-addr=$TARGET_HOST --tcp --offline --elf $ELFFILE  --create-a2l --a2l $A2LFILE_TEMPLATE  >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi

if [ $XCP_ONLINE == true ]; then

cp $A2LFILE_TEMPLATE $A2LFILE
echo ""
echo "========================================================================================================"
echo "Fixing A2L file with XCP server information to correct event ids, segment numbers and IF_DATA IP address ..."
echo "========================================================================================================"
echo ""
$XCPCLIENT --log-level=3 --verbose=0 --dest-addr=$TARGET_HOST --tcp --elf $ELFFILE   --a2l $A2LFILE  --fix-a2l >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi

fi


fi
# OFFLINE_A2L_CREATION_AND_FIX
#---------------------------------------------------


#======================================================================================================================
# Shutdown
#======================================================================================================================


echo ""
echo ""


if [ $ECU_ONLINE == true ]; then
if [ $XCP_ONLINE == true ]; then

# Stop target - kill SSH process if it still exists, then kill remote process by name
echo "Stopping target executable $SSH_PID on Target ..." 
if kill -0 $SSH_PID 2>/dev/null; then
    kill $SSH_PID
fi
# Also kill the remote process by name to be sure
# ssh rainer@192.168.0.206  "pkill -f no_a2l_demo" 
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo" 2>/dev/null || true
# Give target some time to shutdown
sleep 1

fi
fi

echo ""
echo "✅ SUCCESS:"
echo "Created a new A2L file $A2LFILE"
echo ""


#======================================================================================================================
# Test
#======================================================================================================================

if [ $TEST == true ]; then


echo "========================================================================================================"
echo "Test measurement"
echo "========================================================================================================"

echo "Start a test measurement"
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
sleep 1
$XCPCLIENT --log-level=3  --dest-addr=$TARGET_HOST:5555 --tcp  --a2l $A2LFILE  --mea ".*" --time-ms 100
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo" 


fi