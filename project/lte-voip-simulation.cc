/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file enhanced-lte-voip-simulation-fixed.cc
 * @brief Enhanced LTE + VoIP simulation in ns-3 LENA 3.39 with multiple eNodeBs and UEs.
 *
 * This script:
 * - Creates 4 eNodeBs at quarter points in a square area.
 * - Creates 5 UEs with RandomWaypointMobilityModel in a [0..areaSize] x [0..areaSize].
 * - Uses ThreeLogDistancePropagationLossModel for suburban path loss.
 * - Installs G.711-like VoIP traffic via OnOff (64 kbps, 160-byte frames).
 * - Sets default routes for UEs to enable traffic forwarding.
 * - Records throughput, latency, and packet loss using FlowMonitor.
 * - Generates Gnuplot figures and a Markdown report.
 */

#include "ns3/animation-interface.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
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

#include <fstream>
#include <limits>
#include <map>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EnhancedLteVoipSimulationFixed");

/**
 * Struct holding various simulation parameters. Modify as needed via CommandLine.
 */
struct SimulationParameters
{
    uint16_t numEnb = 4;     // Number of eNodeBs (fixed at corner-ish points)
    uint16_t numUe = 5;      // Number of UEs
    double simTime = 20.0;   // Simulation time (seconds)
    double areaSize = 200.0; // Simulation area side length (square)

    bool enableNetAnim = true;  // If true, produce NetAnim XML
    double statsInterval = 1.0; // FlowMonitor stats interval (seconds)

    // Path Loss model parameters (suburban-like)
    double distance0 = 70.0;
    double distance1 = 300.0;
    double exponent0 = 1.5;
    double exponent1 = 2.0;
    double exponent2 = 3.0;
};

// Globals for time-series data
static double g_currentTime = 0.0;
std::vector<double> g_timeSeries;
std::vector<std::vector<double>> g_ueThroughputSeries; // [UE][time-slice]
std::vector<double> g_avgLatencySeries;
std::vector<std::vector<double>> g_uePacketLossSeries; // [UE][time-slice]

// Maps to track flow info for incremental stats
std::map<FlowId, uint64_t> g_previousRxBytes;
std::map<FlowId, Time> g_previousDelaySum;
std::map<FlowId, uint64_t> g_previousRxPackets;
std::map<FlowId, uint32_t> g_flowIdToUeIndex;

// Prototypes
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
void EnableLteTraces(Ptr<LteHelper> lteHelper);
void PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor,
                         FlowMonitorHelper& flowHelper,
                         const SimulationParameters params);
void FixZCoordinate(Ptr<Node> node);
void LogAllNodePositions();
void AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper,
                        Ptr<FlowMonitor> flowMonitor,
                        const SimulationParameters params);

