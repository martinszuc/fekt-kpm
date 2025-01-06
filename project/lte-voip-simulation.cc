/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file enhanced-lte-voip-simulation-simple.cc
 * @brief Enhanced LTE + VoIP simulation for ns-3.39 with multiple eNodeBs and UEs.
 */

#include "ns3/animation-interface.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EnhancedLteVoipSimulationSimple");

// Global variables for real-time stats
static double g_statsInterval = 1.0; // <-- MODIFIED: Changed to 1 second
static double g_currentTime = 0.0;

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
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper& flowHelper);
void AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper,
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
    uint16_t numEnb = 4;
    uint16_t numUe = 5;        // Five UEs as per assignment
    double simTime = 20.0;     // Simulation time in seconds
    double areaSize = 100.0;   // Area size for node distribution (100x100 meters)
    bool enableNetAnim = true; // Enable NetAnim visualization

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
    enbNodes.Create(numEnb);
    ueNodes.Create(numUe);
    remoteHostContainer.Create(1); // Remote Host

    // Create LTE & EPC helpers
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // <-- MODIFIED: Set the Path Loss Model to ThreeLogDistance + some attribute tuning
    lteHelper->SetPathlossModelType(
        TypeId::LookupByName("ns3::ThreeLogDistancePropagationLossModel"));
    // Optional attribute tuning:
    lteHelper->SetPathlossModelAttribute("Distance0", DoubleValue(50.0));
    lteHelper->SetPathlossModelAttribute("Distance1", DoubleValue(200.0));
    lteHelper->SetPathlossModelAttribute("Exponent0", DoubleValue(2.0));
    lteHelper->SetPathlossModelAttribute("Exponent1", DoubleValue(3.0));
    lteHelper->SetPathlossModelAttribute("Exponent2", DoubleValue(3.5));

    // Configure automatic handover algorithm (Optional)
    lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
    lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger",
                                             TimeValue(MilliSeconds(120)));

    // Configure Mobility
    ConfigureEnbMobility(enbNodes, areaSize);
    ConfigureUeMobility(ueNodes, areaSize);

    // Set mobility for PGW & RemoteHost
    MobilityHelper remoteMobility;
    remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    remoteMobility.Install(remoteHostContainer.Get(0));
    remoteHostContainer.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(areaSize / 2, areaSize / 2, 1.5));

    remoteMobility.Install(epcHelper->GetPgwNode());
    epcHelper->GetPgwNode()->GetObject<MobilityModel>()->SetPosition(
        Vector(areaSize / 2, areaSize / 2, 1.5));

    // Log node positions
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> node = NodeList::GetNode(i);
        Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
        if (mobility)
        {
            Vector pos = mobility->GetPosition();
            NS_LOG_INFO("Node " << i << " Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
        }
        else
        {
            NS_LOG_WARN("Node " << i << " does not have a MobilityModel!");
        }
    }

    // Install LTE devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(remoteHostContainer);
    internet.Install(epcHelper->GetPgwNode());

    // Assign IP addresses
    epcHelper->AssignUeIpv4Address(ueDevs);

    // Attach UEs to the nearest eNB
    Ptr<MobilityModel> enbMobilityModel[numEnb];
    for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
    {
        enbMobilityModel[i] = enbNodes.Get(i)->GetObject<MobilityModel>();
    }

    for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
    {
        Ptr<MobilityModel> ueMobility = ueNodes.Get(i)->GetObject<MobilityModel>();
        Vector uePos = ueMobility->GetPosition();
        double minDistance = std::numeric_limits<double>::max();
        uint32_t closestEnb = 0;
        for (uint32_t j = 0; j < enbNodes.GetN(); ++j)
        {
            Vector enbPos = enbMobilityModel[j]->GetPosition();
            double distance =
                std::sqrt(std::pow(uePos.x - enbPos.x, 2) +
                          std::pow(uePos.y - enbPos.y, 2) +
                          std::pow(uePos.z - enbPos.z, 2));
            if (distance < minDistance)
            {
                minDistance = distance;
                closestEnb = j;
            }
        }
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(closestEnb));

        // Ensure UEs have z-coordinate = 1.5
        Vector pos = ueMobility->GetPosition();
        if (pos.z != 1.5)
        {
            pos.z = 1.5;
            ueMobility->SetPosition(pos);
            NS_LOG_WARN("UE " << i << " z-coordinate set to 1.5 meters after attachment.");
        }
    }

    // Create remote host link
    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer, areaSize);

    // Install VoIP applications
    InstallVoipApplications(ueNodes, remoteHostAddr, simTime, remoteHostContainer);

    // Enable LTE traces
    EnableLteTraces(lteHelper);

    // Setup FlowMonitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

    // Enable NetAnim (optional)
    AnimationInterface* anim = nullptr;
    if (enableNetAnim)
    {
        anim = new AnimationInterface("animation.xml");
        anim->SetMaxPktsPerTraceFile(5000000);

        for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
        {
            Ptr<Node> enbNode = enbNodes.Get(i);
            Vector enbPosition = enbNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(enbNode, "eNodeB " + std::to_string(i));
            anim->UpdateNodeColor(enbNode, 0, 0, 255);
            anim->SetConstantPosition(enbNode, enbPosition.x, enbPosition.y);
        }

        for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        {
            Ptr<Node> ueNode = ueNodes.Get(i);
            Vector uePosition = ueNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(ueNode, "UE " + std::to_string(i));
            anim->UpdateNodeColor(ueNode, 0, 255, 0);
            anim->SetConstantPosition(ueNode, uePosition.x, uePosition.y);
        }
    }

    // Schedule position logging every second
    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);

    // Schedule statistics updates every g_statsInterval
    Simulator::Schedule(Seconds(g_statsInterval),
                        &PeriodicStatsUpdate,
                        flowMonitor,
                        std::ref(flowHelper),
                        simTime);

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Analyze final FlowMonitor results
    AnalyzeFlowMonitor(flowHelper, flowMonitor, simTime);

    Simulator::Destroy();
    NS_LOG_INFO("Enhanced LTE simulation finished!");
    return 0;
}

