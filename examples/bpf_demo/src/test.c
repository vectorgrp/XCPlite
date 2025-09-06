

#include <sys/mman.h>
#include <unistd.h>

typedef int (*fun_t)(void);

staticint test(void) {
    size_t pagesz = sysconf(_SC_PAGESIZE);
    void *buf = mmap(NULL, pagesz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

    // Example: return 42;
    // AArch64: mov w0, #42 ; ret
    // Encodings: movz w0,#42 -> 0x52800540 ; ret -> 0xd65f03c0
    uint32_t code[] = {0x52800540u, 0xd65f03c0u};
    memcpy(buf, code, sizeof(code));

    // Make I-cache see our stores (does dc+ic+barriers under the hood)
    __builtin___clear_cache((char *)buf, (char *)buf + sizeof(code));

    // Flip to RX (W^X)
    mprotect(buf, pagesz, PROT_READ | PROT_EXEC /* | PROT_BTI if needed */);

    // Call it
    fun_t f = (fun_t)buf;
    int r = f(); // should be 42
    return r;
}

void mprotect_test(void) {
    int r = test();
    assert(r == 42);
    printf("Test %d\n", r);
}
