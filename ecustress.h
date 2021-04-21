// ecustress.h 

#ifndef __ECUSTRESS_H_
#define __ECUSTRESS_H_

#ifdef XCP_ENABLE_STRESSTEST // Enable measurement stress generator

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int gXcpEvent_EcuStress;

extern void ecuStressInit(void);
extern void ecuStressCreateA2lDescription(void);
extern void ecuStressCyclic(void);

#ifdef __cplusplus
}
#endif

#endif

#endif
