// ecu.h 

#ifndef __ECU_H_
#define __ECU_H_

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned short CALRAM_START;
extern unsigned short CALRAM_LAST;

#define CALRAM       ((unsigned char*)&CALRAM_START)
#define CALRAM_SIZE  ((unsigned char*)&CALRAM_LAST-(unsigned char*)&CALRAM_START)

extern void ecuCyclic( void );
extern void ecuInit( void );

#ifdef __cplusplus
}
#endif

#endif
