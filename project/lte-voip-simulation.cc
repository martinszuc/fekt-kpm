/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file enhanced-lte-voip-simulation-udp.cc
 * @brief Enhanced LTE + VoIP simulation for ns-3.39 using UDP clients and multiple eNodeBs and UEs.
 */

#include "ns3/animation-interface.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/log.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/ping-helper.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EnhancedLteVoipSimulationUDP");

// Global variables for real-time stats
static double g_statsInterval = 5.0; // seconds
static double g_currentTime   = 0.0;

// Time-series data
std::vector<double> g_timeSeries;
std::vector<double> g_throughputSeries; // Kbps
std::vector<double> g_avgLatencySeries; // ms

// Function Prototypes
void ConfigureLogging();
void ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize);
void ConfigureUeMobility(NodeContainer& ueNodes, double areaSize);
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper,
                             NodeContainer& remoteHostContainer,
                             double areaSize);
void InstallVoipApplications(NodeContainer& ueNodes,
                             Ipv4Address remoteAddr,
                             double simTime,
                             NodeContainer& remoteHostContainer);
void InstallPingApplications(NodeContainer& ueNodes, Ipv4Address remoteAddr, double simTime);
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper& flowHelper);
void AnalyzeFlowMonitor(FlowMonitorHelper& flowMonitorHelper,
                        Ptr<FlowMonitor> flowMonitor,
                        double simTime);
void EnableLteTraces(Ptr<LteHelper> lteHelper);
void PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor,
                         FlowMonitorHelper& flowHelper,
                         double simTime);
void FixZCoordinate(Ptr<Node> node); // Callback to fix z-coordinate
void LogAllNodePositions();          // Function to log node positions

