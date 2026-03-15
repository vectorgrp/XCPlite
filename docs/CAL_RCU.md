# XCPlite memory-safe, lock-less and wait-free Calibration Data Access

This document contains advanced technical information about XCPlite's lock-less and wait-free calibration implementation.  

The base implementation is not specific to XCP, and can be used in other contexts as well, where similar requirements for memory-safe, lock-less and wait-free modification of software parameters exist.  
Examples may be a REST API, a web server, test stimulation or a shared memory interface.  

This particular implementation shows how to implement XCP specific features (like page switching, freeze, copy, init, atomic transactions for consistency, ...) on top of the basic lock-less and wait-free calibration access pattern.

The terms calibration segment and calibration block used in this document have basically the same meaning.
A calibration memory segment is a calibration memory block that represents an XCP/A2L MEMORY_SEGMENT and implements its XCP specific functionality.  


## Functional Overview:

Precondition for this concept is, that there is only one writer thread (e.g. the XCP command server thread), but multiple consumer threads using the same calibration parameters.  

Access to calibration parameter blocks in the ECU application threads must be guarded with the lock-free and wait-free functions:

- XcpLockCalSeg(handle)
- XcpUnlockCalSeg(handle)

This assures memory-safe access to the calibration parameters.  


Creation and registration (in a global registration list) of calibration parameter blocks and segments by the application threads is done with the following functions:

- handle = XcpCreateCalSeg
- handle = XcpCreateCalBlk
- handle = XcpCreateCalSegPreloaded

Creation is lock-free, but not wait-free (there is a CAS loop for a bump memory allocator).  
Duplicate names are not allowed, the registry just returns the existing block.  

The implicit one time registration of a new calibration segment handle in a global registry is protected by a mutex.  
Registration is required for XCP, because the server implements operations that iterate over all calibration segments and needs it for automatic A2L generation.    
XcpCreateCalSegPreloaded is optionally used to pre create and load default calibration data from a non volatile memory or file system (e.g. from XCPlite .BIN file).   
Remember, the difference between XcpCreateCalSeg and XcpCreateCalBlk is, that the first one creates an XCP/A2L MEMORY_SEGMENT with all related XCP features, like page switching, freeze, copy, init, ...  


The calibration server operations needed to implement typical XCP memory segment handling are:  
- XcpCalSegSetCalPage - for reference/working page switching
- XcpCalSegGetCalPage
- XcpCalSegCopyCalPage - for copying the reference to working page
- XcpSetCalSegMode - for freeze request
- XcpGetSegInfo - for query of segment information
- XcpGetSegPageInfo
- XcpGetCalSegMode


Functions to access the calibration segment data from the server side are:  
- XcpCalSegWriteMemory
- XcpCalSegReadMemory
- XcpCalSegBeginAtomicTransaction
- XcpCalSegEndAtomicTransaction

An atomic transaction is a lock-less, wait-free global operation, which does not have any runtime costs.  
Calibration updates from the single threaded writer just get delayed and collected in each segments writer page, until the transaction is ended.  
Therefore there is no need for any other, finer grained locking or synchronization mechanism for calibration updates.



## Compromises made in this RCU algorithm for calibration segment updates:

To achieve memory-safe, lock-less and wait-free access to calibration parameters in the ECU application threads, the writer thread (in any process) has to accept the following compromises:

1. Exactly one writer thread allowed (e.g. the XCP command server thread) !!.  

2. A segment write operation will become visible to the application earliest in the second first nesting level XcpLockCalSeg after the XcpCalSegWriteMemory !!.  
('second' in the definition of sequentiality of this implementation).  

3. Visibility delays of calibration updates under contention may be non deterministic !!.  
If there are additional writes to the calibration block before the second lock (which is normally rare), these writes are accumulated and are not applied sequentially.  
To flush these additional changes, the function XcpPublishAll must be called explicitly.  
This could be done non-blocking in a cyclic way or blocking on demand, even each time a parameter change took place.  
In non-blocking mode, calibration changes become visible, when no other application thread holds the lock, which is non deterministic!!!     
The alternative approach of doing blocking mode calls to XcpPublishAll after each write operation, would occasionally delay the protocol command responses by a non-deterministic amount of time, which might be acceptable in some use cases.  

4. Calibration updates may theoretically starve, when there is always at least one reader holding a lock or the lock rate is very low.  
Worst case is, that a write operation may time out.

5. The lazy, non-blocking approach has the drawback, that calibration changes are acknowledged to the writer, before they become visible to the application threads (see 2.).  
But there is no risk for failure after acknowledge.  

