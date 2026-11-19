#pragma once
#define __XCPLIB_CONTEXT_HPP__

// xcplib_context.hpp
// Contexts and spans for XCPlite, inspired by OpenTelemetry
// Included by xcplib.hpp, when OPTION_DAQ_CONTEXT_LIST is enabled (default and shm configuration only)

// Copyright (c) Vector Informatik GmbH. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for details.

//-----------------------------------------------------------------------------------------------------
// Problem:
//   An XCP event is bound to a code location. When the same code is executed for several instances of a class or by
//   several threads, the values of all instances are mixed in one event and one measurement name.
//
// Concept:
//   Context  A named identity ("who is executing"), for example one per class instance or per worker thread.
//            Unlike an OpenTelemetry context, which is an anonymous id created per request, an XCP context must be named,
//            long-lived and there must be only a few of them, because every measurement has to be declared in the A2L file.
//   Scope    Makes a context the current context of the calling thread (attach) and restores the previous one when it goes
//            out of scope (detach). The current context is stored in thread-local storage, exactly as in OpenTelemetry.
//            A new thread does not inherit the context: capture the context in the parent and attach it in the new thread.
//   DaqEventCtxVar
//            Context-aware variant of DaqEventVar. Each context gets its own event (<context>.<event>)
//            and its own measurements, prefixed with the context name (<context>.<variable>).
//   Span     A named, timed section of code. Begin and end are recorded on the event <context>.spans of the current context,
//            with the current span, the nesting level and the duration of the span that ended.
//            With OPTION_DAQ_SPAN_THREAD (default), the index of the thread is recorded as well, it tells the spans of several
//            threads in the same context apart.
//
// Usage:
//   xcp::Context ctx("SigGen1");                              // once, for example as a class member
//   xcp::Scope scope(ctx);                                    // in the thread or method which works for this instance
//   DaqContext(Mainloop);                                     // or both in one for a literal name, created once per code location
//   DaqEventCtxVar(siggen, A2L_MEAS(value_, "Output"));       // event SigGen1.siggen, measurement SigGen1.value_
//   { DaqSpan(calc); ... }                                    // span "calc", ends at the end of the block
//
// Span names:
//   With OPTION_DAQ_SPAN_SECTION_REGISTRATION (default, ELF and Mach-O, not in SHM mode), the span names are registered at link time
//   in the linker section xcp_spans. The span ids are the same in every run of a binary and conv.spans is complete, also for
//   spans which have not been executed. A span name used at several code locations may have several span ids with the same name.
//   With GCC, DaqSpan can then not be used in inline functions, templates and member functions defined in the class body.
//   Without the option, span names are registered at runtime when a span is executed for the first time, in this order.
//
// Limitations:
//   - Contexts and context-aware events must have been executed once before the A2L file is finalized,
//     spans as well without OPTION_DAQ_SPAN_SECTION_REGISTRATION
//   - One XCP event per context plus one per context and context-aware code location, see OPTION_DAQ_EVENT_COUNT
//   - The first execution of a context-aware event in a context creates the event and registers the measurements. Other threads
//     which trigger it in the same context at this moment do not wait, they skip this one trigger
//   - The number of contexts is limited by OPTION_DAQ_CONTEXT_COUNT,
//     the number of span names by OPTION_DAQ_SPAN_COUNT (not limited with OPTION_DAQ_SPAN_SECTION_REGISTRATION)
//   - Attaching a context starts with no current span, the span of the capturing thread is not carried over
//   - A context should be executed by one thread at a time. Several threads may work in it one after the other, the hand-over
//     between them keeps the timestamps of its events in order. A context and its spans describe one track of activity.
//     When several threads trigger the same event at the same time (the span events of a context in which several threads
//     work at once, or a context-aware event at the same code location in the same context), a thread can be overtaken between
//     taking the timestamp and queueing the event, its sample then has an older timestamp than the sample before.
//     Nothing is serialized, a thread never waits for another one. For a worker pool, use one context per worker, or rely on
//     the thread index (OPTION_DAQ_SPAN_THREAD) to tell the threads apart. See docs/TIME_AND_CAUSALITY.md
//-----------------------------------------------------------------------------------------------------

#include <atomic>      // for std::atomic
#include <cstddef>     // for size_t
#include <cstdint>     // for uintxx_t
#include <cstdio>      // for snprintf
#include <type_traits> // for std::is_trivially_destructible

