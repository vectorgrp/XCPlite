// namelist_test - unit and concurrency stress test for the lock-free name list (src/namelist.c)
//
// Self-checking, exits 0 on pass. POSIX only (pthreads).
// The module has no dependency on the XCP library, the test can be built standalone:
//
//   cc -std=c11 -O1 -g -fsanitize=thread -Isrc -Iinc src/namelist.c test/namelist_test/src/main.c -o namelist_test
//
// Usage: namelist_test [rounds]

#include <pthread.h>   // for pthread_create, pthread_join
#include <stdatomic.h> // for atomic_int
#include <stdbool.h>   // for bool
#include <stdint.h>    // for uintxx_t
#include <stdio.h>     // for printf, snprintf
#include <stdlib.h>    // for atoi
#include <string.h>    // for strcmp, strlen

#include "../../support/check.h" // for the shared CHECK macro

#include "../src/namelist.h"

#define WRITER_COUNT 8
#define READER_COUNT 2
#define KEY_COUNT 96 // Number of different keys (scope, name)
#define NAME_SIZE 16 // Maximum name length 15
#define MAX_SPARE (2 * WRITER_COUNT)

// User data of an entry
typedef struct {
    uint64_t hash;    // Depends on the key, to detect incomplete or mixed up entries
    uint32_t creator; // Writer thread which created the entry
} tTestData;

static NAMELIST_STORAGE(KEY_COUNT, MAX_SPARE, NAME_SIZE, sizeof(tTestData)) gStorage;
static tNameList *const gList = &gStorage.list;

//----------------------------------------------------------------------------------
// Keys

// Every name is used in 4 scopes
static uint32_t keyScope(uint32_t key) { return key & 3u; }
static void keyName(uint32_t key, char *name) { snprintf(name, NAME_SIZE, "name_%u", key >> 2); }

static uint64_t keyHash(uint32_t scope, const char *name) {
    uint64_t h = 1469598103934665603ull ^ scope;
    for (const char *p = name; *p != 0; p++)
        h = (h ^ (uint8_t)*p) * 1099511628211ull;
    return h;
}

//----------------------------------------------------------------------------------
// Basic single threaded tests

