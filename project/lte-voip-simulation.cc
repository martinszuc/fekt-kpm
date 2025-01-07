/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file enhanced-lte-voip-simulation-enhanced.cc
 * @brief Enhanced LTE + VoIP simulation in ns-3 LENA 3.39 with multiple eNodeBs and UEs,
 *        measuring throughput, latency, packet loss, and jitter. Enhanced animation with
 *        color-coded eNodeBs and UEs, and labels indicating connections.
 *
 * @authors
 *   Martin Szuc <matoszuc@gmail.com>
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
 * @struct SimulationParameters
 * @brief Holds various simulation parameters for easy configuration.
 */
struct SimulationParameters
{
    // Network Configuration
    uint16_t numEnb = 2;     ///< Number of eNodeBs
    uint16_t numUe = 5;      ///< Number of UEs
    double simTime = 30.0;   ///< Simulation time in seconds
    double areaSize = 200.0; ///< Size of the simulation area (square in meters)

    // Animation and Monitoring
    bool enableNetAnim = true;  ///< Enable NetAnim output
    double statsInterval = 1.0; ///< Interval for statistics collection in seconds

    // Path Loss Model Parameters (Suburban)
    double distance0 = 20.0; ///< First distance threshold in meters
    double distance1 = 50.0; ///< Second distance threshold in meters
    double exponent0 = 1.5;  ///< Path loss exponent before distance0
    double exponent1 = 2.0;  ///< Path loss exponent between distance0 and distance1
    double exponent2 = 3.0;  ///< Path loss exponent beyond distance1

    // VoIP Codec Parameters
    struct VoipCodec
    {
        std::string name;     ///< Codec name
        double bitrate;       ///< Bitrate in kbps
        uint32_t packetSize;  ///< Packet size in bytes
    } codec;

    /**
     * @brief Initializes default VoIP codec parameters.
     */
    SimulationParameters()
    {
        // Default Codec: G.711
        codec.name = "G.711";
        codec.bitrate = 64.0;       // kbps
        codec.packetSize = 80;      // bytes
        // frameInterval = 10.0; // ms

        // Uncomment below to use different codecs
        /*
        // G.722.2
        codec.name = "G.722.2";
        codec.bitrate = 25.84;
        codec.packetSize = 60;
        // frameInterval = 20.0;

        // G.723.1
        codec.name = "G.723.1";
        codec.bitrate = 6.3;
        codec.packetSize = 24;
        // frameInterval = 30.0;

        // G.729
        codec.name = "G.729";
        codec.bitrate = 8.0;
        codec.packetSize = 10;
        // frameInterval = 10.0;
        */
    }
};

// Global Variables for Time-Series Data
static double g_currentTime = 0.0;
std::vector<double> g_timeSeries;
std::vector<std::vector<double>> g_ueThroughputSeries;
std::vector<std::vector<double>> g_uePacketLossSeries;
std::vector<std::vector<double>> g_ueJitterSeries;
std::vector<double> g_avgLatencySeries;

// Flow Statistics Tracking
std::map<FlowId, uint64_t> g_previousRxBytes;
std::map<FlowId, Time> g_previousDelaySum;
std::map<FlowId, Time> g_previousJitterSum;
std::map<FlowId, uint64_t> g_previousRxPackets;
std::map<FlowId, uint32_t> g_flowIdToUeIndex;

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
                             NodeContainer& remoteHostContainer,
                             const SimulationParameters& params);
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper& flowHelper);
void EnableLteTraces(Ptr<LteHelper> lteHelper);
void PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor,
                         FlowMonitorHelper& flowHelper,
                         const SimulationParameters& params);
void LogAllNodePositions();
void FixZCoordinate(Ptr<Node> node);
void AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper,
                        Ptr<FlowMonitor> flowMonitor,
                        const SimulationParameters& params);