6. Registration and creation of calibration segments is protected by a mutex, to share a consistent state of the calibration segment list among threads.  

7.  Each calibration block needs a header of 64 bytes, plus 4 times the page size.  
Page size is rounded up to 64 bit alignment.  
So to calibrate a block of N bytes, we need 64+4*(8*N+7)/8 bytes of memory.

8. Lazy calibration updates need a background processing in the same thread which handles XCP commands! It was quite difficult in XCPlite to provide platform abstraction for it, when doing it with blocking sockets and SO_RCVTIMEO. Maybe a better approach would be to use non blocking sockets and a waitable event.  


## RCU algorithm pseudo code for calibration segment updates:

Here is the used RCU algorithm as pseudo code:

There are only 3 RCU pages needed, named as follows:
  1. ecu_page - current reader state
  2. xcp_page - current writer state
  3. free_page - for swapping pages

There is no non-deterministic, number of application threads related amount of memory needed, like in normal RCU implementations !!

The variables lock_count, ecu_page_next and free_page are atomic pointers or memory offsets.
Keep in mind, that the code needs correct acquire/release relations on ecu_page_next and free_page to work on ARM and of course for atomic increment of lock_count !!!
Also note, that all calibration segment state is located in the same cache line.  

This algorithm can be treated as a RCU like pattern with exactly one element (free_page) in its memory reclamation list.
When there is no free_page, calibration changes just get collected in the xcp_page.  
This is used to realize calibration consistency requirements, were the collection is controlled with a begin/end atomic transaction pattern, which is not shown here.  


```
    Shared mutable atomic state between the XCP thread and the ECU application threads is:
        - ecu_page_next: a page with newer data, taken over into ecu_page 
        - free_page: the page freed when new_page is taken over 
        - ecu_access: 0 - ecu_page (RAM, working page) active, 1 - default_page access mode (FLASH, default page) active
        
    Shared mutable atomic state between the ECU application threads is:
        - lock_count: number of locks on this segment


// Multithreaded lock
function lock(segment) {
    if (lock_count++ (atomic) == 0 }  
      if (ecu_page != ecu_page_next (acquire) ) { // Need to update ecu_page
          
          // The ecu_page (which now becomes the free_page) might be used by some other thread, since we got the lock==0 on this segment, free page is not safe yet, set hazard flag
          free_page_hazard = true; // Non atomic, guarded by the ecu_page release store on weak memory models
          free_page = ecu_page;
          ecu_page (release) = ecu_page_next;

      } else {
          free_page_hazard = false; // There was no other lock and no need for update, free page must be safe now (unsynchronized conservative reset)
      }
    }
    return ecu_page;
 }

// Multithreaded unlock
function unlock(segment) {
    lock_count--;
}

// Single threaded write
function write(segment, offset, data[]) {

    // Update data in the current xcp page
    xcp_page[offset] = data;

    // Calibration page RCU
    // If updates are hold back for consistency, (begin/end atomic calibration)  we do not update the ECU page yet
    if (!consistency_hold) {
        try_publish(segment);
    }
}

// Single threaded publish
// Tries to publish pending changes collected in the xcp_page to the ECU application, returns true on success, when there was a free page available
// Success means, that the changes will become visible in the future locks of the ECU application threads
// Called cyclically to try to make progress in publishing pending calibration changes. 
// It is also called blocking until success, when consistency hold (end transaction) is released.
function try_publish(segment) -> bool {
   
    // Try allocate a new xcp page
    if (free_page (acquire) == NULL || free_page_hazard ) {
        return false  // No free page available yet
    }

    // Allocate the free page
    xcp_page_new = free_page;
    free_page = NULL;

    // Copy old xcp page to the new xcp page
    xcp_page_old = xcp_page;
    memcpy(xcp_page_new, xcp_page_old);
    xcp_page = xcp_page_new;

    // Update the old xcp page
    ecu_page_next = xcp_page_old;
    return true;
}

```

## Test Results:

There is a test application in the XCPlite repository, which creates multiple threads reading from a shared calibration block.  
The writer thread updates the calibration block with a certain pattern and the reader threads check for consistency and measure the visibility of the changes. The writer produces a mix of single writes and atomic transactions, to test the consistency requirements of the implementation.

The test application is located in the test/cal_test folder.
  
