# Target A2L name to generate in local machines CANape project folder
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo.a2l"

# Path to ELF file in local machine CANape project folder
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"


# Path to a2ltool executable
# A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to xcp_client tool executable
XCPCLIENT="../xcp-lite-RainerZ/target/debug/xcp_client"


# Target (Raspberry Pi on 192.168.0.206) connection details
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-RainerZ/build/no_a2l_demo.out"


# Sync target
echo ""
echo "----------------------------------------------------------------------"
echo "Sync target ..."            
rsync -avz --delete --exclude=build/ --exclude=.git/ --exclude="*.o" --exclude="*.out" --exclude="*.a" ./ rainer@192.168.0.206:~/XCPlite-RainerZ/


# Build on target
# Stop if failed
echo ""
echo "----------------------------------------------------------------------"
echo "Build executable on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && ./build.sh"
if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi


# Download the target executable for local A2L generation
echo ""
echo "----------------------------------------------------------------------"
echo "Downloading executable $ELFFILE ..."
echo ""
scp $TARGET_USER@$TARGET_HOST:$TARGET_PATH $ELFFILE




# Run target executable with XCP on Ethernet in background
echo ""
echo "----------------------------------------------------------------------"
echo "Starting executable on Target ..."
echo "$TARGET_PATH"
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
SSH_PID=$!
echo $SSH_PID
echo ""
# Give target some time to initialize
sleep 2




# Create a A2L template with xcp_client by uploading memory segments and events via XCP
echo ""
echo "----------------------------------------------------------------------"
echo "Creating A2L file template by querying target via XCP..."
echo "$A2LFILE"
echo ""
$XCPCLIENT --log-level=3  --dest-addr=$TARGET_HOST:5555 --tcp --elf $ELFFILE  --create-a2l --a2l $A2LFILE 
# Stop if failed
if [ $? -ne 0 ]; then
    echo "xcp_client failed"
    exit 1
fi







# Use a2ltool to update the A2L template with measurement 'counter' and characteristic 'params'
# @@@@ Does not work: creates TYPEDEF_MEASUREMENT instead of TYPEDEF_CHARACTERISTIC for params
#echo ""
#echo "----------------------------------------------------------------------"
#echo "Updating A2L file with measurement 'counter' and characteristic 'params'..."
#echo "$A2LFILE"
#echo ""
#$A2LTOOL --verbose --update --measurement-regex "counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE $A2LFILE




#echo "Start a test measurement"
#$XCPCLIENT --log-level=3  --dest-addr=$TARGET_HOST:5555 --tcp  --a2l $A2LFILE  --mea ".*" --time-ms 100





# Stop target - kill SSH process if it still exists, then kill remote process by name
echo ""
echo "----------------------------------------------------------------------"
echo "Stopping target executable $SSH_PID on Target ..."
echo ""
if kill -0 $SSH_PID 2>/dev/null; then
    kill $SSH_PID
fi
# Also kill the remote process by name to be sure
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo.out" 2>/dev/null || true
# Give target some time to shutdown
sleep 1