#include <a2l.hpp>    // for xcp::a2l::GetTypeIdFromExpr
#include <xcplib.hpp> // for MeasurementInfo, InstanceInfo

// Require constant initialization of a static variable: no initialization at runtime, so no compiler generated guard
// (__cxa_guard_acquire) on the first execution of the code location, a guard is not lock-free
#if defined(__cpp_constinit)
#define XCP_CONSTINIT constinit
#elif defined(__clang__)
#define XCP_CONSTINIT [[clang::require_constant_initialization]]
#elif defined(__GNUC__) && (__GNUC__ >= 10)
#define XCP_CONSTINIT __constinit
#else
#define XCP_CONSTINIT
#endif

namespace xcp {

constexpr size_t kMaxContextName = 15;                      // Maximum length of a context name
constexpr uint16_t kMaxContexts = OPTION_DAQ_CONTEXT_COUNT; // Maximum number of contexts, id 1..kMaxContexts, id 0 is "no context"
#ifndef OPTION_DAQ_SPAN_SECTION_REGISTRATION
constexpr uint16_t kMaxSpans = OPTION_DAQ_SPAN_COUNT; // Maximum number of different span names, id 1..kMaxSpans-1, id 0 is "no span"
#endif

class Context;

namespace detail {

// Per thread state: the current context and the current span of this thread
struct Current {
    const Context *context{nullptr};
    uint16_t span{0};  // Current span id, 0 = none
    uint16_t level{0}; // Current span nesting level
};

inline Current &current() {
    static thread_local Current c;
    return c;
}

// Measurement data of a span begin or end, sampled from the stack of the triggering thread
// The size is 8 bytes with and without OPTION_DAQ_SPAN_THREAD
struct SpanRecord {
    uint32_t span_dt; // Duration of the span that just ended in clock ticks (ns in the default configuration), 0 at span begin
    uint16_t span;    // Current span id after the begin or end
#ifdef OPTION_DAQ_SPAN_THREAD
    uint8_t level;  // Current nesting level after the begin or end, saturates at 255
    uint8_t thread; // Index of the triggering thread (1..255), see threadIndex
#else
    uint16_t level; // Current nesting level after the begin or end
#endif
};

#ifdef OPTION_DAQ_SPAN_THREAD

// Index of the calling thread, 1..255
// Assigned in the order in which the threads begin their first span, for the lifetime of the thread, indices are not reused
// When there are more than 255 threads, all further threads share the index 255
// The index is local to the process, it is not related to the thread id of the operating system
inline uint8_t threadIndex() {
    static std::atomic<uint16_t> count{0};
    static thread_local uint8_t index = 0;
    if (index == 0) {
        uint16_t n = count.load(std::memory_order_relaxed);
        while (n < 255 && !count.compare_exchange_weak(n, (uint16_t)(n + 1), std::memory_order_relaxed)) {
        }
        index = (uint8_t)(n < 255 ? n + 1 : 255);
    }
    return index;
}

inline SpanRecord makeSpanRecord(uint32_t span_dt, uint16_t span, uint16_t level) { return SpanRecord{span_dt, span, (uint8_t)(level > 255 ? 255 : level), threadIndex()}; }

#else

inline SpanRecord makeSpanRecord(uint32_t span_dt, uint16_t span, uint16_t level) { return SpanRecord{span_dt, span, level}; }

#endif

// Largest possible span id, the upper limit of the span measurement of a context
#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION
inline uint16_t maxSpanId() { return (uint16_t)(XcpGetSpanCount() - 1); } // The number of span names is known at link time
#else
inline uint16_t maxSpanId() { return kMaxSpans - 1; }
#endif

#ifndef OPTION_DAQ_SPAN_SECTION_REGISTRATION

// Span id of a code location, registered at runtime when the code location is executed for the first time
// The id is cached per code location in an atomic: 0 is not registered yet, kSpanIdFailed is a failed registration (span name list full)
// Registration is idempotent, all threads get the same id for a name, so the cache needs no lock and no once guard:
// when several threads execute the code location for the first time at the same moment, each of them registers and stores the same id
// Before XcpInit nothing is cached, the span is registered when it is executed for the first time after XcpInit
// Deliberately not a function static with dynamic initialization (static const uint16_t id = XcpRegisterSpanName(name)):
// the compiler protects such an initialization with a guard (__cxa_guard_acquire), which is not lock-free. A thread which
// reaches the code location while another thread is in the initialization blocks until that thread is done, for an unbounded
// time when that thread is preempted. The atomic below is constant initialized, it has no guard
constexpr uint16_t kSpanIdFailed = 0xFFFF;
inline uint16_t spanId(std::atomic<uint16_t> &cache, const char *name) {
    uint16_t id = cache.load(std::memory_order_relaxed);
    if (id == 0) {
        if (!XcpIsActivated())
            return 0;
        id = XcpRegisterSpanName(name);
        if (id == 0)
            id = kSpanIdFailed;
        cache.store(id, std::memory_order_relaxed);
    }
    return (id != kSpanIdFailed) ? id : 0;
}

#endif

// Once per context id: the span measurements of the context have been registered
// The array is value initialized with {} and required to be constant initialized: without the braces, GCC with libstdc++ in C++17
// mode initializes it at runtime under a guard (__cxa_guard_acquire), which is not lock-free
inline bool contextRegisterOnce(uint16_t context_id) {
    XCP_CONSTINIT static std::atomic<bool> registered[kMaxContexts + 1]{};
    return !registered[context_id].exchange(true);
}

} // namespace detail

//-----------------------------------------------------------------------------------------------------
// Context

class Context {

