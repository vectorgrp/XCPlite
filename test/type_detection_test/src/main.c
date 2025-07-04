
#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"      // for xcplib A2l generation
#include "platform.h" // for sleepMs, clockGet
#include "xcplib.h"   // for xcplib application programming interface

//--
// Test structure with various types and arrays
typedef struct {
    uint8_t byte_value;
    uint16_t word_value;
    uint32_t dword_value;
    float float_value;
    double double_value;
    bool bool_value;

    // Arrays for testing complex expressions
    uint16_t curve_data[10];
    float map_data[5][8];
    int32_t signed_array[3];
} TestStruct;

const char *type_id_to_string(tA2lTypeId type_id) {
    switch (type_id) {
    case A2L_TYPE_UINT8:
        return "UINT8";
    case A2L_TYPE_INT8:
        return "INT8";
    case A2L_TYPE_UINT16:
        return "UINT16";
    case A2L_TYPE_INT16:
        return "INT16";
    case A2L_TYPE_UINT32:
        return "UINT32";
    case A2L_TYPE_INT32:
        return "INT32";
    case A2L_TYPE_UINT64:
        return "UINT64";
    case A2L_TYPE_INT64:
        return "INT64";
    case A2L_TYPE_FLOAT:
        return "FLOAT";
    case A2L_TYPE_DOUBLE:
        return "DOUBLE";
    default:
        return "UNDEFINED";
    }
}

int main() {
    TestStruct test_instance = {0};

    printf("Type Detection Test Results:\n");
    printf("============================\n");

    // Test simple types
    printf("Simple types:\n");
    printf("  byte_value: %s\n", type_id_to_string(A2lGetTypeId(test_instance.byte_value)));
    printf("  word_value: %s\n", type_id_to_string(A2lGetTypeId(test_instance.word_value)));
    printf("  dword_value: %s\n", type_id_to_string(A2lGetTypeId(test_instance.dword_value)));
    printf("  float_value: %s\n", type_id_to_string(A2lGetTypeId(test_instance.float_value)));
    printf("  double_value: %s\n", type_id_to_string(A2lGetTypeId(test_instance.double_value)));
    printf("  bool_value: %s\n", type_id_to_string(A2lGetTypeId(test_instance.bool_value)));
    assert(A2lGetTypeId(test_instance.byte_value) == A2L_TYPE_UINT8);
    assert(A2lGetTypeId(test_instance.word_value) == A2L_TYPE_UINT16);
    assert(A2lGetTypeId(test_instance.dword_value) == A2L_TYPE_UINT32);
    assert(A2lGetTypeId(test_instance.float_value) == A2L_TYPE_FLOAT);
    assert(A2lGetTypeId(test_instance.double_value) == A2L_TYPE_DOUBLE);
    assert(A2lGetTypeId(test_instance.bool_value) == A2L_TYPE_UINT8);

    // Test complex expressions (array indexing)
    printf("\nComplex expressions (array indexing):\n");
    printf("  curve_data[0]: %s\n", type_id_to_string(A2lGetTypeId(test_instance.curve_data[0])));
    printf("  map_data[0][0]: %s\n", type_id_to_string(A2lGetTypeId(test_instance.map_data[0][0])));
    printf("  signed_array[0]: %s\n", type_id_to_string(A2lGetTypeId(test_instance.signed_array[0])));
    assert(A2lGetTypeId(test_instance.curve_data[0]) == A2L_TYPE_UINT16);
    assert(A2lGetTypeId(test_instance.map_data[0][0]) == A2L_TYPE_FLOAT);
    assert(A2lGetTypeId(test_instance.signed_array[0]) == A2L_TYPE_INT32);

    // Test helper macros
    printf("\nHelper macros:\n");
    printf("  A2lGetArrayElementTypeId(curve_data): %s\n", type_id_to_string(A2lGetArrayElementTypeId(test_instance.curve_data)));
    printf("  A2lGetArray2DElementTypeId(map_data): %s\n", type_id_to_string(A2lGetArray2DElementTypeId(test_instance.map_data)));
    assert(A2lGetArrayElementTypeId(test_instance.curve_data) == A2L_TYPE_UINT16);
    assert(A2lGetArray2DElementTypeId(test_instance.map_data) == A2L_TYPE_FLOAT);

    return 0;
}
