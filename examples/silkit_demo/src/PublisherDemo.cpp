// silkit_demo - Publisher application

#include "ApplicationBase.hpp"
#include "PubSubDemoCommon.hpp"
#include "XcpHelper.hpp"

using namespace PubSubDemoCommon;
using namespace xcplib; // For CalSeg

//-----------------------------------------------------------------------------------------------------
// Tunable and persistent calibration parameters

struct ParametersT {
    uint16_t counter_max;    // Maximum value for the step loop counter
    uint32_t delay_us;       // Sleep time in microseconds for DoWorkSync
    double signal_amplitude; // Amplitude for the simulated GPS signal strength
    bool use_simulated_time; // Whether to use simulated time for XCP events or real time
};

// Default values
const ParametersT kParameters = {.counter_max = 1000, .delay_us = 1000, .signal_amplitude = 1.0, .use_simulated_time = true};

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

class Publisher : public ApplicationBase {
  public:
    Publisher(Arguments args = Arguments{}) : ApplicationBase(args), _parameters("kParameters", &kParameters) {}
    ~Publisher() { XcpServerShutdown(); }

  private:
    IDataPublisher *_gpsPublisher;
    IDataPublisher *_temperaturePublisher;

    // Tunable and persistent calibration parameters
    CalSeg<ParametersT> _parameters;

    // Member variables for measurements
    uint16_t _counter = 0;
    GpsData _gps_data = {0.0, 0.0, 0.0};
    double _temperature = 0.0;

    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {

        _gpsPublisher = GetParticipant()->CreateDataPublisher("GpsPublisher", dataSpecGps, 0);
        _temperaturePublisher = GetParticipant()->CreateDataPublisher("TemperaturePublisher", dataSpecTemperature, 0);

        // Create a typedef for struct GpsData
        A2lCreateTypedef(GpsData, "GPS data struct", A2L_MEASUREMENT_COMPONENT(latitude, "GPS latitude in degrees", ""), //
                         A2L_MEASUREMENT_COMPONENT(longitude, "GPS longitude in degrees", ""),                           //
                         A2L_MEASUREMENT_COMPONENT(signal, "GPS signal quality", "")                                     //
        );

        // Create a typedef for struct ParametersT
        A2lTypedefBegin(ParametersT, &kParameters, "A2L Typedef for ParametersT");
        A2lTypedefParameterComponent(counter_max, "Maximum counter value", "", 0, 2000);
        A2lTypedefParameterComponent(delay_us, "Mainloop delay time in us", "us", 0, 999999);
        A2lTypedefParameterComponent(signal_amplitude, "Amplitude for the simulated GPS signal", "", 0.0, 10.0);
        A2lTypedefParameterComponent(use_simulated_time, "Whether to use simulated time for XCP events or real time", "", 0, 1);
        A2lTypedefEnd();

        // Add the calibration segment description as a typedef instance to the A2L file
        _parameters.CreateA2lTypedefInstance("ParametersT", "Main parameters");
    }

    void InitControllers() override {}

    void PublishGPSData(std::chrono::nanoseconds now) {

        _gps_data.latitude = 48.8235 + static_cast<double>((rand() % 150)) / 100000;
        _gps_data.longitude = 9.0965 + static_cast<double>((rand() % 150)) / 100000;
        _gps_data.signal =
            _parameters.lock()->signal_amplitude * std::sin(2.0 * M_PI * std::chrono::duration<double>(now).count()); // sine signal with values between -1 and 1 and period of 1s

        auto gpsSerialized = SerializeGPSData(_gps_data);
        _gpsPublisher->Publish(gpsSerialized);

        // std::stringstream ss;
        // ss << "Publishing GPS data: lat=" << _gps_data.latitude << ", lon=" << _gps_data.longitude << ", signal=" << _gps_data.signal;
        // GetLogger()->Info(ss.str());
    }

    void PublishTemperatureData() {

        _temperature = 25.0 + static_cast<double>(rand() % 10) / 10.0;

        auto temperatureSerialized = SerializeTemperature(_temperature);
        _temperaturePublisher->Publish(temperatureSerialized);

        // std::stringstream ss;
        // ss << "Publishing temperature data: temperature=" << temperature;
        // GetLogger()->Info(ss.str());
    }

    void DoWorkSync(std::chrono::nanoseconds now) override {

        _counter++;
        if (_counter > _parameters.lock()->counter_max)
            _counter = 0;

        PublishGPSData(now);
        PublishTemperatureData();

        // Trigger a XCP measurement event with simulated time
        XcpUpdateSimTime(now.count());
        DaqEventVar(DoWorkSync,                                                               //
                    A2L_MEAS(_counter, "Simulation step counter"),                            //
                    A2L_MEAS_PHYS(_temperature, "Temperature in Celsius", "C", -50.0, 100.0), //
                    A2L_MEAS_INST(_gps_data, "GpsData", "GPS position data struct"));

        // Sleep some time to simulate work
        // Read delay_us before sleep to avoid holding the calibration lock during sleep
        const auto delay_us = _parameters.lock()->delay_us;
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }

    void DoWorkAsync() override {}
};

int main(int argc, char **argv) {

    // Initialize XCP server for measurement on TCP port 5555
    XcpServerInit("Publisher", "V1.7", 5555);

    Arguments args;
    args.participantName = "Publisher";
    Publisher app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - Publisher");
    return app.Run();
}