  public:
    /// Create a named context
    /// Creates the XCP event <name>.spans, which records the spans executed in this context
    /// Must be called after XcpInit, the number of contexts is limited by OPTION_DAQ_CONTEXT_COUNT
    /// If the context can not be created, code executed in this context behaves as without context
    /// Creating a context searches the context list and the event list by name: create the object once and keep it,
    /// for example as a class member. For a context with a literal name which is entered at a code location, use DaqContext
    /// @param name Context name, at most 15 characters
    explicit Context(const char *name) { create(name); }

    /// Construct a context which is not created yet, it is created by the first call of Create, see DaqContext
    /// Until then, code executed in this context behaves as without context
    /// The constructor is constexpr and the class is trivially destructible, so a static object is constant initialized:
    /// there is no initialization at runtime and no compiler generated guard at the code location
    constexpr Context() noexcept = default;

    /// Create the context if it has not been created yet, returns this
    /// After the context has been created, this is a single load, nothing is searched
    /// May be called by several threads at the same time: creating a context is idempotent, each of these threads creates or
    /// finds the context and its event, and all of them store the same values. No thread waits for another one to finish,
    /// but creating the event takes the event list mutex and registering the measurements takes the A2L lock for a short time
    /// Before XcpInit nothing happens and nothing is cached, the context is created by the first call after XcpInit
    /// @param name Context name, at most 15 characters, must be the same in every call for this object
    const Context *Create(const char *name) {
        if (!created_.load(std::memory_order_acquire))
            create(name);
        return this;
    }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    // Context name, nullptr if the context could not be created
    [[nodiscard]] const char *name() const { return name_.load(std::memory_order_relaxed); }
    // 1..kMaxContexts, 0 if the context could not be created
    [[nodiscard]] uint16_t id() const { return id_.load(std::memory_order_relaxed); }
    // Event <name>.spans of this context, used to record spans
    [[nodiscard]] tXcpEventId event() const { return event_.load(std::memory_order_relaxed); }

    /// Get the current context of the calling thread, nullptr if there is none
    /// Use it to capture the context in a parent thread and attach it with a Scope in another thread
    [[nodiscard]] static const Context *GetCurrent() { return detail::current().context; }

    // Trigger the spans event of this context with a span record and the timestamp clock, taken by the caller with ApplXcpGetClock64()
    // The caller takes the timestamp, so that the duration of a span is exactly the difference of the timestamps of its begin and end event
    // The measurement data is a record on the stack of the calling thread, the context itself has no mutable span state
    // A context may be attached to several threads at the same time. Their span events are not serialized and a thread never
    // waits for another one. The order of the samples is decided when the event is queued, after the timestamp was taken:
    // a thread which is overtaken by another one in between produces a sample with a declining timestamp
    // Using the newest timestamp of the context instead of an older one does not solve this. With more than two threads it
    // gave more declining timestamps than no correction in a stress test, and it needs a shared atomic in the context
    void TriggerSpanEvent(const detail::SpanRecord &record, uint64_t clock) const {
        const uint8_t *bases[] = {xcp_get_base_addr(), xcp_get_base_addr(), (const uint8_t *)&record, (const uint8_t *)&record};
        XcpEventExtAt_(event(), (int)(sizeof(bases) / sizeof(bases[0])), bases, clock);
    }

