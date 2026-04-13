// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#include "ApplicationBase.hpp"
#include "PubSubDemoCommon.hpp"
#include "XcpHelper.hpp"

class Publisher : public ApplicationBase {
  public:
    // Inherit constructors
    using ApplicationBase::ApplicationBase;

    ~Publisher() { XcpServerShutdown(); }

  private:
    IDataPublisher *_gpsPublisher;
    IDataPublisher *_temperaturePublisher;

    double _latitude = 0.0;
    double _longitude = 0.0;
    double _signal = 0.0;
    double _temperatur = 0.0;

    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {
        _gpsPublisher = GetParticipant()->CreateDataPublisher("GpsPublisher", PubSubDemoCommon::dataSpecGps, 0);
        _temperaturePublisher = GetParticipant()->CreateDataPublisher("TemperaturePublisher", PubSubDemoCommon::dataSpecTemperature, 0);

        // Initialize XCP server for measurement on TCP port 5555
        XcpServerInit(GetArguments().participantName, "V1.0", 5555);

        DaqCreateEvent(PubTask);
        A2lSetRelativeAddrMode(PubTask, this);
        A2lCreateMeasurement(_latitude, "GPS latitude in degrees");
        A2lCreateMeasurement(_longitude, "GPS longitude in degrees");
        A2lCreateMeasurement(_signal, "GPS signal quality");
        A2lCreateMeasurement(_temperatur, "Temperature in Celsius");
    }

    void InitControllers() override {}

    void PublishGPSData(std::chrono::nanoseconds now) {

        _latitude = 48.8235 + static_cast<double>((rand() % 150)) / 100000;
        _longitude = 9.0965 + static_cast<double>((rand() % 150)) / 100000;
        _signal = std::sin(2.0 * M_PI * std::chrono::duration<double>(now).count()); // sine signal with values between -1 and 1 and period of 1s

        PubSubDemoCommon::GpsData gpsData;
        gpsData.latitude = _latitude;
        gpsData.longitude = _longitude;
        gpsData.signal = _signal;
        auto gpsSerialized = PubSubDemoCommon::SerializeGPSData(gpsData);

        std::stringstream ss;
        ss << "Publishing GPS data: lat=" << gpsData.latitude << ", lon=" << gpsData.longitude << ", signal=" << gpsData.signal;
        GetLogger()->Info(ss.str());

        _gpsPublisher->Publish(gpsSerialized);
    }

    void PublishTemperatureData() {
        double temperature = 25.0 + static_cast<double>(rand() % 10) / 10.0;
        auto temperatureSerialized = PubSubDemoCommon::SerializeTemperature(temperature);

        std::stringstream ss;
        ss << "Publishing temperature data: temperature=" << temperature;
        GetLogger()->Info(ss.str());

        _temperatur = temperature;
        _temperaturePublisher->Publish(temperatureSerialized);
    }

    void DoWorkSync(std::chrono::nanoseconds now) override {
        XcpUpdateSimTime(now);
        PublishGPSData(now);
        PublishTemperatureData();
        DaqTriggerEventExt(PubTask, this);
    }

    void DoWorkAsync() override {
        // PublishGPSData();
        // PublishTemperatureData();
        // DaqTriggerEventExt(PubTask, this);
    }
};

int main(int argc, char **argv) {
    Arguments args;
    args.participantName = "Publisher";
    Publisher app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - Publisher: Publish GPS and Temperature data");

    return app.Run();
}
