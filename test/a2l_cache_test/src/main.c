// Each cache scenario runs in its own process because the A2L generator is a singleton.
// Run: ctest --test-dir build -R '^a2l_cache_' -V
#include <stdio.h>
#include <string.h>

#include "a2l.h"
#include "xcplib.h"
#include "persistence.h"
#include "../../support/check.h"

int main(int argc, char **argv) {
    CHECK(argc == 2, "expected empty or nonempty cache scenario");
    const bool empty = strcmp(argv[1], "empty") == 0;
    CHECK(empty || (strcmp(argv[1], "nonempty") == 0), "unknown cache scenario: %s", argv[1]);
    const char *project = empty ? "a2l_cache_empty" : "a2l_cache_nonempty";
    char a2l_filename[128];
    char bin_filename[128];
    snprintf(bin_filename, sizeof(bin_filename), "%s_1.0.bin", project);
    remove(bin_filename);

    // WRITE_ONCE requires persistence; otherwise A2lInit forces regeneration in both cases.
    XcpSetLogLevel(0);
    CHECK(XcpInit(project, "1.0", XCP_MODE_LOCAL | XCP_MODE_PERSISTENCE), "could not initialize XCP");
    snprintf(a2l_filename, sizeof(a2l_filename), "%s", A2lGetFilename());
    snprintf(bin_filename, sizeof(bin_filename), "%s", XcpBinGetFilename());

    // Seed an empty cache or recognizable content that must survive reuse unchanged.
    const char cached_content[] = "existing A2L cache\n";
    FILE *file = fopen(a2l_filename, "wb");
    CHECK(file != NULL, "could not create cache fixture");
    if (!empty) {
        CHECK(fwrite(cached_content, 1, sizeof(cached_content) - 1, file) == sizeof(cached_content) - 1, "could not write cache fixture");
    }
    CHECK(fclose(file) == 0, "could not close cache fixture");

    const uint8_t address[] = {127, 0, 0, 1};
    CHECK(A2lInit(address, 5555, false, A2L_MODE_WRITE_ONCE), "could not initialize A2L generation");

    // Finalize returns true only when generation was started, rather than the cache reused.
    CHECK(A2lFinalize() == empty, "an empty cache must regenerate; a nonempty cache must be reused");

    file = fopen(a2l_filename, "rb");
    CHECK(file != NULL, "A2L file is unavailable after initialization");
    char content[16384];
    const size_t length = fread(content, 1, sizeof(content) - 1, file);
    CHECK(!ferror(file), "could not read resulting A2L file");
    CHECK(fclose(file) == 0, "could not close resulting A2L file");
    content[length] = 0;
    if (empty) {
        CHECK(strstr(content, "/begin PROJECT") != NULL, "empty cache was not replaced with generated A2L content");
        CHECK(strstr(content, "/end PROJECT") != NULL, "generated A2L file is incomplete");
    } else {
        CHECK(strcmp(content, cached_content) == 0, "existing nonempty cache was overwritten");
    }

    XcpDeinit();
    CHECK(remove(a2l_filename) == 0, "could not remove A2L fixture");
    if (empty) {
        // Regeneration also creates the associated persistence file.
        CHECK(remove(bin_filename) == 0, "could not remove generated persistence file");
    }
    printf("PASSED: %s A2L cache\n", argv[1]);
    return 0;
}