static void testBasics(void) {

    static NAMELIST_STORAGE(4, 0, NAME_SIZE, sizeof(tTestData)) storage;
    tNameList *list = &storage.list;
    bool created = false;

    // Parameter checks
    CHECK(!NameListInit(NULL, sizeof(storage), 4, 0, NAME_SIZE, sizeof(tTestData)));
    CHECK(!NameListInit(list, sizeof(storage), 0, 0, NAME_SIZE, sizeof(tTestData)));
    CHECK(!NameListInit(list, sizeof(storage), 4, 0, 1, sizeof(tTestData)));
    CHECK(!NameListInit(list, sizeof(storage) - 1, 4, 0, NAME_SIZE, sizeof(tTestData)));
    CHECK(!NameListInit(list, sizeof(storage), 5, 0, NAME_SIZE, sizeof(tTestData)));
    CHECK(!NameListInit(list, sizeof(storage), 4, 0, 0xFFFFFFFFu, sizeof(tTestData))); // Sizes which overflow 32 bit
    CHECK(!NameListInit(list, sizeof(storage), 4, 0, NAME_SIZE, 0xFFFFFFFFu));
    CHECK(!NameListInit(list, sizeof(storage), 0xFFFFFFFFu, 0xFFFFFFFFu, NAME_SIZE, sizeof(tTestData)));
    CHECK(NameListInit(list, sizeof(storage), 4, 0, NAME_SIZE, sizeof(tTestData)));

    // Empty list
    CHECK(NameListGetCount(list) == 0);
    CHECK(NameListGetCapacity(list) == 4);
    CHECK(NameListGetName(list, 0) == NULL);
    CHECK(NameListGetData(list, 0) == NULL);
    CHECK(NameListFind(list, 0, "a") == NAMELIST_UNDEFINED);
    CHECK(NameListRegister(list, 0, NULL, NULL, NULL) == NAMELIST_UNDEFINED);
    CHECK(NameListRegister(list, 0, "", NULL, NULL) == NAMELIST_UNDEFINED);
    CHECK(NameListGetCount(list) == 0);

    // Register and find
    tTestData d = {.hash = 0x1122334455667788ull, .creator = 42};
    CHECK(NameListRegister(list, 0, "a", &d, &created) == 0 && created);
    CHECK(NameListGetCount(list) == 1);
    CHECK(NameListRegister(list, 0, "b", NULL, &created) == 1 && created);
    CHECK(NameListGetCount(list) == 2);
    d.creator = 43;
    CHECK(NameListRegister(list, 0, "a", &d, &created) == 0 && !created); // Existing entry, data ignored
    CHECK(NameListGetCount(list) == 2);
    CHECK(NameListFind(list, 0, "a") == 0);
    CHECK(NameListFind(list, 0, "b") == 1);
    CHECK(NameListFind(list, 0, "c") == NAMELIST_UNDEFINED);
    CHECK(strcmp(NameListGetName(list, 0), "a") == 0);
    CHECK(strcmp(NameListGetName(list, 1), "b") == 0);
    CHECK(NameListGetName(list, 2) == NULL);
    CHECK(NameListGetName(list, 4) == NULL);
    CHECK(NameListGetName(list, NAMELIST_UNDEFINED) == NULL);

    // User data: copied for a new entry, cleared when NULL
    const tTestData *p = (const tTestData *)NameListGetConstData(list, 0);
    CHECK(p != NULL && p->hash == 0x1122334455667788ull && p->creator == 42);
    CHECK(((uintptr_t)p & 7u) == 0);
    p = (const tTestData *)NameListGetData(list, 1);
    CHECK(p != NULL && p->hash == 0 && p->creator == 0);
    CHECK(NameListGetConstData(list, 2) == NULL);

    // Scope is part of the key
    CHECK(NameListRegister(list, 7, "a", NULL, &created) == 2 && created);
    CHECK(NameListFind(list, 7, "a") == 2);
    CHECK(NameListFind(list, 0, "a") == 0);
    CHECK(NameListGetScope(list, 2) == 7);
    CHECK(NameListGetScope(list, 0) == 0);

    // Long names are truncated to NAME_SIZE-1 characters, a prefix is a different name
    CHECK(NameListRegister(list, 0, "0123456789abcdefXXX", NULL, &created) == 3 && created);
    CHECK(strcmp(NameListGetName(list, 3), "0123456789abcde") == 0);
    CHECK(NameListFind(list, 0, "0123456789abcdefYYY") == 3);
    CHECK(NameListFind(list, 0, "0123456789abcde") == 3);
    CHECK(NameListFind(list, 0, "0123456789abcd") == NAMELIST_UNDEFINED);

    // Full
    CHECK(NameListGetCount(list) == 4);
    CHECK(NameListRegister(list, 0, "c", NULL, &created) == NAMELIST_UNDEFINED && !created);
    CHECK(NameListRegister(list, 0, "b", NULL, &created) == 1 && !created); // Existing entries are still found
    CHECK(NameListGetCount(list) == 4);

    // A list without user data
    static NAMELIST_STORAGE(2, 0, 8, 0) names;
    CHECK(NameListInit(&names.list, sizeof(names), 2, 0, 8, 0));
    CHECK(NameListRegister(&names.list, 0, "none", NULL, NULL) == 0);
    CHECK(NameListGetData(&names.list, 0) == NULL);
    CHECK(NameListGetConstData(&names.list, 0) == NULL);
    CHECK(strcmp(NameListGetName(&names.list, 0), "none") == 0);

    printf("basics: ok\n");
}

//----------------------------------------------------------------------------------
// Concurrent stress test

static atomic_int gStart;                      // Start signal for the writers of a round
static atomic_int gDone;                       // Stop signal for the readers of a round
static uint32_t gRound;                        // Round number, changes the order of the keys
static uint32_t gIds[WRITER_COUNT][KEY_COUNT]; // Id returned for each key, by writer
static uint32_t gCreated[WRITER_COUNT];        // Number of entries created, by writer
static uint64_t gReaderChecks[READER_COUNT];   // Number of entries checked, by reader

