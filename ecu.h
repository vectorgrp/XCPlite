// ecu.h 
// V1.0 23.9.2020

extern unsigned short CALRAM_START;
extern unsigned short CALRAM_LAST;

#define CALRAM       ((unsigned char*)&CALRAM_START)
#define CALRAM_SIZE  ((unsigned char*)&CALRAM_LAST-(unsigned char*)&CALRAM_START)

extern void ecuCyclic( void );
extern void ecuInit( void );