int
main(int argc, char* argv[])
{
    // Initialize simulation parameters
    SimulationParameters params;
    CommandLine cmd;

    // Command-line overrides
    cmd.AddValue("numEnb", "Number of eNodeBs", params.numEnb);
    cmd.AddValue("numUe", "Number of UEs", params.numUe);
    cmd.AddValue("simTime", "Simulation time (s)", params.simTime);
    cmd.AddValue("areaSize", "Square area side [m]", params.areaSize);
    cmd.AddValue("enableNetAnim", "Enable NetAnim output", params.enableNetAnim);

    // Path-loss parameters
    cmd.AddValue("distance0", "PathLoss Distance0 (m)", params.distance0);
    cmd.AddValue("distance1", "PathLoss Distance1 (m)", params.distance1);
    cmd.AddValue("exponent0", "PathLoss Exponent0", params.exponent0);
    cmd.AddValue("exponent1", "PathLoss Exponent1", params.exponent1);
    cmd.AddValue("exponent2", "PathLoss Exponent2", params.exponent2);

    cmd.Parse(argc, argv);

    // Prepare data vectors for throughput and packet loss
    g_ueThroughputSeries.resize(params.numUe, std::vector<double>());
    g_uePacketLossSeries.resize(params.numUe, std::vector<double>());

    // Enable logging
    ConfigureLogging();

    // Create nodes
    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(params.numEnb);
    ueNodes.Create(params.numUe);
    remoteHostContainer.Create(1); // Remote Host

    // LteHelper + EpcHelper
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Path loss: ThreeLogDistancePropagationLossModel
    lteHelper->SetPathlossModelType(
        TypeId::LookupByName("ns3::ThreeLogDistancePropagationLossModel"));
    lteHelper->SetPathlossModelAttribute("Distance0", DoubleValue(params.distance0));
    lteHelper->SetPathlossModelAttribute("Distance1", DoubleValue(params.distance1));
    lteHelper->SetPathlossModelAttribute("Exponent0", DoubleValue(params.exponent0));
    lteHelper->SetPathlossModelAttribute("Exponent1", DoubleValue(params.exponent1));
    lteHelper->SetPathlossModelAttribute("Exponent2", DoubleValue(params.exponent2));

    // LTE Scheduler
    lteHelper->SetSchedulerType("ns3::RrFfMacScheduler");

    // Optional handover
    lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
    lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(120)));

    // Mobility
    ConfigureEnbMobility(enbNodes, params.areaSize);
    ConfigureUeMobility(ueNodes, params.areaSize);

    // Position the remoteHost and the PGW
    MobilityHelper remoteMobility;
    remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    remoteMobility.Install(remoteHostContainer.Get(0));
    // Set position of remote host
    remoteHostContainer.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(params.areaSize / 2, params.areaSize / 2, 1.5));
    // PGW node
    remoteMobility.Install(epcHelper->GetPgwNode());
    epcHelper->GetPgwNode()->GetObject<MobilityModel>()->SetPosition(
        Vector(params.areaSize / 2, params.areaSize / 2, 1.5));

    // Install LTE devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Install Internet stack on UEs + remote host + PGW
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(remoteHostContainer);
    internet.Install(epcHelper->GetPgwNode());

    // Assign IP addresses to UEs
    epcHelper->AssignUeIpv4Address(ueDevs);

    // Attach each UE to the nearest eNB
    Ptr<MobilityModel> enbMobilityModel[params.numEnb];
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
            double dist =
                std::sqrt(std::pow(uePos.x - enbPos.x, 2) + std::pow(uePos.y - enbPos.y, 2) +
                          std::pow(uePos.z - enbPos.z, 2));
            if (dist < minDistance)
            {
                minDistance = dist;
                closestEnb = j;
            }
        }
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(closestEnb));

        // Force z=1.5 for UEs
        Vector pos = ueMobility->GetPosition();
        pos.z = 1.5;
        ueMobility->SetPosition(pos);
    }

    // Create the remote host link
    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer, params.areaSize);

    // Install VoIP (OnOff + PacketSink) traffic
    InstallVoipApplications(ueNodes, remoteHostAddr, params.simTime, remoteHostContainer);

    // Set default routes for all UEs
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
        Ptr<Node> ueNode = ueNodes.Get(u);
        Ptr<Ipv4> ipv4 = ueNode->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ipv4);
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
        NS_LOG_INFO("UE " << u << " default route set to "
                          << epcHelper->GetUeDefaultGatewayAddress());
    }

    // Enable LTE traces
    EnableLteTraces(lteHelper);

    // Setup FlowMonitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

    // Prepare incremental stats
    Simulator::Schedule(Seconds(0.0), [&flowMonitor, &flowHelper, &params]() {
        auto initialStats = flowMonitor->GetFlowStats();
        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

        // Map flows to UEs based on destination port
        for (auto it = initialStats.begin(); it != initialStats.end(); ++it)
        {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it->first);
            // Assuming the destination port uniquely identifies the UE
            uint16_t destPort = t.destinationPort;
            uint32_t ueIndex = 0;

            if (destPort >= 5000 && destPort < 5000 + params.numUe)
            {
                ueIndex = destPort - 5000;
                g_flowIdToUeIndex[it->first] = ueIndex;
                NS_LOG_INFO("Flow " << it->first << " mapped to UE " << ueIndex
                                    << " (src=" << t.sourceAddress
                                    << ", dst=" << t.destinationAddress << ":" << destPort << ")");
            }
            else
            {
                NS_LOG_WARN("Flow " << it->first
                                    << " has unexpected destination port: " << destPort);
            }

            // Initialize previous stats
            g_previousRxBytes[it->first] = 0;
            g_previousDelaySum[it->first] = Seconds(0);
            g_previousRxPackets[it->first] = 0;
        }
    });

    // (Optional) NetAnim
    AnimationInterface* anim = nullptr;
    if (params.enableNetAnim)
    {
        anim = new AnimationInterface("animation.xml");
        anim->SetMaxPktsPerTraceFile(5000000);

        // eNB info
        for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
        {
            Ptr<Node> enbNode = enbNodes.Get(i);
            Vector enbPos = enbNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(enbNode, "eNodeB " + std::to_string(i));
            anim->UpdateNodeColor(enbNode, 0, 0, 255); // Blue
            anim->SetConstantPosition(enbNode, enbPos.x, enbPos.y);
        }

        // UE info
        for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        {
            Ptr<Node> ueNode = ueNodes.Get(i);
            Vector uePos = ueNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(ueNode, "UE " + std::to_string(i));
            anim->UpdateNodeColor(ueNode, 0, 255, 0); // Green
            anim->SetConstantPosition(ueNode, uePos.x, uePos.y);
        }

        // Remote Host
        anim->UpdateNodeDescription(remoteHostContainer.Get(0), "RemoteHost");
        anim->UpdateNodeColor(remoteHostContainer.Get(0), 255, 0, 0); // Red
    }

    // Periodic log of node positions
    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);

    // Periodic stats
    Simulator::Schedule(Seconds(params.statsInterval),
                        &PeriodicStatsUpdate,
                        flowMonitor,
                        std::ref(flowHelper),
                        params);

    // Run Simulation
    Simulator::Stop(Seconds(params.simTime));
    Simulator::Run();

    // Final analysis
    AnalyzeFlowMonitor(flowHelper, flowMonitor, params);

    Simulator::Destroy();
    NS_LOG_INFO("Enhanced LTE simulation finished!");
    return 0;
}

