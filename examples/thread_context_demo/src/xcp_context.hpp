#pragma once

//-----------------------------------------------------------------------------------------------------
// Prototype: context and spans for XCPlite, inspired by OpenTelemetry
//
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
//            Context-aware variant of DaqEventVar. Each context gets its own event instance (<event>_<context index>)
//            and its own measurements, prefixed with the context name (<context>.<variable>).
//   Span     A named, timed section of code. Begin and end are recorded on the event of the current context, with
//            the current span, the nesting level and the duration of the span that ended.
//
// Usage:
//   xcp::Context ctx("SigGen1");                              // once, for example as a class member
//   xcp::Scope scope(ctx);                                    // in the thread or method which works for this instance
//   DaqEventCtxVar(siggen, A2L_MEAS(value_, "Output"));       // event siggen_1, measurement SigGen1.value_
//   { DaqSpan(calc); ... }                                    // span "calc", ends at the end of the block
//   xcp::A2lCreateSpanConversion();                           // once, after all spans have been executed once
//
// Limitations of the prototype:
//   - Default configuration only (runtime event creation and on-target A2L generation)
//   - Contexts, context-aware events and spans must have been executed once before the A2L file is finalized
//   - One XCP event per context plus one per context and context-aware code location, see OPTION_DAQ_EVENT_COUNT
//   - Attaching a context starts with no current span, the span of the capturing thread is not carried over
//   - A context-aware event must not be triggered concurrently by several threads in the same context at the same code
//     location, the timestamps of an XCP event must be monotonic (the span events of a context are serialized for this reason)
//-----------------------------------------------------------------------------------------------------

/*
Questions:
- Could we move A2lCreateSpanConversion into the A2L generator finalize ?
- In OTel a context is a trace-id and a span-id (plus flags t.b.d.), a tracer has a name and a trace-id, add this to make it more OTel like ?
- What if a class instance does not know its identity (the instance name), could it just capture the current context ?
*/

#include <atomic>  // for std::atomic
#include <cstdint> // for uintxx_t
#include <cstdio>  // for snprintf
#include <cstring> // for strncpy
#include <mutex>   // for std::mutex

#include "a2l.hpp"    // for A2l generation application programming interface
#include "xcplib.hpp" // for application programming interface

#include "dbg_print.h" // for DBG_PRINT

namespace xcp {

constexpr uint16_t kMaxContexts = 8;      // Maximum number of contexts, index 1..kMaxContexts, index 0 is "no context"
constexpr uint16_t kMaxSpans = 32;        // Maximum number of different span names, id 1..kMaxSpans-1, id 0 is "no span"
constexpr size_t kMaxContextNameLen = 15; // Limited by the maximum length of an XCP event name

class Context;

//-----------------------------------------------------------------------------------------------------
// Span names
// A span name gets a small id, which is measured and displayed with the enum conversion conv.spans

namespace detail {

struct SpanNames {
    std::mutex mutex;
    uint16_t count{1}; // id 0 is reserved for "no span"
    const char *name[kMaxSpans]{"none"};
};

inline SpanNames &spanNames() {
    static SpanNames names;
    return names;
}

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
struct SpanRecord {
    uint32_t span_dt; // Duration of the span that just ended in clock ticks (ns in the default configuration), 0 at span begin
    uint16_t span;    // Current span id after the begin or end
    uint16_t level;   // Current nesting level after the begin or end
};

} // namespace detail

// Register a span name, returns the span id
// The name must have static lifetime
inline uint16_t RegisterSpanName(const char *name) {
    DBG_PRINTF3(ANSI_COLOR_YELLOW "Register span name '%s'\n" ANSI_COLOR_RESET, name);

    detail::SpanNames &names = detail::spanNames();
    std::lock_guard<std::mutex> lock(names.mutex);
    for (uint16_t i = 1; i < names.count; i++) {
        if (strcmp(names.name[i], name) == 0)
            return i;
    }
    if (names.count >= kMaxSpans)
        return 0; // Too many span names, treated as "no span"
    names.name[names.count] = name;
    return names.count++;
}

// Create the A2L enum conversion conv.spans for all span names registered so far
// Must be called once, after all spans have been executed once and before the A2L file is finalized
inline void A2lCreateSpanConversion() {

    detail::SpanNames &names = detail::spanNames();
    std::lock_guard<std::mutex> lock(names.mutex);
    char description[kMaxSpans * 40];
    int n = snprintf(description, sizeof(description), "%u", names.count);
    for (uint16_t i = 0; i < names.count && n < (int)sizeof(description); i++) {
        n += snprintf(description + n, sizeof(description) - (size_t)n, " %u \"%s\"", i, names.name[i]);
    }
    A2lLock();
    A2lCreateEnumConversion_("spans", description);
    A2lUnlock();
}

//-----------------------------------------------------------------------------------------------------
// Context

class Context {

