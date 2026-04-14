// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#include "ApplicationBase.hpp"
#include "PubSubDemoCommon.hpp"
#include "XcpHelper.hpp"

using namespace PubSubDemoCommon;

class Subscriber : public ApplicationBase {
  public:
    // Inherit constructors
    using ApplicationBase::ApplicationBase;

    ~Subscriber() { XcpServerShutdown(); }

  private:
    IDataSubscriber *_gpsSubscriber;
    IDataSubscriber *_temperatureSubscriber;

    uint16_t _counter = 0;
    GpsData _gps_data = {0.0, 0.0, 0.0};
    double _temperature = 0.0;

    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {
        _gpsSubscriber = GetParticipant()->CreateDataSubscriber("GpsSubscriber", dataSpecGps, [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
            _gps_data = DeserializeGPSData(SilKit::Util::ToStdVector(dataMessageEvent.data));

            // std::stringstream ss;
            // ss << "Received GPS data: lat=" << _gps_data.latitude << ", lon=" << _gps_data.longitude << ", signal=" << _gps_data.signal;
            // GetLogger()->Info(ss.str());

            XcpUpdateSimTime(dataMessageEvent.timestamp);
            DaqTriggerEventExt(Gps, this);
        });

        _temperatureSubscriber = GetParticipant()->CreateDataSubscriber("TemperatureSubscriber", dataSpecTemperature,
                                                                        [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
                                                                            _temperature = DeserializeTemperature(SilKit::Util::ToStdVector(dataMessageEvent.data));

                                                                            // std::stringstream ss;
                                                                            // ss << "Received temperature data: temperature=" << _temperature;
                                                                            // GetLogger()->Info(ss.str());

                                                                            XcpUpdateSimTime(dataMessageEvent.timestamp);
                                                                            DaqTriggerEventExt(Temp, this);
                                                                        });

        // Initialize XCP server for measurement on TCP port 5556 or SHM mode server on 5555, depending on the build configuration
        XcpServerInit(GetArguments().participantName, "V1.3", 5555, 5556);

        // Create a typedef for struct GpsData
        A2lCreateTypedef(GpsData, "GPS data struct", A2L_MEASUREMENT_COMPONENT(latitude, "GPS latitude in degrees", ""), //
                         A2L_MEASUREMENT_COMPONENT(longitude, "GPS longitude in degrees", ""),                           //
                         A2L_MEASUREMENT_COMPONENT(signal, "GPS signal quality", "")                                     //
        );

        // Create events and measurements of instance variable
        DaqCreateEvent(DoWorkSync); // On simulation step
        A2lSetRelativeAddrMode(DoWorkSync, this);
        A2lCreateMeasurement(_counter, "Simulation step counter");
        DaqCreateEvent(Gps); // On reception of GPS data
        A2lSetRelativeAddrMode(Gps, this);
        A2lCreateTypedefInstance(_gps_data, GpsData, "GPS data struct");
        DaqCreateEvent(Temp); // On reception of temperature data
        A2lSetRelativeAddrMode(Temp, this);
        A2lCreateMeasurement(_temperature, "Received temperature in Celsius");
    }

    void InitControllers() override {}

    void DoWorkSync(std::chrono::nanoseconds now) override {

        _counter++;

        XcpUpdateSimTime(now);
        DaqTriggerEventExt(DoWorkSync, this);

        // Sleep some time to simulate work
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    void DoWorkAsync() override { printf("Doing async work\n"); }
};

int main(int argc, char **argv) {
    Arguments args;
    args.participantName = "Subscriber";
    Subscriber app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - Subscriber: Receive GPS and Temperature data");

    return app.Run();
}
