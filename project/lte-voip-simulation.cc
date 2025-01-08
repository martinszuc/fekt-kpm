/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file lte-voip-simulation.cc
 * @brief LTE + VoIP simulation in ns-3 LENA 3.39 with multiple eNodeBs and UEs,
 *        measuring throughput, latency, packet loss, jitter, and handover events.
 *
 * Features:
 * - LTE + VoIP simulation using ns-3 LTE module (LENA).
 * - Multiple eNodeBs and UEs with configurable positions and mobility.
 * - VoIP traffic generated using OnOff applications with configurable codecs.
 * - Path loss modeled using ThreeLogDistancePropagationLossModel.
 * - Handover simulated using A3-RSRP algorithm with hysteresis and Time-To-Trigger.
 * - Per-UE metrics tracked: throughput, latency, packet loss, jitter.
 * - Handover metrics tracked: handover starts, successes, failures.
 * - Aggregated metrics tracked: average throughput, latency.
 * - Outputs:
 *   - Gnuplot graphs for throughput, latency, average throughput, RSRP, and RSRQ.
 *   - CSV export of metrics over time.
 *   - FlowMonitor XML output for detailed analysis.
 *   - NetAnim XML visualization.
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

#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("VoipLteSimulation");

/**
 * @struct SimulationParameters
 * @brief Holds various simulation parameters for easy configuration.
 */
struct SimulationParameters
{
    // Network Configuration
    uint16_t numEnb = 2;     ///< Number of eNodeBs
    uint16_t numUe = 5;      ///< Number of UEs
    double simTime = 20.0;   ///< Simulation time in seconds
    double areaSize = 500.0; ///< Size of the simulation area (square in meters)

    // Path Loss Model Parameters
    double distance0 = 30.0;  ///< First distance threshold in meters
    double distance1 = 50.0; ///< Second distance threshold in meters
    double exponent0 = 1.7;   ///< Path loss exponent before distance0
    double exponent1 = 2.5;   ///< Path loss exponent between distance0 and distance1
    double exponent2 = 3.2;   ///< Path loss exponent beyond distance1

    double static constexpr HANDOVER_HYSTERESIS = 3;
    double static constexpr HANDOVER_TimeToTrigger = 120; // ms

    // Animation and Monitoring
    bool enableNetAnim = true;  ///< Enable NetAnim output
    double statsInterval = 1.0; ///< Interval for statistics collection in seconds

    /**
     * @brief Enum for different mobility modes.
     */
    enum MobilityMode
    {
        RANDOM_WAYPOINT = 0,          ///< Random Waypoint Mobility Model
        CONSTANT_UNDER_DISTANCE0 = 1, ///< Constant Position within Distance0
        CONSTANT_UNDER_DISTANCE1 = 2, ///< Constant Position within Distance1
        CONSTANT_ABOVE_DISTANCE1 = 3  ///< Constant Position above Distance1
    } mobilityMode = RANDOM_WAYPOINT; ///< Selected Mobility Mode

    /**
     * @brief Initializes default VoIP codec parameters.
     */
    SimulationParameters()
    {
        // Uncomment and configure other codecs as needed

         codec.name = "G.711";
         codec.bitrate = 64.0;  // kbps
         codec.packetSize = 80; // bytes

        // // G.722.2
        // codec.name = "G.722.2";
        // codec.bitrate = 25.84;
        // codec.packetSize = 60;

        // // G.723.1
        // codec.name = "G.723.1";
        // codec.bitrate = 6.3;
        // codec.packetSize = 24;

        // // G.729
//        codec.name = "G.729";
//        codec.bitrate = 8.0;
//        codec.packetSize = 10;

    }

    // VoIP Codec Parameters
    struct VoipCodec
    {
        std::string name;    ///< Codec name
        double bitrate;      ///< Bitrate in kbps
        uint32_t packetSize; ///< Packet size in bytes
    } codec;
};