// Writer: register all keys, in an order which depends on the writer and the round
// In the first rounds all writers use the same order, to maximize the races for the same key
static void *writerThread(void *arg) {
    uint32_t writer = (uint32_t)(uintptr_t)arg;
    uint32_t offset = (gRound % 4 == 0) ? 0 : writer * (gRound % 13);
    uint32_t created_count = 0;

    while (atomic_load(&gStart) == 0) {
    }

    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        uint32_t key = (i + offset) % KEY_COUNT;
        char name[NAME_SIZE];
        keyName(key, name);
        tTestData d = {.hash = keyHash(keyScope(key), name), .creator = writer};
        bool created = false;
        uint32_t id = NameListRegister(gList, keyScope(key), name, &d, &created);
        gIds[writer][key] = id;
        if (created) {
            created_count++;
            // The creator finds its own entry immediately, the count includes it
            CHECK(id != NAMELIST_UNDEFINED);
            CHECK(NameListGetCount(gList) > id);
            CHECK(NameListFind(gList, keyScope(key), name) == id);
        }
    }
    gCreated[writer] = created_count;
    return NULL;
}

// Reader: every entry below the count is complete and immutable, the count never decreases
static void *readerThread(void *arg) {
    uint32_t reader = (uint32_t)(uintptr_t)arg;
    uint32_t last_count = 0;
    uint64_t checks = 0;
    bool done = false;

    while (!done) {
        done = atomic_load(&gDone) != 0; // One more complete pass after the writers finished
        uint32_t count = NameListGetCount(gList);
        CHECK(count >= last_count, "count decreased from %u to %u", last_count, count);
        CHECK(count <= NameListGetCapacity(gList));
        last_count = count;
        for (uint32_t id = 0; id < count; id++) {
            const char *name = NameListGetName(gList, id);
            CHECK(name != NULL, "entry %u of %u does not exist", id, count);
            CHECK(strlen(name) >= 6 && strncmp(name, "name_", 5) == 0, "entry %u has name '%s'", id, name);
            uint32_t scope = NameListGetScope(gList, id);
            const tTestData *d = (const tTestData *)NameListGetConstData(gList, id);
            CHECK(d != NULL);
            CHECK(d->hash == keyHash(scope, name), "entry %u '%s' is incomplete", id, name);
            CHECK(d->creator < WRITER_COUNT);
            CHECK(NameListFind(gList, scope, name) == id, "entry %u '%s' is not unique", id, name);
            checks++;
        }
    }
    gReaderChecks[reader] += checks;
    return NULL;
}

typedef struct {
    uint64_t rounds;
    uint64_t contended;  // Rounds in which more than one writer created entries
    uint64_t cells_extra; // Entry cells which lost a race for the same key and are parked at the end of the round
    uint64_t cells_lost;  // Entry cells taken from the pool, but neither published nor parked
} tStats;

