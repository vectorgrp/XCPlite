# XCPlite Daemon

This application serves as a daemon for multi application measurement and calibration use cases:  

- Just another XCP instrumented application in SHM mode, but is configured to be the only XCP server (XCP_MODE_SHM_SERVER). 
- Creates the master A2L file and manages the binary calibration data persistence file. 
- Must not be started first. 
- It has own measurement and calibration objects to monitor the system and multiple XCP /SHM instrumented applications.  