/**
 * Enable logging for specific components.
 *
 * IMPORTANT: To suppress unnecessary logs from OnOffApplication and PacketSink,
 * their logging has been disabled by commenting out the corresponding lines.
 */
void
ConfigureLogging()
{
    LogComponentEnable("EnhancedLteVoipSimulationFixed", LOG_LEVEL_INFO);
    // Disable logs for OnOffApplication and PacketSink to remove verbose packet logs
    // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
}

/**
 * Place the eNBs at four distinct points in a square area.
 */
void
ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();

    // Four eNBs at quarter points in the square
    posAlloc->Add(Vector(areaSize / 4, areaSize / 4, 30.0));
    posAlloc->Add(Vector(areaSize / 4, 3 * areaSize / 4, 30.0));
    posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 4, 30.0));
    posAlloc->Add(Vector(3 * areaSize / 4, 3 * areaSize / 4, 30.0));

    // If numEnb > 4, distribute remaining eNBs evenly
    for (uint16_t i = 4; i < enbNodes.GetN(); ++i)
    {
        double x = (i % 2 == 0) ? areaSize / 4 : 3 * areaSize / 4;
        double y = (i / 2 == 0) ? areaSize / 4 : 3 * areaSize / 4;
        posAlloc->Add(Vector(x, y, 30.0));
    }

    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}

/**
 * Configure UEs using RandomWaypointMobilityModel in a [0..areaSize] x [0..areaSize].
 */
void
ConfigureUeMobility(NodeContainer& ueNodes, double areaSize)
{
    MobilityHelper ueMobility;
    Ptr<RandomRectanglePositionAllocator> positionAlloc =
        CreateObject<RandomRectanglePositionAllocator>();

    std::ostringstream xBound, yBound;
    xBound << "ns3::UniformRandomVariable[Min=0.0|Max=" << areaSize << "]";
    yBound << "ns3::UniformRandomVariable[Min=0.0|Max=" << areaSize << "]";
    positionAlloc->SetAttribute("X", StringValue(xBound.str()));
    positionAlloc->SetAttribute("Y", StringValue(yBound.str()));

    ueMobility.SetPositionAllocator(positionAlloc);
    ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                "Speed",
                                StringValue("ns3::UniformRandomVariable[Min=10.0|Max=30.0]"),
                                "Pause",
                                StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                                "PositionAllocator",
                                PointerValue(positionAlloc));
    ueMobility.Install(ueNodes);

    // Force z=1.5 for UEs and fix Z on movement
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<Node> ue = ueNodes.Get(i);
        Ptr<MobilityModel> mobility = ue->GetObject<MobilityModel>();
        Ptr<RandomWaypointMobilityModel> rwm = DynamicCast<RandomWaypointMobilityModel>(mobility);
        if (rwm)
        {
            // Connect the "NewPosition" callback to fix the Z coordinate
            rwm->TraceConnectWithoutContext("NewPosition", MakeBoundCallback(&FixZCoordinate, ue));
        }
        Vector pos = mobility->GetPosition();
        pos.z = 1.5;
        mobility->SetPosition(pos);
    }
}

