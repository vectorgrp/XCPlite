// context_test - verifies contexts, context events and spans (OPTION_DAQ_CONTEXT_LIST)
//
// Goal:
//   An event which belongs to a context is identified by (context, event name) and is named <context name>.<event name>.
//   Contexts and context events are created once and found again by name, also after a restart with the binary persistence file.
//   The context-aware trigger DaqEventCtxVar creates one event per context and code location.
//
// Steps:
//   1. C core: contexts, context events, span names (idempotent creation, naming, lookup rules)
//   2. C++ API: two code locations executed in two contexts and without a context, scope nesting, spans
//   3. Persistence: write the BIN file, restart, contexts are rebuilt from the events and all ids are unchanged
//   4. Limits: context name too long, context list full, span name list full
//   5. DaqContext: a static context at a code location, executed before XcpInit, created once, entered by several threads at the same time
//   6. DaqEventCtxVar: several threads trigger a context event for the first time at the same time, one creates it, none waits
//
// Span names are tested in both registration modes, link time (OPTION_DAQ_SPAN_SECTION_REGISTRATION, default) and runtime

#include <atomic>  // for std::atomic
#include <cstdint> // for uintxx_t
#include <cstdio>  // for printf, remove
#include <cstring> // for strcmp
#include <thread>  // for std::thread

#include <a2l.hpp>    // for A2L generation application programming interface
#include <xcplib.hpp> // for application programming interface, xcp::Context, xcp::Scope, DaqSpan, DaqEventCtxVar

// Internal interfaces used to inspect the event list and to write the BIN file without an XCP client
extern "C" {
#include "ctx.h"         // for XcpGetSpanName, XcpGetSpanCount
#include "persistence.h" // for XcpBinWrite
#include "xcplite.h"     // for XcpGetEventName, XcpGetEventCount, XcpFindContextEvent, XcpGetEcuEpk
}

#include "../../support/check.h" // for the shared CHECK macro

#if !defined(OPTION_DAQ_CONTEXT_LIST) || !defined(OPTION_ENABLE_PERSISTENCE)
#error "context_test requires OPTION_DAQ_CONTEXT_LIST and OPTION_ENABLE_PERSISTENCE"
#endif

#define OPTION_PROJECT_NAME "context_test"
#define OPTION_PROJECT_VERSION "V1.0.0"
#define OPTION_LOG_LEVEL 2 // 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = print XCP commands
#define BIN_FILE_NAME OPTION_PROJECT_NAME "_" OPTION_PROJECT_VERSION ".bin"

//-----------------------------------------------------------------------------------------------------

// Check the full name and the context of an event
static void checkEvent(tXcpEventId id, const char *expected_name, uint16_t expected_context) {
    CHECK(id != XCP_UNDEFINED_EVENT_ID, "event '%s' not created", expected_name);
    const char *name = XcpGetEventName(id);
    CHECK(name != nullptr && strcmp(name, expected_name) == 0, "event %u is named '%s', expected '%s'", id, name != nullptr ? name : "(null)", expected_name);
    CHECK(XcpGetEventContext(id) == expected_context, "event '%s' has context %u, expected %u", expected_name, XcpGetEventContext(id), expected_context);
}

// Two code locations with context-aware instrumentation, shared by all contexts
// Not inlined, so each is exactly one code location
static uint32_t gCounter = 0;
static std::atomic<uint32_t> gSharedCounter{0};

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

NOINLINE static void location1() {
    uint32_t counter = ++gCounter;
    DaqSpan(work);
    DaqEventCtxVar(loc1, A2L_MEAS(counter, "Counter at location 1"));
}

NOINLINE static void location2() {
    uint32_t counter = ++gCounter;
    DaqEventCtxVar(loc2, A2L_MEAS(counter, "Counter at location 2"));
}

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION
// Two code locations which use the same span name, each returns its span id
NOINLINE static uint16_t span_dup_1() {
    XCP_SPAN_SECTION_DECL(dup);
    return XCP_SPAN_SECTION_ID(dup);
}
NOINLINE static uint16_t span_dup_2() {
    XCP_SPAN_SECTION_DECL(dup);
    return XCP_SPAN_SECTION_ID(dup);
}
#endif

