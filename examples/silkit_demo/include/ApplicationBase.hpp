// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#include "silkit/SilKit.hpp"
#include "silkit/services/logging/ILogger.hpp"
#include "silkit/services/orchestration/all.hpp"
#include "silkit/services/orchestration/string_utils.hpp"

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <thread>
#include <unordered_set>
#include <cctype>

#include "SignalHandler.hpp"
#include "CommandlineParser.hpp"

using namespace SilKit;
using namespace SilKit::Services::Orchestration;
using namespace SilKit::Services::Can;
using namespace SilKit::Services::Logging;
using namespace SilKit::Util;

using namespace std::chrono_literals;

// The application arguments.
// Values might be overridden by the command line arguments.
struct Arguments
{
    std::string participantName = "Participant1";
    std::string registryUri = "silkit://localhost:8500";
    bool runAutonomous{false};
    bool runAsync{false};
    std::chrono::nanoseconds duration = 1ms;
    std::chrono::nanoseconds sleep = 1000ms;
    bool asFastAsPossible{false};
};
std::shared_ptr<SilKit::Config::IParticipantConfiguration> _participantConfiguration{nullptr};

std::ostream& operator<<(std::ostream& out, std::chrono::nanoseconds timestamp)
{
    out << std::chrono::duration_cast<std::chrono::milliseconds>(timestamp).count() << "ms";
    return out;
}

class ApplicationBase
{
public:
    ApplicationBase(Arguments args = Arguments{})
    {
        _arguments = args;
        _commandLineParser = std::make_shared<CommandlineParser>();
    }

    virtual ~ApplicationBase() = default;

protected:
    // Must be implemented by the actual application

    // Add application specific command line arguments. Called during SetupCommandLineArgs.
    virtual void AddCommandLineArgs() = 0;
    // Evaluate application specific command line arguments. Called during SetupCommandLineArgs.
    virtual void EvaluateCommandLineArgs() = 0;

    // Controller creates SIL Kit controllers / services here.
    virtual void CreateControllers() = 0;

    // Boot up SIL Kit controllers here.
    // Called in the CommunicationReadyHandler to ensure that messages emerging from the
    // controller creation reach other involved participants.
    virtual void InitControllers() = 0;

    // When running with the '--async' flag, this is called continuously in a separate worker thread.
    // This thread is managed by the ApplicationBase.
    // The function will trigger as soon as the participant state is 'Running'.
    virtual void DoWorkAsync() = 0;

    // When running with time synchronization (no '--async' flag), this is called in the SimulationStepHandler.
    virtual void DoWorkSync(std::chrono::nanoseconds now) = 0;

public:
    // Default command line argument identifiers (to allow exclusion)
    enum struct DefaultArg
    {
        Name,
        Uri,
        Log,
        Config,
        Async,
        Autonomous,
        Duration,
        AsFastAsPossible,
        Sleep
    };

private:
    // Command line parser
    std::shared_ptr<CommandlineParser> _commandLineParser;

    // Names of the default command line arguments.
    // Note that the 'short name' (e.g. -n) and description are defined at runtime.
    const std::unordered_map<DefaultArg, std::string> defaultArgName = {
        {DefaultArg::Name, "name"},
        {DefaultArg::Uri, "registry-uri"},
        {DefaultArg::Log, "log"},
        {DefaultArg::Config, "config"},
        {DefaultArg::Async, "async"},
        {DefaultArg::Autonomous, "autonomous"},
        {DefaultArg::Duration, "sim-step-duration"},
        {DefaultArg::AsFastAsPossible, "fast"},
        {DefaultArg::Sleep, "sleep"},
    };
    Arguments _arguments;

    // SIL Kit API
    std::unique_ptr<IParticipant> _participant;
    ILifecycleService* _lifecycleService{nullptr};
    ITimeSyncService* _timeSyncService{nullptr};
    ISystemMonitor* _systemMonitor{nullptr};
    bool _sleepingEnabled = false;

    // For sync: wait for sil-kit-system-controller start/abort or manual user abort
    enum struct SystemControllerResult
    {
        Unknown,
        CoordinatedStart,
        SystemControllerAbort,
        UserAbort
    };
    std::atomic<bool> _hasSystemControllerResult{false};
    std::promise<SystemControllerResult> _waitForSystemControllerPromise;
    std::future<SystemControllerResult> _waitForSystemControllerFuture;
    std::atomic<SystemControllerResult> _systemControllerResult{SystemControllerResult::Unknown};