// ============================================================================
// MAIN
// ============================================================================
int
main(int argc, char* argv[])
{
    // Initialize simulation parameters
    SimulationParameters params;
    CommandLine cmd;

    // VoIP Codec Configuration
    // Codec Name | Bit rate (kbps) | Size of Packet (bytes) | Frame Interval (ms)
    // G.711      | 64              | 80                      | 10
    // G.722.2    | 25.84           | 60                      | 20
    // G.723.1    | 6.3             | 24                      | 30
    // G.729      | 8               | 10                      | 10

    // Command-line overrides
    cmd.AddValue("numEnb", "Number of eNodeBs", params.numEnb);
    cmd.AddValue("numUe", "Number of UEs", params.numUe);
    cmd.AddValue("simTime", "Simulation time (s)", params.simTime);
    cmd.AddValue("areaSize", "Square area side [m]", params.areaSize);
    cmd.AddValue("enableNetAnim", "Enable NetAnim output", params.enableNetAnim);
    cmd.AddValue("distance0", "PathLoss Distance0 (m)", params.distance0);
    cmd.AddValue("distance1", "PathLoss Distance1 (m)", params.distance1);
    cmd.AddValue("exponent0", "PathLoss Exponent0", params.exponent0);
    cmd.AddValue("exponent1", "PathLoss Exponent1", params.exponent1);
    cmd.AddValue("exponent2", "PathLoss Exponent2", params.exponent2);
    cmd.Parse(argc, argv);

    // Initialize data vectors for UEs
    g_ueThroughputSeries.resize(params.numUe, std::vector<double>());
    g_uePacketLossSeries.resize(params.numUe, std::vector<double>());
    g_ueJitterSeries.resize(params.numUe, std::vector<double>());

    // Enable logging
    ConfigureLogging();

    // Create nodes
    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(params.numEnb);
    ueNodes.Create(params.numUe);
    remoteHostContainer.Create(1); // Remote Host

    // LTE and EPC Helpers
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Configure Path Loss Model
    lteHelper->SetPathlossModelType(
        TypeId::LookupByName("ns3::ThreeLogDistancePropagationLossModel"));
    lteHelper->SetPathlossModelAttribute("Distance0", DoubleValue(params.distance0));
    lteHelper->SetPathlossModelAttribute("Distance1", DoubleValue(params.distance1));
    lteHelper->SetPathlossModelAttribute("Exponent0", DoubleValue(params.exponent0));
    lteHelper->SetPathlossModelAttribute("Exponent1", DoubleValue(params.exponent1));
    lteHelper->SetPathlossModelAttribute("Exponent2", DoubleValue(params.exponent2));

    // LTE Scheduler Configuration
    lteHelper->SetSchedulerType("ns3::RrFfMacScheduler");

    // Handover Configuration
    lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
    lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(120)));

    // Configure Mobility
    ConfigureEnbMobility(enbNodes, params.areaSize);
    ConfigureUeMobility(ueNodes, params.areaSize);

    // Position Remote Host and PGW
    MobilityHelper remoteMobility;
    remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    remoteMobility.Install(remoteHostContainer.Get(0));
    remoteHostContainer.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(params.areaSize / 2, params.areaSize / 2, 1.5));

    remoteMobility.Install(epcHelper->GetPgwNode());
    epcHelper->GetPgwNode()->GetObject<MobilityModel>()->SetPosition(
        Vector(params.areaSize / 2, params.areaSize / 2, 1.5));

    // Install LTE Devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Install Internet Stack
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(remoteHostContainer);
    internet.Install(epcHelper->GetPgwNode());

    // Assign IP Addresses to UEs
    epcHelper->AssignUeIpv4Address(ueDevs);

    // Attach UEs to the Nearest eNodeB
    std::vector<Ptr<MobilityModel>> enbMobilityModel(params.numEnb);
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
            double dist = CalculateDistance(uePos, enbPos);
            if (dist < minDistance)
            {
                minDistance = dist;
                closestEnb = j;
            }
        }
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(closestEnb));

        // Fix Z-coordinate for UEs
        Vector pos = ueMobility->GetPosition();
        pos.z = 1.5;
        ueMobility->SetPosition(pos);
    }

    // Create Remote Host Link
    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer, params.areaSize);

    // Install VoIP Applications
    InstallVoipApplications(ueNodes, remoteHostAddr, params.simTime, remoteHostContainer, params);

    // Set Default Routes for UEs
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
        Ptr<Ipv4> ipv4 = ueNodes.Get(u)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ipv4);
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
        NS_LOG_INFO("UE " << u << " default route set to "
                          << epcHelper->GetUeDefaultGatewayAddress());
    }

    // Enable LTE Traces
    EnableLteTraces(lteHelper);

    // Setup FlowMonitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

    // Map Flows to UEs and Initialize Statistics
    Simulator::Schedule(Seconds(0.0), [&flowMonitor, &flowHelper, &params]() {
        auto initialStats = flowMonitor->GetFlowStats();
        Ptr<Ipv4FlowClassifier> classifier =
            DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
        for (auto it = initialStats.begin(); it != initialStats.end(); ++it)
        {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it->first);
            uint16_t destPort = t.destinationPort;
            if (destPort >= 5000 && destPort < (5000 + params.numUe))
            {
                uint32_t ueIndex = destPort - 5000;
                g_flowIdToUeIndex[it->first] = ueIndex;
                NS_LOG_INFO("Flow " << it->first << " mapped to UE " << ueIndex
                                    << " (src=" << t.sourceAddress
                                    << ", dst=" << t.destinationAddress << ":" << destPort << ")");

                // Initialize statistics
                g_previousRxBytes[it->first] = 0;
                g_previousDelaySum[it->first] = Seconds(0);
                g_previousJitterSum[it->first] = Seconds(0);
                g_previousRxPackets[it->first] = 0;
            }
            else
            {
                NS_LOG_WARN("Flow " << it->first
                                    << " has unexpected destination port: " << destPort);
            }
        }
    });

    // Initialize NetAnim
    AnimationInterface* anim = nullptr;
    if (params.enableNetAnim)
    {
        anim = new AnimationInterface("animation.xml");
        anim->SetMaxPktsPerTraceFile(5000000);

        // Configure eNodeBs in NetAnim
        for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
        {
            Ptr<Node> enbNode = enbNodes.Get(i);
            Vector pos = enbNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(enbNode, "eNodeB_" + std::to_string(i));
            anim->UpdateNodeColor(enbNode, 0, 0, 255); // Blue
            anim->SetConstantPosition(enbNode, pos.x, pos.y);
        }

        // Configure UEs in NetAnim
        for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        {
            Ptr<Node> ueNode = ueNodes.Get(i);
            Vector pos = ueNode->GetObject<MobilityModel>()->GetPosition();
            anim->UpdateNodeDescription(ueNode, "UE_" + std::to_string(i));
            anim->UpdateNodeColor(ueNode, 0, 255, 0); // Green
            anim->SetConstantPosition(ueNode, pos.x, pos.y);
        }

        // Configure Remote Host in NetAnim
        anim->UpdateNodeDescription(remoteHostContainer.Get(0), "RemoteHost");
        anim->UpdateNodeColor(remoteHostContainer.Get(0), 255, 0, 0); // Red
    }

    // Schedule Periodic Logging of Node Positions (for Debugging)
    Simulator::Schedule(Seconds(1.0), &LogAllNodePositions);

    // Schedule Periodic Statistics Updates
    Simulator::Schedule(Seconds(params.statsInterval),
                        &PeriodicStatsUpdate,
                        flowMonitor,
                        std::ref(flowHelper),
                        params);

    // Run Simulation
    Simulator::Stop(Seconds(params.simTime));
    Simulator::Run();

    // Final Analysis of Flow Monitor Data
    AnalyzeFlowMonitor(flowHelper, flowMonitor, params);

    // Clean Up
    Simulator::Destroy();
    NS_LOG_INFO("Enhanced LTE simulation finished!");
    return 0;
}

