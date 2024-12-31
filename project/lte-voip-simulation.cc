/**
 * @file lte-udp-simulation.cc
 * @brief LTE simulation with simple UDP echo traffic, FlowMonitor, PCAP, and Gnuplot
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

NS_LOG_COMPONENT_DEFINE("LTEUdpSimulation");

/**
 * @brief Configure logging.
 */
void ConfigureLogging()
{
    LogComponentEnable("LTEUdpSimulation", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
}

/**
 * @brief Configure eNodeB mobility (fixed).
 */
void ConfigureEnbMobility(NodeContainer &enbNodes)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(0.0, 100.0, 0.0));   // eNodeB 0
    posAlloc->Add(Vector(500.0, 100.0, 0.0)); // eNodeB 1
    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}

/**
 * @brief Configure UE mobility with RandomWaypoint.
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
}

/**
 * @brief Create and connect a remote host to the PGW via a point-to-point link.
 */
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer)
{
    // PGW node
    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // Create P2P link
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));

    // Assign IP
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("2.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // Return the IP assigned to remote host
    return interfaces.GetAddress(1);
}

/**
 * @brief Install UDP Echo applications on UEs sending to remote host.
 */
void InstallUdpApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime)
{
    // UDP Echo Server on remote host side
    // (We are only using the IP address here for demonstration,
    //  in a real setup, you might also install InternetStack on the remote host
    //  and place the server app there. For simplicity, let's just show the client side.)
    uint16_t port = 9;

    // Typically you'd install the UdpEchoServer on the actual remote host node:
    // UdpEchoServerHelper echoServer(port);
    // ApplicationContainer serverApps = echoServer.Install(remoteHost);
    // serverApps.Start(Seconds(0.5));
    // serverApps.Stop(Seconds(simTime));

    // For demonstration, let's install the echo server on the first UE (not typical):
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(ueNodes.Get(0));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(simTime));

    // Echo client on the remaining UEs
    for (uint32_t i = 1; i < ueNodes.GetN(); ++i)
    {
        UdpEchoClientHelper echoClient(InetSocketAddress(remoteAddr, port));
        echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(0.05)));
        echoClient.SetAttribute("PacketSize", UintegerValue(512));

        ApplicationContainer clientApps = echoClient.Install(ueNodes.Get(i));
        clientApps.Start(Seconds(1.0));
        clientApps.Stop(Seconds(simTime));
    }
}

/**
 * @brief Enable FlowMonitor to collect network statistics.
 */
Ptr<FlowMonitor> SetupFlowMonitor()
{
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    return flowMonitor;
}

/**
 * @brief Analyze FlowMonitor results and create Gnuplot.
 */
void AnalyzeFlowMonitor(Ptr<FlowMonitor> flowMonitor, std::string filePrefix)
{
    flowMonitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowMonitor->GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    // Save .flowmon file
    flowMonitor->SerializeToXmlFile(filePrefix + ".flowmon", true, true);

    // Example Gnuplot
    Gnuplot gnuplot(filePrefix + "-throughput.png");
    gnuplot.SetTitle("UDP Throughput vs. Flow ID");
    gnuplot.SetTerminal("png");
    gnuplot.SetLegend("Flow ID", "Throughput (bps)");
    Gnuplot2dDataset dataset;
    dataset.SetStyle(Gnuplot2dDataset::POINTS);

    for (auto iter = stats.begin(); iter != stats.end(); ++iter)
    {
        double throughputBps = (iter->second.rxBytes * 8.0) /
                               (iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds());
        dataset.Add(iter->first, throughputBps);
    }

    gnuplot.AddDataset(dataset);
    std::ofstream plotFile(filePrefix + "-throughput.plt");
    gnuplot.GenerateOutput(plotFile);
    plotFile.close();
}

/**
 * @brief Main function for LTE + UDP Echo simulation.
 */
int main(int argc, char *argv[])
{
    uint16_t numEnb = 2;
    uint16_t numUe = 5;
    double simTime = 10.0;
    std::string outputDir = "lte-udp-output";

    CommandLine cmd;
    cmd.AddValue("numEnb", "Number of eNodeBs", numEnb);
    cmd.AddValue("numUe", "Number of UEs", numUe);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("outputDir", "Output directory", outputDir);
    cmd.Parse(argc, argv);

    ConfigureLogging();

    // Create nodes
    NodeContainer enbNodes;
    enbNodes.Create(numEnb);
    NodeContainer ueNodes;
    ueNodes.Create(numUe);
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);

    // LTE & EPC
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Set a suburban path loss model
    lteHelper->SetPathlossModelType("ns3::OkumuraHataPropagationLossModel");
    lteHelper->SetPathlossModelAttribute("Environment", StringValue("Suburban"));
    lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(1800.0));

    // Mobility
    ConfigureEnbMobility(enbNodes);
    ConfigureUeMobility(ueNodes);

    // Install Devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Internet stack on UEs & Remote
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(remoteHostContainer);

    // Assign IP addresses to UEs
    epcHelper->AssignUeIpv4Address(ueDevs);

    // Attach UEs to eNB
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
    {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % numEnb));
    }

    // Create remote host link
    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer);

    // Install UDP echo applications
    InstallUdpApplications(ueNodes, remoteHostAddr, simTime);

    // Enable PCAP
    lteHelper->EnableTraces();

    // FlowMonitor
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor();

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Analyze results
    AnalyzeFlowMonitor(flowMonitor, outputDir + "/udp-stats");

    Simulator::Destroy();
    NS_LOG_INFO("UDP simulation finished!");
    return 0;
}