  public:
    // Create a named context
    // Creates the XCP event <name>, which records the spans executed in this context
    // Must be called after XcpInit, the number of contexts is limited to kMaxContexts
    explicit Context(const char *name) {

        DBG_PRINTF3(ANSI_COLOR_YELLOW "Create context '%s'\n" ANSI_COLOR_RESET, name);
        strncpy(name_, name, kMaxContextNameLen);
        if (!XcpIsActivated())
            return;

        static std::atomic<uint16_t> context_count{0};
        uint16_t index = (uint16_t)(context_count.fetch_add(1) + 1);
        if (index > kMaxContexts) {
            printf("ERROR: too many contexts, context '%s' is not separated from other contexts\n", name_);
            return;
        }
        index_ = index;
        event_ = XcpCreateEvent(name_, 0, 0);

        // Register the span measurements of this context, addressed relative to a SpanRecord given at the trigger
        detail::SpanRecord record{};
        A2lLock();
        A2lSetRelativeAddrMode__i(event_, 1, (const uint8_t *)&record);
        A2lCreateMeasurement_(name_, "span", xcp::a2l::GetTypeIdFromExpr(record.span), 1, &record.span, "conv.spans", 0, kMaxSpans - 1, "Current span");
        A2lCreateMeasurement_(name_, "level", xcp::a2l::GetTypeIdFromExpr(record.level), 1, &record.level, nullptr, 0, 255, "Current span nesting level");
        A2lCreateMeasurement_(name_, "span_dt", xcp::a2l::GetTypeIdFromExpr(record.span_dt), 1, &record.span_dt, "ns", 0, 4294967295.0, "Duration of the span that ended");
        A2lUnlock();
    }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    [[nodiscard]] const char *name() const { return name_; }
    [[nodiscard]] uint16_t index() const { return index_; }    // 1..kMaxContexts, 0 if the context could not be created
    [[nodiscard]] tXcpEventId event() const { return event_; } // Event of this context, used to record spans

    // Get the current context of the calling thread, nullptr if there is none
    // Use it to capture the context in a parent thread and attach it with a Scope in another thread
    [[nodiscard]] static const Context *GetCurrent() { return detail::current().context; }

    // Trigger the event of this context with a span record, returns the timestamp of the event
    // The measurement data is a record on the stack of the calling thread, the context itself has no mutable span state
    // A context may be attached to several threads at the same time, but the timestamps of an XCP event must be monotonic,
    // so taking the timestamp and queueing the event is serialized per context with a spin lock (uncontended in the normal case)
    uint64_t TriggerSpanEvent(const detail::SpanRecord &record) const {
        while (lock_.test_and_set(std::memory_order_acquire)) {
        }
        const uint64_t clock = ApplXcpGetClock64();
        const uint8_t *bases[] = {xcp_get_base_addr(), xcp_get_base_addr(), (const uint8_t *)&record, (const uint8_t *)&record};
        XcpEventExtAt_(event_, (int)(sizeof(bases) / sizeof(bases[0])), bases, clock);
        lock_.clear(std::memory_order_release);
        return clock;
    }

