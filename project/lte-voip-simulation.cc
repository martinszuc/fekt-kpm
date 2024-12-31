/**
 * @file lte-voip-simulation.cc
 * @brief LTE simulation with VoIP traffic, FlowMonitor, PCAP, and Gnuplot (ns-3.39 compatible).
 * @author Martin Szuc
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/lte-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LTEVoipSimulation");

// Function Prototypes
void ConfigureLogging();
void ConfigureEnbMobility(NodeContainer &enbNodes);
void ConfigureUeMobility(NodeContainer &ueNodes);
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer);
void InstallVoipApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime);
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper &flowHelper);
void AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper, Ptr<FlowMonitor> flowMonitor, const std::string &filePrefix);
void EnableLteTraces(Ptr<LteHelper> lteHelper);

/**
 * @brief Main function for LTE + VoIP simulation.
 */
int main(int argc, char *argv[])
{
    uint16_t numEnb = 2;
    uint16_t numUe = 5;
    double simTime = 10.0;
    std::string outputDir = "lte-voip-output";

    CommandLine cmd;
    cmd.AddValue("numEnb", "Number of eNodeBs", numEnb);
    cmd.AddValue("numUe", "Number of UEs", numUe);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("outputDir", "Output directory", outputDir);
    cmd.Parse(argc, argv);

    system(("mkdir -p " + outputDir).c_str());
    ConfigureLogging();

    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(numEnb);
    ueNodes.Create(numUe);
    remoteHostContainer.Create(1);

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::OkumuraHataPropagationLossModel"));
    lteHelper->SetPathlossModelAttribute("Environment", StringValue("SubUrban"));
    lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(1800.0));

    ConfigureEnbMobility(enbNodes);
    ConfigureUeMobility(ueNodes);

    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(remoteHostContainer);

    epcHelper->AssignUeIpv4Address(ueDevs);

    for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
    {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % numEnb));
    }

    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer);

    InstallVoipApplications(ueNodes, remoteHostAddr, simTime);
    EnableLteTraces(lteHelper);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    AnalyzeFlowMonitor(flowHelper, flowMonitor, outputDir + "/voip-stats");

    Simulator::Destroy();
    NS_LOG_INFO("VoIP simulation finished!");
    return 0;
}

/**
 * @brief Configure logging for simulation components.
 */
void ConfigureLogging()
{
    LogComponentEnable("LTEVoipSimulation", LOG_LEVEL_INFO);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
}

/**
 * @brief Configure mobility for eNodeBs (fixed positions, 30m height).
 */
void ConfigureEnbMobility(NodeContainer &enbNodes)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(0.0, 100.0, 30.0));   // eNodeB 0
    posAlloc->Add(Vector(500.0, 100.0, 30.0)); // eNodeB 1
    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}

/**
 * @brief Configure mobility for UEs with RandomWaypoint and height of 1.5m.
 */
void ConfigureUeMobility(NodeContainer &ueNodes)
{
    MobilityHelper ueMobility;
    Ptr<PositionAllocator> positionAlloc = CreateObject<RandomRectanglePositionAllocator>();
    positionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
    positionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
    ueMobility.SetPositionAllocator(positionAlloc);
    ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                "Speed", StringValue("ns3::UniformRandomVariable[Min=2.0|Max=10.0]"),
                                "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                                "PositionAllocator", PointerValue(positionAlloc));
    ueMobility.Install(ueNodes);

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<MobilityModel> mobility = ueNodes.Get(i)->GetObject<MobilityModel>();
        Vector position = mobility->GetPosition();
        position.z = 1.5; // Set height to 1.5m
        mobility->SetPosition(position);
    }
}

/**
 * @brief Create a remote host connected to the PGW.
 */
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer)
{
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("1.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    return interfaces.GetAddress(1);
}

/**
 * @brief Install VoIP applications on UEs.
 */
void InstallVoipApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime)
{
    uint16_t port = 5000;
    OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(remoteAddr, port));
    onOff.SetAttribute("DataRate", StringValue("64kbps"));
    onOff.SetAttribute("PacketSize", UintegerValue(160));
    onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer apps = onOff.Install(ueNodes);
    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(simTime));
}

/**
 * @brief Enable LTE traces (PHY and MAC layers).
 */
void EnableLteTraces(Ptr<LteHelper> lteHelper)
{
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();
}

/**
 * @brief Setup FlowMonitor to collect metrics.
 */
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper &flowHelper)
{
    return flowHelper.InstallAll();
}

/**
 * @brief Analyze FlowMonitor results and generate plots.
 */
void AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper, Ptr<FlowMonitor> flowMonitor, const std::string &filePrefix)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    Gnuplot throughputPlot(filePrefix + "-throughput.png");
    throughputPlot.SetTitle("Throughput vs. Flow ID");
    throughputPlot.SetTerminal("png");
    throughputPlot.SetLegend("Flow ID", "Throughput (bps)");
    Gnuplot2dDataset dataset;
    dataset.SetStyle(Gnuplot2dDataset::POINTS);

    for (auto &iter : stats)
    {
        double timeFirstTx = iter.second.timeFirstTxPacket.GetSeconds();
        double timeLastRx = iter.second.timeLastRxPacket.GetSeconds();
        double throughputBps = 0.0;

        if (timeLastRx > timeFirstTx)
        {
            throughputBps = (iter.second.rxBytes * 8.0) / (timeLastRx - timeFirstTx);
        }

        dataset.Add(iter.first, throughputBps);
    }

    throughputPlot.AddDataset(dataset);
    std::ofstream plotFile(filePrefix + "-throughput.plt");
    throughputPlot.GenerateOutput(plotFile);
    plotFile.close();
}