Test parameters are defined in the test/cal_test/src/main.cpp file:  
- TEST_THREAD_COUNT: Number of threads writing to the same calibration block, default is 4.
- TEST_WRITE_COUNT: Overall number of writes by the writer thread, default is 10000.
- TEST_MAIN_LOOP_DELAY_US: Main loop delay of the writer thread in microseconds, default is 100.
- TEST_ATOMIC_CAL: If defined, every N writes are done in an atomic transaction, default is 10.
- TEST_TASK_LOOP_DELAY_US: Task loop delay of the reader threads in microseconds, default is 100.
- TEST_TASK_LOCK_DELAY_US: Task lock delay in the reader threads in microseconds, the time the lock is hold, default is 0 = off.
- TEST_DATA_SIZE: Size of the test data in bytes, default is 8.
  
The test application measures the total number of writes, atomic writes, reads and changes observed by the reader threads.  
The test application also measures the duration of the lock operation and creates a histogram of the lock durations.  

The test result for the default parameters is:

On MacBook Pro M3:
```
Final Statistics:
===========================================================

Test parameters:
TEST_WRITE_COUNT = 10000
TEST_THREAD_COUNT = 4
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = ON
TEST_TASK_LOOP_DELAY_US = 100
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 100
TEST_DATA_SIZE = 8

Thread 0: reads=13360, changes=9284, avg_time=0.12us, max_time=5.08us
Thread 1: reads=13360, changes=9268, avg_time=0.14us, max_time=25.29us
Thread 2: reads=13360, changes=9258, avg_time=0.13us, max_time=25.71us
Thread 3: reads=13361, changes=9272, avg_time=0.14us, max_time=10.17us

Total Results:
  Total writes: 10000
  Total atomic writes: 1000
  Total reads: 53441
  Total changes observed: 37082 (69.4%)
  Total writes pending: 136
  Total publish all count: 1001
  Total errors: 0
  Average lock time: 0.13 us
  Maximum lock time: 25.71 us

Producer acquire lock time statistics:
  count=53441  max=25709ns  avg=132ns

Lock time histogram (53441 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                         2    0.00%  
  40-80ns                     4116    7.70%  #######
  80-120ns                   15014   28.09%  #########################
  120-160ns                  17386   32.53%  ##############################
  160-200ns                  10999   20.58%  ##################
  200-240ns                   3543    6.63%  ######
  240-280ns                   1243    2.33%  ##
  280-320ns                    597    1.12%  #
  320-360ns                    222    0.42%  
  360-400ns                     71    0.13%  
  400-600ns                     76    0.14%  
  600-800ns                     50    0.09%  
  800-1000ns                    54    0.10%  
  1000-1500ns                   41    0.08%  
  1500-2000ns                    6    0.01%  
  2000-3000ns                    2    0.00%  
  3000-4000ns                    1    0.00%  
  4000-6000ns                    5    0.01%  
  6000-8000ns                    7    0.01%  
  8000-10000ns                   1    0.00%  
  10000-20000ns                  3    0.01%  
  20000-40000ns                  2    0.00%  
```


Raspberry Pi5
```
Final Statistics:
===========================================================

Test parameters:
TEST_WRITE_COUNT = 10000
TEST_THREAD_COUNT = 4
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = ON
TEST_TASK_LOOP_DELAY_US = 100
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 100
TEST_DATA_SIZE = 8

Thread 0: reads=12872, changes=8522, avg_time=0.32us, max_time=6.74us
Thread 1: reads=12928, changes=9963, avg_time=0.39us, max_time=8.72us
Thread 2: reads=13312, changes=9968, avg_time=0.39us, max_time=8.59us
Thread 3: reads=12891, changes=9927, avg_time=0.34us, max_time=4.76us

Total Results:
  Total writes: 10000
  Total atomic writes: 1000
  Total reads: 52003
  Total changes observed: 38380 (73.8%)
  Total writes pending: 1
  Total publish all count: 1001
  Total errors: 0
  Average lock time: 0.36 us
  Maximum lock time: 8.72 us

Producer acquire lock time statistics:
  count=52003  max=8723ns  avg=360ns

Lock time histogram (52003 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  160-200ns                    579    1.11%  #
  200-240ns                    747    1.44%  #
  240-280ns                   6638   12.76%  ###########
  280-320ns                   7584   14.58%  #############
  320-360ns                  10654   20.49%  ##################
  360-400ns                   8396   16.15%  ##############
  400-600ns                  17368   33.40%  ##############################
  600-800ns                     22    0.04%  
  800-1000ns                     1    0.00%  
  3000-4000ns                    1    0.00%  
  4000-6000ns                    8    0.02%  
  6000-8000ns                    3    0.01%  
  8000-10000ns                   2    0.00%  

```

## Suggestions for improvement

1. Implement full RCU with a reclamation list of free pages, to avoid the non deterministic visibility delays and starvation of calibration updates.