// Three code locations which enter a context with a literal name, each returns the id of the current context inside the scope
NOINLINE static uint16_t enter_early() {
    DaqContext(early);
    return xcp::Context::GetCurrent()->id();
}
NOINLINE static uint16_t enter_once() {
    DaqContext(once);
    return xcp::Context::GetCurrent()->id();
}
NOINLINE static uint16_t enter_shared() {
    uint32_t counter = ++gSharedCounter;
    DaqContext(shared);
    DaqSpan(shared_work);
    DaqEventCtxVar(shared_evt, A2L_MEAS(counter, "Counter in the shared context"));
    return xcp::Context::GetCurrent()->id();
}

// The context event of a code location is claimed by one of the threads which trigger it for the first time at the same moment
// The code location is simulated with a new state object in each round, the event is the same in all rounds
static void raceContextEvent(int rounds, int thread_count) {
    tXcpEventId race_event = XCP_UNDEFINED_EVENT_ID;
    for (int round = 0; round < rounds; round++) {
        xcp::detail::ContextEventIds ids{};
        std::atomic<bool> start{false};
        std::atomic<int> ready{0};
        std::thread threads[16];
        CHECK(thread_count <= 16);
        for (int i = 0; i < thread_count; i++) {
            threads[i] = std::thread([&]() {
                uint32_t value = 1;
                ready.fetch_add(1);
                while (!start.load(std::memory_order_acquire)) {
                }
                // Triggers the event or skips the trigger while another thread creates it, never waits
                xcp::DaqEventCtxVarTemplate(ids, nullptr, "race", A2L_MEAS(value, "Value"));
            });
        }
        while (ready.load() < thread_count)
            std::this_thread::yield();
        start.store(true, std::memory_order_release);
        for (int i = 0; i < thread_count; i++)
            threads[i].join();

        // Exactly one thread created or found the event and published it, all others skipped or used it
        uint16_t state = ids.state[0].load();
        CHECK(state >= xcp::detail::kEventIdOffset, "round %d: state is %u", round, state);
        tXcpEventId event = (tXcpEventId)(state - xcp::detail::kEventIdOffset);
        if (round == 0)
            race_event = event;
        CHECK(event == race_event && event == XcpFindEvent("race"), "round %d: event id is %u, expected %u", round, event, race_event);
        for (uint16_t context = 1; context <= OPTION_DAQ_CONTEXT_COUNT; context++)
            CHECK(ids.state[context].load() == xcp::detail::kEventNew);
    }
}