  private:
    // Create the context, its spans event and the span measurements
    // Idempotent: may be executed by several threads at the same time for the same object, all threads store the same values
    void create(const char *name) {

        if (!XcpIsActivated())
            return; // Not cached, Create tries again after XcpInit

        const uint16_t id = XcpCreateContext(name);
        if (id != 0) {
            const char *context_name = XcpGetContextName(id);
            name_.store(context_name, std::memory_order_relaxed);
            id_.store(id, std::memory_order_relaxed);
            const tXcpEventId event = XcpCreateContextEvent(id, "spans", 0, 0);
            if (event != XCP_UNDEFINED_EVENT_ID) {

                // Register the span measurements of this context, addressed relative to a SpanRecord given at the trigger
                // Exactly one thread does this for a context id
                if (detail::contextRegisterOnce(id)) {
                    detail::SpanRecord record{};
                    A2lLock();
                    A2lSetRelativeAddrMode__i(event, 1, (const uint8_t *)&record);
                    A2lCreateMeasurement_(context_name, "span", xcp::a2l::GetTypeIdFromExpr(record.span), 1, &record.span, "conv.spans", 0, detail::maxSpanId(),
                                          "Current span");
                    A2lCreateMeasurement_(context_name, "level", xcp::a2l::GetTypeIdFromExpr(record.level), 1, &record.level, nullptr, 0, 255,
                                          "Current span nesting level");
                    A2lCreateMeasurement_(context_name, "span_dt", xcp::a2l::GetTypeIdFromExpr(record.span_dt), 1, &record.span_dt, "ns", 0, 4294967295.0,
                                          "Duration of the span that ended");
#ifdef OPTION_DAQ_SPAN_THREAD
                    A2lCreateMeasurement_(context_name, "thread", xcp::a2l::GetTypeIdFromExpr(record.thread), 1, &record.thread, nullptr, 0, 255,
                                          "Index of the thread which began or ended the span");
#endif
                    A2lUnlock();
                }
                event_.store(event, std::memory_order_relaxed);
            }
        }

        // Created, or finally failed (context list or event list full): do not try again
        created_.store(true, std::memory_order_release);
    }

    // The members are atomics, because a static context (DaqContext) may be created by several threads at the same time
    // They are written once with the same values by all these threads and are constant afterwards
    std::atomic<const char *> name_{nullptr};
    std::atomic<uint16_t> id_{0};
    std::atomic<tXcpEventId> event_{XCP_UNDEFINED_EVENT_ID};
    std::atomic<bool> created_{false};
};

// A static context must not need a destructor call at exit, which would be registered under a guard
static_assert(std::is_trivially_destructible<Context>::value, "xcp::Context must be trivially destructible");

//-----------------------------------------------------------------------------------------------------
// Scope
// Attach a context to the calling thread, detach and restore the previous state in the destructor
// A scope is bound to the thread which created it, it must be destroyed in the same thread

class Scope {

