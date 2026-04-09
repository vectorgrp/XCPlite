# C++ Type Manipulation & Metaprogramming Guide

This guide explains the C++ language features used in `a2l.hpp` for compile-time type manipulation.

## Table of Contents
1. [Type Traits Overview](#type-traits-overview)
2. [decltype() - Type Deduction](#decltype---type-deduction)
3. [std::declval<T>() - Hypothetical Objects](#stddeclvalt---hypothetical-objects)
4. [Type Modification Traits](#type-modification-traits)
5. [Array Type Traits](#array-type-traits)
6. [Practical Examples](#practical-examples)
7. [Automatic Array Dimension Detection](#automatic-array-dimension-detection)

---

## Type Traits Overview

Type traits are **compile-time** template utilities (from `<type_traits>` header) that query and transform types. They don't generate runtime code - everything happens during compilation.

### Key Concepts:
- **Compile-time evaluation**: All type manipulations happen before the program runs
- **Zero runtime cost**: Type traits produce no runtime overhead
- **Template metaprogramming**: Using templates to compute types and values at compile time

---

## decltype() - Type Deduction

`decltype` deduces the type of an expression at compile time.

### Syntax:
```cpp
decltype(expression)
```

### Examples:
```cpp
int x = 5;
decltype(x) y;           // y is int

double arr[10];
decltype(arr) z;         // z is double[10]
decltype(arr[0]) w;      // w is double& (reference!)

struct Point { int x, y; };
Point p;
decltype(p.x) val;       // val is int
decltype(Point::y) val2; // val2 is int
```

### Important: Reference vs Value
```cpp
int arr[5];
decltype(arr[0])    // Returns: int&  (reference to element)
decltype(arr)       // Returns: int[5] (array type)
```

When you index an array with `[]`, it returns a **reference** to the element, not the element itself!

---

## std::declval<T>() - Hypothetical Objects

`std::declval<T>()` lets you "pretend" an object of type T exists, without actually creating it. This is crucial for accessing members of types that can't be easily constructed.

### Why is this needed?

```cpp
struct MyStruct {
    int data[10];
    MyStruct(int complex, const char* args) { /* complex constructor */ }
};

// Problem: How do we get the type of data[0] without creating MyStruct?
// We can't do: MyStruct().data[0]  // Constructor needs arguments!

// Solution: Use declval
using ElementType = decltype(std::declval<MyStruct>().data[0]);
// Now ElementType is int&
```

### How it works:
```cpp
// Conceptually, declval does this:
template<typename T>
T&& declval() noexcept;  // Never implemented - only for decltype!
```

It's a **declaration only** - never implemented or called. It only exists inside `decltype()` for compile-time type deduction.

### Example Usage:
```cpp
struct Widget {
    double values[100];
    std::string name;
    
    Widget() = delete;  // Can't construct!
};

// Still works - no construction needed:
using T1 = decltype(std::declval<Widget>().values[0]);    // T1 is double&
using T2 = decltype(std::declval<Widget>().name);         // T2 is std::string&
```

---

## Type Modification Traits

These traits remove or add type qualifiers.

### Common Type Modification Traits:

| Trait | Purpose | Example |
|-------|---------|---------|
| `std::remove_reference_t<T>` | Remove & or && | `int&` → `int` |
| `std::remove_pointer_t<T>` | Remove * | `int*` → `int` |
| `std::remove_const_t<T>` | Remove const | `const int` → `int` |
| `std::remove_volatile_t<T>` | Remove volatile | `volatile int` → `int` |
| `std::remove_cv_t<T>` | Remove const/volatile | `const volatile int` → `int` |
| `std::remove_extent_t<T>` | Remove one array dimension | `int[10]` → `int` |
| `std::remove_all_extents_t<T>` | Remove all array dims | `int[5][10]` → `int` |

### Detailed Examples:

```cpp
// std::remove_reference_t
using T1 = std::remove_reference_t<int&>;       // int
using T2 = std::remove_reference_t<int&&>;      // int
using T3 = std::remove_reference_t<int>;        // int (unchanged)

// std::remove_pointer_t
using T4 = std::remove_pointer_t<int*>;         // int
using T5 = std::remove_pointer_t<int**>;        // int* (only one level)
using T6 = std::remove_pointer_t<int>;          // int (unchanged)

// std::remove_extent_t
using T7 = std::remove_extent_t<int[10]>;       // int
using T8 = std::remove_extent_t<int[5][10]>;    // int[10]
using T9 = std::remove_extent_t<int>;           // int (unchanged)

// std::remove_all_extents_t
using T10 = std::remove_all_extents_t<int[5][10][20]>;  // int
```

### Chaining Traits:

You can chain these together:

```cpp
int arr[10][20];

// Get the array type
using ArrayType = decltype(arr);                    // int[10][20]

// Get reference to element
using ElemRef = decltype(arr[0][0]);                // int&

// Remove reference to get element type
using ElemType = std::remove_reference_t<ElemRef>;  // int

// All in one expression:
using ElemType2 = std::remove_reference_t<decltype(arr[0][0])>;  // int
```

---

## Array Type Traits

Special traits for working with arrays.

### Array Query Traits:

| Trait | Purpose | Example |
|-------|---------|---------|
| `std::is_array_v<T>` | Check if T is an array | `int[10]` → `true` |
| `std::rank_v<T>` | Number of dimensions | `int[5][10]` → `2` |
| `std::extent_v<T, N>` | Size of Nth dimension | `int[5][10]`, N=0 → `5` |

### Examples:

```cpp
using T1 = int[10];
using T2 = double[5][20];
using T3 = char[3][4][5];

// std::is_array_v - Check if type is an array
static_assert(std::is_array_v<T1>);       // true
static_assert(std::is_array_v<T2>);       // true
static_assert(!std::is_array_v<int>);     // false

// std::rank_v - Get number of dimensions
static_assert(std::rank_v<T1> == 1);      // 1D array
static_assert(std::rank_v<T2> == 2);      // 2D array
static_assert(std::rank_v<T3> == 3);      // 3D array
static_assert(std::rank_v<int> == 0);     // Not an array

// std::extent_v<T, N> - Get size of Nth dimension (0-indexed)
static_assert(std::extent_v<T1, 0> == 10);      // First dimension
static_assert(std::extent_v<T2, 0> == 5);       // First dimension
static_assert(std::extent_v<T2, 1> == 20);      // Second dimension
static_assert(std::extent_v<T3, 0> == 3);       // First dimension
static_assert(std::extent_v<T3, 1> == 4);       // Second dimension
static_assert(std::extent_v<T3, 2> == 5);       // Third dimension

// If dimension doesn't exist, returns 0
static_assert(std::extent_v<T1, 1> == 0);       // T1 has no 2nd dimension
static_assert(std::extent_v<int, 0> == 0);      // int is not an array
```

---

## Practical Examples

### Example 1: Getting Element Type from Array Field

```cpp
struct Config {
    double curve[16];
    int map[8][4];
};

// Method 1: Using declval and indexing (current approach)
using CurveElem = std::remove_reference_t<
    decltype(std::declval<Config>().curve[0])
>;  // Result: double

using MapElem = std::remove_reference_t<
    decltype(std::declval<Config>().map[0][0])
>;  // Result: int

// Method 2: Using remove_extent
using CurveElem2 = std::remove_extent_t<
    decltype(Config::curve)
>;  // Result: double

using MapElem2 = std::remove_all_extents_t<
    decltype(Config::map)
>;  // Result: int
```

### Example 2: Our A2L Macro Breakdown

Let's break down what happens in `A2L_CURVE_COMPONENT`:

```cpp
#define A2L_CURVE_COMPONENT(field_name, x_dim, comment, unit, min, max) \
    [](auto type_ptr) {                                                  \
        using T = std::remove_pointer_t<decltype(type_ptr)>;            \
        using ElementType = std::remove_reference_t<                     \
            decltype(std::declval<T>().field_name[0])                   \
        >;                                                               \
        return xcplib::A2lParameterComponentInfo(                        \
            #field_name,                                                 \
            (ElementType *)nullptr,                                      \
            (uint16_t)offsetof(T, field_name),                          \
            x_dim, 1, comment, unit, min, max                           \
        );                                                               \
    }
```

**Step-by-step execution** when called with `A2L_CURVE_COMPONENT(curve, 16, ...)`:

```cpp
// Given:
struct ParametersT {
    double curve[16];
};

// The lambda receives: ParametersT*

// Step 1: type_ptr is ParametersT*
auto type_ptr = (ParametersT*)nullptr;

// Step 2: Remove pointer to get the struct type
using T = std::remove_pointer_t<decltype(type_ptr)>;
// T is now: ParametersT

// Step 3: Access the field through a hypothetical instance
std::declval<T>()           // "Pretend" ParametersT exists
.curve                       // Access curve field
[0]                          // Index to get first element

// Step 4: Get the type
decltype(std::declval<T>().curve[0])
// Result: double& (reference to double)

// Step 5: Remove the reference
using ElementType = std::remove_reference_t<decltype(...)>;
// ElementType is now: double

// Step 6: Create nullptr of that type
(ElementType *)nullptr
// Result: (double*)nullptr
```

---

## Automatic Array Dimension Detection

Now let's solve the problem of automatically detecting array dimensions!

### Solution for 1D Arrays:

```cpp
// Template to detect 1D array size
template<typename T, typename = void>
struct array_size_1d {
    static constexpr size_t value = 0;  // Not an array or field doesn't exist
};

// Specialization for 1D arrays
template<typename T>
struct array_size_1d<T, std::void_t<decltype(std::extent_v<T, 0>)>> {
    static constexpr size_t value = std::extent_v<T, 0>;
};

// Helper to get array size from a field
#define ARRAY_SIZE_1D(struct_type, field_name) \
    std::extent_v<decltype(struct_type::field_name), 0>
```

### Solution for 2D Arrays:

```cpp
// Get dimensions of 2D array
#define ARRAY_SIZE_2D_X(struct_type, field_name) \
    std::extent_v<decltype(struct_type::field_name), 0>

#define ARRAY_SIZE_2D_Y(struct_type, field_name) \
    std::extent_v<decltype(struct_type::field_name), 1>
```

### Updated A2L Macros with Auto-Detection:

```cpp
// Auto-detect dimension for CURVE (1D array)
#define A2L_CURVE_COMPONENT_AUTO(field_name, comment, unit, min, max) \
    [](auto type_ptr) {                                                \
        using T = std::remove_pointer_t<decltype(type_ptr)>;          \
        using FieldType = decltype(T::field_name);                     \
        using ElementType = std::remove_reference_t<                   \
            decltype(std::declval<T>().field_name[0])                 \
        >;                                                             \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;         \
        return xcplib::A2lParameterComponentInfo(                      \
            #field_name, (ElementType *)nullptr,                       \
            (uint16_t)offsetof(T, field_name),                        \
            x_dim, 1, comment, unit, min, max                         \
        );                                                             \
    }

// Auto-detect dimensions for MAP (2D array)
#define A2L_MAP_COMPONENT_AUTO(field_name, comment, unit, min, max)   \
    [](auto type_ptr) {                                                \
        using T = std::remove_pointer_t<decltype(type_ptr)>;          \
        using FieldType = decltype(T::field_name);                     \
        using ElementType = std::remove_reference_t<                   \
            decltype(std::declval<T>().field_name[0][0])              \
        >;                                                             \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;         \
        constexpr size_t y_dim = std::extent_v<FieldType, 1>;         \
        return xcplib::A2lParameterComponentInfo(                      \
            #field_name, (ElementType *)nullptr,                       \
            (uint16_t)offsetof(T, field_name),                        \
            x_dim, y_dim, comment, unit, min, max                     \
        );                                                             \
    }
```

### Usage Example:

```cpp
struct ParametersT {
    double scalar;
    double curve[16];
    uint8_t map[8][4];
};

// Old way - manual dimensions:
A2lTypedefBuilder(ParametersT, "Parameters",
    A2L_PARAMETER_COMPONENT(scalar, "A scalar", "", 0.0, 100.0),
    A2L_CURVE_COMPONENT(curve, 16, "Curve", "", 0.0, 100.0),
    A2L_MAP_COMPONENT(map, 8, 4, "Map", "", 0, 255)
);

// New way - automatic dimensions:
A2lTypedefBuilder(ParametersT, "Parameters",
    A2L_PARAMETER_COMPONENT(scalar, "A scalar", "", 0.0, 100.0),
    A2L_CURVE_COMPONENT_AUTO(curve, "Curve", "", 0.0, 100.0),    // Detects [16]
    A2L_MAP_COMPONENT_AUTO(map, "Map", "", 0, 255)               // Detects [8][4]
);
```

---

## Complete Test Example

Here's a complete test to verify all concepts:

```cpp
#include <type_traits>
#include <cstddef>
#include <iostream>

struct TestStruct {
    int scalar;
    double curve[16];
    uint8_t map[8][4];
    float cube[3][4][5];
};

int main() {
    // Test 1: decltype and declval
    using T1 = decltype(std::declval<TestStruct>().scalar);
    using T2 = decltype(std::declval<TestStruct>().curve[0]);
    using T3 = decltype(std::declval<TestStruct>().map[0][0]);
    
    std::cout << "T1 is reference: " << std::is_reference_v<T1> << "\n";  // true
    std::cout << "T2 is reference: " << std::is_reference_v<T2> << "\n";  // true
    std::cout << "T3 is reference: " << std::is_reference_v<T3> << "\n";  // true
    
    // Test 2: Remove reference
    using E1 = std::remove_reference_t<T1>;  // int
    using E2 = std::remove_reference_t<T2>;  // double
    using E3 = std::remove_reference_t<T3>;  // uint8_t
    
    // Test 3: Array dimensions
    std::cout << "curve dimensions: " << std::rank_v<decltype(TestStruct::curve)> << "\n";        // 1
    std::cout << "map dimensions: " << std::rank_v<decltype(TestStruct::map)> << "\n";            // 2
    std::cout << "cube dimensions: " << std::rank_v<decltype(TestStruct::cube)> << "\n";          // 3
    
    // Test 4: Array sizes
    std::cout << "curve size: " << std::extent_v<decltype(TestStruct::curve), 0> << "\n";         // 16
    std::cout << "map size [0]: " << std::extent_v<decltype(TestStruct::map), 0> << "\n";         // 8
    std::cout << "map size [1]: " << std::extent_v<decltype(TestStruct::map), 1> << "\n";         // 4
    std::cout << "cube size [0]: " << std::extent_v<decltype(TestStruct::cube), 0> << "\n";       // 3
    std::cout << "cube size [1]: " << std::extent_v<decltype(TestStruct::cube), 1> << "\n";       // 4
    std::cout << "cube size [2]: " << std::extent_v<decltype(TestStruct::cube), 2> << "\n";       // 5
    
    // Test 5: offsetof
    std::cout << "offset scalar: " << offsetof(TestStruct, scalar) << "\n";
    std::cout << "offset curve: " << offsetof(TestStruct, curve) << "\n";
    std::cout << "offset map: " << offsetof(TestStruct, map) << "\n";
    
    return 0;
}
```

---

## Summary

### Key Takeaways:

1. **`decltype(expr)`** - Gets the type of an expression at compile time
2. **`std::declval<T>()`** - Pretends an object exists for type deduction (only in decltype!)
3. **`std::remove_reference_t<T>`** - Strips & or && from a type
4. **`std::remove_pointer_t<T>`** - Strips * from a type
5. **`std::extent_v<T, N>`** - Gets the size of the Nth array dimension
6. **`std::rank_v<T>`** - Gets the number of array dimensions

### For Automatic Array Dimension Detection:

Use `std::extent_v<decltype(T::field_name), N>` where:
- N=0 for first dimension
- N=1 for second dimension
- etc.

This allows you to eliminate the manual dimension parameters from your macros!

---

## References

- C++ Reference: [Type Traits](https://en.cppreference.com/w/cpp/header/type_traits)
- C++ Reference: [decltype](https://en.cppreference.com/w/cpp/language/decltype)
- C++ Reference: [std::declval](https://en.cppreference.com/w/cpp/utility/declval)
