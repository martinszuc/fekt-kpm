/**
 * @file lte-voip-simulation.cc
 * @brief LTE simulation with VoIP traffic, FlowMonitor, PCAP on Point-to-Point Link, and Gnuplot (ns-3.39 compatible).
 * @details Generates .plt files for throughput, latency, packet loss, and jitter metrics.
 *          Enables PCAP tracing on the point-to-point link between PGW and remote host.
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
#include "ns3/packet-sink-helper.h"    // Required for PacketSinkHelper

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LTEVoipSimulation");

// Function Prototypes
void ConfigureLogging();
void ConfigureEnbMobility(NodeContainer &enbNodes);
void ConfigureUeMobility(NodeContainer &ueNodes);
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer, const std::string &outputDir);
void InstallVoipApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime, NodeContainer &remoteHostContainer);
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper &flowHelper);
void AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper, Ptr<FlowMonitor> flowMonitor, const std::string &outputDir);
void EnableLteTraces(Ptr<LteHelper> lteHelper);
void FixZPosition(Ptr<MobilityModel> mobility, double desiredZ, Vector newPos);

/**
 * @brief Callback function to enforce a fixed z-position.
 * @param mobility Pointer to the MobilityModel.
 * @param desiredZ The desired z-coordinate.
 * @param newPos The new position vector.
 */
void FixZPosition(Ptr<MobilityModel> mobility, double desiredZ, Vector newPos)
{
    // Log the incoming position
    NS_LOG_INFO("FixZPosition Callback: Current Position = ("
                << newPos.x << ", " << newPos.y << ", " << newPos.z << ")");

    // Check if z is already set correctly to avoid infinite loops
    if (newPos.z != desiredZ)
    {
        newPos.z = desiredZ;
        mobility->SetPosition(newPos);
        NS_LOG_INFO("FixZPosition Callback: z-coordinate set to " << desiredZ << " meters.");
    }
}

/**
 * @brief Main function for LTE + VoIP simulation.
 */
int main(int argc, char *argv[])
{
    uint16_t numEnb = 2;
    uint16_t numUe = 5;
    double simTime = 20.0; // Increased simulation time for better metrics observation
    std::string outputDir = "lte-voip-output";

    CommandLine cmd;
    cmd.AddValue("numEnb", "Number of eNodeBs", numEnb);
    cmd.AddValue("numUe", "Number of UEs", numUe);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.AddValue("outputDir", "Output directory", outputDir);
    cmd.Parse(argc, argv);

    // Create output directory
    (void) system(("mkdir -p " + outputDir).c_str());

    // Configure logging
    ConfigureLogging();

    // Create nodes
    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(numEnb);
    ueNodes.Create(numUe);
    remoteHostContainer.Create(1);

    // Create LTE & EPC helpers
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Configure Mobility
    ConfigureEnbMobility(enbNodes);
    ConfigureUeMobility(ueNodes);

    // Configure Path Loss Model AFTER setting node positions
    lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::OkumuraHataPropagationLossModel"));
    lteHelper->SetPathlossModelAttribute("Environment", StringValue("SubUrban"));
    lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(1800.0));

    // Install LTE devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Install Internet stack on UEs and remote host
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(remoteHostContainer);

    // Assign IP addresses to UEs
    epcHelper->AssignUeIpv4Address(ueDevs);

    // Attach UEs to eNodeBs
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
    {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % numEnb));
    }

    // Create remote host link, enable PCAP, and retrieve IP
    Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer, outputDir);

    // Install VoIP applications on UEs
    InstallVoipApplications(ueNodes, remoteHostAddr, simTime, remoteHostContainer);

    // Enable LTE traces (PHY and MAC layers)
    EnableLteTraces(lteHelper);

    // Setup FlowMonitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Analyze FlowMonitor results
    AnalyzeFlowMonitor(flowHelper, flowMonitor, outputDir);

    // Cleanup
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
    LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
    // Removed invalid log component
    // LogComponentEnable("MobilityModel", LOG_LEVEL_INFO); // Removed because it's not a valid component
}

/**
 * @brief Configure mobility for eNodeBs (fixed positions, 30m height).
 * @param enbNodes Container of eNodeB nodes.
 */
