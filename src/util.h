#pragma once

/* util.h */
/*
| Code released into public domain, no attribution required
*/




//-------------------------------------------------------------------------------
// Load a file to memory

uint8_t* loadFile(const char* filename, uint32_t* length);
void releaseFile(uint8_t* file);


