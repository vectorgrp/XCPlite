#pragma once

// ecu.h
/*
| Code released into public domain, no attribution required
*/

#ifdef __cplusplus
extern "C" {
#endif


/**************************************************************************/
/* ECU Parameters */
/**************************************************************************/

    struct ecuPar {
        unsigned int CALRAM_SIZE;
        unsigned int cycleTime;
        double period;
        double offset1, offset2, offset3;
        double phase1, phase2, phase3;
        double ampl1, ampl2, ampl3;
        unsigned char map1_8_8[8][8];
        unsigned char curve1_32[32];
    };


extern volatile struct ecuPar *ecuPar;
extern volatile struct ecuPar ecuRamPar;
extern struct ecuPar ecuRomPar;

#ifdef XCP_ENABLE_CAL_PAGE
extern uint8_t ecuParGetCalPage();
extern void ecuParSetCalPage(uint8_t page);
extern uint8_t* ecuParAddrMapping(uint8_t* a);
#endif
extern void ecuInit();
extern void ecuCreateA2lDescription();
extern void* ecuTask(void* p);

#ifdef __cplusplus
}
#endif