//-----------------------------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    (void)argc;
    printf("\nXCP context test - %s\n\n", argv[0]);

    XcpSetLogLevel(OPTION_LOG_LEVEL);
    remove(BIN_FILE_NAME);

    // A static context which is entered before XcpInit is not created, code in it behaves as without context
    CHECK(enter_early() == 0);
    CHECK(xcp::Context::GetCurrent() == nullptr);

    CHECK(XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL | XCP_MODE_PERSISTENCE));
    CHECK(XcpIsActivated());
    CHECK(XcpGetContextCount() == 0);
    CHECK(XcpGetEventCount() == 0, "event count is %u after XcpInit", XcpGetEventCount());

    // 1. C core
    printf("C core\n");

    // Contexts are created once and found by name
    uint16_t ctx_a = XcpCreateContext("ctx_a");
    uint16_t ctx_b = XcpCreateContext("ctx_b");
    CHECK(ctx_a == 1 && ctx_b == 2, "context ids are %u and %u", ctx_a, ctx_b);
    CHECK(XcpCreateContext("ctx_a") == ctx_a);
    CHECK(XcpCreateContext("ctx_b") == ctx_b);
    CHECK(XcpGetContextCount() == 2);
    CHECK(strcmp(XcpGetContextName(ctx_a), "ctx_a") == 0);
    CHECK(strcmp(XcpGetContextName(ctx_b), "ctx_b") == 0);
    CHECK(XcpGetContextName(0) == nullptr);
    CHECK(XcpGetContextName(3) == nullptr);

    // An event is identified by (context, name), an event without context and context events with the same name coexist
    tXcpEventId evt = XcpCreateEvent("evt", 0, 0);
    tXcpEventId evt_a = XcpCreateContextEvent(ctx_a, "evt", 0, 0);
    tXcpEventId evt_b = XcpCreateContextEvent(ctx_b, "evt", 0, 0);
    checkEvent(evt, "evt", 0);
    checkEvent(evt_a, "ctx_a.evt", ctx_a);
    checkEvent(evt_b, "ctx_b.evt", ctx_b);
    CHECK(evt != evt_a && evt != evt_b && evt_a != evt_b);
    CHECK(XcpCreateEvent("evt", 0, 0) == evt);
    CHECK(XcpCreateContextEvent(ctx_a, "evt", 0, 0) == evt_a);
    CHECK(XcpCreateContextEvent(ctx_b, "evt", 0, 0) == evt_b);
    CHECK(XcpCreateContextEvent(0, "evt", 0, 0) == evt); // Context 0 is the event without context
    CHECK(XcpGetEventCount() == 3);

    // XcpFindEvent finds events without context only
    CHECK(XcpFindEvent("evt") == evt);
    tXcpEventId only_a = XcpCreateContextEvent(ctx_a, "only_a", 0, 0);
    checkEvent(only_a, "ctx_a.only_a", ctx_a);
    CHECK(XcpFindEvent("only_a") == XCP_UNDEFINED_EVENT_ID);
    CHECK(XcpFindContextEvent(ctx_a, "only_a") == only_a);
    CHECK(XcpFindContextEvent(ctx_b, "only_a") == XCP_UNDEFINED_EVENT_ID);

    // A context event needs an existing context
    CHECK(XcpCreateContextEvent(3, "evt", 0, 0) == XCP_UNDEFINED_EVENT_ID);

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION
    // Span names are registered at link time, id 0 is "none"
    // The list is complete before any span has been executed:
    // work in location1, shared_work in enter_shared, and two descriptors for dup in span_dup_1 and span_dup_2
    CHECK(XcpGetSpanCount() == 5, "span count is %u", XcpGetSpanCount());
    CHECK(strcmp(XcpGetSpanName(0), "none") == 0);
    CHECK(XcpGetSpanName(5) == nullptr);
    uint16_t span_work = 0;
    uint16_t dup_count = 0;
    for (uint16_t id = 1; id < XcpGetSpanCount(); id++) {
        const char *span_name = XcpGetSpanName(id);
        CHECK(span_name != nullptr);
        if (strcmp(span_name, "work") == 0)
            span_work = id;
        if (strcmp(span_name, "dup") == 0)
            dup_count++;
    }
    CHECK(span_work != 0 && dup_count == 2, "span work has id %u, %u spans are named dup", span_work, dup_count);
    // A span name used at two code locations has two ids, each code location always gets its own id
    uint16_t dup_1 = span_dup_1();
    uint16_t dup_2 = span_dup_2();
    CHECK(dup_1 != dup_2 && dup_1 != span_work && dup_2 != span_work, "span ids are %u and %u", dup_1, dup_2);
    CHECK(dup_1 >= 1 && dup_1 <= 4 && strcmp(XcpGetSpanName(dup_1), "dup") == 0);
    CHECK(dup_2 >= 1 && dup_2 <= 4 && strcmp(XcpGetSpanName(dup_2), "dup") == 0);
    CHECK(span_dup_1() == dup_1 && span_dup_2() == dup_2);
