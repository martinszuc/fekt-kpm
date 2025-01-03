/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * @file enhanced-lte-voip-simulation.cc
 * @brief Enhanced LTE + VoIP simulation for ns-3.39 with various requested enhancements.
 *
 * This version fixes compilation issues:
 *  - Correct usage of HandoverRequest.
 *  - Proper usage of TrafficControlHelper.
 *  - Removal of unused variables.
 *  - Outputs are saved in the current directory.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EnhancedLteVoipSimulation");

// Global variables for real-time stats
static double g_statsInterval = 5.0; // seconds
static double g_currentTime = 0.0;

// Time-series data
std::vector<double> g_timeSeries;
std::vector<double> g_throughputSeries; // bits/s
std::vector<double> g_avgLatencySeries; // s

// Function Prototypes
void ConfigureLogging();
void ConfigureEnbMobility(NodeContainer &enbNodes);
void ConfigureUeMobility(NodeContainer &ueNodes, double areaSize);
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer);
void InstallVoipApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime, NodeContainer &remoteHostContainer);
void InstallBackgroundTraffic(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime, NodeContainer &remoteHostContainer);
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper &flowHelper);
void AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper, Ptr<FlowMonitor> flowMonitor, double simTime);
void EnableLteTraces(Ptr<LteHelper> lteHelper);

void PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor, FlowMonitorHelper &flowHelper);
void ManualHandoverCheck(Ptr<LteHelper> lteHelper, NodeContainer ueNodes, NodeContainer enbNodes);
void NotifyUeAttached(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti);
void NotifyPacketDrop(std::string context, Ptr<const Packet> packet);
void NotifyRrcStateChange(std::string context, uint64_t imsi, uint16_t cellId,
                          uint16_t rnti, ns3::LteUeRrc::State oldState,
                          ns3::LteUeRrc::State newState);
void LogUePositions(NodeContainer &ueNodes);

int
main(int argc, char *argv[])
{
  // Default parameters
  uint16_t numEnb = 2;
  uint16_t numUe = 5;
  double simTime = 50.0;
  double areaSize = 500.0;
  bool enableBackgroundTraffic = false;

  CommandLine cmd;
  cmd.AddValue("numEnb", "Number of eNodeBs", numEnb);
  cmd.AddValue("numUe", "Number of UEs", numUe);
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("areaSize", "Square area for UE random positions [0..areaSize]", areaSize);
  cmd.AddValue("enableBackgroundTraffic", "Enable extra TCP background traffic", enableBackgroundTraffic);
  cmd.Parse(argc, argv);

  // Removed outputDir logic to save all outputs in the current directory

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

  lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::Cost231PropagationLossModel"));
  lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(1800.0)); // Frequency in MHz

  // Enable fading (Optional: Remove if not needed)
  lteHelper->SetFadingModel("ns3::TraceFadingLossModel");
  lteHelper->SetFadingModelAttribute("TraceFilename", StringValue("src/lte/model/fading-traces/fading_trace_EPA_3kmph.fad"));
  lteHelper->SetFadingModelAttribute("TraceLength", TimeValue(Seconds(10.0)));
  lteHelper->SetFadingModelAttribute("SamplesNum", UintegerValue(10000));
  lteHelper->SetFadingModelAttribute("WindowSize", TimeValue(MilliSeconds(100.0)));
  lteHelper->SetFadingModelAttribute("RbNum", UintegerValue(25));

  // Configure mobility
  ConfigureEnbMobility(enbNodes);
  ConfigureUeMobility(ueNodes, areaSize);

  // Install LTE devices
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

  // Install Internet stack
  InternetStackHelper internet;
  internet.Install(ueNodes);
  internet.Install(remoteHostContainer);

  // Assign IP addresses
  epcHelper->AssignUeIpv4Address(ueDevs);

  // Attach UEs to eNBs
  for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
  {
    lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % numEnb));
  }

  // Create remote host link without outputDir
  Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer);

  // Install VoIP apps
  InstallVoipApplications(ueNodes, remoteHostAddr, simTime, remoteHostContainer);

  // Optionally install background traffic
  if (enableBackgroundTraffic)
  {
    InstallBackgroundTraffic(ueNodes, remoteHostAddr, simTime, remoteHostContainer);
  }

  // Enable LTE traces
  EnableLteTraces(lteHelper);

  // Setup FlowMonitor
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

  // Connect debug callbacks
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                  MakeCallback(&NotifyUeAttached));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/StateTransition",
                  MakeCallback<void, std::string, uint64_t, uint16_t, uint16_t, ns3::LteUeRrc::State, ns3::LteUeRrc::State>(
                      &NotifyRrcStateChange));

  // Schedule stats updates
  Simulator::Schedule(Seconds(g_statsInterval), &PeriodicStatsUpdate, flowMonitor, std::ref(flowHelper));

  // Periodic position logging
  Simulator::Schedule(Seconds(1.0), &LogUePositions, std::ref(ueNodes));

  // Schedule manual handover checks
  Simulator::Schedule(Seconds(1.0), &ManualHandoverCheck, lteHelper, ueNodes, enbNodes);

  // Run simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Post-processing (FlowMonitor analysis, output generation)
  AnalyzeFlowMonitor(flowHelper, flowMonitor, simTime);

  Simulator::Destroy();
  NS_LOG_INFO("Enhanced LTE simulation finished!");
  return 0;
}