    // For async: Couple worker thread to SIL Kit lifecycle
    enum struct UnleashWorkerThreadResult
    {
        Unknown,
        ParticipantStarting,
        UserAbort
    };
    std::promise<UnleashWorkerThreadResult> _startWorkPromise;
    std::future<UnleashWorkerThreadResult> _startWorkFuture;

    std::promise<void> _stopWorkPromise;
    std::future<void> _stopWorkFuture;
    std::atomic<bool> _gracefulStop{false};

    std::thread _workerThread;

    // Wait for the SIL Kit lifecycle to end
    std::future<ParticipantState> _participantStateFuture;
    ParticipantState _finalParticipantState{ParticipantState::Invalid};

private:
    void AddDefaultArgs(std::unordered_set<DefaultArg> excludedCommandLineArgs = {})
    {
        _commandLineParser->Add<CommandlineParser::Flag>("help", "h", "-h, --help",
                                                         std::vector<std::string>{"Get this help."});

        Arguments defaultArgs{};

        if (!excludedCommandLineArgs.count(DefaultArg::Name))
        {
            _commandLineParser->Add<CommandlineParser::Option>(
                defaultArgName.at(DefaultArg::Name), "n", defaultArgs.participantName,
                "-n, --" + defaultArgName.at(DefaultArg::Name) + " <name>",
                std::vector<std::string>{"The participant name used to take part in the simulation.",
                                         "Defaults to '" + _arguments.participantName + "'."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Uri))
        {
            _commandLineParser->Add<CommandlineParser::Option>(
                defaultArgName.at(DefaultArg::Uri), "u", defaultArgs.registryUri,
                "-u, --" + defaultArgName.at(DefaultArg::Uri) + " <uri>",
                std::vector<std::string>{"The registry URI to connect to.",
                                         "Defaults to '" + _arguments.registryUri + "'."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Log))
        {
            _commandLineParser->Add<CommandlineParser::Option>(
                defaultArgName.at(DefaultArg::Log), "l", "info",
                "-l, --" + defaultArgName.at(DefaultArg::Log) + " <level>",
                std::vector<std::string>{
                    "Log to stdout with level:", "'trace', 'debug', 'warn', 'info', 'error', 'critical' or 'off'.",
                    "Defaults to 'info'.",
                    "Cannot be used together with '--" + defaultArgName.at(DefaultArg::Config) + "'."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Config))
        {
            _commandLineParser->Add<CommandlineParser::Option>(
                defaultArgName.at(DefaultArg::Config), "c", "",
                "-c, --" + defaultArgName.at(DefaultArg::Config) + " <filePath>",
                std::vector<std::string>{"Path to the Participant configuration YAML or JSON file.",
                                         "Cannot be used together with '--" + defaultArgName.at(DefaultArg::Log) + "'.",
                                         "Will always run as fast as possible."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Async))
        {
            _commandLineParser->Add<CommandlineParser::Flag>(
                defaultArgName.at(DefaultArg::Async), "a", "-a, --" + defaultArgName.at(DefaultArg::Async),
                std::vector<std::string>{
                    "Run without time synchronization mode.",
                    "Cannot be used together with '--" + defaultArgName.at(DefaultArg::Duration) + "'."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Autonomous))
        {
            _commandLineParser->Add<CommandlineParser::Flag>(
                defaultArgName.at(DefaultArg::Autonomous), "A", "-A, --" + defaultArgName.at(DefaultArg::Autonomous),
                std::vector<std::string>{"Start the simulation autonomously.",
                                         "Without this flag, a coordinated start is performed",
                                         "which requires the SIL Kit System Controller."});
        }


        if (!excludedCommandLineArgs.count(DefaultArg::Duration))
        {
            auto defaultValue = std::to_string(defaultArgs.duration.count() / 1000);
            _commandLineParser->Add<CommandlineParser::Option>(
                defaultArgName.at(DefaultArg::Duration), "d", defaultValue,
                "-d, --" + defaultArgName.at(DefaultArg::Duration) + " <us>",
                std::vector<std::string>{
                    "The duration of a simulation step in microseconds.", "Defaults to " + defaultValue + "us.",
                    "Cannot be used together with '--" + defaultArgName.at(DefaultArg::Async) + "'."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::AsFastAsPossible))
        {
            _commandLineParser->Add<CommandlineParser::Flag>(
                defaultArgName.at(DefaultArg::AsFastAsPossible), "f",
                "-f, --" + defaultArgName.at(DefaultArg::AsFastAsPossible),
                std::vector<std::string>{
                    "Run the simulation as fast as possible.",
                    "By default, the execution is slowed down to two work cycles per second.",
                    "Cannot be used together with '--" + defaultArgName.at(DefaultArg::Config) + "'."});
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Sleep))
        {
            auto defaultValue = std::to_string(defaultArgs.sleep.count() / 1000000);
            _commandLineParser->Add<CommandlineParser::Option>(
                defaultArgName.at(DefaultArg::Sleep), "s", defaultValue,
                "-s, --" + defaultArgName.at(DefaultArg::Sleep) + " <ms>",
                std::vector<std::string>{
                    "The sleep duration per work cycle in milliseconds.", "Default is no sleeping.",
                    "Using this options overrides the default execution slow down.",
                    "Cannot be used together with '--" + defaultArgName.at(DefaultArg::AsFastAsPossible) + "'."});
        }
    }

    auto ToLowerCase(std::string s) -> std::string
    {
        std::for_each(s.begin(), s.end(), [](char& c) { c = static_cast<char>(std::tolower(c)); });
        return s;
    }
    auto IsValidLogLevel(const std::string& levelStr) -> bool
    {
        auto logLevel = ToLowerCase(levelStr);
        return logLevel == "trace" || logLevel == "debug" || logLevel == "warn" || logLevel == "info"
               || logLevel == "error" || logLevel == "critical" || logLevel == "off";
    }

    void ParseArguments(int argc, char** argv)
    {
        try
        {
            _commandLineParser->ParseArguments(argc, argv);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error parsing command line arguments: " << e.what() << std::endl;
            _commandLineParser->PrintUsageInfo(std::cerr);
            exit(-1);
        }
    }

    void EvaluateDefaultArgs(std::unordered_set<DefaultArg> excludedCommandLineArgs)
    {
        if (_commandLineParser->Get<CommandlineParser::Flag>("help").Value())
        {
            _commandLineParser->PrintUsageInfo(std::cout);
            exit(0);
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Name))
        {
            if (_commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Name)).HasValue())
            {
                _arguments.participantName =
                    _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Name)).Value();
            }
        }

        if (!excludedCommandLineArgs.count(DefaultArg::Uri))
        {
            if (_commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Uri)).HasValue())
            {
                _arguments.registryUri =
                    _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Uri)).Value();
            }
        }

        bool hasAsyncFlag = false;
        if (!excludedCommandLineArgs.count(DefaultArg::Async))
        {
            _arguments.runAsync = hasAsyncFlag =
                _commandLineParser->Get<CommandlineParser::Flag>(defaultArgName.at(DefaultArg::Async)).Value();
        }


        bool hasAutonomousFlag = false;
        if (!excludedCommandLineArgs.count(DefaultArg::Autonomous))
        {
            _arguments.runAutonomous = hasAutonomousFlag =
                _commandLineParser->Get<CommandlineParser::Flag>(defaultArgName.at(DefaultArg::Autonomous)).Value();
        }

        bool hasDurationOption = false;
        if (!excludedCommandLineArgs.count(DefaultArg::Duration))
        {
            hasDurationOption =
                _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Duration)).HasValue();
            if (hasDurationOption)
            {
                int durationUs = std::stoi(
                    _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Duration))
                        .Value());
                _arguments.duration = std::chrono::microseconds(durationUs);
            }
        }

        bool hasSleepOption = false;
        if (!excludedCommandLineArgs.count(DefaultArg::Sleep))
        {
            hasSleepOption =
                _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Sleep)).HasValue();
            if (hasSleepOption)
            {
                int sleepDurationMs = std::stoi(
                    _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Sleep)).Value());
                _arguments.sleep = std::chrono::milliseconds(sleepDurationMs);
                _sleepingEnabled = true;
            }
        }

        if (hasAsyncFlag && hasDurationOption)
        {
            std::cerr << "Error: Options '--" << defaultArgName.at(DefaultArg::Async) << "' and '--"
                      << defaultArgName.at(DefaultArg::Duration) << "' cannot be used simultaneously" << std::endl;
            _commandLineParser->PrintUsageInfo(std::cerr);
            exit(-1);
        }

        bool hasAsFastAsPossibleFlag = false;
        if (!excludedCommandLineArgs.count(DefaultArg::AsFastAsPossible))
        {
            _arguments.asFastAsPossible = hasAsFastAsPossibleFlag =
                _commandLineParser->Get<CommandlineParser::Flag>(defaultArgName.at(DefaultArg::AsFastAsPossible))
                    .Value();
        }

        if (hasAsFastAsPossibleFlag && hasSleepOption)
        {
            std::cerr << "Error: Options '--" << defaultArgName.at(DefaultArg::AsFastAsPossible) << "' and '--"
                      << defaultArgName.at(DefaultArg::Sleep) << "' cannot be used simultaneously" << std::endl;
            _commandLineParser->PrintUsageInfo(std::cerr);
            exit(-1);
        }

        bool hasLogOption = false;
        if (!excludedCommandLineArgs.count(DefaultArg::Log))
        {
            hasLogOption =
                _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Log)).HasValue();
        }

        bool hasCfgOption = false;
        if (!excludedCommandLineArgs.count(DefaultArg::Config))
        {
            hasCfgOption =
                _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Config)).HasValue();
        }

        if (hasLogOption && hasCfgOption)
        {
            std::cerr << "Error: Options '--" << defaultArgName.at(DefaultArg::Log) << "' and '--"
                      << defaultArgName.at(DefaultArg::Config) << "' cannot be used simultaneously" << std::endl;
            _commandLineParser->PrintUsageInfo(std::cerr);
            exit(-1);
        }

        if (hasAsFastAsPossibleFlag && hasCfgOption)
        {
            std::cerr << "Error: Options '--" << defaultArgName.at(DefaultArg::AsFastAsPossible) << "' and '--"
                      << defaultArgName.at(DefaultArg::Config) << "' cannot be used simultaneously" << std::endl;
            _commandLineParser->PrintUsageInfo(std::cerr);
            exit(-1);
        }

        if (hasCfgOption)
        {
            // --config always runs as fast as possible
            _arguments.asFastAsPossible = true;

            const auto configFileName =
                _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Config)).Value();
            try
            {
                _participantConfiguration = SilKit::Config::ParticipantConfigurationFromFile(configFileName);
            }
            catch (const SilKit::ConfigurationError& error)
            {
                std::cerr << "Failed to load configuration '" << configFileName << "':" << std::endl
                          << error.what() << std::endl;
                exit(-1);
            }
        }
        else
        {
            std::string configLogLevel{"Off"};
            if (!excludedCommandLineArgs.count(DefaultArg::Log))
            {
                const auto logLevel{
                    _commandLineParser->Get<CommandlineParser::Option>(defaultArgName.at(DefaultArg::Log)).Value()};
                if (!IsValidLogLevel(logLevel))
                {
                    std::cerr << "Error: Argument of the '--" << defaultArgName.at(DefaultArg::Log)
                              << "' option must be one of 'trace', 'debug', 'warn', 'info', "
                                 "'error', 'critical', or 'off'"
                              << std::endl;
                    exit(-1);
                }
                configLogLevel = logLevel;
            }

            configLogLevel[0] = static_cast<char>(std::toupper(configLogLevel[0]));

            std::ostringstream ss;
            ss << "{";
            ss << R"("Logging":{"Sinks":[{"Type":"Stdout","Level":")" << configLogLevel << R"("}]})";

            if (!_arguments.runAsync && !_arguments.asFastAsPossible && !_sleepingEnabled)
            {
                // For async: sleep 0.5s per cycle
                // For sync: set the animation factor to 0.5/duration(s) here, resulting in two simulation step per second
                double animationFactor =
                    (_arguments.duration.count() > 0) ? 0.5 / (1e-9 * _arguments.duration.count()) : 1.0;
                ss << R"(,"Experimental":{"TimeSynchronization":{"AnimationFactor":)" << animationFactor << R"(}})";
            }
            ss << "}";

            const auto configString = ss.str();
            try
            {
                _participantConfiguration = SilKit::Config::ParticipantConfigurationFromString(configString);
            }
            catch (const SilKit::ConfigurationError& error)
            {
                std::cerr << "Failed to set configuration from string '" << configString << "', " << error.what()
                          << std::endl;
                exit(-1);
            }
        }
    }

    void SetupSignalHandler()
    {
        RegisterSignalHandler([&](auto signalValue) {
            {
                std::ostringstream ss;
                ss << "Signal " << signalValue << " received, attempting to stop simulation...";
                _participant->GetLogger()->Info(ss.str());

                Stop();
            }
        });
    }

    void WorkerThread()
    {
        auto result = _startWorkFuture.get();
        if (result == UnleashWorkerThreadResult::UserAbort)
        {
            return;
        }

        while (true)
        {
            DoWorkAsync();

            auto wait = _arguments.asFastAsPossible || _sleepingEnabled ? 0ms : 500ms;
            auto futureStatus = _stopWorkFuture.wait_for(wait);
            if (futureStatus == std::future_status::ready)
            {
                break;
            }

            if (_sleepingEnabled)
            {
                std::this_thread::sleep_for(_arguments.sleep);
            }
        }
    }

    void SetupParticipant()
    {
        _participant =
            SilKit::CreateParticipant(_participantConfiguration, _arguments.participantName, _arguments.registryUri);
        _systemMonitor = _participant->CreateSystemMonitor();
    }

    void SetupLifecycle()
    {
        auto operationMode = (_arguments.runAutonomous ? OperationMode::Autonomous : OperationMode::Coordinated);
        _lifecycleService = _participant->CreateLifecycleService({operationMode});

        // Handle simulation abort by sil-kit-system-controller
        _waitForSystemControllerFuture = _waitForSystemControllerPromise.get_future();
        _lifecycleService->SetAbortHandler([this](ParticipantState /*lastState*/) {
            if (!_hasSystemControllerResult)
            {
                _hasSystemControllerResult = true;
                _waitForSystemControllerPromise.set_value(SystemControllerResult::SystemControllerAbort);
            }
        });

        // Called during startup
        _lifecycleService->SetCommunicationReadyHandler([this]() {
            if (!_arguments.runAutonomous)
            {
                // Handle valid simulation start by sil-kit-system-controller
                if (!_hasSystemControllerResult)
                {
                    _hasSystemControllerResult = true;
                    _waitForSystemControllerPromise.set_value(SystemControllerResult::CoordinatedStart);
                }
            }
            // App should initialize it's controllers here
            InitControllers();
        });
    }

    void SetupAsync()
    {
        // No time sync: Work by the app is done in the worker thread

        // Future / promise to control entrance / exit of the main loop in the worker thread
        _startWorkFuture = _startWorkPromise.get_future();
        _stopWorkFuture = _stopWorkPromise.get_future();

        // The worker thread of participants without time synchronization is 'unleashed' in the starting handler...
        _lifecycleService->SetStartingHandler(
            [this]() { _startWorkPromise.set_value(UnleashWorkerThreadResult::ParticipantStarting); });

        // ... and stopped in the stop handler.
        _lifecycleService->SetStopHandler([this]() {
            _gracefulStop = true;
            _stopWorkPromise.set_value();
        });

        _lifecycleService->SetShutdownHandler([this]() {
            if (!_gracefulStop)
            {
                // If the simulation was aborted, the stop handler has been skipped and the promise must be set here.
                _stopWorkPromise.set_value();
            }
        });

        // Start the worker thread
        _workerThread = std::thread{&ApplicationBase::WorkerThread, this};
    }

    void SetupSync()
    {
        // With time sync: Work by the app is done in the SimulationStepHandler
        _timeSyncService = _lifecycleService->CreateTimeSyncService();
        _timeSyncService->SetSimulationStepHandler(
            [this](std::chrono::nanoseconds now, std::chrono::nanoseconds /*duration*/) {
            std::stringstream ss;
            ss << "--------- Simulation step T=" << now << " ---------";
            _participant->GetLogger()->Info(ss.str());

            DoWorkSync(now);
            if (_sleepingEnabled)
            {
                std::this_thread::sleep_for(_arguments.sleep);
            }
        }, _arguments.duration);
    }

    void Launch()
    {
        _participantStateFuture = _lifecycleService->StartLifecycle();
    }

    void Stop()
    {
        auto state = _lifecycleService->State();
        if (state == ParticipantState::Running || state == ParticipantState::Paused)
        {
            _lifecycleService->Stop("User requested to stop");
        }
        else
        {
            if (!_arguments.runAutonomous && !_hasSystemControllerResult)
            {
                _waitForSystemControllerPromise.set_value(SystemControllerResult::UserAbort);
            }
        }
    }

    void WaitUntilDone()
    {
        if (!_arguments.runAutonomous)
        {
            _participant->GetLogger()->Info("Waiting for the system controller to start the simulation");
            // Allow the application to exit by itself in case the user aborted while waiting for the sil-kit-system-controller
            _systemControllerResult = _waitForSystemControllerFuture.get();
            if (_systemControllerResult == SystemControllerResult::UserAbort)
            {
                // Premature user abort, don't wait for _participantStateFuture
                _participant->GetLogger()->Info("Terminated while waiting for coordinated start");

                if (_arguments.runAsync)
                {
                    _startWorkPromise.set_value(UnleashWorkerThreadResult::UserAbort);

                    if (_workerThread.joinable())
                    {
                        _workerThread.join();
                    }
                }
                return;
            }
            else if (_systemControllerResult == SystemControllerResult::SystemControllerAbort)
            {
                _participant->GetLogger()->Info("System Controller aborted the simulation");
                // No return here as a System Controller abort leads to ParticipantState::Shutdown
                // and the _participantStateFuture.get() will return a result.
            }
        }

        _finalParticipantState = _participantStateFuture.get();

        if (_arguments.runAsync)
        {
            if (_workerThread.joinable())
            {
                _workerThread.join();
            }
        }
    }

    // Only use inside class
