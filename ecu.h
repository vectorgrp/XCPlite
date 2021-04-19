// ecu.h 

#ifndef __ECU_H_
#define __ECU_H_

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int gXcpEvent_EcuCyclic;
extern unsigned int gXcpEvent_EcuCyclic_packed;

extern void ecuInit(void);
extern void ecuCreateA2lDescription(void);
extern void ecuCyclic(void);

#ifdef __cplusplus
}
#endif

#endif