int
main(int argc, char* argv[])
{
    // Default parameters
    uint16_t numEnb        = 4;    // 4 eNodeBs
    uint16_t numUe         = 5;    // 5 UEs
    double simTime         = 100.0; // Simulation time in seconds
    double areaSize        = 100.0; // Area size for node distribution (100x100 meters)
    bool enableNetAnim     = true;  // Enable NetAnim visualization

    // Parse command-line arguments
    CommandLine cmd;
    cmd.AddValue("numEnb", "Number of eNodeBs", numEnb);
    cmd.AddValue("numUe", "Number of UEs", numUe);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("areaSize", "Square area for UE random positions [0..areaSize]", areaSize);
    cmd.AddValue("enableNetAnim", "Enable NetAnim animation", enableNetAnim);
    cmd.Parse(argc, argv);

    // Configure logging
    ConfigureLogging();

    // Create nodes
    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(numEnb); // eNodeB nodes
    ueNodes.Create(numUe);   // UE nodes
    remoteHostContainer.Create(1); // Remote Host node

    // Create LTE & EPC helpers
    Ptr<LteHelper> lteHelper            = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // (Optional) Set the Path Loss Model to ThreeLogDistancePropagationLossModel
    // Or switch to Friis for simpler debugging
    lteHelper->SetPathlossModelType(
        TypeId::LookupByName("ns3::ThreeLogDistancePropagationLossModel"));
    // Example adjustments (uncomment if needed):
    // lteHelper->SetPathlossModelAttribute("ReferenceDistance", DoubleValue(1.0));
    // lteHelper->SetPathlossModelAttribute("ReferenceLoss", DoubleValue(46.0));

    // Configure automatic handover algorithm (Optional)
    lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
    lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(120)));

    // 1. Configure Mobility for eNodeBs
    ConfigureEnbMobility(enbNodes, areaSize);

    // 2. Configure Mobility for UEs
    ConfigureUeMobility(ueNodes, areaSize);

    // 3. Configure Mobility for PGW and Remote Host
    MobilityHelper remoteMobility;
    remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

    // 3a. Remote Host Mobility
    remoteMobility.Install(remoteHostContainer.Get(0));
    remoteHostContainer.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(areaSize / 2, areaSize / 2, 1.5)); // Central

    // 3b. PGW Mobility
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NS_ASSERT_MSG(pgw, "PGW node is null! The EPC helper did not create a PGW node.");
    remoteMobility.Install(pgw);
    Ptr<MobilityModel> pgwMobility = pgw->GetObject<MobilityModel>();
    NS_ASSERT_MSG(pgwMobility, "PGW MobilityModel is null after Install().");
    pgwMobility->SetPosition(Vector(areaSize / 2, areaSize / 2, 1.5));

    // **Install LTE Devices**
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

    // **Install Internet stack**
    InternetStackHelper internet;
    // Install on UEs
    internet.Install(ueNodes);
    // Install on the remote host
    internet.Install(remoteHostContainer);
    // Install on the PGW node
    internet.Install(pgw);

    // **Assign IP addresses to UEs**
    epcHelper->AssignUeIpv4Address(ueDevs);

    // **Create P2P link to Remote Host & get IP address**
    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer, areaSize);
    NS_LOG_INFO("Remote Host IP Address: " << remoteHostAddr);

    // **Attach UEs to the nearest eNodeB**
    //   (Manual approach or you can do: lteHelper->AttachToClosestEnb(ueDevs, enbDevs);)
    std::vector<Ptr<MobilityModel>> enbMobilityModel(numEnb);
    for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
    {
        enbMobilityModel[i] = enbNodes.Get(i)->GetObject<MobilityModel>();
    }
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
    {
        Ptr<MobilityModel> ueMobility = ueNodes.Get(i)->GetObject<MobilityModel>();
        Vector uePos                  = ueMobility->GetPosition();
        double minDistance            = std::numeric_limits<double>::max();
        uint32_t closestEnb          = 0;

        for (uint32_t j = 0; j < enbNodes.GetN(); ++j)
        {
            Vector enbPos = enbMobilityModel[j]->GetPosition();
            double distance =
                std::sqrt(std::pow(uePos.x - enbPos.x, 2) + std::pow(uePos.y - enbPos.y, 2) +
                          std::pow(uePos.z - enbPos.z, 2));

            if (distance < minDistance)
            {
                minDistance = distance;
                closestEnb  = j;
            }
        }
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(closestEnb));
        // Force z=1.5 for sanity
        Vector pos = ueMobility->GetPosition();
        if (pos.z != 1.5)
        {
            pos.z = 1.5;
            ueMobility->SetPosition(pos);
            NS_LOG_WARN("UE " << i << " z-coordinate set to 1.5m after attachment.");
        }
    }

    // ----------------------------------------------------------------
    //         >>> CRUCIAL ROUTING FIXES <<<
    // ----------------------------------------------------------------
    // Populate global routing so the remote host and UEs know routes
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Optionally set a default route on each UE to the PGW:
    /*
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<Ipv4> ipv4 = ueNodes.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> ueStaticRouting = Ipv4RoutingHelper::GetStaticRouting(ipv4);
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }
    */

    // **Install Ping apps** for quick connectivity check
    InstallPingApplications(ueNodes, remoteHostAddr, simTime);

    // **Install VoIP apps** (UDP client & PacketSink)
    InstallVoipApplications(ueNodes, remoteHostAddr, simTime, remoteHostContainer);

    // **Enable LTE traces**
    EnableLteTraces(lteHelper);

    // ----------------------------------------------------------------
    //             >>> ADDITIONAL PCAP CAPTURE ON LTE <<<
    // ----------------------------------------------------------------
    // This may produce many pcap files, but is useful for debugging
    lteHelper->EnablePdcpTraces(); // PDCP-level
    // If you also want to sniff IP traffic, see lteHelper->EnablePcap(...) in the LENA docs.

    // **Setup FlowMonitor**
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

    // ----------------------------------------------------------------
    //    Move the “Comprehensive Node Verification” AFTER PGW setup
    // ----------------------------------------------------------------
    NS_LOG_INFO("=== Comprehensive Node Verification ===");
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> node         = NodeList::GetNode(i);
        Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
        if (mob)
        {
            Vector p = mob->GetPosition();
            NS_LOG_INFO("Node " << i << " Position: (" << p.x << ", " << p.y << ", " << p.z << ")");
        }
        else
        {
            NS_LOG_WARN("Node " << i << " does not have a MobilityModel!");
        }
    }

    // **Enable NetAnim Visualization** (optional)
    AnimationInterface* anim = nullptr;
    if (enableNetAnim)
    {
        anim = new AnimationInterface("animation.xml");
        anim->SetMaxPktsPerTraceFile(5000000);

        // eNodeBs
        for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
        {
            Ptr<Node> enbNode    = enbNodes.Get(i);
            Vector enbPosition   = enbNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(enbNode, "eNodeB " + std::to_string(i));
            anim->UpdateNodeColor(enbNode, 0, 0, 255);
            anim->SetConstantPosition(enbNode, enbPosition.x, enbPosition.y);
        }

        // UEs
        for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        {
            Ptr<Node> ueNode   = ueNodes.Get(i);
            Vector uePosition  = ueNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(ueNode, "UE " + std::to_string(i));
            anim->UpdateNodeColor(ueNode, 0, 255, 0);
            anim->SetConstantPosition(ueNode, uePosition.x, uePosition.y);
        }
    }

    // **Schedule Periodic Logging of All Node Positions every second**
    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);

    // **Schedule statistics updates** (FlowMonitor metrics every g_statsInterval)
    Simulator::Schedule(Seconds(g_statsInterval),
                        &PeriodicStatsUpdate,
                        flowMonitor,
                        std::ref(flowHelper),
                        simTime);

    // **Run simulation**
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // **Analyze FlowMonitor results**
    AnalyzeFlowMonitor(flowHelper, flowMonitor, simTime);

    Simulator::Destroy();
    NS_LOG_INFO("Enhanced LTE simulation finished!");
    return 0;
}

