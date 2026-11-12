#pragma once

// multi_thread_demo - work interface
//
// Implement this interface to define the work which is distributed over the worker threads by main.c
// main.c is generic, it only knows the number of work items of a batch, as returned by work_begin_batch, and their indices
//
// Call sequence and threading contract:
//
//   work_init()                  main thread, once, after XCP and A2L initialization, before the worker threads are started
//   loop:
//     n = work_begin_batch(req)  main thread, no batch running, on request or cyclic with auto restart
//     work_execute(index)        worker threads, concurrently, called exactly once for each index 0..n-1
//     work_complete(n)           main thread, after work_execute returned for all indices of the batch
//   work_shutdown()              main thread, once, after the worker threads are joined
//
// Memory visibility:
//   Everything written by work_begin_batch is visible in work_execute, everything written by work_execute is visible in work_complete
//   (main.c publishes the batch with release/acquire semantics). Work items executed concurrently must not write to shared data
//   without their own synchronization, writing to data owned by the index (e.g. result[index]) is safe.

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#ifdef __cplusplus
extern "C" {
#endif

/// Initialize the work, create calibration segments, register parameters and measurements, allocate resources
/// @return false on error, the application terminates
bool work_init(void);

/// Begin a new batch of work: define the work items and prepare their input data
/// Called on an explicit request (params.start_request changed), or cyclic while no batch is running when params.auto_restart is set
/// The work decides whether there is something to do, e.g. only when its input or parameters changed
/// @param requested true on an explicit request, false when called by auto restart
/// @return Number of work items in this batch, 0 = nothing to do, the batch is not started
uint32_t work_begin_batch(bool requested);

/// Execute work item 'index' of the current batch
/// Called concurrently from multiple worker threads, each index exactly once
/// @param index Work item index 0..n-1, n returned by work_begin_batch
void work_execute(uint32_t index);

/// Called when all work items of the current batch are completed
/// @param items Number of work items of the batch
/// @return false if the batch result is not valid
bool work_complete(uint32_t items);

/// Release the resources of the work
void work_shutdown(void);

#ifdef __cplusplus
}
#endif