  public:
    explicit Scope(const Context *context) : previous_(detail::current()) { detail::current() = detail::Current{context, 0, 0}; }
    explicit Scope(const Context &context) : Scope(&context) {}
    ~Scope() { detail::current() = previous_; }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    detail::Current previous_;
};

/// Enter a context with a literal name: the context is the current context of the calling thread until the end of the enclosing block
/// The context is created when the code location is executed for the first time after XcpInit, the context object is a static
/// at the code location. From then on nothing is created or searched, entering the context is a load and the attach of the scope
/// Use this where a context is entered in code which is executed more than once, instead of constructing an xcp::Context there
/// The static context is constant initialized, there is no compiler generated guard at the code location and no thread waits for
/// another one, see xcp::Context::Create
/// The same name at several code locations is the same context, each code location has its own context object for it
/// @param name Context name given as identifier, at most 15 characters
#define DaqContext(name)                                                                                                                                                           \
    static_assert(sizeof(#name) <= xcp::kMaxContextName + 1, "context name too long, at most 15 characters");                                                                      \
    XCP_CONSTINIT static xcp::Context ctx__##name;                                                                                                                                 \
    xcp::Scope scope__##name(ctx__##name.Create(#name))

//-----------------------------------------------------------------------------------------------------
// Span
// Begin a span in the constructor, end it in the destructor
// Without a current context with its span event, a span does nothing

class Span {

  public:
    explicit Span(uint16_t span_id) {
        detail::Current &current = detail::current();
        if (current.context == nullptr || current.context->event() == XCP_UNDEFINED_EVENT_ID)
            return;
        active_ = true;
        previous_span_ = current.span;
        current.span = span_id;
        current.level++;
        begin_ = ApplXcpGetClock64();
        current.context->TriggerSpanEvent(detail::makeSpanRecord(0, current.span, current.level), begin_);
    }

    ~Span() {
        if (!active_)
            return;
        detail::Current &current = detail::current();
        current.span = previous_span_;
        current.level--;
        // One timestamp for the duration and the end event: span_dt is exactly the difference of the timestamps of the begin and end event
        const uint64_t clock = ApplXcpGetClock64();
        const uint64_t dt = clock - begin_;
        current.context->TriggerSpanEvent(detail::makeSpanRecord(dt > UINT32_MAX ? UINT32_MAX : (uint32_t)dt, current.span, current.level), clock);
    }

    Span(const Span &) = delete;
    Span &operator=(const Span &) = delete;

  private:
    bool active_{false};
    uint16_t previous_span_{0};
    uint64_t begin_{0};
};

#ifdef OPTION_DAQ_SPAN_SECTION_REGISTRATION

/// Begin a span with the given name, the span ends at the end of the enclosing block
/// The span name is registered at link time: the macro emits a constant descriptor into the linker section xcp_spans,
/// the span id is the position of the descriptor in the section. Nothing is registered or looked up at runtime.
/// The A2L generator creates the enum conversion conv.spans from the section.
/// A span name used at several code locations may get several span ids, which all have the same name.
/// With GCC, the macro can not be used in inline functions, templates and member functions defined in the class body
/// (compile error "section type conflict"), use it in ordinary functions or disable OPTION_DAQ_SPAN_SECTION_REGISTRATION.
/// @param name Span name given as identifier, at most 31 characters
#define DaqSpan(name)                                                                                                                                                              \
    XCP_SPAN_SECTION_DECL(name);                                                                                                                                                   \
    xcp::Span span__##name(XCP_SPAN_SECTION_ID(name))

#else

/// Begin a span with the given name, the span ends at the end of the enclosing block
/// The span name is registered when the code location is executed for the first time, the span id is then cached at the code location,
/// the A2L generator creates the enum conversion conv.spans from all registered span names
/// @param name Span name given as identifier, longer names are truncated to 31 characters
#define DaqSpan(name)                                                                                                                                                              \
    XCP_CONSTINIT static std::atomic<uint16_t> span_id__##name{0};                                                                                                                 \
    xcp::Span span__##name(xcp::detail::spanId(span_id__##name, #name))

#endif

//-----------------------------------------------------------------------------------------------------
// Context-aware event trigger

namespace detail {

// Event of one code location, one state per context
// The state is kEventNew (no event yet), kEventClaimed (a thread is creating the event and registering the measurements right now),
// kEventFailed (the event could not be created) or the event id + kEventIdOffset
// An aggregate without constructor: a function static of this type which is initialized with {} (see DaqEventCtxVar_) is constant
// initialized to kEventNew. There is no initialization at runtime and so no compiler generated guard (__cxa_guard_acquire) at the
// code location, a guard is not lock-free
struct ContextEventIds {
    std::atomic<uint16_t> state[kMaxContexts + 1];
};
constexpr uint16_t kEventNew = 0;
constexpr uint16_t kEventClaimed = 1;
constexpr uint16_t kEventFailed = 2;
constexpr uint16_t kEventIdOffset = 3;
static_assert(OPTION_DAQ_EVENT_COUNT <= 0xFFFF - kEventIdOffset, "event ids must fit into the state of a context event");

// Register a single measurement with its own relative addressing mode XCP_ADDR_EXT_DYN + index and the context name as instance name
template <typename T> void registerContextMeasurement(uint8_t index, tXcpEventId event_id, const char *context_name, const MeasurementInfo<T> &info) {
    A2lSetRelativeAddrMode__i(event_id, index, (const uint8_t *)info.addr);
    A2lCreateMeasurement_(context_name, info.name, xcp::a2l::GetTypeIdFromExpr(info.value), info.dim, (const void *)info.addr, info.unit, info.min_value, info.max_value,
                          info.comment);
}

// Register a single typedef instance with its own relative addressing mode XCP_ADDR_EXT_DYN + index and the context name as instance name
template <typename T> void registerContextMeasurement(uint8_t index, tXcpEventId event_id, const char *context_name, const InstanceInfo<T> &info) {
    char name[128];
    if (context_name != nullptr) {
        snprintf(name, sizeof(name), "%s.%s", context_name, info.name);
    } else {
        snprintf(name, sizeof(name), "%s", info.name);
    }
    A2lSetRelativeAddrMode__i(event_id, index, (const uint8_t *)info.addr);
    A2lCreateInstance_(name, info.type_name, info.dim, (const void *)info.addr, info.comment);
}

} // namespace detail

// Create the context event and register the measurements once per context and code location, then trigger the event
// The first execution in a context is claimed by one thread with a compare-and-swap. This thread creates the event (event list mutex),
// registers the measurements (A2L lock) and publishes the event id. A thread which executes the code location in the same context
// in the meantime does not wait: it skips this one trigger. There is no mutex and no once guard at the code location.
// If the thread which claimed the event never finishes, the event of this code location and context is never triggered
template <typename... Measurements>
XCPLIB_ALWAYS_INLINE void DaqEventCtxVarTemplate(detail::ContextEventIds &ids, const Context *context, const char *event_name, Measurements &&...measurements) {
    if (XcpIsActivated()) {

        const uint16_t context_id = (context != nullptr) ? context->id() : 0;
        std::atomic<uint16_t> &state = ids.state[context_id];
        uint16_t s = state.load(std::memory_order_acquire);

        // Once per context and code location
        if (s < detail::kEventIdOffset) {

            // Claim the event, the compare-and-swap loads the current state when it fails
            if (s == detail::kEventNew && state.compare_exchange_strong(s, detail::kEventClaimed, std::memory_order_acq_rel, std::memory_order_acquire)) {

                // Event <context_name>.<event_name>, or <event_name> without a context
                const tXcpEventId new_event_id = XcpCreateContextEvent(context_id, event_name, 0, 0);
                if (new_event_id == XCP_UNDEFINED_EVENT_ID) {
                    state.store(detail::kEventFailed, std::memory_order_release); // Out of event list memory, do not try again
                    return;
                }

                // Measurements <context_name>.<variable>, each with its own base address
                const char *context_name = (context_id != 0) ? context->name() : nullptr;
                A2lLock();
                uint8_t index = 1; // Start at 1, 0 is reserved for frame pointer relative addressing mode
                (detail::registerContextMeasurement(index++, new_event_id, context_name, measurements), ...);
                A2lUnlock();

                // Publish the event id
                s = (uint16_t)(new_event_id + detail::kEventIdOffset);
                state.store(s, std::memory_order_release);
            }

            // Claimed by another thread which is not done yet, or failed: skip this trigger, do not wait
            else if (s < detail::kEventIdOffset) {
                return;
            }
        }

        // Always
        const tXcpEventId event_id = (tXcpEventId)(s - detail::kEventIdOffset);
        const uint8_t *bases[] = {xcp_get_base_addr(), xcp_get_base_addr(), xcp_get_frame_addr(), (const uint8_t *)measurements.addr...};
        XcpEventExtAt_(event_id, (int)(sizeof(bases) / sizeof(bases[0])), bases, 0);
        XCP_NO_TAIL_CALL();
    }
}

} // namespace xcp

/// Trigger an event in a given context, create the event and register the measurement variables once per context
/// The event is named <context name>.<event_name>, the measurement variables <context name>.<variable name>
/// The first execution in each context creates the event and registers the measurements, this is not lock-free for the one
/// thread which does it (event list mutex, A2L lock). Other threads which execute the code location in the same context at the
/// same moment do not wait, they skip this one trigger. Afterwards the trigger is wait-free.
/// @param context Pointer to the context (const xcp::Context *), nullptr for no context
/// @param event_name Event name given as identifier
#define DaqEventCtxVar_(context, event_name, ...)                                                                                                                                  \
    {                                                                                                                                                                              \
        XCP_CONSTINIT static xcp::detail::ContextEventIds evt_ids__##event_name{};                                                                                                 \
        xcp::DaqEventCtxVarTemplate(evt_ids__##event_name, context, #event_name, __VA_ARGS__);                                                                                     \
    }

/// Trigger an event in the current context of the calling thread
/// Without a current context, the event is a single global event, like DaqEventVar
#define DaqEventCtxVar(event_name, ...) DaqEventCtxVar_(xcp::Context::GetCurrent(), event_name, __VA_ARGS__)