/* -------------------------------------------------------------
 *  IMPLEMENTATION OF THE AUXILIARY FUNCTIONS
 * ------------------------------------------------------------- */

// Logging
void
ConfigureLogging()
{
  LogComponentEnable("EnhancedLteVoipSimulation", LOG_LEVEL_INFO);
  LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
  LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
}

// eNB Mobility
void
ConfigureEnbMobility(NodeContainer &enbNodes)
{
  MobilityHelper enbMobility;
  Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
  // Example positions for two eNBs:
  posAlloc->Add(Vector(0.0, 100.0, 30.0));
  posAlloc->Add(Vector(500.0, 100.0, 30.0));
  enbMobility.SetPositionAllocator(posAlloc);
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.Install(enbNodes);

  for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
  {
    Ptr<MobilityModel> mobility = enbNodes.Get(i)->GetObject<MobilityModel>();
    Vector pos = mobility->GetPosition();
    NS_LOG_INFO("eNodeB " << i << " Position: " << pos);
  }
}

// UE Mobility
void
ConfigureUeMobility(NodeContainer &ueNodes, double areaSize)
{
  MobilityHelper ueMobility;

  Ptr<RandomRectanglePositionAllocator> positionAlloc = CreateObject<RandomRectanglePositionAllocator>();
  positionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  positionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
  ueMobility.SetPositionAllocator(positionAlloc);

  ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                              "Speed", StringValue("ns3::UniformRandomVariable[Min=5.0|Max=15.0]"),
                              "Pause", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                              "PositionAllocator", PointerValue(positionAlloc));

  ueMobility.Install(ueNodes);

  // Clamp positions in [0..areaSize, 0..areaSize], set z=1.5
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    Ptr<MobilityModel> mobility = ueNodes.Get(i)->GetObject<MobilityModel>();
    Vector pos = mobility->GetPosition();
    if (pos.x < 0.0 || pos.x > areaSize || pos.y < 0.0 || pos.y > areaSize)
    {
      NS_LOG_WARN("UE " << i << " assigned out-of-bounds (" << pos.x << "," << pos.y << "). Clamping.");
      pos.x = std::max(0.0, std::min(pos.x, areaSize));
      pos.y = std::max(0.0, std::min(pos.y, areaSize));
    }
    pos.z = 1.5;
    mobility->SetPosition(pos);

    NS_LOG_INFO("UE " << i << " Initial Position: " << pos);
  }
}

// Remote Host + PGW link
Ipv4Address
CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper,
                 NodeContainer &remoteHostContainer)
{
  Ptr<Node> pgw = epcHelper->GetPgwNode();
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));

  NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("1.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

  // Mobility for remote host
  MobilityHelper remoteMobility;
  remoteMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  remoteMobility.Install(remoteHostContainer.Get(0));
  remoteHostContainer.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 1.5));

  // Mobility for PGW
  MobilityHelper pgwMobility;
  pgwMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  pgwMobility.Install(pgw);
  pgw->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 1.5));

  // Enable PCAP without outputDir
  p2p.EnablePcap("pgw-p2p", devices.Get(0), true);
  p2p.EnablePcap("remote-host-p2p", devices.Get(1), true);

  return interfaces.GetAddress(1);
}

