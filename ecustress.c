/*----------------------------------------------------------------------------
| File:
|   ecuStress.c
|
| Description:
|   Stress Test Measurement Tools and Loggers
|
 ----------------------------------------------------------------------------*/

#include "xcpLite.h"
#include "ecustress.h"
#include "A2L.h"

// Pseudo random unsigned int 0-15
static unsigned int r = 0;
static void seed16(unsigned int seed) {
    r = seed;
}
static unsigned int random16() {
        r = 36969 * (r & 65535) + (r >> 16);
    return r&0xF;
}

#ifdef XCP_ENABLE_STRESSTEST // Enable measurement stress generator

unsigned short ecuStressCounter = 0;

#define MEM_SIZE (1024*16)
double mem[MEM_SIZE];
char* names[MEM_SIZE];


// Init
void ecuStressInit(void) {

    ecuStressCounter = 0;

    for (unsigned int i = 0; i < MEM_SIZE; i++) {
        mem[i] = 0.0;
    }
}


// Create A2L File content
#ifdef XCP_ENABLE_A2L
void ecuStressCreateA2lDescription( void) {
      
    A2lSetEvent(gXcpEvent_EcuStress); // Associate XCP event "EcuStressCyclic" to the variables created below
    A2lCreateMeasurement(ecuStressCounter);
    
    // Create random measurement variables
    char name[32];
    int type;
    char* typeName;
    unsigned int size;
    unsigned int a = 0;
    unsigned int i = 0;
    seed16(12345);
    while (a<sizeof(mem)-8) {
        switch (random16()) {
        case 0: type = +1; size = 1;  typeName = "UByte"; break; 
        case 1: type = -1; size = 1;  typeName = "Byte"; break; 
        case 2:
        case 3: type = +2; size = 2;  typeName = "UWord"; break;
        case 4: type = -2; size = 2;  typeName = "Word"; break; 
        case 5:
        case 6: type = +4; size = 4;  typeName = "ULong"; break; 
        case 7: type = -4; size = 4;  typeName = "Long"; break;
        case 8: type = 0; size = random16()/5+1;  typeName = "Gap 1-3"; break;
        default: 
            if (a % 8 != 0) continue;
            type = +8; size = 8;  typeName = "Double"; break;
        }
        if (type != 0) {
            sprintf_s(name, sizeof(name), "Var_%s_%X", typeName, a);
            A2lCreateMeasurement_(NULL, name, type, (unsigned long)&mem[0] + a, 1.0, 0.0, "Unit", "Comment");
            names[i] = (char*)malloc(strlen(name)+1);
            strcpy_s(names[i], strlen(name)+1, name);
            i++;
        }
        a += size;
    }

    A2lMeasurementGroupFromList("Stress", names, i);
}
#endif


// Cyclic stress task
void ecuStressCyclic( void )
{
  // Cycle counter
  ecuStressCounter++;

  for (unsigned int i = 0; i < MEM_SIZE; i++) {
      mem[i] += 1.234567890;
      if (mem[i] > 1234) mem[i] = 0;
  }

}



#endif