// Enable logging for specific components
void
ConfigureLogging()
{
    // Enable more detailed logging for debugging (optional)
    LogComponentEnable("EnhancedLteVoipSimulationUDP", LOG_LEVEL_INFO);
    LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
    LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
    // If you need more verbosity, uncomment:
    // LogComponentEnable("EnhancedLteVoipSimulationUDP", LOG_LEVEL_ALL);
    // LogComponentEnable("UdpClient", LOG_LEVEL_ALL);
    // LogComponentEnable("PacketSink", LOG_LEVEL_ALL);
    // LogComponentEnable("PingApplication", LOG_LEVEL_ALL);
}

// Configure mobility for eNodeBs with fixed positions
void
ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();

    // 4 eNodeBs placed at quarter points of area
    posAlloc->Add(Vector(areaSize / 4, areaSize / 4, 30.0));       // eNodeB0
    posAlloc->Add(Vector(areaSize / 4, 3 * areaSize / 4, 30.0));   // eNodeB1
    posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 4, 30.0));   // eNodeB2
    posAlloc->Add(Vector(3 * areaSize / 4, 3 * areaSize / 4, 30.0)); // eNodeB3

    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);

    for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
    {
        Ptr<MobilityModel> mobility = enbNodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition();
        NS_LOG_INFO("eNodeB " << i << " Position: (" << pos.x << ", " << pos.y << ", " << pos.z
                              << ")");
    }
}

