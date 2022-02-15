#pragma once

// ecu.h
/*
| Code released into public domain, no attribution required
*/


#ifdef XCP_ENABLE_CAL_PAGE
extern uint8_t ecuParGetCalPage();
extern void ecuParSetCalPage(uint8_t page);
extern uint8_t* ecuParAddrMapping(uint8_t* a);
#endif

extern void ecuInit();
extern void ecuCreateA2lDescription();
extern char* ecuGetEPK();

#ifdef _WIN
DWORD WINAPI ecuTask(LPVOID p);
#else
void* ecuTask(void* p);
#endif