/**
 * Helper to force the Z coordinate = 1.5 when the node moves.
 */
void
FixZCoordinate(Ptr<Node> node)
{
    Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
    Vector pos = mobility->GetPosition();
    pos.z = 1.5;
    mobility->SetPosition(pos);
}

/**
 * Create a P2P link between PGW and RemoteHost, assign IP, and return RemoteHost IP address.
 */
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

    // PCAP traces
    p2p.EnablePcap("pgw-p2p", devices.Get(0), true);
    p2p.EnablePcap("remote-host-p2p", devices.Get(1), true);

    return interfaces.GetAddress(1); // Remote host IP
}

/**
 * Install a G.711-like VoIP traffic (OnOff + PacketSink) from each UE to the RemoteHost.
 */
void
InstallVoipApplications(NodeContainer& ueNodes,
                        Ipv4Address remoteAddr,
                        double simTime,
                        NodeContainer& remoteHostContainer)
{
    // G.711 ~ 64 kbps => 160-byte frames every ~20 ms
    uint64_t voiceBitrateBps = 64000; // 64 kbps
    uint32_t packetSizeBytes = 160;
    uint16_t basePort = 5000;

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        uint16_t port = basePort + i; // Unique port per UE

        // OnOff -> sending voice data
        OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(remoteAddr, port));
        onOff.SetAttribute("DataRate", DataRateValue(DataRate(voiceBitrateBps)));
        onOff.SetAttribute("PacketSize", UintegerValue(packetSizeBytes));
        // Always ON
        onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        // Install the "OnOff" traffic generator on the UE
        ApplicationContainer apps = onOff.Install(ueNodes.Get(i));
        apps.Start(Seconds(1.0));
        apps.Stop(Seconds(simTime));

        // PacketSink on remote host to receive voice
        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = sinkHelper.Install(remoteHostContainer.Get(0));
        sinkApps.Start(Seconds(0.5));
        sinkApps.Stop(Seconds(simTime));

        NS_LOG_INFO("Installed VoIP application on UE " << i << " with port " << port);
    }
}

/**
 * Setup FlowMonitor on all nodes.
 */
Ptr<FlowMonitor>
SetupFlowMonitor(FlowMonitorHelper& flowHelper)
{
    return flowHelper.InstallAll();
}

/**
 * Enable standard LTE PHY, MAC, RLC, PDCP traces.
 */
void
EnableLteTraces(Ptr<LteHelper> lteHelper)
{
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();
    lteHelper->EnableRlcTraces();
    lteHelper->EnablePdcpTraces();
}

/**
 * Periodically check throughput/latency for each flow and log results.
 */