// ============================================================================
// FUNCTION DEFINITIONS
// ============================================================================

/**
 * @brief Configures the logging levels for various components.
 */
void
ConfigureLogging()
{
    LogComponentEnable("EnhancedLteVoipSimulationFixed", LOG_LEVEL_INFO);
    // Uncomment below for verbose logs from specific applications:
    // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
}

/**
 * @brief Configures the mobility model for eNodeBs, placing them strategically based on the number of eNodeBs.
 *        - If 4 eNodeBs: Position them at four corners of the simulation area.
 *        - If 2 eNodeBs: Align them centrally along the Y-axis, maintaining X-axis positions from the 4 eNodeB setup.
 * @param enbNodes Container of eNodeB nodes.
 * @param areaSize Size of the simulation area.
 */
void ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();

    if (enbNodes.GetN() == 4)
    {
        // Four eNodeBs: Default positioning (corners)
        posAlloc->Add(Vector(areaSize / 4, areaSize / 4, 30.0));        // Bottom-left
        posAlloc->Add(Vector(areaSize / 4, 3 * areaSize / 4, 30.0));    // Top-left
        posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 4, 30.0));    // Bottom-right
        posAlloc->Add(Vector(3 * areaSize / 4, 3 * areaSize / 4, 30.0)); // Top-right
    }
    else if (enbNodes.GetN() == 2)
    {
        // Two eNodeBs: Centered along the Y-axis
        posAlloc->Add(Vector(areaSize / 4, areaSize / 2, 30.0));        // Center-left
        posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 2, 30.0));    // Center-right
    }
    else
    {
        NS_LOG_WARN("Unsupported number of eNodeBs: " << enbNodes.GetN());
    }

    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}


