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

1.
Exactly one writer thread allowed (e.g. the XCP command server thread) !!.  

2.
A segment write operation will become visible to the application earliest in the second first nesting level XcpLockCalSeg after the XcpCalSegWriteMemory !!.  
('second' in the definition of sequentiality of this implementation).  

3.
Visibility delays of calibration updates under contention may be non deterministic !!.  
If there are additional writes to the calibration block before the second lock (which is normally rare), these writes are accumulated in the xcp_page and are not applied automatically.  
To flush these additional changes, the function XcpPublishAll must be called explicitly.  
This could be done non-blocking in a cyclic way or blocking on demand, even each time a parameter change took place.  
In non-blocking mode, calibration changes become visible, when no other application thread holds the lock, which is non deterministic!!!     
The alternative approach of doing blocking mode calls to XcpPublishAll after each write operation, would occasionally delay the protocol command responses by a non-deterministic amount of time, which might be acceptable in some use cases.  

4.
Calibration updates may theoretically starve, when there is always at least one reader holding a lock or the lock rate is very low.  
Worst case is, that a calibration update command may time out.

5.
The lazy, non-blocking approach has the drawback, that calibration changes are acknowledged to the writer, before they become visible to the application threads (see 2.).  
But there is no risk for failure.  

6.
Registration and creation of calibration segments is protected by a mutex, to share a consistent state of the calibration segment list among threads.  

7. 
Each calibration block needs a header of 64 bytes, plus 4 times the page size.  
Page size is rounded up to 64 bit alignment.  
So to calibrate a block of N bytes, we need 64+4*(8*N+7)/8 bytes of memory.




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

## Open issues in the current XCPlite implementation:

1.
Iteration over the calibration segment list does not guarantee a consistent view, if segments are created simultaneously in different threads.  
The iteration may be used only, when the application has finalized the registration. New segments created after finalization should not be registered.  
Needs shared global state between all processes.  
Solution unclear.  

2.
XCPlite uses XcpFindCalSeg to check for duplicate names.  
There is still a race condition, if segments with the same name are created simultaneously in different threads.  

2.1
An optimization of the linear search runtime would be to use a hash table to store the segments.  

3.
Incrementing memory_segment_count is not atomic yet.   
Needs shared global state between all processes. A2L MEMORY_SEGMENT numbers are a global namespace.  

4.
XcpPublishAll is called (and may only be called) in the XCP receive thread, which blocks without XCP communication.  
Maybe add a blocking timeout.  

5.
Begin atomic transaction does not flush pending prior non atomic changes. 
Are there different opinions on this ?