protected:
    IParticipant* GetParticipant() const
    {
        return _participant.get();
    }

    ILifecycleService* GetLifecycleService() const
    {
        return _lifecycleService;
    }

    ITimeSyncService* GetTimeSyncService() const
    {
        return _timeSyncService;
    }

    ISystemMonitor* GetSystemMonitor() const
    {
        return _systemMonitor;
    }

    ILogger* GetLogger() const
    {
        return _participant->GetLogger();
    }

    // Also accessible in app / main
public:
    const Arguments& GetArguments() const
    {
        return _arguments;
    }

    const auto& GetCommandLineParser()
    {
        return _commandLineParser;
    }

    // Setup of default and application command line arguments
    void SetupCommandLineArgs(int argc, char** argv, const std::string& appDescription,
                              std::unordered_set<DefaultArg> excludedCommandLineArgs = {})
    {
        _commandLineParser->SetDescription(appDescription);
        AddDefaultArgs(excludedCommandLineArgs);
        try
        {
            AddCommandLineArgs();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            exit(-1);
        }
        ParseArguments(argc, argv);
        EvaluateDefaultArgs(excludedCommandLineArgs);
        try
        {
            EvaluateCommandLineArgs();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            _commandLineParser->PrintUsageInfo(std::cerr);
            exit(-1);
        }
    }

    // Run the SIL Kit workflow
    int Run()
    {
        try
        {
            // Create participant
            SetupParticipant();

            // Stop() on Ctrl-C
            SetupSignalHandler();

            // app: Controller creation
            CreateControllers();

            // React on valid start / simulation abort
            // app: Controller initialization in CommunicationReadyHandler
            SetupLifecycle();

            if (_arguments.runAsync)
            {
                // Create thread and couple to starting handler
                // app: DoWorkAsync
                SetupAsync();
            }
            else
            {
                // Create timeSyncService and simulationStepHandler
                // app: DoWorkSync
                SetupSync();
            }

            // Start lifecycle
            Launch();

            // Wait for lifecycle to end
            // async: Join worker thread
            WaitUntilDone();

            ShutdownSignalHandler();

            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Something went wrong: " << error.what() << std::endl;
            return -1;
        }
    }
};