void ConfigureEnbMobility(NodeContainer &enbNodes)
{
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(0.0, 100.0, 30.0));    // eNodeB 0
    posAlloc->Add(Vector(500.0, 100.0, 30.0));  // eNodeB 1
    enbMobility.SetPositionAllocator(posAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);

    // Log eNodeB positions
    for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
    {
        Ptr<MobilityModel> mobility = enbNodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition();
        NS_LOG_INFO("eNodeB " << i << " Position: " << pos);
    }
}

/**
 * @brief Configure mobility for UEs with RandomWaypoint and height of 1.5m.
 * @param ueNodes Container of UE nodes.
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

    // Set height of UEs (Z-axis) and connect the FixZPosition callback
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<MobilityModel> mobility = ueNodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition();
        pos.z = 1.5; // Set height to 1.5m
        mobility->SetPosition(pos);

        // Connect the FixZPosition callback to ensure z remains at 1.5m
        mobility->TraceConnectWithoutContext("Position", MakeBoundCallback(&FixZPosition, mobility, 1.5));

        // Log UE positions
        NS_LOG_INFO("UE " << i << " Initial Position: " << pos);
    }
}

/**
 * @brief Create a remote host connected to the PGW and enable PCAP tracing on the point-to-point link.
 * @param epcHelper EPC helper.
 * @param remoteHostContainer Container with the remote host node.
 * @param outputDir Output directory for PCAP files.
 * @return IP address of the remote host.
 */
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer, const std::string &outputDir)
{
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("1.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // Assign a mobility model to the remote host to set its z-position
    MobilityHelper remoteMobility;
    remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    remoteMobility.Install(remoteHostContainer.Get(0));
    Ptr<MobilityModel> remoteMobilityModel = remoteHostContainer.Get(0)->GetObject<MobilityModel>();
    remoteMobilityModel->SetPosition(Vector(0.0, 0.0, 1.5)); // Set x=0, y=0, z=1.5m

    // Log Remote Host position
    Vector remotePos = remoteMobilityModel->GetPosition();
    NS_LOG_INFO("Remote Host Position: " << remotePos);

    // Assign a mobility model to the PGW node to set its z-position
    MobilityHelper pgwMobility;
    pgwMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    pgwMobility.Install(pgw);
    Ptr<MobilityModel> pgwMobilityModel = pgw->GetObject<MobilityModel>();
    pgwMobilityModel->SetPosition(Vector(0.0, 0.0, 1.5)); // Set x=0, y=0, z=1.5m

    // Log PGW position
    Vector pgwPos = pgwMobilityModel->GetPosition();
    NS_LOG_INFO("PGW Position: " << pgwPos);

    // Enable PCAP tracing on the point-to-point link (remote host side)
    p2p.EnablePcap("remote-host-p2p", devices.Get(1), true); // 'true' for promiscuous mode

    return interfaces.GetAddress(1); // Remote host's IP
}

/**
 * @brief Install VoIP applications on UEs.
 * @param ueNodes Container with UE nodes.
 * @param remoteAddr IP address of the remote host.
 * @param simTime Simulation time in seconds.
 * @param remoteHostContainer Container with the remote host node.
 */
void InstallVoipApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime, NodeContainer &remoteHostContainer)
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

    // Install a PacketSink on the remote host to receive the packets
    PacketSinkHelper packetSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = packetSink.Install(remoteHostContainer.Get(0));
    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime));

    // Log application installation
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        NS_LOG_INFO("VoIP application installed on UE " << i);
    }
}

/**
 * @brief Enable LTE traces (PHY and MAC layers).
 * @param lteHelper LTE helper instance.
 */
void EnableLteTraces(Ptr<LteHelper> lteHelper)
{
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();
    NS_LOG_INFO("Enabled PHY and MAC traces.");
}

/**
 * @brief Setup FlowMonitor to collect metrics.
 * @param flowHelper FlowMonitorHelper instance.
 * @return Pointer to the installed FlowMonitor.
 */
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper &flowHelper)
{
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    NS_LOG_INFO("FlowMonitor installed.");
    return flowMonitor;
}

/**
 * @brief Analyze FlowMonitor results and generate .plt files for metrics.
 * @param flowHelper FlowMonitorHelper instance.
 * @param flowMonitor Pointer to the installed FlowMonitor.
 * @param outputDir Output directory to save the .plt files.
 */
void AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper, Ptr<FlowMonitor> flowMonitor, const std::string &outputDir)
{
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

    // Throughput Plot
    {
        Gnuplot throughputPlot(outputDir + "/throughput.plt");
        throughputPlot.SetTitle("Throughput vs. Flow ID");
        throughputPlot.SetTerminal("png");
        throughputPlot.SetLegend("Flow ID", "Throughput (bps)");
        Gnuplot2dDataset dataset;
        dataset.SetTitle("Throughput");
        dataset.SetStyle(Gnuplot2dDataset::POINTS);

        uint32_t flowId = 1;
        for (auto &iter : stats)
        {
            double throughputBps = 0.0;
            double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
            if (duration > 0)
            {
                throughputBps = (iter.second.rxBytes * 8.0) / duration;
            }
            dataset.Add(flowId, throughputBps);
            flowId++;
        }

        throughputPlot.AddDataset(dataset);
        std::ofstream plotFile(outputDir + "/throughput.plt");
        throughputPlot.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Throughput plot generated.");
    }

    // Latency Plot
    {
        Gnuplot latencyPlot(outputDir + "/latency.plt");
        latencyPlot.SetTitle("Latency vs. Flow ID");
        latencyPlot.SetTerminal("png");
        latencyPlot.SetLegend("Flow ID", "Latency (s)");
        Gnuplot2dDataset dataset;
        dataset.SetTitle("Latency");
        dataset.SetStyle(Gnuplot2dDataset::POINTS);

        uint32_t flowId = 1;
        for (auto &iter : stats)
        {
            double avgLatency = 0.0;
            if (iter.second.rxPackets > 0)
            {
                avgLatency = iter.second.delaySum.GetSeconds() / iter.second.rxPackets;
            }
            dataset.Add(flowId, avgLatency);
            flowId++;
        }

        latencyPlot.AddDataset(dataset);
        std::ofstream plotFile(outputDir + "/latency.plt");
        latencyPlot.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Latency plot generated.");
    }

    // Packet Loss Plot
    {
        Gnuplot lossPlot(outputDir + "/packet-loss.plt");
        lossPlot.SetTitle("Packet Loss vs. Flow ID");
        lossPlot.SetTerminal("png");
        lossPlot.SetLegend("Flow ID", "Packet Loss (%)");
        Gnuplot2dDataset dataset;
        dataset.SetTitle("Packet Loss");
        dataset.SetStyle(Gnuplot2dDataset::POINTS);

        uint32_t flowId = 1;
        for (auto &iter : stats)
        {
            double lossRate = 0.0;
            if (iter.second.txPackets > 0)
            {
                lossRate = ((double)(iter.second.txPackets - iter.second.rxPackets) / iter.second.txPackets) * 100.0;
            }
            dataset.Add(flowId, lossRate);
            flowId++;
        }

        lossPlot.AddDataset(dataset);
        std::ofstream plotFile(outputDir + "/packet-loss.plt");
        lossPlot.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Packet Loss plot generated.");
    }

    // Jitter Plot
    {
        Gnuplot jitterPlot(outputDir + "/jitter.plt");
        jitterPlot.SetTitle("Jitter vs. Flow ID");
        jitterPlot.SetTerminal("png");
        jitterPlot.SetLegend("Flow ID", "Jitter (s)");
        Gnuplot2dDataset dataset;
        dataset.SetTitle("Jitter");
        dataset.SetStyle(Gnuplot2dDataset::POINTS);

        uint32_t flowId = 1;
        for (auto &iter : stats)
        {
            double avgJitter = 0.0;
            if (iter.second.jitterSum.GetSeconds() > 0)
            {
                avgJitter = iter.second.jitterSum.GetSeconds() / iter.second.rxPackets;
            }
            dataset.Add(flowId, avgJitter);
            flowId++;
        }

        jitterPlot.AddDataset(dataset);
        std::ofstream plotFile(outputDir + "/jitter.plt");
        jitterPlot.GenerateOutput(plotFile);
        plotFile.close();
        NS_LOG_INFO("Jitter plot generated.");
    }

    // Serialize FlowMonitor results to XML
    flowMonitor->SerializeToXmlFile(outputDir + "/flowmon.xml", true, true);
    NS_LOG_INFO("FlowMonitor results serialized to XML.");
}