// Global Variables for Time-Plot Data
static double g_currentTime = 0.0;
std::vector<double> g_timePlot;
std::vector<std::vector<double>> g_ueThroughputPlot;
std::vector<std::vector<double>> g_uePacketLossPlot;
std::vector<std::vector<double>> g_ueJitterPlot;
std::vector<double> g_avgLatencyPlot;
std::vector<double> g_avgThroughputPlot;

// Flow Statistics Tracking
std::map<FlowId, uint64_t> g_previousRxBytes;
std::map<FlowId, Time> g_previousDelaySum;
std::map<FlowId, Time> g_previousJitterSum;
std::map<FlowId, uint64_t> g_previousRxPackets;
std::map<FlowId, uint32_t> g_flowIdToUeIndex;

// Handover Logging Variables
std::ofstream handoverLogFile;
uint32_t g_handoverStartCount = 0;
uint32_t g_handoverSuccessCount = 0;
uint32_t g_handoverFailureCount = 0;
std::vector<uint32_t> g_handoverStartPlot;
std::vector<uint32_t> g_handoverSuccessPlot;
std::vector<uint32_t> g_handoverFailurePlot;

// Function Prototypes
void ConfigureLogging();
void ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize);
void ConfigureUeMobility(NodeContainer& ueNodes,
                         double areaSize,
                         NodeContainer& enbNodes,
                         SimulationParameters::MobilityMode mobilityMode,
                         const SimulationParameters& params);
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
void AnalyzeData(FlowMonitorHelper& flowHelper,
                 Ptr<FlowMonitor> flowMonitor,
                 const SimulationParameters& params,
                 Ipv4Address remoteHostAddr,
                 const std::vector<Ipv4Address>& ueAddresses);

// Handover Callback Functions
void
HandoverStartCallback(uint64_t imsi, uint16_t cellId, uint16_t targetCellId, unsigned short reason)
{
    Time currentTime = Simulator::Now();
    g_handoverStartCount++;
    handoverLogFile << "Handover Start: IMSI=" << imsi << ", from Cell=" << cellId
                    << " to Cell=" << targetCellId << " Reason=" << reason
                    << " at Time=" << currentTime.GetMilliSeconds() << "ms" << std::endl;
}

void
HandoverSuccessCallback(uint64_t imsi,
                        uint16_t cellId,
                        uint16_t targetCellId,
                        unsigned short reason)
{
    Time currentTime = Simulator::Now();
    g_handoverSuccessCount++;
    handoverLogFile << "Handover Success: IMSI=" << imsi << ", to Cell=" << targetCellId
                    << " Reason=" << reason << " at Time=" << currentTime.GetMilliSeconds() << "ms"
                    << std::endl;
}

void
HandoverFailureCallback(uint64_t imsi,
                        uint16_t cellId,
                        uint16_t targetCellId,
                        unsigned short reason)
{
    Time currentTime = Simulator::Now();
    g_handoverFailureCount++;
    handoverLogFile << "Handover Failure: IMSI=" << imsi << ", from Cell=" << cellId
                    << " to Cell=" << targetCellId << " Reason=" << reason
                    << " at Time=" << currentTime.GetMilliSeconds() << "ms" << std::endl;
}

// ============================================================================
/**
 * @brief The main function that sets up and runs the simulation.
 */