/**
 * @brief Configures the mobility model for UEs using Random Waypoint Mobility.
 * @param ueNodes Container of UE nodes.
 * @param areaSize Size of the simulation area.
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

    // Ensure UEs remain at a fixed Z-coordinate
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<Node> ue = ueNodes.Get(i);
        Ptr<MobilityModel> mobility = ue->GetObject<MobilityModel>();
        Ptr<RandomWaypointMobilityModel> rwm = DynamicCast<RandomWaypointMobilityModel>(mobility);

        if (rwm)
        {
            // Fix Z-coordinate on every new position
            rwm->TraceConnectWithoutContext("NewPosition", MakeBoundCallback(&FixZCoordinate, ue));
        }

        // Immediately fix Z-coordinate
        Vector pos = mobility->GetPosition();
        pos.z = 1.5;
        mobility->SetPosition(pos);
    }
}

/**
 * @brief Ensures that a node maintains a fixed Z-coordinate.
 * @param node Pointer to the node.
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
 * @brief Creates a remote host connected to the PGW via a Point-to-Point link.
 * @param epcHelper EPC helper.
 * @param remoteHostContainer Container holding the remote host node.
 * @param areaSize Size of the simulation area.
 * @return Ipv4Address of the remote host.
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

    // Enable PCAP tracing
    p2p.EnablePcap("pgw-p2p", devices.Get(0), true);
    p2p.EnablePcap("remote-host-p2p", devices.Get(1), true);

    return interfaces.GetAddress(1); // Remote Host IP
}

/**
 * @brief Installs VoIP applications on UEs and the remote host.
 * @param ueNodes Container of UE nodes.
 * @param remoteAddr IP address of the remote host.
 * @param simTime Total simulation time.
 * @param remoteHostContainer Container holding the remote host node.
 * @param params Simulation parameters including VoIP codec configurations.
 */
void
InstallVoipApplications(NodeContainer& ueNodes,
                        Ipv4Address remoteAddr,
                        double simTime,
                        NodeContainer& remoteHostContainer,
                        const SimulationParameters& params)
{
    uint16_t basePort = 5000;

    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        uint16_t port = basePort + i;

        // Configure OnOff application to simulate VoIP traffic
        OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(remoteAddr, port));
        onOff.SetAttribute("DataRate", DataRateValue(DataRate(params.codec.bitrate * 1000)));
        onOff.SetAttribute("PacketSize", UintegerValue(params.codec.packetSize));
        onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        // Install OnOff application on UE
        ApplicationContainer apps = onOff.Install(ueNodes.Get(i));
        apps.Start(Seconds(1.0));
        apps.Stop(Seconds(simTime));

        // Install PacketSink on Remote Host
        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = sinkHelper.Install(remoteHostContainer.Get(0));
        sinkApps.Start(Seconds(0.5));
        sinkApps.Stop(Seconds(simTime));

        NS_LOG_INFO("Installed VoIP application on UE " << i << " with port " << port);
    }
}

/**
 * @brief Sets up FlowMonitor to track flow statistics.
 * @param flowHelper FlowMonitorHelper instance.
 * @return Pointer to the installed FlowMonitor.
 */
Ptr<FlowMonitor>
SetupFlowMonitor(FlowMonitorHelper& flowHelper)
{
    return flowHelper.InstallAll();
}

/**
 * @brief Enables LTE tracing for PHY, MAC, RLC, and PDCP layers.
 * @param lteHelper LTE helper instance.
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
 * @brief Periodically updates and logs network statistics.
 * @param flowMonitor FlowMonitor instance.
 * @param flowHelper FlowMonitorHelper instance.
 * @param params Simulation parameters.
 */