// One round: WRITER_COUNT writers register the same KEY_COUNT keys concurrently into an empty list
//   exact  true:  capacity and spare are sufficient, all keys must be registered
//          false: capacity is smaller than KEY_COUNT or there are no spare cells, registration may fail
static void runRound(uint32_t round, uint32_t capacity, uint32_t spare, bool exact, tStats *stats) {

    CHECK(NameListInit(gList, sizeof(gStorage), capacity, spare, NAME_SIZE, sizeof(tTestData)));
    gRound = round;
    atomic_store(&gStart, 0);
    atomic_store(&gDone, 0);

    pthread_t writer[WRITER_COUNT];
    pthread_t reader[READER_COUNT];
    for (uintptr_t i = 0; i < READER_COUNT; i++)
        CHECK(pthread_create(&reader[i], NULL, readerThread, (void *)i) == 0);
    for (uintptr_t i = 0; i < WRITER_COUNT; i++)
        CHECK(pthread_create(&writer[i], NULL, writerThread, (void *)i) == 0);
    atomic_store(&gStart, 1);
    for (uint32_t i = 0; i < WRITER_COUNT; i++)
        CHECK(pthread_join(writer[i], NULL) == 0);
    atomic_store(&gDone, 1);
    for (uint32_t i = 0; i < READER_COUNT; i++)
        CHECK(pthread_join(reader[i], NULL) == 0);

    uint32_t count = NameListGetCount(gList);
    CHECK(count <= capacity);
    CHECK((uint32_t)atomic_load(&gList->count) == count, "cached count %u, count %u", (uint32_t)atomic_load(&gList->count), count);
    if (exact)
        CHECK(count == (capacity < KEY_COUNT ? capacity : KEY_COUNT), "count is %u", count);

    // Every entry was created exactly once
    uint32_t created = 0;
    uint32_t creators = 0;
    for (uint32_t w = 0; w < WRITER_COUNT; w++) {
        created += gCreated[w];
        creators += gCreated[w] > 0 ? 1 : 0;
    }
    stats->contended += creators > 1 ? 1 : 0;
    CHECK(created == count, "%u entries created, count is %u", created, count);

    // All writers got the same id for the same key, the ids are unique and dense
    bool id_used[KEY_COUNT] = {false};
    for (uint32_t key = 0; key < KEY_COUNT; key++) {
        char name[NAME_SIZE];
        keyName(key, name);
        uint32_t id = NameListFind(gList, keyScope(key), name);
        for (uint32_t w = 0; w < WRITER_COUNT; w++) {
            if (exact || gIds[w][key] != NAMELIST_UNDEFINED) // Without spare cells a writer may fail, while another writer succeeds
                CHECK(gIds[w][key] == id, "key %u: writer %u got id %u, list has id %u", key, w, gIds[w][key], id);
        }
        if (id != NAMELIST_UNDEFINED) {
            CHECK(id < count);
            CHECK(!id_used[id], "id %u is used twice", id);
            id_used[id] = true;
            CHECK(strcmp(NameListGetName(gList, id), name) == 0);
            CHECK(NameListGetScope(gList, id) == keyScope(key));
        } else {
            CHECK(count == capacity || !exact, "key %u is missing, count %u", key, count);
        }
    }
    for (uint32_t id = 0; id < count; id++)
        CHECK(id_used[id], "id %u has no key", id);

    // Entry cells: published, parked or lost
    uint32_t alloc = (uint32_t)atomic_load(&gList->alloc);
    uint32_t parked = 0;
    for (uint32_t i = 0; i < NAMELIST_PARK_SLOTS; i++)
        parked += atomic_load(&gList->park[i]) != 0 ? 1 : 0;
    CHECK(alloc <= capacity + spare);
    CHECK(alloc >= count + parked);
    stats->cells_extra += parked;
    stats->cells_lost += alloc - count - parked;
    stats->rounds++;
}

static void printStats(const char *title, const tStats *stats) {
    printf("%s: ok, %" PRIu64 " rounds, %" PRIu64 " with more than one creating writer, %" PRIu64 " cells parked, %" PRIu64 " cells lost\n", title, stats->rounds, stats->contended,
           stats->cells_extra, stats->cells_lost);
}

int main(int argc, char *argv[]) {

    uint32_t rounds = argc > 1 ? (uint32_t)atoi(argv[1]) : 300;

    testBasics();

    // All keys fit, sufficient spare cells
    tStats s1 = {0};
    for (uint32_t r = 0; r < rounds; r++)
        runRound(r, KEY_COUNT, MAX_SPARE, true, &s1);
    printStats("concurrent register", &s1);

    // The list overflows, exactly capacity entries must be registered
    tStats s2 = {0};
    for (uint32_t r = 0; r < rounds; r++)
        runRound(r, KEY_COUNT / 2 + r % 7, MAX_SPARE, true, &s2);
    printStats("concurrent register, overflow", &s2);

    // No spare cells: single registrations may fail, the list must stay consistent
    tStats s3 = {0};
    for (uint32_t r = 0; r < rounds; r++)
        runRound(r, KEY_COUNT - r % 5, 0, false, &s3);
    printStats("concurrent register, no spare cells", &s3);

    uint64_t checks = 0;
    for (uint32_t i = 0; i < READER_COUNT; i++)
        checks += gReaderChecks[i];
    printf("readers checked %" PRIu64 " entries\n", checks);
    printf("namelist_test passed\n");
    return 0;
}