// Enable logging for specific components
void
ConfigureLogging()
{
    LogComponentEnable("EnhancedLteVoipSimulationSimple", LOG_LEVEL_INFO);
    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
}

// Configure mobility for eNodeBs with fixed positions
void
ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();

    // Place eNodeBs at corners or quarter points
    posAlloc->Add(Vector(areaSize / 4, areaSize / 4, 30.0));
    posAlloc->Add(Vector(areaSize / 4, 3 * areaSize / 4, 30.0));
    posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 4, 30.0));
    posAlloc->Add(Vector(3 * areaSize / 4, 3 * areaSize / 4, 30.0));

    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}

// Configure mobility for UEs using RandomWaypointMobilityModel
void
ConfigureUeMobility(NodeContainer& ueNodes, double areaSize)
{
    MobilityHelper ueMobility;
    Ptr<RandomRectanglePositionAllocator> positionAlloc =
        CreateObject<RandomRectanglePositionAllocator>();
    positionAlloc->SetAttribute("X",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"));
    positionAlloc->SetAttribute("Y",
        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"));
    ueMobility.SetPositionAllocator(positionAlloc);

    // Use RandomWaypointMobilityModel
    ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
        "Speed", StringValue("ns3::UniformRandomVariable[Min=10.0|Max=30.0]"),
        "Pause", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
        "PositionAllocator", PointerValue(positionAlloc));
    ueMobility.Install(ueNodes);

    // Ensure UEs have z=1.5
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<Node> ue = ueNodes.Get(i);
        Ptr<MobilityModel> mobility = ue->GetObject<MobilityModel>();
        Ptr<RandomWaypointMobilityModel> rwm = DynamicCast<RandomWaypointMobilityModel>(mobility);
        if (rwm)
        {
            rwm->TraceConnectWithoutContext("NewPosition", MakeBoundCallback(&FixZCoordinate, ue));
        }

        Vector pos = mobility->GetPosition();
        pos.z = 1.5;
        mobility->SetPosition(pos);
    }
}