// Configure mobility for UEs using RandomWaypointMobilityModel
void
ConfigureUeMobility(NodeContainer& ueNodes, double areaSize)
{
    MobilityHelper ueMobility;
    Ptr<RandomRectanglePositionAllocator> positionAlloc =
        CreateObject<RandomRectanglePositionAllocator>();
    positionAlloc->SetAttribute("X",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaSize) + "]"));
    positionAlloc->SetAttribute("Y",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaSize) + "]"));
    ueMobility.SetPositionAllocator(positionAlloc);

    // RandomWaypointMobilityModel with speeds 10-30 m/s, 2s pause
    ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                "Speed",
                                StringValue("ns3::UniformRandomVariable[Min=10.0|Max=30.0]"),
                                "Pause",
                                StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                                "PositionAllocator",
                                PointerValue(positionAlloc));
    ueMobility.Install(ueNodes);

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<Node> ue = ueNodes.Get(i);
        Ptr<MobilityModel> mobility = ue->GetObject<MobilityModel>();
        Ptr<RandomWaypointMobilityModel> rwm = DynamicCast<RandomWaypointMobilityModel>(mobility);
        if (rwm)
        {
            // Fix Z-coordinate to 1.5m after every movement
            rwm->TraceConnectWithoutContext("NewPosition", MakeBoundCallback(&FixZCoordinate, ue));
            NS_LOG_INFO("Connected FixZCoordinate callback to UE " << i);
        }
        // Initially set z=1.5
        Vector pos = mobility->GetPosition();
        pos.z = 1.5;
        mobility->SetPosition(pos);
        NS_LOG_INFO("UE " << i << " Initial Pos: (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
    }
}

// Callback to fix z-coordinate after each position update
void
FixZCoordinate(Ptr<Node> node)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    Vector pos = mobility->GetPosition();
    pos.z = 1.5; // Keep UEs at z=1.5m
    mobility->SetPosition(pos);
}

// Create a remote host and link to the PGW
Ipv4Address
CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper,
                 NodeContainer& remoteHostContainer,
                 double areaSize)
{
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NS_ASSERT_MSG(pgw, "PGW node is null in CreateRemoteHost()!");

    // Create a P2P link between PGW and Remote Host
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("1.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // PCAP tracing on that P2P link
    p2p.EnablePcap("pgw-p2p", devices.Get(0), true);
    p2p.EnablePcap("remote-host-p2p", devices.Get(1), true);

    return interfaces.GetAddress(1); // Typically 1.0.0.2
}

// Install VoIP applications on UEs
void
InstallVoipApplications(NodeContainer& ueNodes,
                        Ipv4Address remoteAddr,
                        double simTime,
                        NodeContainer& remoteHostContainer)
{
    uint16_t basePort = 5000;
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        uint16_t port = basePort + i;
        UdpClientHelper client(remoteAddr, port);
        client.SetAttribute("MaxPackets", UintegerValue(1000000));
        // ~100 kbps voice => 160 bytes every ~12.8ms
        client.SetAttribute("Interval", TimeValue(MilliSeconds(12.8)));
        client.SetAttribute("PacketSize", UintegerValue(160));

        ApplicationContainer clientApps = client.Install(ueNodes.Get(i));
        clientApps.Start(Seconds(1.0));
        clientApps.Stop(Seconds(simTime));

        // PacketSink on remote host
        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = sink.Install(remoteHostContainer.Get(0));
        sinkApps.Start(Seconds(0.5));
        sinkApps.Stop(Seconds(simTime));

        NS_LOG_INFO("VoIP (UdpClient) installed on UE " << i << " -> port " << port);
    }
}

// Simple Ping app to test connectivity
void
InstallPingApplications(NodeContainer& ueNodes, Ipv4Address remoteAddr, double simTime)
{
    PingHelper pingHelper(remoteAddr);
    pingHelper.SetAttribute("Interval", TimeValue(Seconds(1.0)));

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        ApplicationContainer pingApps = pingHelper.Install(ueNodes.Get(i));
        pingApps.Start(Seconds(2.0));  // start pinging at t=2s
        pingApps.Stop(Seconds(simTime));
        NS_LOG_INFO("Ping application installed on UE " << i);
    }
}