// VoIP Applications
void
InstallVoipApplications(NodeContainer &ueNodes,
                        Ipv4Address remoteAddr,
                        double simTime,
                        NodeContainer &remoteHostContainer)
{
  uint16_t basePort = 5000;
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    uint16_t port = basePort + i;
    OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(remoteAddr, port));
    onOff.SetAttribute("DataRate", StringValue("32kbps"));
    onOff.SetAttribute("PacketSize", UintegerValue(160));
    onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer apps = onOff.Install(ueNodes.Get(i));
    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(simTime));

    PacketSinkHelper packetSink("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = packetSink.Install(remoteHostContainer.Get(0));
    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime));

    NS_LOG_INFO("VoIP installed on UE " << i << " -> port " << port);
  }
}

// Background TCP traffic
void
InstallBackgroundTraffic(NodeContainer &ueNodes,
                         Ipv4Address remoteAddr,
                         double simTime,
                         NodeContainer &remoteHostContainer)
{
  NS_LOG_INFO("Installing background TCP traffic...");
  if (ueNodes.GetN() < 1)
  {
    NS_LOG_WARN("No UEs to install background traffic!");
    return;
  }

  uint16_t port = 9000;
  BulkSendHelper bulkSend("ns3::TcpSocketFactory", InetSocketAddress(remoteAddr, port));
  bulkSend.SetAttribute("MaxBytes", UintegerValue(0)); // unlimited
  ApplicationContainer sendApps = bulkSend.Install(ueNodes.Get(0));
  sendApps.Start(Seconds(2.0));
  sendApps.Stop(Seconds(simTime - 5.0));

  // Install sink on remote host
  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApps = sink.Install(remoteHostContainer.Get(0));
  sinkApps.Start(Seconds(1.5));
  sinkApps.Stop(Seconds(simTime));

  NS_LOG_INFO("Background TCP traffic installed on UE 0 -> port " << port);
}

// Enable LTE traces
void
EnableLteTraces(Ptr<LteHelper> lteHelper)
{
  lteHelper->EnablePhyTraces();
  lteHelper->EnableMacTraces();
  lteHelper->EnableRlcTraces();
  lteHelper->EnablePdcpTraces();
  NS_LOG_INFO("Enabled LTE traces.");
}

// FlowMonitor setup
Ptr<FlowMonitor>
SetupFlowMonitor(FlowMonitorHelper &flowHelper)
{
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
  flowMonitor->CheckForLostPackets();
  auto stats = flowMonitor->GetFlowStats();
  NS_LOG_INFO("FlowMonitor installed.");
  return flowMonitor;
}

// Periodic stats update
void
PeriodicStatsUpdate(Ptr<FlowMonitor> flowMonitor, FlowMonitorHelper &flowHelper)
{
  g_currentTime += g_statsInterval;

  flowMonitor->CheckForLostPackets();
  auto stats = flowMonitor->GetFlowStats();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

  double totalThroughput = 0.0;     // bits/s
  double totalLatencySum = 0.0;     // s
  uint64_t totalRxPackets = 0;      // for average latency calculation

  for (auto &iter : stats)
  {
    // If you only want to track certain flows, you can use the FiveTuple here:
    // Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
    // Currently, we consider all flows.

    double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
    double throughput = 0.0;
    if (duration > 0)
    {
      throughput = (iter.second.rxBytes * 8.0) / duration; // bits/s
      totalThroughput += throughput;
    }
    if (iter.second.rxPackets > 0)
    {
      double avgFlowLatency = iter.second.delaySum.GetSeconds() / iter.second.rxPackets;
      totalLatencySum += (avgFlowLatency * iter.second.rxPackets);
      totalRxPackets += iter.second.rxPackets;
    }
  }

  double avgLatency = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
  double avgThroughput = totalThroughput; // bits/s

  // Store time-series data
  g_timeSeries.push_back(g_currentTime);
  g_throughputSeries.push_back(avgThroughput);
  g_avgLatencySeries.push_back(avgLatency);

  NS_LOG_INFO("Time: " << g_currentTime
              << "s, Aggregate Throughput: " << avgThroughput / 1e6
              << " Mbps, Avg Latency: " << avgLatency << " s");

  if (Simulator::Now().GetSeconds() + g_statsInterval <= Simulator::GetMaximumSimulationTime().GetSeconds())
  {
    Simulator::Schedule(Seconds(g_statsInterval), &PeriodicStatsUpdate, flowMonitor, std::ref(flowHelper));
  }
}