int
main(int argc, char* argv[])
{
    // Initialize simulation parameters
    SimulationParameters params;

    // Initialize data vectors for UEs
    g_ueThroughputPlot.resize(params.numUe, std::vector<double>());
    g_uePacketLossPlot.resize(params.numUe, std::vector<double>());
    g_ueJitterPlot.resize(params.numUe, std::vector<double>());
    g_avgThroughputPlot.resize(0);

    // Enable logging
    ConfigureLogging();

    // Open the handover log file
    handoverLogFile.open("handover_events.log", std::ios::out | std::ios::trunc);
    if (!handoverLogFile.is_open())
    {
        NS_LOG_ERROR("Failed to open handover_events.log for writing.");
        return 1;
    }

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
    // lteHelper->SetSchedulerType("ns3::PfFfMacScheduler");
    // lteHelper->SetSchedulerType("ns3::TdBetFfMacScheduler");

    // Handover Configuration
    lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
    lteHelper->SetHandoverAlgorithmAttribute(
        "Hysteresis",
        DoubleValue(SimulationParameters::HANDOVER_HYSTERESIS));
    lteHelper->SetHandoverAlgorithmAttribute(
        "TimeToTrigger",
        TimeValue(MilliSeconds(SimulationParameters::HANDOVER_TimeToTrigger)));

    // Configure Mobility for eNodeBs
    ConfigureEnbMobility(enbNodes, params.areaSize);

    // Configure Mobility for UEs based on selected mobility mode
    ConfigureUeMobility(ueNodes, params.areaSize, enbNodes, params.mobilityMode, params);

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
    Ipv4InterfaceContainer ueInterfaces = epcHelper->AssignUeIpv4Address(ueDevs);
    std::vector<Ipv4Address> ueAddresses;
    for (uint32_t i = 0; i < ueInterfaces.GetN(); ++i)
    {
        ueAddresses.push_back(ueInterfaces.GetAddress(i));
        NS_LOG_INFO("UE " << i << " IP Address: " << ueAddresses.back());
    }

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
    Simulator::Schedule(Seconds(0.0), [&flowMonitor, &flowHelper, &params, &ueDevs]() {
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

    // Connect Handover Trace Sources to Callbacks
    for (uint32_t i = 0; i < enbDevs.GetN(); ++i)
    {
        Ptr<LteEnbNetDevice> enbNetDevice = DynamicCast<LteEnbNetDevice>(enbDevs.Get(i));
        if (enbNetDevice)
        {
            Ptr<LteEnbRrc> enbRrc = enbNetDevice->GetRrc();
            if (enbRrc)
            {
                // Connect Handover Start
                enbRrc->TraceConnectWithoutContext("HandoverStart",
                                                   MakeCallback(&HandoverStartCallback));

                // Connect Handover Success
                enbRrc->TraceConnectWithoutContext("HandoverSuccess",
                                                   MakeCallback(&HandoverSuccessCallback));

                // Connect Handover Failure
                enbRrc->TraceConnectWithoutContext("HandoverFailure",
                                                   MakeCallback(&HandoverFailureCallback));
            }
            else
            {
                NS_LOG_WARN("eNodeB RRC not found for device " << i);
            }
        }
        else
        {
            NS_LOG_WARN("eNodeB NetDevice not found for device " << i);
        }
    }

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

    // Finalize logging
    handoverLogFile.close();

    // Final Analysis of Flow Monitor Data
    AnalyzeData(flowHelper, flowMonitor, params, remoteHostAddr, ueAddresses);

    // Clean Up
    Simulator::Destroy();
    NS_LOG_INFO("LTE simulation finished!");
    return 0;
}

// ============================================================================
/**
 * @brief Configures the logging levels for various components.
 */
void
ConfigureLogging()
{
    LogComponentEnable("VoipLteSimulation", LOG_LEVEL_INFO);
    LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
    LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
    // Uncomment to enable more detailed logging
    // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
}

/**
 * @brief Configures the mobility model for eNodeBs, placing them strategically based on the number
 *        of eNodeBs.
 *        - If 4 eNodeBs: Position them at four corners of the simulation area.
 *        - If 2 eNodeBs: Align them centrally along the Y-axis, maintaining X-axis positions from
 *          the 4 eNodeB setup.
 * @param enbNodes Container of eNodeB nodes.
 * @param areaSize Size of the simulation area.
 */
void
ConfigureEnbMobility(NodeContainer& enbNodes, double areaSize)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();

    if (enbNodes.GetN() == 4)
    {
        // Four eNodeBs: Default positioning (corners)
        posAlloc->Add(Vector(areaSize / 4, areaSize / 4, 30.0));         // Bottom-left
        posAlloc->Add(Vector(areaSize / 4, 3 * areaSize / 4, 30.0));     // Top-left
        posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 4, 30.0));     // Bottom-right
        posAlloc->Add(Vector(3 * areaSize / 4, 3 * areaSize / 4, 30.0)); // Top-right
    }
    else if (enbNodes.GetN() == 2)
    {
        // Two eNodeBs: Centered along the Y-axis
        posAlloc->Add(Vector(areaSize / 4, areaSize / 2, 30.0));     // Center-left
        posAlloc->Add(Vector(3 * areaSize / 4, areaSize / 2, 30.0)); // Center-right
    }
    else
    {
        NS_LOG_WARN("Unsupported number of eNodeBs: " << enbNodes.GetN());
        // Place all eNodeBs at the center if unsupported number
        for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
        {
            posAlloc->Add(Vector(areaSize / 2, areaSize / 2, 30.0));
        }
    }

    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}