void
PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor,
                    FlowMonitorHelper& flowHelper,
                    const SimulationParameters params)
{
    g_currentTime += params.statsInterval;
    flowMonitor->CheckForLostPackets();
    auto stats = flowMonitor->GetFlowStats();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    // Initialize per-UE throughput and packet loss for this interval
    std::vector<double> ueThroughputKbps(params.numUe, 0.0);
    std::vector<double> uePacketLossRate(params.numUe, 0.0); // Packet loss rate per UE

    double totalLatencySum = 0.0;
    uint64_t totalRxPackets = 0;

    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);

        // Focus only on UDP flows (VoIP)
        if (t.protocol != 17)
            continue;

        // Identify which UE this flow belongs to based on destination port
        uint16_t destPort = t.destinationPort;
        if (destPort >= 5000 && destPort < 5000 + params.numUe)
        {
            uint32_t ueIndex = destPort - 5000;

            // Throughput calculation
            uint64_t deltaBytes = iter.second.rxBytes - g_previousRxBytes[iter.first];
            g_previousRxBytes[iter.first] = iter.second.rxBytes;

            Time deltaDelaySum = iter.second.delaySum - g_previousDelaySum[iter.first];
            g_previousDelaySum[iter.first] = iter.second.delaySum;

            uint64_t deltaPackets = iter.second.rxPackets - g_previousRxPackets[iter.first];
            g_previousRxPackets[iter.first] = iter.second.rxPackets;

            double flowThroughputKbps =
                (deltaBytes * 8.0) / 1000.0 / params.statsInterval; // bits->Kbits
            ueThroughputKbps[ueIndex] += flowThroughputKbps;

            // Packet Loss Calculation
            uint64_t txPackets = iter.second.txPackets;
            uint64_t rxPackets = iter.second.rxPackets;
            double lossRate =
                (txPackets > 0) ? ((double)(txPackets - rxPackets) / txPackets * 100.0) : 0.0;
            uePacketLossRate[ueIndex] = lossRate; // Assuming one flow per UE

            // Latency
            if (deltaPackets > 0)
            {
                double avgFlowLatencyMs = (deltaDelaySum.GetSeconds() / deltaPackets) * 1000.0;
                totalLatencySum += (avgFlowLatencyMs * deltaPackets);
                totalRxPackets += deltaPackets;
            }
        }
        // Else, it's an unintended flow; ignore without logging a warning
    }

    // Aggregate stats
    double aggregateThroughputKbps = 0.0;
    for (uint32_t i = 0; i < params.numUe; i++)
    {
        aggregateThroughputKbps += ueThroughputKbps[i];
    }

    double avgLatency = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;

    // Store time-series data
    g_timeSeries.push_back(g_currentTime);
    g_avgLatencySeries.push_back(avgLatency);
    for (uint32_t i = 0; i < params.numUe; i++)
    {
        g_ueThroughputSeries[i].push_back(ueThroughputKbps[i]);
        g_uePacketLossSeries[i].push_back(uePacketLossRate[i]);
    }

    // Log to console
    std::ostringstream ueThroughputLog;
    ueThroughputLog << "Time: " << g_currentTime
                    << "s, Aggregate Throughput: " << aggregateThroughputKbps
                    << " Kbps, Avg Latency: " << avgLatency << " ms";
    for (uint32_t i = 0; i < params.numUe; i++)
    {
        ueThroughputLog << ", UE" << i << " Throughput: " << ueThroughputKbps[i] << " Kbps";
    }
    NS_LOG_INFO(ueThroughputLog.str());

    // Schedule next update
    if (g_currentTime + params.statsInterval <= params.simTime)
    {
        Simulator::Schedule(Seconds(params.statsInterval),
                            &PeriodicStatsUpdate,
                            flowMonitor,
                            std::ref(flowHelper),
                            params);
    }
}

/**
 * Periodically log the position of each node (useful for mobility debugging).
 */
void
LogAllNodePositions()
{
    NS_LOG_INFO("----- Node Positions at " << Simulator::Now().GetSeconds() << "s -----");
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> node = NodeList::GetNode(i);
        Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
        if (mob)
        {
            Vector pos = mob->GetPosition();
            NS_LOG_INFO("Node " << i << " Position: (" << pos.x << ", " << pos.y << ", " << pos.z
                                << ")");
        }
        else
        {
            NS_LOG_WARN("Node " << i << " has no MobilityModel!");
        }
    }
    NS_LOG_INFO("-----------------------------------------------------");
    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);
}

/**
 * Analyze final flow monitor stats: throughput, latency, packet loss.
 * Generate Gnuplot throughput + latency plots and a Markdown summary.
 */