// Manual Handover
void
ManualHandoverCheck(Ptr<LteHelper> lteHelper, NodeContainer ueNodes, NodeContainer enbNodes)
{
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    Ptr<NetDevice> ueDevice = ueNodes.Get(i)->GetDevice(0);
    Ptr<LteUeNetDevice> ueLteDevice = DynamicCast<LteUeNetDevice>(ueDevice);
    if (!ueLteDevice)
    {
      continue; // Not an LTE device
    }
    Ptr<MobilityModel> ueMobility = ueNodes.Get(i)->GetObject<MobilityModel>();

    // Find closest eNB
    double minDistance = 1e9;
    uint16_t bestEnbIdx = 0;
    for (uint32_t j = 0; j < enbNodes.GetN(); ++j)
    {
      Ptr<MobilityModel> enbMobility = enbNodes.Get(j)->GetObject<MobilityModel>();
      double distance = ueMobility->GetDistanceFrom(enbMobility);
      if (distance < minDistance)
      {
        minDistance = distance;
        bestEnbIdx = j;
      }
    }

    // Current Cell ID from UE RRC
    Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
    if (!ueRrc)
    {
      continue; // RRC not established
    }
    uint16_t currentCellId = ueRrc->GetCellId();

    // Find the NetDevice of the source eNB by matching currentCellId
    Ptr<NetDevice> sourceEnbDev;
    for (uint32_t j = 0; j < enbNodes.GetN(); ++j)
    {
      Ptr<LteEnbNetDevice> enbDev = enbNodes.Get(j)->GetDevice(0)->GetObject<LteEnbNetDevice>();
      if (enbDev && enbDev->GetCellId() == currentCellId)
      {
        sourceEnbDev = enbDev;
        break;
      }
    }

    // Best Cell ID from eNB net device
    Ptr<LteEnbNetDevice> bestEnbLteDev = DynamicCast<LteEnbNetDevice>(enbNodes.Get(bestEnbIdx)->GetDevice(0));
    if (!bestEnbLteDev)
    {
      continue;
    }
    uint16_t bestCellId = bestEnbLteDev->GetCellId();

    // Trigger if bestCellId != currentCellId and closer than threshold
    double threshold = 50.0; // e.g., 50m threshold
    if ((bestCellId != currentCellId) && (minDistance < threshold) && sourceEnbDev)
    {
      NS_LOG_INFO("Triggering handover for UE " << i
                  << " from Cell " << currentCellId
                  << " to Cell " << bestCellId
                  << " (distance=" << minDistance << "m)");
      // hoTime, ueDev, sourceEnbDev, targetCellId
      lteHelper->HandoverRequest(Seconds(0.1), ueDevice, sourceEnbDev, bestCellId);
    }
  }

  // Reschedule
  if (Simulator::Now().GetSeconds() + 1.0 <= Simulator::GetMaximumSimulationTime().GetSeconds())
  {
    Simulator::Schedule(Seconds(1.0), &ManualHandoverCheck, lteHelper, ueNodes, enbNodes);
  }
}

// UE attachment callback
void
NotifyUeAttached(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  NS_LOG_INFO(Simulator::Now().GetSeconds()
              << "s - UE IMSI=" << imsi
              << " attached to CellId=" << cellId
              << ", RNTI=" << rnti);
}

// Packet drop callback
void
NotifyPacketDrop(std::string context, Ptr<const Packet> packet)
{
  NS_LOG_WARN("Packet Drop at " << context
              << " time " << Simulator::Now().GetSeconds() << "s");
}

/**
 * RRC state transition callback
 */
void NotifyRrcStateChange(std::string context, uint64_t imsi, uint16_t cellId,
                          uint16_t rnti, ns3::LteUeRrc::State oldState,
                          ns3::LteUeRrc::State newState)
{
    NS_LOG_INFO("UE IMSI=" << imsi
                << " RNTI=" << rnti
                << " transitioned from RRC state "
                << (unsigned)oldState
                << " to " << (unsigned)newState
                << " in Cell " << cellId
                << " at time " << Simulator::Now().GetSeconds() << "s");
}

