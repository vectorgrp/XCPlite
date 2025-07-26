#include <stdio.h>

void c_version_test(void) {

    // Check C standard version
    printf("\nC Language Version:\n");
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 202311L
    printf("C23 (ISO/IEC 9899:2024) - __STDC_VERSION__ = %ld\n", __STDC_VERSION__);
#elif __STDC_VERSION__ >= 201710L
    printf("C18 (ISO/IEC 9899:2018) - __STDC_VERSION__ = %ld\n", __STDC_VERSION__);
#elif __STDC_VERSION__ >= 201112L
    printf("C11 (ISO/IEC 9899:2011) - __STDC_VERSION__ = %ld\n", __STDC_VERSION__);
#elif __STDC_VERSION__ >= 199901L
    printf("C99 (ISO/IEC 9899:1999) - __STDC_VERSION__ = %ld\n", __STDC_VERSION__);
#elif __STDC_VERSION__ >= 199409L
    printf("C95 (ISO/IEC 9899/AMD1:1995) - __STDC_VERSION__ = %ld\n", __STDC_VERSION__);
#else
    printf("Unknown C standard - __STDC_VERSION__ = %ld\n", __STDC_VERSION__);
#endif
#else
#ifdef __STDC__
    printf("C89/C90 (ISO/IEC 9899:1990) - No __STDC_VERSION__ defined\n");
#else
    printf("Pre-standard C or non-compliant compiler\n");
#endif
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

    // Additional feature detection
    printf("\nC Standard Features:\n");
#ifdef __STDC_HOSTED__
    printf("Hosted implementation: %d\n", __STDC_HOSTED__);
#endif

#ifdef __STDC_IEC_559__
    printf("IEC 559 floating point: %d\n", __STDC_IEC_559__);
#endif

#ifdef __STDC_ISO_10646__
    printf("ISO 10646 wide characters: %ld\n", __STDC_ISO_10646__);
#endif

#ifdef __STDC_NO_ATOMICS__
    printf("Atomics not supported\n");
#else
    printf("Atomics supported\n");
#endif

#ifdef __STDC_NO_THREADS__
    printf("Threads not supported\n");
#else
    printf("Threads supported\n");
#endif
}
