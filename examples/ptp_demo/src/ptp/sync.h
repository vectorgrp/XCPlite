#pragma once
/* sync.h */
/*
| Code released into public domain, no attribution required
*/


typedef struct sync_state { 
  uint64_t clock1Ref;
  uint64_t clock2Ref;
  int32_t clock2Drift; // drift in ns per s
  uint32_t clock2DriftFract; // drift per ns in ns/(2^32)
} sync_state_t;


// Init
// clockdrift in ns per s
extern void syncInit(sync_state_t* s, uint64_t clock2, int32_t clock2Drift, uint64_t clock1);

// Calculate clock2 from clock1
extern uint64_t syncGetClock(sync_state_t* s, uint64_t clock1);

// Update to new parameters (clock1/2 pair and clock2 drift)
// Assures monotonic behaviour of syncGetClock()
extern void syncUpdate(sync_state_t* s, uint64_t clock2, int32_t clock2Drift, uint64_t clock);









