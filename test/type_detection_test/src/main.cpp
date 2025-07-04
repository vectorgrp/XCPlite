#include <assert.h> // for assert
#include <iostream>
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "a2l.h"      // for xcplib A2l generation
#include "platform.h" // for sleepMs, clockGet
#include "xcplib.h"   // for xcplib application programming interface

// Test structure with various types and arrays
struct TestStruct {
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
};

std::string type_id_to_string(tA2lTypeId type_id) {
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
    TestStruct test_instance = {};

    std::cout << "C++ Type Detection Test Results:\n";
    std::cout << "================================\n";

    // Test simple types
    std::cout << "Simple types:\n";
    std::cout << "  byte_value: " << type_id_to_string(A2lGetTypeId(test_instance.byte_value)) << "\n";
    std::cout << "  word_value: " << type_id_to_string(A2lGetTypeId(test_instance.word_value)) << "\n";
    std::cout << "  dword_value: " << type_id_to_string(A2lGetTypeId(test_instance.dword_value)) << "\n";
    std::cout << "  float_value: " << type_id_to_string(A2lGetTypeId(test_instance.float_value)) << "\n";
    std::cout << "  double_value: " << type_id_to_string(A2lGetTypeId(test_instance.double_value)) << "\n";
    std::cout << "  bool_value: " << type_id_to_string(A2lGetTypeId(test_instance.bool_value)) << "\n";

    // Test complex expressions (array indexing)
    std::cout << "\nComplex expressions (array indexing):\n";
    std::cout << "  curve_data[0]: " << type_id_to_string(A2lGetTypeId(test_instance.curve_data[0])) << "\n";
    std::cout << "  map_data[0][0]: " << type_id_to_string(A2lGetTypeId(test_instance.map_data[0][0])) << "\n";
    std::cout << "  signed_array[0]: " << type_id_to_string(A2lGetTypeId(test_instance.signed_array[0])) << "\n";

    // Test helper macros
    std::cout << "\nHelper macros:\n";
    std::cout << "  A2lGetArrayElementTypeId(curve_data): " << type_id_to_string(A2lGetArrayElementTypeId(test_instance.curve_data)) << "\n";
    std::cout << "  A2lGetArray2DElementTypeId(map_data): " << type_id_to_string(A2lGetArray2DElementTypeId(test_instance.map_data)) << "\n";

    // Test sizeof-based fallback
    std::cout << "\nSizeof-based fallback:\n";
    std::cout << "  A2lGetTypeIdBySizeof(byte_value): " << type_id_to_string(A2lGetTypeIdBySizeof(test_instance.byte_value)) << "\n";
    std::cout << "  A2lGetTypeIdBySizeof(float_value): " << type_id_to_string(A2lGetTypeIdBySizeof(test_instance.float_value)) << "\n";
    std::cout << "  A2lGetTypeIdBySizeof(double_value): " << type_id_to_string(A2lGetTypeIdBySizeof(test_instance.double_value)) << "\n";

// Test C++11 decltype if available
#if __cplusplus >= 201103L
    std::cout << "\nC++11 decltype-based (compile-time):\n";
    std::cout << "  decltype(byte_value): " << type_id_to_string(A2lGetTypeIdDecltype(test_instance.byte_value)) << "\n";
    std::cout << "  decltype(curve_data[0]): " << type_id_to_string(A2lGetTypeIdDecltype(test_instance.curve_data[0])) << "\n";
    std::cout << "  decltype(map_data[0][0]): " << type_id_to_string(A2lGetTypeIdDecltype(test_instance.map_data[0][0])) << "\n";
#endif

    return 0;
}
