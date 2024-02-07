/*----------------------------------------------------------------------------
| File:
|   util.c
|
| Description:
|   Some helper functions
|
|   Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "util.h"



/**************************************************************************/
// load file to memory
/**************************************************************************/

void releaseFile(uint8_t* file) {

    if (file != NULL) {
        free(file);
    }
}

uint8_t* loadFile(const char* filename, uint32_t* length) {

    uint8_t* fileBuf = NULL; // file content
    uint32_t fileLen = 0; // file length

    DBG_PRINTF1("Load %s\n", filename);

#if defined(_LINUX) // Linux

    FILE* fd;
    fd = fopen(filename, "r");
    if (fd == NULL) {
        DBG_PRINTF_ERROR("ERROR: file %s not found!\n", filename);
        return NULL;
    }
    struct stat fdstat;
    stat(filename, &fdstat);
    fileBuf = (uint8_t*)malloc((size_t)(fdstat.st_size + 1));
    if (fileBuf == NULL) return NULL;
    fileLen = (uint32_t)fread(fileBuf, 1, (uint32_t)fdstat.st_size, fd);
    fclose(fd);

#elif defined(_WIN) // Windows

    wchar_t wcfilename[256] = { 0 };
    MultiByteToWideChar(0, 0, filename, (int)strlen(filename), wcfilename, (int)strlen(filename));
    HANDLE hFile = CreateFileW((wchar_t*)wcfilename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DBG_PRINTF_ERROR("file %s not found!\n", filename);
        return NULL;
    }
    fileLen = (uint32_t)GetFileSize(hFile, NULL);
    fileBuf = (uint8_t*)malloc(fileLen+1);
    if (fileBuf == NULL) {
        DBG_PRINTF_ERROR("Error: out of memory!\n");
        CloseHandle(hFile);
        return NULL;
    }
    if (!ReadFile(hFile, fileBuf, fileLen, NULL, NULL)) {
        DBG_PRINTF_ERROR("Error: could not read from %s!\n",filename);
        free(fileBuf);
        CloseHandle(hFile);
        return NULL;
    }
    fileBuf[fileLen] = 0;
    CloseHandle(hFile);

#endif

    DBG_PRINTF3("  file %s ready for upload, size=%u\n\n", filename, fileLen);

    *length = fileLen;
    return fileBuf;
}