2. The visibility after second lock behavior wouldn't be necessary in calibration blocks owned by a single thread.  
We could introduce an 'owned' calibration segment, by just adding a XcpCalSegSetOwnedMode(handle) function.  

2.1. Proposal how this could be implemented type-safe in Rust:

The current Rust implementation has a calibration segment wrapper type. The wrapper type is currently send, !sync and clone, and just wrapped the handle from the C implementation, while keeping a reference to the static lifetime default page.  

The most idiomatic Rust approach to introduce a owned mode is the typestate pattern — two distinct types encoding the mode at compile time, making the transition explicit and the state unrepresentable at the wrong type:

```Rust

// Excerpt from the current Rust implementation:
// Maybe a blueprint for something similar in modern C++ ???

// Calibration pages must be Sized + Send + Sync + Copy + Clone + 'static
pub trait CalPageTrait
where Self: Sized + Send + Sync + Copy + Clone + 'static,
{
    // This trait is empty, it's just a marker for the page type
}

// CalSeg 
// Is Send + !Sync + Clone: freely shareable across threads by cloning (like an Arc<T>), but not shareable by reference (no &CalSeg<T>)
pub struct SharedCalSeg<T: CalPageTrait> {
    index: xcplib::tXcpCalSegIndex, // The calibration segment handle from the C implementation
    default_page: &'static T, // The static immutable reference to the default page
    _not_sync_marker: PhantomData<std::cell::Cell<()>>, // CalSeg is send, not sync (like a Cell)
}

impl<T: CalPageTrait> SharedCalSeg<T> {
    pub fn new(instance_name: &'static str, default_page: &'static T) -> SharedCalSeg<T> {
    ...
    }
}

// Implement clone for CalSeg, which is a simple copy of the handle and the default page reference
impl<T: CalPageTrait> Clone for SharedCalSeg<T> {
    fn clone(&self) -> Self {
        SharedCalSeg {
            index: self.index,
            default_page: self.default_page, // &T is Copy, so this is fine
            _not_sync_marker: PhantomData, 
        }
    }
}



// New owned mode CalSeg
// Single reader thread, no deferred visibility
// Send: can be moved to another thread
// !Sync: cannot be shared across threads (enforced by not implementing Sync)
// !Clone: no accidental sharing (enforced by not implementing Clone)
pub struct OwnedCalSeg<T: CalPageTrait> { inner: SharedCalSeg<T> }

impl<T: CalPageTrait> OwnedCalSeg<T> {
    pub fn new(instance_name: &'static str, default_page: &'static T) -> OwnedCalSeg<T> {
        let calseg = SharedCalSeg::new(instance_name, default_page);
        xcplib::XcpCalSegSetOwnedMode(calseg.index);
        OwnedCalSeg { inner: calseg }
    }

    // Consuming transition back to shared mode
    pub fn into_shared(self) -> SharedCalSeg<T> {
        xcplib::XcpCalSegClearOwnedMode(self.inner.index);
        self.inner
    }
}

// Deref gives zero-boilerplate access to all SharedCalSeg<T> methods.
// Clone is not forwarded through Deref, OwnedCalSeg stays !Clone.
// Send/!Sync are inherited automatically from SharedCalSeg<T> through the inner field.
// This is the same pattern Rust's standard library uses, e.g. Box<T> derefs to T, String derefs to str.
use std::ops::Deref;
impl<T: CalPageTrait> Deref for OwnedCalSeg<T> {
    type Target = SharedCalSeg<T>;
    fn deref(&self) -> &SharedCalSeg<T> {
        &self.inner
    }
}

// Add DerefMut if mutable access to SharedCalSeg methods is needed


```




## Open issues in the current XCPlite implementation:

1. Iteration over the calibration segment list does not guarantee a consistent view, if segments are created simultaneously in different threads.  
The iteration may be used only, when the application has finalized the registration.  
New segments created after finalization should not be registered.  
Needs shared global state between all processes.  
Solution unclear.  

2. XCPlite uses XcpFindCalSeg to check for duplicate names.  
There is still a race condition, if segments with the same name are created simultaneously in different threads.  

2.1. An optimization of the linear search runtime would be to use a hash table to store the segments.  

3. Incrementing memory_segment_count is not atomic yet.   
Needs shared global state between all processes. A2L MEMORY_SEGMENT numbers are a global namespace.  

4. XcpPublishAll is called (and may only be called) in the XCP receive thread, which blocks without XCP communication (in the socket receive). Maybe add a blocking timeout to the socket.  

5. Keep in mind, that begin atomic transaction does not flush pending prior non atomic changes. 