#else
    // Span names are registered once, id 0 is "none"
    CHECK(XcpGetSpanCount() == 1 && strcmp(XcpGetSpanName(0), "none") == 0);
    uint16_t span_x = XcpRegisterSpanName("span_x");
    uint16_t span_y = XcpRegisterSpanName("span_y");
    CHECK(span_x == 1 && span_y == 2, "span ids are %u and %u", span_x, span_y);
    CHECK(XcpRegisterSpanName("span_x") == span_x);
    CHECK(strcmp(XcpGetSpanName(span_y), "span_y") == 0);
    CHECK(XcpGetSpanName(3) == nullptr);
    // Names longer than 31 characters are truncated, the reserved name of span id 0 is not registered again
    CHECK(XcpRegisterSpanName("a_span_name_with_more_than_31_characters") == 3);
    CHECK(strcmp(XcpGetSpanName(3), "a_span_name_with_more_than_31_c") == 0);
    CHECK(XcpRegisterSpanName("a_span_name_with_more_than_31_c") == 3);
    CHECK(XcpRegisterSpanName("none") == 0 && XcpGetSpanCount() == 4);
#endif

    // 2. C++ API
    printf("C++ API\n");
    uint16_t event_count = XcpGetEventCount();

    // A context object for an existing context name gets the same id, and its spans event
    xcp::Context context_a("ctx_a");
    xcp::Context context_c("ctx_c");
    uint16_t ctx_c = context_c.id();
    CHECK(context_a.id() == ctx_a);
    CHECK(ctx_c == 3, "context id is %u", ctx_c);
    CHECK(strcmp(context_c.name(), "ctx_c") == 0);
    checkEvent(context_a.event(), "ctx_a.spans", ctx_a);
    checkEvent(context_c.event(), "ctx_c.spans", ctx_c);
    CHECK(XcpGetEventCount() == event_count + 2);

    // A second context object with the same name is the same context: same id, name and spans event, nothing is created
    {
        xcp::Context context_c2("ctx_c");
        CHECK(context_c2.id() == ctx_c && context_c2.event() == context_c.event() && context_c2.name() == context_c.name());
        CHECK(XcpGetContextCount() == 3 && XcpGetEventCount() == event_count + 2);
    }

    // Scopes nest and restore the previous context
    CHECK(xcp::Context::GetCurrent() == nullptr);
    {
        xcp::Scope scope_a(context_a);
        CHECK(xcp::Context::GetCurrent() == &context_a);
        {
            xcp::Scope scope_c(context_c);
            CHECK(xcp::Context::GetCurrent() == &context_c);
        }
        CHECK(xcp::Context::GetCurrent() == &context_a);
    }
    CHECK(xcp::Context::GetCurrent() == nullptr);

    // Two code locations, executed without context and in two contexts, several times
    for (int i = 0; i < 3; i++) {
        location1();
        location2();
        {
            xcp::Scope scope(context_a);
            location1();
            location2();
        }
        {
            xcp::Scope scope(context_c);
            location1();
            location2();
        }
    }
    tXcpEventId loc1 = XcpFindEvent("loc1");
    tXcpEventId loc2 = XcpFindEvent("loc2");
    tXcpEventId loc1_a = XcpFindContextEvent(ctx_a, "loc1");
    tXcpEventId loc2_a = XcpFindContextEvent(ctx_a, "loc2");
    tXcpEventId loc1_c = XcpFindContextEvent(ctx_c, "loc1");
    tXcpEventId loc2_c = XcpFindContextEvent(ctx_c, "loc2");
    checkEvent(loc1, "loc1", 0);
    checkEvent(loc2, "loc2", 0);
    checkEvent(loc1_a, "ctx_a.loc1", ctx_a);
    checkEvent(loc2_a, "ctx_a.loc2", ctx_a);
    checkEvent(loc1_c, "ctx_c.loc1", ctx_c);
    checkEvent(loc2_c, "ctx_c.loc2", ctx_c);
    CHECK(XcpFindContextEvent(ctx_b, "loc1") == XCP_UNDEFINED_EVENT_ID); // Never executed in context b
    CHECK(XcpGetEventCount() == event_count + 2 + 6, "event count is %u", XcpGetEventCount());

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION
    // Executing spans does not change the span names
    CHECK(XcpGetSpanCount() == 5);
