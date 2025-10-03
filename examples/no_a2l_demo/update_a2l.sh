# Target A2L
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo_test1.a2l"

# Path to a2ltool
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to ELF file
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"

# Update existing A2L file with ELF file
$A2LTOOL --verbose  --strict --elffile  $ELFFILE  --enable-structures --update FULL --update-mode STRICT --output $A2LFILE $A2LFILE