/**
 * @brief Configures the mobility model for UEs based on the selected mobility mode.
 * @param ueNodes Container of UE nodes.
 * @param areaSize Size of the simulation area.
 * @param enbNodes Container of eNodeB nodes.
 * @param mobilityMode Selected mobility mode.
 * @param params Simulation parameters including path loss distances.
 */
void
ConfigureUeMobility(NodeContainer& ueNodes,
                    double areaSize,
                    NodeContainer& enbNodes,
                    SimulationParameters::MobilityMode mobilityMode,
                    const SimulationParameters& params)
{
    switch (mobilityMode)
    {
    case SimulationParameters::RANDOM_WAYPOINT: {
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
        NS_LOG_INFO("Configured UEs with RandomWaypoint Mobility Model.");
        break;
    }
    case SimulationParameters::CONSTANT_UNDER_DISTANCE0:
    case SimulationParameters::CONSTANT_UNDER_DISTANCE1:
    case SimulationParameters::CONSTANT_ABOVE_DISTANCE1: {
        // Constant Position Mobility Model with placement based on distance
        MobilityHelper ueMobility;
        ueMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        ueMobility.Install(ueNodes);

        // Random number generators for position offsets
        std::default_random_engine generator;
        std::uniform_real_distribution<double> angleDist(0.0, 2 * M_PI);
        std::uniform_real_distribution<double> distanceDist;

        // Define distance range based on mobility mode
        double minDist = 0.0;
        double maxDist = 0.0;
        if (mobilityMode == SimulationParameters::CONSTANT_UNDER_DISTANCE0)
        {
            minDist = 0.0;
            maxDist = params.distance0;
        }
        else if (mobilityMode == SimulationParameters::CONSTANT_UNDER_DISTANCE1)
        {
            minDist = (params.distance1 / 2.0) - 10;
            maxDist = (params.distance1 / 2.0) + 10;
        }
        else if (mobilityMode == SimulationParameters::CONSTANT_ABOVE_DISTANCE1)
        {
            minDist = params.distance1;
            maxDist = params.areaSize / 2.0; // Assuming maximum possible distance
        }

        for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
        {
            Ptr<Node> ue = ueNodes.Get(i);
            Ptr<MobilityModel> ueMob = ue->GetObject<MobilityModel>();
            if (!ueMob)
            {
                NS_LOG_WARN("UE " << i << " has no MobilityModel!");
                continue;
            }

            // Find the closest eNodeB
            double minDistance = std::numeric_limits<double>::max();
            uint32_t closestEnb = 0;
            Vector uePos = ueMob->GetPosition();

            for (uint32_t j = 0; j < enbNodes.GetN(); ++j)
            {
                Ptr<MobilityModel> enbMob = enbNodes.Get(j)->GetObject<MobilityModel>();
                Vector enbPos = enbMob->GetPosition();
                double dist = CalculateDistance(uePos, enbPos);
                if (dist < minDistance)
                {
                    minDistance = dist;
                    closestEnb = j;
                }
            }

            Ptr<MobilityModel> enbMob = enbNodes.Get(closestEnb)->GetObject<MobilityModel>();
            Vector enbPos = enbMob->GetPosition();

            // Determine distance based on mobility mode
            if (mobilityMode == SimulationParameters::CONSTANT_ABOVE_DISTANCE1)
            {
                distanceDist =
                    std::uniform_real_distribution<double>(params.distance1, params.areaSize / 2.0);
            }
            else
            {
                distanceDist = std::uniform_real_distribution<double>(minDist, maxDist);
            }

            double distance = distanceDist(generator);
            double angle = angleDist(generator);

            // Calculate UE position relative to eNodeB
            double ueX = enbPos.x + distance * cos(angle);
            double ueY = enbPos.y + distance * sin(angle);

            // Ensure UE is within the simulation area
            ueX = std::max(0.0, std::min(ueX, params.areaSize));
            ueY = std::max(0.0, std::min(ueY, params.areaSize));

            ueMob->SetPosition(Vector(ueX, ueY, 0.0));
        }

        NS_LOG_INFO("Configured UEs with Constant Position Mobility Model.");
        break;
    }
    default:
        NS_LOG_ERROR("Misconfigured, stop simulation.");
        exit(1);
    }
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

    // Log handover counts
    g_handoverStartPlot.push_back(g_handoverStartCount);
    g_handoverSuccessPlot.push_back(g_handoverSuccessCount);
    g_handoverFailurePlot.push_back(g_handoverFailureCount);

    // Reset counters for the next interval
    g_handoverStartCount = 0;
    g_handoverSuccessCount = 0;
    g_handoverFailureCount = 0;

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
        if (destPort >= 5000 && destPort < (5000 + params.numUe))
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

    double avgLatencyMs = (totalRxPackets > 0) ? (totalLatencySum / (double)totalRxPackets) : 0.0;

    // Compute average throughput across all UEs
    double avgThroughputKbps = (params.numUe > 0) ? (aggregateThroughputKbps / params.numUe) : 0.0;
    g_avgThroughputPlot.push_back(avgThroughputKbps); // Store average throughput

    // Store time-plot data
    g_timePlot.push_back(g_currentTime);
    g_avgLatencyPlot.push_back(avgLatencyMs);

    for (uint32_t i = 0; i < params.numUe; i++)
    {
        g_ueThroughputPlot[i].push_back(ueThroughputKbps[i]);
        g_uePacketLossPlot[i].push_back(uePacketLossRate[i]);
        g_ueJitterPlot[i].push_back(ueJitterMs[i]);
    }

    // Log current statistics
    std::ostringstream oss;
    oss << "Time: " << g_currentTime << "s, "
        << "Aggregate Throughput: " << aggregateThroughputKbps << " Kbps, "
        << "Average Throughput: " << avgThroughputKbps << " Kbps, "
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
 * @param remoteHostAddr IP address of the remote host.
 * @param ueAddresses Vector of UE IP addresses.
 */
void
AnalyzeData(FlowMonitorHelper& flowHelper,
            Ptr<FlowMonitor> flowMonitor,
            const SimulationParameters& params,
            Ipv4Address remoteHostAddr,
            const std::vector<Ipv4Address>& ueAddresses)
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

        // Check if the flow is from a UE to the Remote Host
        bool isFromUe = false;
        for (const auto& ueIp : ueAddresses)
        {
            if (t.sourceAddress == ueIp)
            {
                isFromUe = true;
                break;
            }
        }

        if (!isFromUe || t.destinationAddress != remoteHostAddr)
            continue; // Skip flows not originating from UEs to Remote Host

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
    plotThroughput.SetOutputFilename("ue-throughput-time-plot.png");
    plotThroughput.SetLegend("Time (s)", "Throughput (Kbps)");

    for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
    {
        Gnuplot2dDataset dsT;
        dsT.SetTitle("UE-" + std::to_string(ueIndex));
        dsT.SetStyle(Gnuplot2dDataset::LINES_POINTS);

        size_t steps = std::min(g_timePlot.size(), g_ueThroughputPlot[ueIndex].size());
        for (size_t i = 0; i < steps; i++)
        {
            dsT.Add(g_timePlot[i], g_ueThroughputPlot[ueIndex][i]);
        }
        plotThroughput.AddDataset(dsT);
    }

    // Write Gnuplot Script for Throughput
    {
        std::ofstream fileT("ue-throughput-time-plot.plt");
        fileT << "set terminal png size 800,600\n";
        fileT << "set output 'ue-throughput-time-plot.png'\n";
        fileT << "set title 'Per-UE Throughput Over Time'\n";
        fileT << "set xlabel 'Time (s)'\n";
        fileT << "set ylabel 'Throughput (Kbps)'\n";
        fileT << "set key left top\n";
        fileT << "plot ";
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            fileT << "'-' with linespoints title 'UE-" << ueIndex << "'";
            if (ueIndex != static_cast<uint32_t>(params.numUe) - 1)
                fileT << ", ";
        }
        fileT << "\n";

        // Write data for each UE
        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            for (size_t i = 0; i < g_timePlot.size(); i++)
            {
                double thr =
                    (i < g_ueThroughputPlot[ueIndex].size()) ? g_ueThroughputPlot[ueIndex][i] : 0.0;
                fileT << g_timePlot[i] << " " << thr << "\n";
            }
            fileT << "e\n";
        }
        fileT.close();

        NS_LOG_INFO("UE Throughput Gnuplot script: ue-throughput-time-plot.plt");
    }

    // Generate Gnuplot for Aggregate Latency
    Gnuplot plotLatency;
    plotLatency.SetTitle("Aggregate Latency Over Time");
    plotLatency.SetTerminal("png size 800,600");
    plotLatency.SetOutputFilename("latency-time-plot.png");
    plotLatency.SetLegend("Time (s)", "Latency (ms)");

    Gnuplot2dDataset dsL;
    dsL.SetTitle("Avg Latency (all flows)");
    dsL.SetStyle(Gnuplot2dDataset::LINES_POINTS);

    for (size_t i = 0; i < g_timePlot.size(); ++i)
    {
        dsL.Add(g_timePlot[i], g_avgLatencyPlot[i]);
    }
    plotLatency.AddDataset(dsL);

    // Write Gnuplot Script for Latency
    {
        std::ofstream fileL("latency-time-plot.plt");
        fileL << "set terminal png size 800,600\n";
        fileL << "set output 'latency-time-plot.png'\n";
        fileL << "set title 'Aggregate Latency Over Time'\n";
        fileL << "set xlabel 'Time (s)'\n";
        fileL << "set ylabel 'Latency (ms)'\n";
        fileL << "set key left top\n";
        fileL << "plot '-' with linespoints title 'Avg Latency'\n";

        for (size_t i = 0; i < g_timePlot.size(); i++)
        {
            double lat = (i < g_avgLatencyPlot.size()) ? g_avgLatencyPlot[i] : 0.0;
            fileL << g_timePlot[i] << " " << lat << "\n";
        }
        fileL << "e\n";
        fileL.close();

        NS_LOG_INFO("Latency Gnuplot script: latency-time-plot.plt");
    }

    // Generate Gnuplot for Average Throughput
    Gnuplot plotAvgThroughput;
    plotAvgThroughput.SetTitle("Average Throughput Over Time");
    plotAvgThroughput.SetTerminal("png size 800,600");
    plotAvgThroughput.SetOutputFilename("avg-throughput-time-plot.png");
    plotAvgThroughput.SetLegend("Time (s)", "Average Throughput (Kbps)");

    Gnuplot2dDataset dsAvgThroughput;
    dsAvgThroughput.SetTitle("Avg Throughput");
    dsAvgThroughput.SetStyle(Gnuplot2dDataset::LINES_POINTS);

    for (size_t i = 0; i < g_timePlot.size(); ++i)
    {
        dsAvgThroughput.Add(g_timePlot[i], g_avgThroughputPlot[i]);
    }
    plotAvgThroughput.AddDataset(dsAvgThroughput);

    // Write Gnuplot Script for Average Throughput
    {
        std::ofstream fileAvg("avg-throughput-time-plot.plt");
        fileAvg << "set terminal png size 800,600\n";
        fileAvg << "set output 'avg-throughput-time-plot.png'\n";
        fileAvg << "set title 'Average Throughput Over Time'\n";
        fileAvg << "set xlabel 'Time (s)'\n";
        fileAvg << "set ylabel 'Average Throughput (Kbps)'\n";
        fileAvg << "set key left top\n";
        fileAvg << "plot '-' with linespoints title 'Avg Throughput'\n";

        for (size_t i = 0; i < g_timePlot.size(); i++)
        {
            double avgThr = (i < g_avgThroughputPlot.size()) ? g_avgThroughputPlot[i] : 0.0;
            fileAvg << g_timePlot[i] << " " << avgThr << "\n";
        }
        fileAvg << "e\n";
        fileAvg.close();

        NS_LOG_INFO("Average Throughput Gnuplot script: avg-throughput-time-plot.plt");
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

    // Export Time-Plot Data to CSV
    std::ofstream csvFile("simulation_metrics.csv");
    if (!csvFile.is_open())
    {
        NS_LOG_ERROR("Failed to open simulation_metrics.csv for writing.");
        return;
    }

    // Write CSV Header
    csvFile << "Time(s),Avg_Throughput(Kbps)";
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

    // Add Handover Metrics to Header
    csvFile << ",Handover_Start_Count,Handover_Success_Count,Handover_Failure_Count";
    csvFile << "\n";

    // Write CSV Data Rows
    size_t numEntries = g_timePlot.size();
    for (size_t i = 0; i < numEntries; ++i)
    {
        csvFile << g_timePlot[i] << ",";

        double avgThr = (i < g_avgThroughputPlot.size()) ? g_avgThroughputPlot[i] : 0.0;
        csvFile << avgThr;

        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double thr =
                (i < g_ueThroughputPlot[ueIndex].size()) ? g_ueThroughputPlot[ueIndex][i] : 0.0;
            csvFile << "," << thr;
        }

        double lat = (i < g_avgLatencyPlot.size()) ? g_avgLatencyPlot[i] : 0.0;
        csvFile << "," << lat;

        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double loss =
                (i < g_uePacketLossPlot[ueIndex].size()) ? g_uePacketLossPlot[ueIndex][i] : 0.0;
            csvFile << "," << loss;
        }

        for (uint32_t ueIndex = 0; ueIndex < params.numUe; ueIndex++)
        {
            double jit = (i < g_ueJitterPlot[ueIndex].size()) ? g_ueJitterPlot[ueIndex][i] : 0.0;
            csvFile << "," << jit;
        }

        // Add Handover Counts
        uint32_t hs = (i < g_handoverStartPlot.size()) ? g_handoverStartPlot[i] : 0;
        uint32_t hsucc = (i < g_handoverSuccessPlot.size()) ? g_handoverSuccessPlot[i] : 0;
        uint32_t hf = (i < g_handoverFailurePlot.size()) ? g_handoverFailurePlot[i] : 0;
        csvFile << "," << hs << "," << hsucc << "," << hf;

        csvFile << "\n";
    }

    csvFile.close();
    NS_LOG_INFO("Simulation metrics exported to simulation_metrics.csv.");
}
