# XCPlite lock-less and wait-free Calibration Data Access

This document contains advanced technical information about XCPlite's calibration implementation


## Calibration Segment Management and RCU Algorithm

### Overview:

Precondition for lock-free and wait-free modification of calibration parameters is, that there is only one writer thread (XCP thread).  

Access to calibration parameters on the application side is done with lock-free and wait-free functions:

- XcpUnlockCalSeg
- XcpWriteCalSeg

Creation and registration of calibration segments by the application

- XcpCreateCalSeg
- XcpCreateCalBlk
- XcpCreateCalSegPreloaded

Creation or check for existence is lock-free, but not wait-free (there is a CAS loop for a bump memory allocator).  
The registration of a new calibration segment is protected by a mutex.  
Duplicate names are not allowed, in this case the registration just returns the existing segment.  
Registration is required in XCPlite, because the calibration server and A2L generation need operations that iterate over all calibration segments:  

The calibration server operations needed to implement XCP are:  
- XcpGetSegInfo
- XcpGetSegPageInfo
- XcpCalSegGetCalPage
- XcpCalSegSetCalPage
- XcpCalSegCopyCalPage
- XcpGetCalSegMode
- XcpSetCalSegMode


Functions to access the calibration segment data from the server side are:  
- XcpCalSegWriteMemory
- XcpCalSegReadMemory
- XcpCalSegBeginAtomicTransaction
- XcpCalSegEndAtomicTransaction

An atomic transaction is a lock-less, wait-free global operation, which does not have any runtime costs.  
Calibration updates from the single threaded writer just get delayed and collected in each segments XCP page, until the transaction is ended.  
Therefore there is no need for any other, finer grained locking or synchronization mechanism for calibration updates.



### Compromises made in the XCPlite RCU algorithm for calibration segment updates:

To achieve lock-less and wait-free access to calibration parameters on the application side, the writer has to accept the following compromises:

1.
Exactly one writer thread allowed (e.g. the XCP command server thread).  

2.
A segment write operation will become visible to the application in the SECOND lock after the write !!!      

3.
Visibility delays of calibration updates may be non deterministic.  
If there are additional writes to the calibration block before the second lock, which is normally rare, these writes are accumulated in the xcp_page and not applied automatically.  
To flush these additional changes, the function XcpPublishAll may be called explicitly.  
This could be done non-blocking in a cyclic way or blocking, when each time a calibration change took place.  
In non-blocking mode, calibration changes become visible, when no application holds the lock, which is non deterministic!!!     
The alternative approach of doing blocking mode calls to XcpPublishAll after each write operation, would occasionally delay the command responses by a non-deterministic amount of time, which is might be acceptable in some use cases.  

4.
Calibration updates may theoretically starve, when there is always at least one reader holding a lock.  
Worst case is, that a calibration update command may time out.

5.
The lazy, non-blocking approach has the drawback, that calibration changes are acknowledged to the writer, before they become visible to the application.  
But there is no risk, that the changes can not be made visible after acknowledge.

6.
Registration and creating of calibration segments is protected by a mutex to share a consistent state of the calibration segment list among threads.  

5. 
Each calibration block needs a header of 64 bytes, plus 4 times the page size.  
Page size is rounded up to 64 bit alignment.  
So to calibrate a block of N bytes, we need 64+4*(8*N+7)/8 bytes of memory.




### RCU algorithm pseudo code for calibration segment updates:


Here is the used RCU algorithm as pseudo code:

The 3 RCU pages are named as follows:
  1. ecu_page - for current ECU access
  2. xcp_page - for current XCP access
  3. free_page - for swapping

The variables lock_count, next_page and free_page are atomic pointers or offsets.

This algorithm can be treated as a RCU like pattern with exactly one element (free_page) in its memory reclamation list.
When there is no free memory, the calibration changes just get collected in the xcp_page.  
This is used to realize calibration consistency requirements, were the collection is controlled by CANape through user defined commands.  
Drawback of this simple approach is, that calibration updates may starve, when there is always at least one reader holding a lock.
Worst case is, that a calibration update may time out.

```
    Shared mutable atomic state between the XCP thread and the ECU thread is:
        - ecu_page_next: a page with newer data, taken over into ecu_page 
        - free_page: the page freed when new_page is taken over 
        - ecu_access: 0 - ecu_page, 1 - default_page access mode
        
    Shared mutable atomic state between the ECU threads is:
        - lock_count: number of locks on this segment


// Multithreaded lock
function lock(segment) {
    if (lock_count++ == 0 }  
      if (ecu_page != ecu_page_next) { // Need to update ecu_page
          
          // The ecu_page (which now becomes the free_page) might be used by some other thread, since we got the lock==0 on this segment
          free_page_hazard = true; 

          free_page = ecu_page;
          ecu_page = ecu_page_next;

      } else {
          free_page_hazard = false; // There was no other lock and no need for update, free page must be safe now, if there is one
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
// Tries to publish pending changes to the ECU, return true on success
// Called cyclically to publish pending changes. It is also called when consistency hold is released.
function try_publish(segment) -> bool {
   
    // Try allocate a new xcp page
    if (free_page == NULL || free_page_hazard ) {
        return false  // No free page available yet
    }

    // allocate the free page
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

### Problems in the current implementation:

1.
Iteration over the calibration segment list does not guarantee a consistent view, if segments are created simultaneously in different threads.  
The iteration may be used only, when the application has finalized the registration. New segments created after finalization should not be registered.  
Needs shared global state between all processes.  
Solution unclear.  

2.
XCPlite uses XcpFindCalSeg to check for duplicate names.  
There is still a race condition, if they are created simultaneously in different threads.  
TODO: An optimization of the linear search would be to use a hash table to store the segments.  

3.
Incrementing memory_segment_count is not atomic yet. Todo.  
TODO: Needs shared global state between all processes. A2L MEMORY_SEGMENT numbers are a global namespace.  

4.
XcpPublishAll is called (and may only be called) in the XCP receive thread, which blocks without XCP communication.  
TODO: Maybe add a blocking timeout.  

