#include <cstdio>
#include <iostream>

extern void cpp_version_test();

void cpp_version_test() {

    // Check C++ standard version
    printf("\nC++ Language Version:\n");
#ifdef __cplusplus
#if __cplusplus >= 202302L
    printf("C++26 (upcoming) - __cplusplus = %ld\n", __cplusplus);
#elif __cplusplus >= 202002L
    printf("C++20 (ISO/IEC 14882:2020) - __cplusplus = %ld\n", __cplusplus);
#elif __cplusplus >= 201703L
    printf("C++17 (ISO/IEC 14882:2017) - __cplusplus = %ld\n", __cplusplus);
#elif __cplusplus >= 201402L
    printf("C++14 (ISO/IEC 14882:2014) - __cplusplus = %ld\n", __cplusplus);
#elif __cplusplus >= 201103L
    printf("C++11 (ISO/IEC 14882:2011) - __cplusplus = %ld\n", __cplusplus);
#elif __cplusplus >= 199711L
    printf("C++98/C++03 (ISO/IEC 14882:1998/2003) - __cplusplus = %ld\n", __cplusplus);
#else
    printf("Pre-standard C++ - __cplusplus = %ld\n", __cplusplus);
#endif
#else
    printf("Not compiled as C++\n");
#endif

    // Print compiler information
    printf("\nCompiler Information:\n");
#ifdef __GNUC__
    printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__clang__)
    printf("Compiler: Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    printf("Compiler: MSVC %d\n", _MSC_VER);
#else
    printf("Compiler: Unknown compiler\n");
#endif

    // Print C++ feature test macros
    printf("\nC++ Feature Support:\n");
#ifdef __cpp_concepts
    printf("Concepts (C++20): %ld\n", __cpp_concepts);
#endif
#ifdef __cpp_modules
    printf("Modules (C++20): %ld\n", __cpp_modules);
#endif
#ifdef __cpp_coroutines
    printf("Coroutines (C++20): %ld\n", __cpp_coroutines);
#endif
#ifdef __cpp_constexpr
    printf("Constexpr: %d\n", __cpp_constexpr);
#endif
#ifdef __cpp_lambdas
    printf("Lambdas (C++11): %ld\n", __cpp_lambdas);
#endif
#ifdef __cpp_auto_type
    printf("Auto type deduction (C++11): %ld\n", __cpp_auto_type);
#endif
#ifdef __cpp_range_based_for
    printf("Range-based for loops (C++11): %d\n", __cpp_range_based_for);
#endif
#ifdef __cpp_decltype
    printf("Decltype (C++11): %ld\n", __cpp_decltype);
#endif
#ifdef __cpp_rvalue_references
    printf("Rvalue references (C++11): %ld\n", __cpp_rvalue_references);
#endif
#ifdef __cpp_variadic_templates
    printf("Variadic templates (C++11): %ld\n", __cpp_variadic_templates);
#endif
#ifdef __cpp_initializer_lists
    printf("Initializer lists (C++11): %ld\n", __cpp_initializer_lists);
#endif
#ifdef __cpp_generic_lambdas
    printf("Generic lambdas (C++14): %ld\n", __cpp_generic_lambdas);
#endif
#ifdef __cpp_variable_templates
    printf("Variable templates (C++14): %ld\n", __cpp_variable_templates);
#endif
#ifdef __cpp_if_constexpr
    printf("If constexpr (C++17): %ld\n", __cpp_if_constexpr);
#endif
#ifdef __cpp_structured_bindings
    printf("Structured bindings (C++17): %ld\n", __cpp_structured_bindings);
#endif
}