void
PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor,
                    FlowMonitorHelper& flowHelper,
                    const SimulationParameters& params)
{
    g_currentTime += params.statsInterval;
    flowMonitor->CheckForLostPackets();

    auto stats = flowMonitor->GetFlowStats();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    // Initialize per-UE metrics
    std::vector<double> ueThroughputKbps(params.numUe, 0.0);
    std::vector<double> uePacketLossRate(params.numUe, 0.0);
    std::vector<double> ueJitterMs(params.numUe, 0.0);

    double totalLatencySum = 0.0;
    uint64_t totalRxPackets = 0;

    // Process each flow
    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);

        // Consider only UDP flows (VoIP)
        if (t.protocol != 17)
            continue;

        // Identify the UE based on destination port
        uint16_t destPort = t.destinationPort;
        if (destPort >= 5000 && destPort < 5000 + params.numUe)
        {
            uint32_t ueIndex = destPort - 5000;

            // Calculate throughput
            uint64_t currentRxBytes = iter.second.rxBytes;
            uint64_t deltaBytes = currentRxBytes - g_previousRxBytes[iter.first];
            g_previousRxBytes[iter.first] = currentRxBytes;

            double flowThroughputKbps = (deltaBytes * 8.0) / 1000.0 / params.statsInterval;
            ueThroughputKbps[ueIndex] += flowThroughputKbps;

            // Calculate packet loss rate
            uint64_t txPkts = iter.second.txPackets;
            uint64_t rxPkts = iter.second.rxPackets;
            double lossRate =
                (txPkts > 0) ? (double)(txPkts - rxPkts) / (double)txPkts * 100.0 : 0.0;
            uePacketLossRate[ueIndex] = lossRate;

            // Calculate latency
            uint64_t currentRxPackets = iter.second.rxPackets;
            uint64_t deltaPackets = currentRxPackets - g_previousRxPackets[iter.first];
            g_previousRxPackets[iter.first] = currentRxPackets;

            Time currentDelaySum = iter.second.delaySum;
            Time deltaDelaySum = currentDelaySum - g_previousDelaySum[iter.first];
            g_previousDelaySum[iter.first] = currentDelaySum;

            if (deltaPackets > 0)
            {
                double avgFlowLatencyMs = (deltaDelaySum.GetSeconds() / deltaPackets) * 1000.0;
                totalLatencySum += (avgFlowLatencyMs * deltaPackets);
                totalRxPackets += deltaPackets;
            }

            // Calculate jitter
            Time currentJitterSum = iter.second.jitterSum;
            Time deltaJitterSum = currentJitterSum - g_previousJitterSum[iter.first];
            g_previousJitterSum[iter.first] = currentJitterSum;

            double flowJitterMs = 0.0;
            if (deltaPackets > 1)
            {
                flowJitterMs = (deltaJitterSum.GetSeconds() / (double)(deltaPackets - 1)) * 1000.0;
            }
            ueJitterMs[ueIndex] += flowJitterMs;
        }
    }

    // Aggregate metrics
    double aggregateThroughputKbps = 0.0;
    for (uint32_t i = 0; i < params.numUe; i++)
    {
        aggregateThroughputKbps += ueThroughputKbps[i];
    }

    double avgLatencyMs = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;

    // Store time-series data
    g_timeSeries.push_back(g_currentTime);
    g_avgLatencySeries.push_back(avgLatencyMs);

    for (uint32_t i = 0; i < params.numUe; i++)
    {
        g_ueThroughputSeries[i].push_back(ueThroughputKbps[i]);
        g_uePacketLossSeries[i].push_back(uePacketLossRate[i]);
        g_ueJitterSeries[i].push_back(ueJitterMs[i]);
    }

    // Log current statistics
    std::ostringstream oss;
    oss << "Time: " << g_currentTime << "s, "
        << "Aggregate Throughput: " << aggregateThroughputKbps << " Kbps, "
        << "Avg Latency: " << avgLatencyMs << " ms";
    for (uint32_t i = 0; i < params.numUe; i++)
    {
        oss << ", UE" << i << " Thr: " << ueThroughputKbps[i] << " Kbps";
    }
    NS_LOG_INFO(oss.str());

    // Schedule next statistics update
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
 * @brief Logs the positions of all nodes in the simulation (for debugging purposes).
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
 * @brief Analyzes the flow monitor data after simulation ends and generates reports.
 * @param flowHelper FlowMonitorHelper instance.
 * @param flowMonitor FlowMonitor instance.
 * @param params Simulation parameters.
 */