// Callback function to fix z-coordinate after each position update
void
FixZCoordinate(Ptr<Node> node)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    Vector pos = mobility->GetPosition();
    pos.z = 1.5;
    mobility->SetPosition(pos);
}

// Create a remote host and link to the PGW
Ipv4Address
CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper,
                 NodeContainer& remoteHostContainer,
                 double areaSize)
{
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("1.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // Mobility
    MobilityHelper remoteMobility;
    remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    remoteMobility.Install(remoteHostContainer.Get(0));
    remoteHostContainer.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(areaSize / 2, areaSize / 2, 1.5));

    MobilityHelper pgwMobility;
    pgwMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    pgwMobility.Install(pgw);
    pgw->GetObject<MobilityModel>()->SetPosition(
        Vector(areaSize / 2, areaSize / 2, 1.5));

    // Enable PCAP
    p2p.EnablePcap("pgw-p2p", devices.Get(0), true);
    p2p.EnablePcap("remote-host-p2p", devices.Get(1), true);

    return interfaces.GetAddress(1);
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

        OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(remoteAddr, port));
        onOff.SetAttribute("DataRate", StringValue("32kbps")); // Typical VoIP
        onOff.SetAttribute("PacketSize", UintegerValue(160));  // bytes
        onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer apps = onOff.Install(ueNodes.Get(i));
        apps.Start(Seconds(1.0));
        apps.Stop(Seconds(simTime));

        // PacketSink on remote host
        PacketSinkHelper packetSink("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = packetSink.Install(remoteHostContainer.Get(0));
        sinkApps.Start(Seconds(0.5));
        sinkApps.Stop(Seconds(simTime));
    }
}

// Enable LTE protocol tracing
void
EnableLteTraces(Ptr<LteHelper> lteHelper)
{
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();
    lteHelper->EnableRlcTraces();
    lteHelper->EnablePdcpTraces();
}

// Setup FlowMonitor
Ptr<FlowMonitor>
SetupFlowMonitor(FlowMonitorHelper& flowHelper)
{
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
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
    uint64_t totalRxPackets = 0;

    // Example: show per-flow throughput in logs
    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        if (t.protocol != 17) // Skip non-UDP
            continue;

        double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
        if (duration > 0)
        {
            double flowThroughputKbps = (iter.second.rxBytes * 8.0) / 1000.0 / duration;
            totalThroughput += flowThroughputKbps;

            NS_LOG_INFO("FlowID=" << iter.first << " ("
                                  << t.sourceAddress << "->" << t.destinationAddress
                                  << ") Throughput=" << flowThroughputKbps << " Kbps");
        }

        if (iter.second.rxPackets > 0)
        {
            double avgFlowLatency =
                (iter.second.delaySum.GetSeconds() / iter.second.rxPackets) * 1000.0; // ms
            totalLatencySum += avgFlowLatency * iter.second.rxPackets;
            totalRxPackets += iter.second.rxPackets;
        }
    }

    double avgLatency = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
    double avgThroughput = totalThroughput; // aggregated for all UDP flows

    // Store for time-series plotting
    g_timeSeries.push_back(g_currentTime);
    g_throughputSeries.push_back(avgThroughput);
    g_avgLatencySeries.push_back(avgLatency);

    NS_LOG_INFO("Time: " << g_currentTime
                 << "s, Aggregate Throughput: " << avgThroughput
                 << " Kbps, Avg Latency: " << avgLatency << " ms");

    if (g_currentTime + g_statsInterval <= simTime)
    {
        Simulator::Schedule(Seconds(g_statsInterval),
                            &PeriodicStatsUpdate,
                            flowMonitor,
                            std::ref(flowHelper),
                            simTime);
    }
}

// Log node positions every second
void
LogAllNodePositions()
{
    NS_LOG_INFO("----- Node Positions at " << Simulator::Now().GetSeconds() << "s -----");
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> node = NodeList::GetNode(i);
        Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
        if (mobility)
        {
            Vector pos = mobility->GetPosition();
            NS_LOG_INFO("Node " << i << " Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
        }
        else
        {
            NS_LOG_WARN("Node " << i << " does not have a MobilityModel!");
        }
    }
    NS_LOG_INFO("-----------------------------------------------------");

    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);
}