#else
    // The span of location1 is registered once, when it is executed for the first time
    uint16_t span_work = XcpRegisterSpanName("work");
    CHECK(span_work == 4 && XcpGetSpanCount() == 5, "span id is %u, span count is %u", span_work, XcpGetSpanCount());
#endif

    // 3. Persistence
    printf("Persistence\n");
    event_count = XcpGetEventCount();
    CHECK(XcpBinWrite(XcpGetEcuEpk()));
    XcpDeinit();
    CHECK(XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL | XCP_MODE_PERSISTENCE));

    // All events are preloaded with their context, the context list is rebuilt from the events
    // Context b has no event, it is not persisted: the context ids may differ, the names must not
    CHECK(XcpGetEventCount() == event_count, "event count is %u after reload, expected %u", XcpGetEventCount(), event_count);
    CHECK(XcpGetContextCount() == 3, "context count is %u after reload", XcpGetContextCount());
    uint16_t ctx_a2 = XcpCreateContext("ctx_a");
    uint16_t ctx_b2 = XcpCreateContext("ctx_b");
    uint16_t ctx_c2 = XcpCreateContext("ctx_c");
    CHECK(XcpGetContextCount() == 3);
    checkEvent(evt, "evt", 0);
    checkEvent(evt_a, "ctx_a.evt", ctx_a2);
    checkEvent(evt_b, "ctx_b.evt", ctx_b2);
    checkEvent(loc1, "loc1", 0);
    checkEvent(loc1_a, "ctx_a.loc1", ctx_a2);
    checkEvent(loc2_c, "ctx_c.loc2", ctx_c2);
    checkEvent(context_c.event(), "ctx_c.spans", ctx_c2);

    // Creating the events again returns the preloaded ones
    CHECK(XcpCreateEvent("evt", 0, 0) == evt);
    CHECK(XcpCreateContextEvent(ctx_a2, "evt", 0, 0) == evt_a);
    CHECK(XcpCreateContextEvent(ctx_b2, "evt", 0, 0) == evt_b);
    CHECK(XcpCreateContextEvent(ctx_a2, "loc1", 0, 0) == loc1_a);
    CHECK(XcpCreateContextEvent(ctx_c2, "loc2", 0, 0) == loc2_c);
    CHECK(XcpCreateContextEvent(ctx_c2, "spans", 0, 0) == context_c.event());
    CHECK(XcpGetEventCount() == event_count);

    // 4. Limits
    printf("Limits\n");
    XcpSetLogLevel(0); // The following errors are expected

    // Context name empty or too long
    CHECK(XcpCreateContext("") == 0);
    CHECK(XcpCreateContext("0123456789ABCDEF") == 0); // 16 characters
    CHECK(XcpCreateContext("0123456789ABCDE") == 4);  // 15 characters

    // Context list full
    char name[16];
    for (uint16_t i = XcpGetContextCount(); i < OPTION_DAQ_CONTEXT_COUNT; i++) {
        snprintf(name, sizeof(name), "fill_%u", i);
        CHECK(XcpCreateContext(name) == i + 1);
    }
    CHECK(XcpCreateContext("one_too_many") == 0);
    CHECK(XcpCreateContext("ctx_a") == ctx_a2); // Existing contexts are still found

    // A context object which could not be created behaves as no context
    {
        xcp::Context context_x("one_too_many");
        CHECK(context_x.id() == 0 && context_x.event() == XCP_UNDEFINED_EVENT_ID);
        xcp::Scope scope(context_x);
        event_count = XcpGetEventCount();
        location1(); // Triggers the event without context, the span does nothing
        CHECK(XcpGetEventCount() == event_count);
    }

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION
    // The span names and span ids are the same after a restart
    CHECK(XcpGetSpanCount() == 5 && strcmp(XcpGetSpanName(span_work), "work") == 0);
    CHECK(span_dup_1() == dup_1 && span_dup_2() == dup_2);