  private:
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    char name_[kMaxContextNameLen + 1]{};
    uint16_t index_{0};
    tXcpEventId event_{XCP_UNDEFINED_EVENT_ID};
};

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

//-----------------------------------------------------------------------------------------------------
// Span
// Begin a span in the constructor, end it in the destructor
// Without a current context, a span does nothing

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
        begin_ = current.context->TriggerSpanEvent(detail::SpanRecord{0, current.span, current.level});
    }

    ~Span() {
        if (!active_)
            return;
        detail::Current &current = detail::current();
        current.span = previous_span_;
        current.level--;
        // The duration is measured up to the end of the span, the timestamp of the end event is taken slightly later
        const uint64_t dt = ApplXcpGetClock64() - begin_;
        current.context->TriggerSpanEvent(detail::SpanRecord{dt > UINT32_MAX ? UINT32_MAX : (uint32_t)dt, current.span, current.level});
    }

    Span(const Span &) = delete;
    Span &operator=(const Span &) = delete;

  private:
    bool active_{false};
    uint16_t previous_span_{0};
    uint64_t begin_{0};
};

/// Begin a span with the given name, the span ends at the end of the enclosing block
/// @param name Span name given as identifier
#define DaqSpan(name)                                                                                                                                                              \
    static const uint16_t span_id__##name = xcp::RegisterSpanName(#name);                                                                                                          \
    xcp::Span span__##name(span_id__##name)

//-----------------------------------------------------------------------------------------------------
// Context-aware event trigger

namespace detail {

// Event ids of one code location, one per context
struct ContextEventIds {
    std::atomic<tXcpEventId> id[kMaxContexts + 1];
    ContextEventIds() {
        for (auto &i : id)
            i.store(XCP_UNDEFINED_EVENT_ID, std::memory_order_relaxed);
    }
};

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

// Create the event instance and register the measurements once per context and code location, then trigger the event
template <typename... Measurements>
XCPLIB_ALWAYS_INLINE void DaqEventCtxVarTemplate(detail::ContextEventIds &ids, const Context *context, const char *event_name, Measurements &&...measurements) {
    if (XcpIsActivated()) {

        const uint16_t context_index = (context != nullptr) ? context->index() : 0;
        tXcpEventId event_id = ids.id[context_index].load(std::memory_order_acquire);

        // Once per context and code location
        if (event_id == XCP_UNDEFINED_EVENT_ID) {
            static std::mutex mutex;
            std::lock_guard<std::mutex> lock(mutex);
            event_id = ids.id[context_index].load(std::memory_order_acquire);
            if (event_id == XCP_UNDEFINED_EVENT_ID) {
                // Event instance <event_name>_<context_index>, or <event_name> without a context
                event_id = XcpCreateEventWithIndex(event_name, context_index, 0, 0);
                // Measurements <context_name>.<variable>, each with its own base address
                const char *context_name = (context_index != 0) ? context->name() : nullptr;
                A2lLock();
                uint8_t index = 1; // Start at 1, 0 is reserved for frame pointer relative addressing mode
                (detail::registerContextMeasurement(index++, event_id, context_name, measurements), ...);
                A2lUnlock();
                ids.id[context_index].store(event_id, std::memory_order_release);
            }
        }

        // Always
        const uint8_t *bases[] = {xcp_get_base_addr(), xcp_get_base_addr(), xcp_get_frame_addr(), (const uint8_t *)measurements.addr...};
        XcpEventExtAt_(event_id, (int)(sizeof(bases) / sizeof(bases[0])), bases, 0);
        XCP_NO_TAIL_CALL();
    }
}

} // namespace xcp

/// Trigger an event in a given context, create the event instance and register the measurement variables once per context
/// @param context Pointer to the context (const xcp::Context *), nullptr for no context
/// @param event_name Event name given as identifier, the event instance is named <event_name>_<context index>
#define DaqEventCtxVar_(context, event_name, ...)                                                                                                                                  \
    {                                                                                                                                                                              \
        static xcp::detail::ContextEventIds evt_ids__##event_name;                                                                                                                 \
        xcp::DaqEventCtxVarTemplate(evt_ids__##event_name, context, #event_name, __VA_ARGS__);                                                                                     \
    }

/// Trigger an event in the current context of the calling thread
/// Without a current context, the event is a single global event, like DaqEventVar
#define DaqEventCtxVar(event_name, ...) DaqEventCtxVar_(xcp::Context::GetCurrent(), event_name, __VA_ARGS__)