// Enable standard LTE traces
void
EnableLteTraces(Ptr<LteHelper> lteHelper)
{
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();
    lteHelper->EnableRlcTraces();
    lteHelper->EnablePdcpTraces();  // also captures PDCP-level traffic
    NS_LOG_INFO("Enabled LTE traces.");
}

// Setup FlowMonitor to capture statistics
Ptr<FlowMonitor>
SetupFlowMonitor(FlowMonitorHelper& flowHelper)
{
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    NS_LOG_INFO("FlowMonitor installed.");
    return flowMonitor;
}

// Periodically update and log network statistics
void
PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor, FlowMonitorHelper& flowHelper, double simTime)
{
    g_currentTime += g_statsInterval;

    flowMonitor->CheckForLostPackets();
    auto stats = flowMonitor->GetFlowStats();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    double totalThroughput = 0.0; // Kbps
    double totalLatencySum = 0.0; // ms
    uint64_t totalRxPackets = 0;  // for average latency

    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        // Focus on UDP (VoIP) only
        if (t.protocol != 17)  // 17 = UDP
            continue;

        double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
        if (duration > 0)
        {
            // Bytes -> bits -> Kb -> / duration => Kbps
            double throughputKbps = (iter.second.rxBytes * 8.0) / 1000.0 / duration;
            totalThroughput += throughputKbps;
        }
        if (iter.second.rxPackets > 0)
        {
            double avgFlowLatencyMs =
                (iter.second.delaySum.GetSeconds() / iter.second.rxPackets) * 1000.0;
            totalLatencySum += (avgFlowLatencyMs * iter.second.rxPackets);
            totalRxPackets  += iter.second.rxPackets;
        }
    }

    double avgLatencyMs   = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
    double avgThroughputKbps = totalThroughput; // aggregated

    // Store time-series data for plotting
    g_timeSeries.push_back(g_currentTime);
    g_throughputSeries.push_back(avgThroughputKbps);
    g_avgLatencySeries.push_back(avgLatencyMs);

    NS_LOG_INFO("Time: " << g_currentTime << "s, "
                         << "Aggregate Throughput: " << avgThroughputKbps << " Kbps, "
                         << "Avg Latency: " << avgLatencyMs << " ms");

    // Re-schedule if sim not finished
    if (g_currentTime + g_statsInterval <= simTime)
    {
        Simulator::Schedule(Seconds(g_statsInterval),
                            &PeriodicStatsUpdate,
                            flowMonitor,
                            std::ref(flowHelper),
                            simTime);
    }
}

// Periodically log node positions
void
LogAllNodePositions()
{
    NS_LOG_INFO("----- Node Positions at " << Simulator::Now().GetSeconds() << "s -----");
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> node         = NodeList::GetNode(i);
        Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
        if (mob)
        {
            Vector pos = mob->GetPosition();
            NS_LOG_INFO("Node " << i << " Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
        }
        else
        {
            NS_LOG_WARN("Node " << i << " does not have a MobilityModel!");
        }
    }
    NS_LOG_INFO("-----------------------------------------------------");

    // Schedule next logging in 1s
    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);
}