#else
    // Span name list full, span names are not persisted
    CHECK(XcpGetSpanCount() == 1);
    for (uint16_t i = 1; i < OPTION_DAQ_SPAN_COUNT; i++) {
        snprintf(name, sizeof(name), "span_%u", i);
        CHECK(XcpRegisterSpanName(name) == i);
    }
    CHECK(XcpRegisterSpanName("one_too_many") == 0);
    CHECK(XcpRegisterSpanName("span_1") == 1);
#endif

    XcpDeinit();
    remove(BIN_FILE_NAME);

    // 5. DaqContext
    // The static contexts of the three code locations have not been created yet, start with empty lists
    printf("DaqContext\n");
    XcpSetLogLevel(OPTION_LOG_LEVEL);
    CHECK(XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL | XCP_MODE_PERSISTENCE));
    CHECK(XcpGetContextCount() == 0 && XcpGetEventCount() == 0, "%u contexts and %u events after XcpInit", XcpGetContextCount(), XcpGetEventCount());

    // The code location which was executed before XcpInit creates its context now
    uint16_t ctx_early = enter_early();
    CHECK(ctx_early == 1 && XcpGetContextCount() == 1, "context id is %u, context count is %u", ctx_early, XcpGetContextCount());
    CHECK(strcmp(XcpGetContextName(ctx_early), "early") == 0);
    checkEvent(XcpFindContextEvent(ctx_early, "spans"), "early.spans", ctx_early);
    CHECK(xcp::Context::GetCurrent() == nullptr); // The scope ended with the function

    // A context is created once, it is the context with this name
    uint16_t ctx_once = enter_once();
    CHECK(ctx_once == 2 && enter_once() == ctx_once && enter_early() == ctx_early);
    CHECK(XcpCreateContext("once") == ctx_once);
    CHECK(XcpGetContextCount() == 2 && XcpGetEventCount() == 2, "%u contexts and %u events", XcpGetContextCount(), XcpGetEventCount());

    // Several threads enter the same code location for the first time at the same moment: one context, one event, the same id for all
    {
        constexpr int kThreads = 4;
        std::atomic<bool> start{false};
        std::atomic<int> ready{0};
        uint16_t ids[kThreads] = {};
        std::thread threads[kThreads];
        for (int i = 0; i < kThreads; i++) {
            threads[i] = std::thread([&, i]() {
                ready.fetch_add(1);
                while (!start.load(std::memory_order_acquire)) {
                }
                ids[i] = enter_shared();
                CHECK(xcp::Context::GetCurrent() == nullptr);
            });
        }
        while (ready.load() < kThreads)
            std::this_thread::yield();
        start.store(true, std::memory_order_release);
        for (auto &t : threads)
            t.join();
        for (int i = 0; i < kThreads; i++)
            CHECK(ids[i] == 3, "thread %d got context id %u", i, ids[i]);
        CHECK(strcmp(XcpGetContextName(3), "shared") == 0);
        // One spans event per context, and one context event of the code location in the shared context
        CHECK(XcpGetContextCount() == 3 && XcpGetEventCount() == 4, "%u contexts and %u events", XcpGetContextCount(), XcpGetEventCount());
        checkEvent(XcpFindContextEvent(3, "shared_evt"), "shared.shared_evt", 3);
        CHECK(enter_shared() == 3);
        CHECK(XcpGetEventCount() == 4);
    }

    // 6. DaqEventCtxVar, first execution of a code location by several threads at the same time
    printf("DaqEventCtxVar race\n");
    raceContextEvent(100, 4);
    CHECK(XcpGetEventCount() == 5, "%u events", XcpGetEventCount());

    XcpDeinit();
    remove(BIN_FILE_NAME);

    printf("PASSED\n");
    return 0;
}
