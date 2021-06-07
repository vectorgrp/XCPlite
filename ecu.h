// ecu.h 

#ifndef __ECU_H_
#define __ECU_H_

#ifdef __cplusplus
extern "C" {
#endif

extern double channel1; // Test
extern unsigned short ecuCounter;

extern unsigned short gXcpEvent_EcuCyclic;


extern void ecuInit(void);
extern void ecuCreateA2lDescription(void);
extern void ecuCyclic(void);

#ifdef __cplusplus
}
#endif

#endif