void
AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper,
                   Ptr<FlowMonitor> flowMonitor,
                   const SimulationParameters params)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = flowMonitor->GetFlowStats();

    double totalThroughputSum = 0.0;
    double totalLatencySum = 0.0;
    uint64_t totalRxPackets = 0;
    uint64_t totalTxPackets = 0;
    uint32_t flowCount = 0;

    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        // Focus only on UDP flows (VoIP)
        if (t.protocol != 17)
            continue;

        flowCount++;
        double duration =
            (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
        if (duration > 0)
        {
            double throughputKbps = (iter.second.rxBytes * 8.0) / 1000.0 / duration;
            totalThroughputSum += throughputKbps;
        }

        if (iter.second.rxPackets > 0)
        {
            double avgFlowLatencyMs =
                (iter.second.delaySum.GetSeconds() / iter.second.rxPackets) * 1000.0;
            totalLatencySum += (avgFlowLatencyMs * iter.second.rxPackets);
            totalRxPackets += iter.second.rxPackets;
        }
        totalTxPackets += iter.second.txPackets;
    }

    double overallAvgLatency = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
    double overallAvgThroughput = (flowCount > 0) ? (totalThroughputSum / flowCount) : 0.0;
    double packetLossRate = 0.0;
    if (totalTxPackets > 0)
    {
        packetLossRate = (double)(totalTxPackets - totalRxPackets) / totalTxPackets * 100.0;
    }

    // --------------------------
    // 1) Generate Gnuplot: Throughput
    // --------------------------
    Gnuplot plotThroughput;
    plotThroughput.SetTitle("Per-UE Throughput Over Time");
    plotThroughput.SetTerminal("png size 800,600");
    plotThroughput.SetOutputFilename("ue-throughput-time-series.png");
    plotThroughput.SetLegend("Time (s)", "Throughput (Kbps)");

    // For each UE, create a dataset
    for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
    {
        std::ostringstream dsName;
        dsName << "UE-" << ueIndex;
        Gnuplot2dDataset dsT;
        dsT.SetTitle(dsName.str());
        dsT.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        // Fill the dataset from g_timeSeries[] and g_ueThroughputSeries[ueIndex][]
        size_t steps = std::min(g_timeSeries.size(), g_ueThroughputSeries[ueIndex].size());
        for (size_t i = 0; i < steps; i++)
        {
            double t = g_timeSeries[i];
            double thr = g_ueThroughputSeries[ueIndex][i];
            dsT.Add(t, thr);
        }
        plotThroughput.AddDataset(dsT);
    }

    // Write out the Gnuplot script
    {
        std::ofstream fileT("ue-throughput-time-series.plt");
        fileT << "set terminal png size 800,600\n";
        fileT << "set output 'ue-throughput-time-series.png'\n";
        fileT << "set title 'Per-UE Throughput Over Time'\n";
        fileT << "set xlabel 'Time (s)'\n";
        fileT << "set ylabel 'Throughput (Kbps)'\n";
        fileT << "set key left top\n";
        fileT << "plot ";
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            fileT << "'-' with linespoints title 'UE-" << ueIndex << "'";
            if (ueIndex != params.numUe - 1)
                fileT << ", ";
        }
        fileT << "\n";

        // Write data for each UE
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            for (size_t i = 0; i < g_timeSeries.size(); i++)
            {
                if (i < g_ueThroughputSeries[ueIndex].size())
                {
                    fileT << g_timeSeries[i] << " " << g_ueThroughputSeries[ueIndex][i] << "\n";
                }
                else
                {
                    fileT << g_timeSeries[i] << " " << 0.0 << "\n";
                }
            }
            fileT << "e\n";
        }

        fileT.close();
        NS_LOG_INFO("UE Throughput Gnuplot script: ue-throughput-time-series.plt");
        NS_LOG_INFO("Run `gnuplot ue-throughput-time-series.plt` to generate "
                    "ue-throughput-time-series.png");
    }

    // --------------------------
    // 2) Generate Gnuplot: Latency
    // --------------------------
    Gnuplot plotLatency;
    plotLatency.SetTitle("Aggregate Latency Over Time");
    plotLatency.SetTerminal("png size 800,600");
    plotLatency.SetOutputFilename("latency-time-series.png");
    plotLatency.SetLegend("Time (s)", "Latency (ms)");

    // Aggregate latency dataset
    Gnuplot2dDataset dsL;
    dsL.SetTitle("Avg Latency (all flows)");
    dsL.SetStyle(Gnuplot2dDataset::LINES_POINTS);

    for (size_t i = 0; i < g_timeSeries.size(); ++i)
    {
        double t = g_timeSeries[i];
        double lat = g_avgLatencySeries[i];
        dsL.Add(t, lat);
    }
    plotLatency.AddDataset(dsL);

    // Write out the Gnuplot script
    {
        std::ofstream fileL("latency-time-series.plt");
        fileL << "set terminal png size 800,600\n";
        fileL << "set output 'latency-time-series.png'\n";
        fileL << "set title 'Aggregate Latency Over Time'\n";
        fileL << "set xlabel 'Time (s)'\n";
        fileL << "set ylabel 'Latency (ms)'\n";
        fileL << "set key left top\n";
        fileL << "plot '-' with linespoints title 'Avg Latency'\n";

        // Write data
        for (size_t i = 0; i < g_timeSeries.size(); ++i)
        {
            double t = g_timeSeries[i];
            double lat = g_avgLatencySeries[i];
            fileL << t << " " << lat << "\n";
        }
        fileL << "e\n";

        fileL.close();
        NS_LOG_INFO("Latency Gnuplot script: latency-time-series.plt");
        NS_LOG_INFO("Run `gnuplot latency-time-series.plt` to generate latency-time-series.png");
    }

    // --------------------------
    // 3) Final Logs
    // --------------------------
    NS_LOG_INFO("===== FINAL METRICS =====");
    NS_LOG_INFO("Avg Throughput (Kbps) : " << overallAvgThroughput);
    NS_LOG_INFO("Avg Latency    (ms)  : " << overallAvgLatency);
    NS_LOG_INFO("Packet Loss    (%)   : " << packetLossRate);

    // Save FlowMonitor results
    flowMonitor->SerializeToXmlFile("flowmon.xml", true, true);
    NS_LOG_INFO("FlowMonitor results in flowmon.xml.");

    // --------------------------
    // 4) Markdown summary
    // --------------------------
    std::ofstream mdReport("simulation-report.md");
    if (!mdReport.is_open())
    {
        NS_LOG_ERROR("Failed to open simulation-report.md for writing.");
    }
    else
    {
        mdReport << "# Simulation Report\n\n";
        mdReport << "**Simulation Time**: " << params.simTime << "s\n\n";
        mdReport << "## Final Metrics\n";
        mdReport << "- **Avg Throughput**: " << overallAvgThroughput << " Kbps\n";
        mdReport << "- **Avg Latency**   : " << overallAvgLatency << " ms\n";
        mdReport << "- **Packet Loss**   : " << packetLossRate << "%\n\n";
        mdReport << "## Generated Plots\n";
        mdReport << "- `ue-throughput-time-series.png`\n";
        mdReport << "- `latency-time-series.png`\n\n";
        mdReport << "## FlowMonitor Results\n";
        mdReport << "Stored in `flowmon.xml`.\n";
        mdReport.close();
        NS_LOG_INFO("Markdown report: simulation-report.md");
    }

    // --------------------------
    // 5) CSV Export of Time-Series Data
    // --------------------------
    std::ofstream csvFile("simulation_metrics.csv");
    if (!csvFile.is_open())
    {
        NS_LOG_ERROR("Failed to open simulation_metrics.csv for writing.");
        return;
    }

    // CSV Header row
    csvFile << "Time(s)";
    for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
    {
        csvFile << ",UE" << ueIndex << "_Throughput(Kbps)";
    }
    csvFile << ",Avg_Latency(ms)";
    for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
    {
        csvFile << ",UE" << ueIndex << "_PacketLoss(%)";
    }
    csvFile << "\n";

    // Write data rows
    size_t numEntries = g_timeSeries.size();
    for (size_t i = 0; i < numEntries; ++i)
    {
        double t = g_timeSeries[i];
        csvFile << t;

        // Throughput
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double thr = 0.0;
            if (i < g_ueThroughputSeries[ueIndex].size())
            {
                thr = g_ueThroughputSeries[ueIndex][i];
            }
            csvFile << "," << thr;
        }

        // Latency
        double lat = 0.0;
        if (i < g_avgLatencySeries.size())
        {
            lat = g_avgLatencySeries[i];
        }
        csvFile << "," << lat;

        // Packet Loss
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double loss = 0.0;
            if (i < g_uePacketLossSeries[ueIndex].size())
            {
                loss = g_uePacketLossSeries[ueIndex][i];
            }
            csvFile << "," << loss;
        }

        csvFile << "\n";
    }

    csvFile.close();
    NS_LOG_INFO("Simulation metrics exported to simulation_metrics.csv.");
}
