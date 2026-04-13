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

    // XCP measurement variables
    double _xcp_latitude = 0.0;
    double _xcp_longitude = 0.0;
    double _xcp_temperature = 0.0;

    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {
        _gpsSubscriber = GetParticipant()->CreateDataSubscriber(
            "GpsSubscriber", PubSubDemoCommon::dataSpecGps, [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
                auto gpsData = PubSubDemoCommon::DeserializeGPSData(SilKit::Util::ToStdVector(dataMessageEvent.data));

                std::stringstream ss;
                ss << "Received GPS data: lat=" << gpsData.latitude << ", lon=" << gpsData.longitude << ", signalQuality=" << gpsData.signalQuality;
                GetLogger()->Info(ss.str());

                _xcp_latitude = gpsData.latitude;
                _xcp_longitude = gpsData.longitude;
                XcpUpdateSimTime(dataMessageEvent.timestamp);
                DaqTriggerEventExt(Gps, this);
            });

        _temperatureSubscriber = GetParticipant()->CreateDataSubscriber(
            "TemperatureSubscriber", PubSubDemoCommon::dataSpecTemperature, [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
                double temperature = PubSubDemoCommon::DeserializeTemperature(SilKit::Util::ToStdVector(dataMessageEvent.data));

                std::stringstream ss;
                ss << "Received temperature data: temperature=" << temperature;
                GetLogger()->Info(ss.str());

                _xcp_temperature = temperature;
                XcpUpdateSimTime(dataMessageEvent.timestamp);
                DaqTriggerEventExt(Temp, this);
            });

        // Initialize XCP server for measurement on TCP port 5556
        XcpServerInit(GetArguments().participantName, __TIME__, 5556);

        DaqCreateEvent(Gps);
        A2lSetRelativeAddrMode(Gps, this);
        A2lCreateMeasurement(_xcp_latitude, "Received GPS latitude in degrees");
        A2lCreateMeasurement(_xcp_longitude, "Received GPS longitude in degrees");
        DaqCreateEvent(Temp);
        A2lSetRelativeAddrMode(Temp, this);
        A2lCreateMeasurement(_xcp_temperature, "Received temperature in Celsius");
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
