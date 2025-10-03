# Target A2L name to generate in local machines CANape project folder
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo_created.a2l"

# Path to ELF file in local machine CANape project folder
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"



# Path to a2ltool executable
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to xcp_client tool executable
XCPCLIENT="../xcp-lite-RainerZ/target/debug/examples/xcp_client"


# Target (Raspberry Pi on 192.168.0.206) connection details
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-RainerZ/build/no_a2l_demo.out"



# Run target executable with XCP on Ethernet in background
echo ""
echo "----------------------------------------------------------------------"
echo "Starting target executable on Target ..."
echo "$TARGET_PATH"
echo ""
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
SSH_PID=$!
# Give target some time to initialize
sleep 2



# Create a A2L template with xcp_client by uploading memory segments and events via XCP on UDP
echo ""
echo "----------------------------------------------------------------------"
echo "Creating A2L file template by querying target via XCP..."
echo "$A2LFILE"
echo ""
$XCPCLIENT --log-level=3  --dest-addr=$TARGET_HOST:5555 --tcp --list-mea ".*" --list-cal ".*" --create-a2l --a2l $A2LFILE 



# Stop target - kill SSH process if it still exists, then kill remote process by name
echo ""
echo "----------------------------------------------------------------------"
echo "Stopping target executable on Target ..."
echo ""
if kill -0 $SSH_PID 2>/dev/null; then
    kill $SSH_PID
fi
# Also kill the remote process by name to be sure
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo.out" 2>/dev/null || true
# Give target some time to shutdown
sleep 1



# Download the target executable from Pi for local A2L processing
echo ""
echo "----------------------------------------------------------------------"
echo "Downloading target executable from Pi..."
echo "$ELFFILE"
echo ""
scp $TARGET_USER@$TARGET_HOST:$TARGET_PATH $ELFFILE



# @@@@ Does not work: creates TYPEDEF_MEASUREMENT instead of TYPEDEF_CHARACTERISTIC for params
# Update the A2L template with measurement 'counter' and characteristic 'params'
echo ""
echo "----------------------------------------------------------------------"
echo "Updating A2L file with measurement 'counter' and characteristic 'params'..."
echo "$A2LFILE"
echo ""
$A2LTOOL --verbose --update --measurement-regex "counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE $A2LFILE


echo ""
echo "----------------------------------------------------------------------"
echo "A2L file created: $A2LFILE"
echo "----------------------------------------------------------------------"