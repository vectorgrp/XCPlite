// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#include "ApplicationBase.hpp"
#include "PubSubDemoCommon.hpp"
#include "XcpHelper.hpp"

using namespace PubSubDemoCommon;

class Publisher : public ApplicationBase {
  public:
    // Inherit constructors
    using ApplicationBase::ApplicationBase;

    ~Publisher() { XcpServerShutdown(); }

  private:
    IDataPublisher *_gpsPublisher;
    IDataPublisher *_temperaturePublisher;

    uint16_t _counter = 0;
    GpsData _gps_data = {0.0, 0.0, 0.0};
    double _temperature = 0.0;

    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {

        _gpsPublisher = GetParticipant()->CreateDataPublisher("GpsPublisher", dataSpecGps, 0);
        _temperaturePublisher = GetParticipant()->CreateDataPublisher("TemperaturePublisher", dataSpecTemperature, 0);

        // Initialize XCP server for measurement on TCP port 5556 or SHM mode server on 5555, depending on the build configuration
        XcpServerInit(GetArguments().participantName, "V1.3", 5555, 5555);

        // Create a typedef for struct GpsData
        A2lCreateTypedef(GpsData, "GPS data struct", A2L_MEASUREMENT_COMPONENT(latitude, "GPS latitude in degrees", ""), //
                         A2L_MEASUREMENT_COMPONENT(longitude, "GPS longitude in degrees", ""),                           //
                         A2L_MEASUREMENT_COMPONENT(signal, "GPS signal quality", "")                                     //
        );
    }

    void InitControllers() override {}

    void PublishGPSData(std::chrono::nanoseconds now) {

        _gps_data.latitude = 48.8235 + static_cast<double>((rand() % 150)) / 100000;
        _gps_data.longitude = 9.0965 + static_cast<double>((rand() % 150)) / 100000;
        _gps_data.signal = std::sin(2.0 * M_PI * std::chrono::duration<double>(now).count()); // sine signal with values between -1 and 1 and period of 1s

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

        PublishGPSData(now);
        PublishTemperatureData();

        XcpUpdateSimTime(now);
        DaqEventVar(DoWorkSync,                                                               //
                    A2L_MEAS(_counter, "Simulation step counter"),                            //
                    A2L_MEAS_PHYS(_temperature, "Temperature in Celsius", "C", -50.0, 100.0), //
                    A2L_MEAS_INST(_gps_data, "GpsData", "GPS position data struct"));

        // Sleep some time to simulate work
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    void DoWorkAsync() override { printf("Doing async work\n"); }
};

int main(int argc, char **argv) {
    Arguments args;
    args.participantName = "Publisher";
    Publisher app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - Publisher: Publish GPS and Temperature data");

    return app.Run();
}
