// silkit_demo - Publisher application

#include "ApplicationBase.hpp"
#include "XcpHelper.hpp"

using namespace xcplib; // For CalSeg

//-----------------------------------------------------------------------------------------------------
// Demo global measurement values

class Publisher : public ApplicationBase {
  public:
    Publisher(Arguments args = Arguments{}) : ApplicationBase(args) {}
    ~Publisher() { XcpServerShutdown(); }

  private:
    void AddCommandLineArgs() override {}

    void EvaluateCommandLineArgs() override {}

    void CreateControllers() override {}

    void InitControllers() override {}

    void DoWorkSync(std::chrono::nanoseconds now) override {}

    void DoWorkAsync() override {}
};

int main(int argc, char **argv) {

    // Initialize XCP server for measurement on TCP port 5555
    XcpServerInit("XcpServer", "V1.0", 5555, XCP_MODE_SHM_SERVER);

    Arguments args;
    args.participantName = "XcpServer";
    Publisher app{args};
    app.SetupCommandLineArgs(argc, argv, "SIL Kit Demo - XCP Server");
    return app.Run();
}
