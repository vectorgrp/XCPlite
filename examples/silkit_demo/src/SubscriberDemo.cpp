// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#include "ApplicationBase.hpp"
#include "PubSubDemoCommon.hpp"

#include <a2l.hpp>
#include <xcplib.hpp>

class Subscriber : public ApplicationBase {
  public:
    // Inherit constructors
    using ApplicationBase::ApplicationBase;

    ~Subscriber() { XcpEthServerShutdown(); }

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
                DaqTriggerEventExt(GpsReceived, this);
            });

        _temperatureSubscriber = GetParticipant()->CreateDataSubscriber(
            "TemperatureSubscriber", PubSubDemoCommon::dataSpecTemperature, [this](IDataSubscriber * /*subscriber*/, const DataMessageEvent &dataMessageEvent) {
                double temperature = PubSubDemoCommon::DeserializeTemperature(SilKit::Util::ToStdVector(dataMessageEvent.data));

                std::stringstream ss;
                ss << "Received temperature data: temperature=" << temperature;
                GetLogger()->Info(ss.str());

                _xcp_temperature = temperature;
                DaqTriggerEventExt(TempReceived, this);
            });

        // Initialize XCP server for measurement on TCP port 5556
        XcpSetLogLevel(3);
        XcpInit("SilKitDemoSubscriber", "1.0", XCP_MODE_LOCAL);
        uint8_t xcp_addr[4] = {0, 0, 0, 0};
        XcpEthServerInit(xcp_addr, 5556, true, 1024 * 32);
        A2lInit(xcp_addr, 5556, true, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT);

        DaqCreateEvent(GpsReceived);
        A2lSetRelativeAddrMode(GpsReceived, this);
        A2lCreateMeasurement(_xcp_latitude, "Received GPS latitude in degrees");
        A2lCreateMeasurement(_xcp_longitude, "Received GPS longitude in degrees");
        DaqCreateEvent(TempReceived);
        A2lSetRelativeAddrMode(TemperatureReceived, this);
        A2lCreateMeasurement(_xcp_temperature, "Received temperature in Celsius");
    }

    void InitControllers() override {}

    void DoWorkSync(std::chrono::nanoseconds /*now*/) override {}

    void DoWorkAsync() override {}
};

int main(int argc, char **argv) {
    Arguments args;
    args.participantName = "Subscriber";
    Subscriber app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - Subscriber: Receive GPS and Temperature data");

    return app.Run();
}
