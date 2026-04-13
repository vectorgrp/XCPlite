// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#include "ApplicationBase.hpp"
#include "PubSubDemoCommon.hpp"
#include "XcpHelper.hpp"

class Subscriber : public ApplicationBase {
  public:
    // Inherit constructors
    using ApplicationBase::ApplicationBase;

    ~Subscriber() { XcpServerShutdown(); }

  private:
    IDataSubscriber *_gpsSubscriber;
    IDataSubscriber *_temperatureSubscriber;

    double _latitude = 0.0;
    double _longitude = 0.0;
    double _signal = 0.0;
    double _temperature = 0.0;

    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {
        _gpsSubscriber = GetParticipant()->CreateDataSubscriber(
            "GpsSubscriber", PubSubDemoCommon::dataSpecGps, [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
                auto gpsData = PubSubDemoCommon::DeserializeGPSData(SilKit::Util::ToStdVector(dataMessageEvent.data));
                _latitude = gpsData.latitude;
                _longitude = gpsData.longitude;
                _signal = gpsData.signal;

                std::stringstream ss;
                ss << "Received GPS data: lat=" << gpsData.latitude << ", lon=" << gpsData.longitude << ", signal=" << gpsData.signal;
                GetLogger()->Info(ss.str());

                XcpUpdateSimTime(dataMessageEvent.timestamp);
                DaqTriggerEventExt(Gps, this);
            });

        _temperatureSubscriber = GetParticipant()->CreateDataSubscriber(
            "TemperatureSubscriber", PubSubDemoCommon::dataSpecTemperature, [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
                _temperature = PubSubDemoCommon::DeserializeTemperature(SilKit::Util::ToStdVector(dataMessageEvent.data));

                std::stringstream ss;
                ss << "Received temperature data: temperature=" << _temperature;
                GetLogger()->Info(ss.str());

                XcpUpdateSimTime(dataMessageEvent.timestamp);
                DaqTriggerEventExt(Temp, this);
            });

        // Initialize XCP server for measurement on TCP port 5556
        XcpServerInit(GetArguments().participantName, "V1.1", 5555);

        DaqCreateEvent(Gps);
        A2lSetRelativeAddrMode(Gps, this);
        A2lCreateMeasurement(_latitude, "Received GPS latitude in degrees");
        A2lCreateMeasurement(_longitude, "Received GPS longitude in degrees");
        A2lCreateMeasurement(_signal, "Received GPS signal quality");
        DaqCreateEvent(Temp);
        A2lSetRelativeAddrMode(Temp, this);
        A2lCreateMeasurement(_temperature, "Received temperature in Celsius");
    }

    void InitControllers() override {}

    void DoWorkSync(std::chrono::nanoseconds now) override { XcpUpdateSimTime(now); }

    void DoWorkAsync() override {}
};

int main(int argc, char **argv) {
    Arguments args;
    args.participantName = "Subscriber";
    Subscriber app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - Subscriber: Receive GPS and Temperature data");

    return app.Run();
}
