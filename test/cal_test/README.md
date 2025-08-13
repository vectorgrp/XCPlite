# Calibration Segment Multi-Threading Test (cal_test)

This test demonstrates and validates multi-threaded access to XCP calibration segments using the xcplib library.

## Purpose

The test creates multiple threads that concurrently access a shared calibration segment created with `CreateCalSeg()`. This validates:

- Thread-safe access to calibration parameters
- Lock-free performance characteristics
- Concurrent read/write operations
- A2L file generation with multi-threaded measurements