// Analyze FlowMonitor results and generate plots
void
AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper, Ptr<FlowMonitor> flowMonitor, double simTime)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = flowMonitor->GetFlowStats();

    double totalThroughputSum = 0.0; // Kbps
    double totalLatencySum = 0.0;    // ms
    uint64_t totalRxPackets = 0;
    uint64_t totalTxPackets = 0;
    uint32_t flowCount = 0;

    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        if (t.protocol != 17) // UDP only
            continue;

        flowCount++;
        double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
        if (duration > 0)
        {
            double throughputKbps = (iter.second.rxBytes * 8.0) / 1000.0 / duration;
            totalThroughputSum += throughputKbps;
        }
        if (iter.second.rxPackets > 0)
        {
            double avgFlowLatency =
                (iter.second.delaySum.GetSeconds() / iter.second.rxPackets) * 1000.0;
            totalLatencySum += (avgFlowLatency * iter.second.rxPackets);
            totalRxPackets += iter.second.rxPackets;
        }
        totalTxPackets += iter.second.txPackets;
    }

    double overallAvgLatency = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
    double overallAvgThroughput = (flowCount > 0) ? (totalThroughputSum / flowCount) : 0.0;
    double packetLossRate = (totalTxPackets > 0)
                                ? (double)(totalTxPackets - totalRxPackets) / totalTxPackets * 100.0
                                : 0.0;

    // Generate time-series plot
    {
        Gnuplot plot("throughput-latency-time-series.plt");
        plot.SetTitle("Throughput and Latency Over Time");
        plot.SetTerminal("png size 800,600");
        plot.SetLegend("Time (s)", "Value");
        Gnuplot2dDataset datasetThroughput;
        datasetThroughput.SetTitle("Throughput (Kbps)");
        datasetThroughput.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        Gnuplot2dDataset datasetLatency;
        datasetLatency.SetTitle("Latency (ms)");
        datasetLatency.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        for (size_t i = 0; i < g_timeSeries.size(); ++i)
        {
            datasetThroughput.Add(g_timeSeries[i], g_throughputSeries[i]);
            datasetLatency.Add(g_timeSeries[i], g_avgLatencySeries[i]);
        }

        plot.AddDataset(datasetThroughput);
        plot.AddDataset(datasetLatency);

        // Save to file
        std::ofstream plotFile("throughput-latency-time-series.plt");
        plotFile << "set output 'throughput-latency-time-series.png'\n";
        plot.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Throughput and latency time-series plot generated.");
    }

    // Log final metrics
    NS_LOG_INFO("===== FINAL METRICS =====");
    NS_LOG_INFO("Avg Throughput: " << overallAvgThroughput << " Kbps");
    NS_LOG_INFO("Avg Latency   : " << overallAvgLatency << " ms");
    NS_LOG_INFO("Packet Loss   : " << packetLossRate << "%");

    flowMonitor->SerializeToXmlFile("flowmon.xml", true, true);
    NS_LOG_INFO("FlowMonitor results saved to XML.");

    // Generate Markdown report
    std::ofstream mdReport("simulation-report.md");
    mdReport << "# Simulation Report\n\n";
    mdReport << "**Simulation Time**: " << simTime << "s\n\n";
    mdReport << "## Final Metrics Summary\n";
    mdReport << "- **Avg Throughput**: " << overallAvgThroughput << " Kbps\n";
    mdReport << "- **Avg Latency**   : " << overallAvgLatency << " ms\n";
    mdReport << "- **Packet Loss**   : " << packetLossRate << "%\n\n";
    mdReport << "## Generated Plots\n";
    mdReport << "- throughput-latency-time-series.png\n\n";
    mdReport << "## FlowMonitor Results\n";
    mdReport << "FlowMonitor results are saved in flowmon.xml.\n";
    mdReport.close();
    NS_LOG_INFO("Markdown report generated: simulation-report.md");
}
