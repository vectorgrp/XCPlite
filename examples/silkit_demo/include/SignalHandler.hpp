// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <thread>
#include <stdexcept>
#include <vector>

using SignalHandler = std::function<void(int)>;

//forward
namespace {
class SignalMonitor;
} // namespace

// Global signal handler
static std::unique_ptr<SignalMonitor> gSignalMonitor;

////////////////////////////////////////////
// Inline Platform Specific Implementations
////////////////////////////////////////////
#if WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {


//forward
BOOL WINAPI systemHandler(DWORD ctrlType);

class SignalMonitor
{
    static constexpr int INVALID_SIGNAL_NUMBER = -1;

public:
    SignalMonitor(SignalHandler handler)
    {
        _handler = std::move(handler);

        auto ok = CreatePipe(&_readEnd, &_writeEnd, nullptr, 0);
        if (!ok)
        {
            throw std::runtime_error("SignalMonitor: Failed to create pipe for signal handler.");
        }
        DWORD nowait = PIPE_NOWAIT;
        ok = SetNamedPipeHandleState(_writeEnd, &nowait, nullptr, nullptr);
        if (!ok)
        {
            throw std::runtime_error(
                "SignalMonitor: Failed to create signal handler, cannot set read end of pipe to non-blocking.");
        }

        SetConsoleCtrlHandler(systemHandler, true);
        _worker = std::thread{std::bind(&SignalMonitor::workerMain, this)};
    }
    ~SignalMonitor()
    {
        SetConsoleCtrlHandler(systemHandler, false);
        Notify(INVALID_SIGNAL_NUMBER);
        _worker.join();
        CloseHandle(_writeEnd);
        CloseHandle(_readEnd);
    }
    void Notify(int signalNumber)
    {
        // No allocs, no error handling
        _signalNumber = signalNumber;
        uint8_t buf{};
        auto ok = WriteFile(_writeEnd, &buf, sizeof(buf), nullptr, nullptr);
        (void)ok;
    }

private:
    void workerMain()
    {
        std::vector<uint8_t> buf(1);
        // Blocking read until Notify() was called
        auto ok = ReadFile(_readEnd, buf.data(), static_cast<DWORD>(buf.size()), nullptr, nullptr);
        if (!ok)
        {
            throw std::runtime_error("SignalMonitor::workerMain: Failed to read from pipe.");
        }

        if ((_signalNumber != INVALID_SIGNAL_NUMBER) && _handler)
        {
            _handler(_signalNumber);
        }
    }

    HANDLE _readEnd{INVALID_HANDLE_VALUE}, _writeEnd{INVALID_HANDLE_VALUE};
    SignalHandler _handler;
    std::thread _worker;
    int _signalNumber{INVALID_SIGNAL_NUMBER};
};

BOOL WINAPI systemHandler(DWORD ctrlType)
{
    if (gSignalMonitor)
    {
        gSignalMonitor->Notify(static_cast<int>(ctrlType));
        return TRUE;
    }
    return FALSE;
}

} // end anonymous namespace

#else //UNIX

#include <csignal>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {

using namespace SilKit::Util;

using SignalHandlerT = void (*)(int);

//forward
static inline void setSignalAction(int sigNum, SignalHandlerT action);

void systemHandler(int sigNum);

auto ErrorMessage()
{
    return std::string{strerror(errno)};
}

class SignalMonitor
{
    static constexpr int INVALID_SIGNAL_NUMBER = -1;

public:
    SignalMonitor(SignalHandler handler)
    {
        _handler = std::move(handler);

        auto ok = ::pipe(_pipe);
        if (ok == -1)
        {
            throw std::runtime_error("SignalMonitor: Failed to create pipe for signal handler, " + ErrorMessage());
        }
        ok = fcntl(_pipe[1], F_SETFL, O_NONBLOCK);
        if (ok == -1)
        {
            throw std::runtime_error(
                "SignalMonitor: Failed to create signal handler, cannot set read end of pipe to non-blocking: "
                + ErrorMessage());
        }

        setSignalAction(SIGINT, &systemHandler);
        setSignalAction(SIGTERM, &systemHandler);
        _worker = std::thread{std::bind(&SignalMonitor::workerMain, this)};
    }

    ~SignalMonitor()
    {
        // Restore default actio0ns
        setSignalAction(SIGINT, nullptr);
        setSignalAction(SIGTERM, nullptr);
        Notify(INVALID_SIGNAL_NUMBER);
        _worker.join();
        ::close(_pipe[0]);
        ::close(_pipe[1]);
    }

    void Notify(int signalNumber)
    {
        // In signal handler context: no allocs, no error handling
        _signalNumber = signalNumber;
        uint8_t buf{};
        auto ok = ::write(_pipe[1], &buf, sizeof(buf));
        (void)ok;
    }

private:
    void workerMain()
    {
        std::vector<uint8_t> buf(1);
        // Blocking read until Notify() was called
        auto ok = ::read(_pipe[0], buf.data(), buf.size());
        if (ok == -1)
        {
            throw std::runtime_error("SignalMonitor::workerMain: Failed to read from pipe: " + ErrorMessage());
        }
        if ((_signalNumber != INVALID_SIGNAL_NUMBER) && _handler)
        {
            _handler(_signalNumber);
        }
    }

    int _pipe[2];
    SignalHandler _handler;
    std::thread _worker;
    int _signalNumber{INVALID_SIGNAL_NUMBER};
};

static inline void setSignalAction(int sigNum, SignalHandlerT action)
{
    // Check current signal handler action to see if it's set to SIGNAL IGNORE
    struct sigaction oldAction
    {
    };
    sigaction(sigNum, NULL, &oldAction);
    if (oldAction.sa_handler == SIG_IGN)
    {
        // A non-job-control shell wants us to ignore this kind of signal
        return;
    }
    // Set new signal handler action to what we want
    struct sigaction newAction
    {
    };
    if (action == nullptr)
    {
        newAction.sa_handler = SIG_DFL;
    }
    else
    {
        newAction.sa_handler = action;
    }

    auto ret = sigaction(sigNum, &newAction, NULL);
    if (ret == -1)
    {
        throw std::runtime_error("SignalMonitor: Failed to set handler for signal " + std::to_string(sigNum) + ": "
                                 + ErrorMessage());
    }
}

void systemHandler(const int sigNum)
{
    if (gSignalMonitor)
    {
        gSignalMonitor->Notify(sigNum);
    }
}

} // namespace
#endif

//! \brief RegisterSignalHandler can be used to portably register a single signal handler.
// It only relies on async-signal-safe C functions internally, but
// it uses a dedicated thread which safely runs the user-provided handler.
void RegisterSignalHandler(SignalHandler handler)
{
    gSignalMonitor.reset(new SignalMonitor(std::move(handler)));
}

void ShutdownSignalHandler()
{
    gSignalMonitor.reset();
}