// Final analysis of FlowMonitor results + plots
void
AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper, Ptr<FlowMonitor> flowMonitor, double simTime)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    uint32_t flowCount = 0;
    double totalThroughputSum = 0.0; // Kbps
    double totalLatencySum   = 0.0;  // ms
    uint64_t totalRxPackets = 0;
    uint64_t totalTxPackets = 0;

    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        // Only consider UDP flows (VoIP)
        if (t.protocol != 17)
            continue;

        flowCount++;
        double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();

        if (duration > 0)
        {
            double throughput = (iter.second.rxBytes * 8.0) / 1000.0 / duration;
            totalThroughputSum += throughput;
        }
        if (iter.second.rxPackets > 0)
        {
            double avgFlowLatencyMs =
                iter.second.delaySum.GetSeconds() / iter.second.rxPackets * 1000.0;
            totalLatencySum += (avgFlowLatencyMs * iter.second.rxPackets);
            totalRxPackets  += iter.second.rxPackets;
        }
        totalTxPackets += iter.second.txPackets;
    }

    double overallAvgLatency   = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
    double overallAvgThroughput= (flowCount > 0) ? (totalThroughputSum / flowCount) : 0.0;
    double packetLossRate      = (totalTxPackets > 0)
                                   ? (double)(totalTxPackets - totalRxPackets) / totalTxPackets * 100.0
                                   : 0.0;

    // Generate Gnuplot for throughput
    {
        Gnuplot plotThroughput("throughput-time-series.plt");
        plotThroughput.SetTitle("Throughput Over Time");
        plotThroughput.SetTerminal("png size 800,600");
        plotThroughput.SetLegend("Time (s)", "Throughput (Kbps)");
        Gnuplot2dDataset datasetThroughput;
        datasetThroughput.SetTitle("Throughput (Kbps)");
        datasetThroughput.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        for (size_t i = 0; i < g_timeSeries.size(); ++i)
        {
            datasetThroughput.Add(g_timeSeries[i], g_throughputSeries[i]);
        }
        plotThroughput.AddDataset(datasetThroughput);

        // Output to .png
        std::ofstream plotFile("throughput-time-series.plt");
        plotFile << "set output 'throughput-time-series.png'\n";
        plotThroughput.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Throughput time-series plot generated.");
    }

    // Generate Gnuplot for latency
    {
        Gnuplot plotLatency("latency-time-series.plt");
        plotLatency.SetTitle("Latency Over Time");
        plotLatency.SetTerminal("png size 800,600");
        plotLatency.SetLegend("Time (s)", "Latency (ms)");
        Gnuplot2dDataset datasetLatency;
        datasetLatency.SetTitle("Latency (ms)");
        datasetLatency.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        for (size_t i = 0; i < g_timeSeries.size(); ++i)
        {
            datasetLatency.Add(g_timeSeries[i], g_avgLatencySeries[i]);
        }
        plotLatency.AddDataset(datasetLatency);

        // Output to .png
        std::ofstream plotFile("latency-time-series.plt");
        plotFile << "set output 'latency-time-series.png'\n";
        plotLatency.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Latency time-series plot generated.");
    }

    // Final logs
    NS_LOG_INFO("===== FINAL METRICS =====");
    NS_LOG_INFO("Avg Throughput: " << overallAvgThroughput << " Kbps");
    NS_LOG_INFO("Avg Latency   : " << overallAvgLatency   << " ms");
    NS_LOG_INFO("Packet Loss   : " << packetLossRate      << "%");

    // Save FlowMonitor results
    flowMonitor->SerializeToXmlFile("flowmon.xml", true, true);
    NS_LOG_INFO("FlowMonitor results saved to XML.");

    // Markdown summary
    std::ofstream mdReport("simulation-report.md");
    mdReport << "# Simulation Report\n\n";
    mdReport << "**Simulation Time**: " << simTime << " s\n\n";
    mdReport << "## Final Metrics Summary\n";
    mdReport << "- **Avg Throughput**: " << overallAvgThroughput << " Kbps\n";
    mdReport << "- **Avg Latency**   : " << overallAvgLatency << " ms\n";
    mdReport << "- **Packet Loss**   : " << packetLossRate << "%\n\n";
    mdReport << "## Generated Plots\n";
    mdReport << "- [Throughput Over Time](throughput-time-series.png)\n";
    mdReport << "- [Latency Over Time](latency-time-series.png)\n\n";
    mdReport << "## FlowMonitor Results\n";
    mdReport << "FlowMonitor results are saved in `flowmon.xml`.\n";
    mdReport.close();
    NS_LOG_INFO("Markdown report generated: simulation-report.md");
}