// Periodic UE positions logging
void
LogUePositions(NodeContainer &ueNodes)
{
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    Ptr<MobilityModel> mob = ueNodes.Get(i)->GetObject<MobilityModel>();
    Vector pos = mob->GetPosition();
    NS_LOG_INFO("Time " << Simulator::Now().GetSeconds()
                << "s - UE " << i
                << " Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")");
  }

  if (Simulator::Now().GetSeconds() + 1.0 <= Simulator::GetMaximumSimulationTime().GetSeconds())
  {
    Simulator::Schedule(Seconds(1.0), &LogUePositions, std::ref(ueNodes));
  }
}

// FlowMonitor analysis
void
AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper,
                   Ptr<FlowMonitor> flowMonitor,
                   double simTime)
{
  flowMonitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
  FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

  std::vector<double> latencyValues;
  std::vector<double> jitterValues;

  uint32_t flowCount = 0;
  double totalThroughputSum = 0.0;
  double totalLatencySum = 0.0;
  double totalJitterSum = 0.0;
  uint64_t totalRxPackets = 0;
  uint64_t totalTxPackets = 0;

  for (auto &iter : stats)
  {
    flowCount++;
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);

    double duration = (iter.second.timeLastRxPacket - iter.second.timeFirstTxPacket).GetSeconds();
    double throughput = 0.0;
    if (duration > 0)
    {
      throughput = (iter.second.rxBytes * 8.0) / duration;
    }
    double avgLatency = 0.0;
    double avgJitter  = 0.0;
    if (iter.second.rxPackets > 0)
    {
      avgLatency = iter.second.delaySum.GetSeconds() / iter.second.rxPackets;
      avgJitter  = iter.second.jitterSum.GetSeconds() / iter.second.rxPackets;
    }
    totalThroughputSum += throughput;
    totalLatencySum += (avgLatency * iter.second.rxPackets);
    totalJitterSum  += (avgJitter  * iter.second.rxPackets);
    totalRxPackets  += iter.second.rxPackets;
    totalTxPackets  += iter.second.txPackets;

    latencyValues.push_back(avgLatency);
    jitterValues.push_back(avgJitter);
  }

  double overallAvgLatency = (totalRxPackets > 0) ? (totalLatencySum / totalRxPackets) : 0.0;
  double overallAvgJitter  = (totalRxPackets > 0) ? (totalJitterSum / totalRxPackets) : 0.0;
  double overallAvgThroughput = (flowCount > 0) ? (totalThroughputSum / flowCount) : 0.0;
  double packetLossRate = (totalTxPackets > 0)
                          ? (double)(totalTxPackets - totalRxPackets) / totalTxPackets * 100.0
                          : 0.0;
  double overallAvgLatencyMs = overallAvgLatency * 1000.0; // Convert latency to ms
  double overallAvgJitterMs = overallAvgJitter * 1000.0;   // Convert jitter to ms

  // Time-series throughput plot
  {
    Gnuplot plot("time-series-throughput.plt");
    plot.SetTitle("Time-Series Throughput");
    plot.SetTerminal("png");
    plot.SetLegend("Time (s)", "Throughput (bps)");
    Gnuplot2dDataset dataset;
    dataset.SetTitle("Throughput Over Time");
    dataset.SetStyle(Gnuplot2dDataset::LINES_POINTS);

    for (size_t i = 0; i < g_timeSeries.size(); ++i)
    {
      dataset.Add(g_timeSeries[i], g_throughputSeries[i]);
    }
    plot.AddDataset(dataset);

    // Set the output to a PNG file in the current directory
    std::ofstream plotFile("time-series-throughput.plt");
    plotFile << "set terminal png\n";
    plotFile << "set output \"time-series-throughput.png\"\n";
    plot.GenerateOutput(plotFile);
    plotFile.close();
    NS_LOG_INFO("Time-series throughput plot generated.");
  }


  // Time-series latency plot
  {
    Gnuplot plot("time-series-latency.plt");
    plot.SetTitle("Time-Series Avg Latency");
    plot.SetTerminal("png");
    plot.SetLegend("Time (s)", "Latency (s)");
    Gnuplot2dDataset dataset;
    dataset.SetTitle("Avg Latency Over Time");
    dataset.SetStyle(Gnuplot2dDataset::LINES_POINTS);

    for (size_t i = 0; i < g_timeSeries.size(); ++i)
    {
      dataset.Add(g_timeSeries[i], g_avgLatencySeries[i]);
    }
    plot.AddDataset(dataset);

    // Set the output to a PNG file in the current directory
    std::ofstream plotFile("time-series-latency.plt");
    plotFile << "set terminal png\n";
    plotFile << "set output \"time-series-latency.png\"\n";
    plot.GenerateOutput(plotFile);
    plotFile.close();
    NS_LOG_INFO("Time-series latency plot generated.");
  }


  // CDF: Latency
  {
    std::sort(latencyValues.begin(), latencyValues.end());
    Gnuplot plot("cdf-latency.plt");
    plot.SetTitle("CDF - Latency");
    plot.SetTerminal("png");
    plot.SetLegend("Latency (s)", "Cumulative Probability");
    Gnuplot2dDataset dataset;
    dataset.SetTitle("Latency CDF");
    dataset.SetStyle(Gnuplot2dDataset::LINES);

    for (size_t i = 0; i < latencyValues.size(); ++i)
    {
      double percent = (double)(i + 1) / latencyValues.size();
      dataset.Add(latencyValues[i], percent);
    }
    plot.AddDataset(dataset);

    // Set the output to a PNG file in the current directory
    std::ofstream plotFile("cdf-latency.plt");
    plotFile << "set terminal png\n";
    plotFile << "set output \"cdf-latency.png\"\n";
    plot.GenerateOutput(plotFile);
    plotFile.close();
    NS_LOG_INFO("CDF for latency generated.");
  }


  // CDF: Jitter
  {
    std::sort(jitterValues.begin(), jitterValues.end());
    Gnuplot plot("cdf-jitter.plt");
    plot.SetTitle("CDF - Jitter");
    plot.SetTerminal("png");
    plot.SetLegend("Jitter (s)", "Cumulative Probability");
    Gnuplot2dDataset dataset;
    dataset.SetTitle("Jitter CDF");
    dataset.SetStyle(Gnuplot2dDataset::LINES);

    for (size_t i = 0; i < jitterValues.size(); ++i)
    {
      double percent = (double)(i + 1) / jitterValues.size();
      dataset.Add(jitterValues[i], percent);
    }
    plot.AddDataset(dataset);

    // Set the output to a PNG file in the current directory
    std::ofstream plotFile("cdf-jitter.plt");
    plotFile << "set terminal png\n";
    plotFile << "set output \"cdf-jitter.png\"\n";
    plot.GenerateOutput(plotFile);
    plotFile.close();
    NS_LOG_INFO("CDF for jitter generated.");
  }

  // Summaries
  NS_LOG_INFO("===== FINAL METRICS =====");
  NS_LOG_INFO("Avg Throughput: " << overallAvgThroughput / 1e6 << " Mbps");
  NS_LOG_INFO("Avg Latency   : " << overallAvgLatencyMs << " ms");
  NS_LOG_INFO("Avg Jitter    : " << overallAvgJitterMs << " ms");
  NS_LOG_INFO("Packet Loss   : " << packetLossRate << "%");


  flowMonitor->SerializeToXmlFile("flowmon.xml", true, true);
  NS_LOG_INFO("FlowMonitor results saved to XML.");

  // Simple Markdown report
  std::ofstream mdReport("simulation-report.md");
  mdReport << "# Simulation Report\n\n";
  mdReport << "**Simulation Time**: " << simTime << "s\n\n";
  mdReport << "## Final Metrics Summary\n";
  mdReport << "- **Avg Throughput**: " << overallAvgThroughput / 1e6 << " Mbps\n";
  mdReport << "- **Avg Latency**   : " << overallAvgLatencyMs << " ms\n";
  mdReport << "- **Avg Jitter**    : " << overallAvgJitterMs << " ms\n";
  mdReport << "- **Packet Loss**   : " << packetLossRate << "%\n\n";
  mdReport << "## Generated Plots\n";
  mdReport << "- time-series-throughput.plt (png)\n";
  mdReport << "- time-series-latency.plt (png)\n";
  mdReport << "- cdf-latency.plt (png)\n";
  mdReport << "- cdf-jitter.plt (png)\n\n";
  mdReport << "(Use gnuplot filename.plt to convert .plt files to .png.)\n";
  mdReport.close();
  NS_LOG_INFO("Markdown report generated: simulation-report.md");
}
