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

#include <cstdint> // for uintxx_t
#include <cstdio>  // for printf, remove
#include <cstring> // for strcmp

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

//-----------------------------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    (void)argc;
    printf("\nXCP context test - %s\n\n", argv[0]);

    XcpSetLogLevel(OPTION_LOG_LEVEL);
    remove(BIN_FILE_NAME);
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

    // Span names are registered once, id 0 is "none"
    CHECK(XcpGetSpanCount() == 1 && strcmp(XcpGetSpanName(0), "none") == 0);
    uint16_t span_x = XcpRegisterSpanName("span_x");
    uint16_t span_y = XcpRegisterSpanName("span_y");
    CHECK(span_x == 1 && span_y == 2, "span ids are %u and %u", span_x, span_y);
    CHECK(XcpRegisterSpanName("span_x") == span_x);
    CHECK(strcmp(XcpGetSpanName(span_y), "span_y") == 0);
    CHECK(XcpGetSpanName(3) == nullptr);

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

    // The span of location1 is registered once
    uint16_t span_work = XcpRegisterSpanName("work");
    CHECK(span_work == 3 && XcpGetSpanCount() == 4, "span id is %u, span count is %u", span_work, XcpGetSpanCount());

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

    // Span name list full, span names are not persisted
    CHECK(XcpGetSpanCount() == 1);
    for (uint16_t i = 1; i < OPTION_DAQ_SPAN_COUNT; i++) {
        snprintf(name, sizeof(name), "span_%u", i);
        CHECK(XcpRegisterSpanName(name) == i);
    }
    CHECK(XcpRegisterSpanName("one_too_many") == 0);
    CHECK(XcpRegisterSpanName("span_1") == 1);

    XcpDeinit();
    remove(BIN_FILE_NAME);

    printf("PASSED\n");
    return 0;
}