void
AnalyzeFlowMonitor(FlowMonitorHelper& flowHelper,
                   Ptr<FlowMonitor> flowMonitor,
                   const SimulationParameters& params)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = flowMonitor->GetFlowStats();

    double totalThroughputSum = 0.0;
    double totalLatencySum = 0.0;
    double totalJitterSum = 0.0;
    uint64_t totalRxPackets = 0;
    uint64_t totalRxPacketsForJitter = 0;
    uint64_t totalTxPackets = 0;
    uint32_t flowCount = 0;

    // Aggregate statistics across all flows
    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        if (t.protocol != 17) // Only UDP (VoIP)
            continue;

        flowCount++;
        double duration =
            (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
        if (duration > 0)
        {
            double throughputKbps = (iter.second.rxBytes * 8.0) / 1000.0 / duration;
            totalThroughputSum += throughputKbps;
        }

        uint64_t rxPkts = iter.second.rxPackets;
        if (rxPkts > 0)
        {
            // Latency
            double avgFlowLatencyMs = (iter.second.delaySum.GetSeconds() / (double)rxPkts) * 1000.0;
            totalLatencySum += avgFlowLatencyMs * rxPkts;
            totalRxPackets += rxPkts;

            // Jitter
            if (rxPkts > 1)
            {
                double meanFlowJitterMs =
                    (iter.second.jitterSum.GetSeconds() / (double)(rxPkts - 1)) * 1000.0;
                totalJitterSum += meanFlowJitterMs * (rxPkts - 1);
                totalRxPacketsForJitter += (rxPkts - 1);
            }
        }
        totalTxPackets += iter.second.txPackets;
    }

    double overallAvgLatencyMs =
        (totalRxPackets > 0) ? (totalLatencySum / (double)totalRxPackets) : 0.0;
    double overallAvgThroughput = (flowCount > 0) ? (totalThroughputSum / (double)flowCount) : 0.0;
    double packetLossRate = 0.0;
    if (totalTxPackets > 0)
    {
        packetLossRate = (double)(totalTxPackets - totalRxPackets) / (double)totalTxPackets * 100.0;
    }
    double overallAvgJitterMs = 0.0;
    if (totalRxPacketsForJitter > 0)
    {
        overallAvgJitterMs = (totalJitterSum / (double)totalRxPacketsForJitter);
    }

    // Generate Gnuplot for Per-UE Throughput
    Gnuplot plotThroughput;
    plotThroughput.SetTitle("Per-UE Throughput Over Time");
    plotThroughput.SetTerminal("png size 800,600");
    plotThroughput.SetOutputFilename("ue-throughput-time-series.png");
    plotThroughput.SetLegend("Time (s)", "Throughput (Kbps)");

    for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
    {
        Gnuplot2dDataset dsT;
        dsT.SetTitle("UE-" + std::to_string(ueIndex));
        dsT.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        size_t steps = std::min(g_timeSeries.size(), g_ueThroughputSeries[ueIndex].size());
        for (size_t i = 0; i < steps; i++)
        {
            dsT.Add(g_timeSeries[i], g_ueThroughputSeries[ueIndex][i]);
        }
        plotThroughput.AddDataset(dsT);
    }

    // Write Gnuplot Script for Throughput
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
                double thr = (i < g_ueThroughputSeries[ueIndex].size())
                                 ? g_ueThroughputSeries[ueIndex][i]
                                 : 0.0;
                fileT << g_timeSeries[i] << " " << thr << "\n";
            }
            fileT << "e\n";
        }
        fileT.close();

        NS_LOG_INFO("UE Throughput Gnuplot script: ue-throughput-time-series.plt");
        NS_LOG_INFO("Run `gnuplot ue-throughput-time-series.plt` to generate the PNG.");
    }

    // Generate Gnuplot for Aggregate Latency
    Gnuplot plotLatency;
    plotLatency.SetTitle("Aggregate Latency Over Time");
    plotLatency.SetTerminal("png size 800,600");
    plotLatency.SetOutputFilename("latency-time-series.png");
    plotLatency.SetLegend("Time (s)", "Latency (ms)");

    Gnuplot2dDataset dsL;
    dsL.SetTitle("Avg Latency (all flows)");
    dsL.SetStyle(Gnuplot2dDataset::LINES_POINTS);

    for (size_t i = 0; i < g_timeSeries.size(); ++i)
    {
        dsL.Add(g_timeSeries[i], g_avgLatencySeries[i]);
    }
    plotLatency.AddDataset(dsL);

    // Write Gnuplot Script for Latency
    {
        std::ofstream fileL("latency-time-series.plt");
        fileL << "set terminal png size 800,600\n";
        fileL << "set output 'latency-time-series.png'\n";
        fileL << "set title 'Aggregate Latency Over Time'\n";
        fileL << "set xlabel 'Time (s)'\n";
        fileL << "set ylabel 'Latency (ms)'\n";
        fileL << "set key left top\n";
        fileL << "plot '-' with linespoints title 'Avg Latency'\n";

        for (size_t i = 0; i < g_timeSeries.size(); ++i)
        {
            fileL << g_timeSeries[i] << " " << g_avgLatencySeries[i] << "\n";
        }
        fileL << "e\n";
        fileL.close();

        NS_LOG_INFO("Latency Gnuplot script: latency-time-series.plt");
        NS_LOG_INFO("Run `gnuplot latency-time-series.plt` to generate the PNG.");
    }

    // Final Metrics Logging
    NS_LOG_INFO("===== FINAL METRICS =====");
    NS_LOG_INFO("Avg Throughput (Kbps) : " << overallAvgThroughput);
    NS_LOG_INFO("Avg Latency (ms)     : " << overallAvgLatencyMs);
    NS_LOG_INFO("Packet Loss (%)      : " << packetLossRate);
    NS_LOG_INFO("Avg Jitter (ms)      : " << overallAvgJitterMs);

    // Serialize FlowMonitor Results
    flowMonitor->SerializeToXmlFile("flowmon.xml", true, true);
    NS_LOG_INFO("FlowMonitor results stored in flowmon.xml.");

    // Generate Markdown Report
    std::ofstream mdReport("simulation-report.md");
    if (!mdReport.is_open())
    {
        NS_LOG_ERROR("Failed to open simulation-report.md for writing.");
    }
    else
    {
        mdReport << "# Simulation Report\n\n";
        mdReport << "**Simulation Time**: " << params.simTime << " s\n\n";
        mdReport << "## Final Metrics\n";
        mdReport << "- **Avg Throughput**: " << overallAvgThroughput << " Kbps\n";
        mdReport << "- **Avg Latency**   : " << overallAvgLatencyMs << " ms\n";
        mdReport << "- **Packet Loss**   : " << packetLossRate << "%\n";
        mdReport << "- **Avg Jitter**    : " << overallAvgJitterMs << " ms\n\n";
        mdReport << "## Generated Plots\n";
        mdReport << "- `ue-throughput-time-series.png`\n";
        mdReport << "- `latency-time-series.png`\n\n";
        mdReport << "## FlowMonitor Results\n";
        mdReport << "Stored in `flowmon.xml`.\n";
        mdReport.close();

        NS_LOG_INFO("Markdown report generated: simulation-report.md");
    }

    // Export Time-Series Data to CSV
    std::ofstream csvFile("simulation_metrics.csv");
    if (!csvFile.is_open())
    {
        NS_LOG_ERROR("Failed to open simulation_metrics.csv for writing.");
        return;
    }

    // Write CSV Header
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
    for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
    {
        csvFile << ",UE" << ueIndex << "_Jitter(ms)";
    }
    csvFile << "\n";

    // Write CSV Data Rows
    size_t numEntries = g_timeSeries.size();
    for (size_t i = 0; i < numEntries; ++i)
    {
        csvFile << g_timeSeries[i];
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double thr =
                (i < g_ueThroughputSeries[ueIndex].size()) ? g_ueThroughputSeries[ueIndex][i] : 0.0;
            csvFile << "," << thr;
        }
        double lat = (i < g_avgLatencySeries.size()) ? g_avgLatencySeries[i] : 0.0;
        csvFile << "," << lat;
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double loss =
                (i < g_uePacketLossSeries[ueIndex].size()) ? g_uePacketLossSeries[ueIndex][i] : 0.0;
            csvFile << "," << loss;
        }
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double jit =
                (i < g_ueJitterSeries[ueIndex].size()) ? g_ueJitterSeries[ueIndex][i] : 0.0;
            csvFile << "," << jit;
        }
        csvFile << "\n";
    }

    csvFile.close();
    NS_LOG_INFO("Simulation metrics exported to simulation_metrics.csv.");
}
