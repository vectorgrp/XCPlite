// ecu.h 

#ifndef __ECU_H_
#define __ECU_H_

#ifdef __cplusplus
extern "C" {
#endif

extern void ecuInit(void);
extern void ecuCreateA2lDescription(void);
extern void ecuCyclic(void);

#ifdef __cplusplus
}
#endif

#endif
