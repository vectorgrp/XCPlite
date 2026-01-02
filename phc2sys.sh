
# Copy timestamps from enp4s0 to system clock and to enp5s0
sudo phc2sys -s enp4s0 -c enp5s0 -m -l 7 -O 0

