/**
 * @file lte-voip-simulation.cc
 * @brief LTE simulation with VoIP traffic, FlowMonitor, PCAP, and Gnuplot (compatible with ns-3.39).
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

/**
 * @brief Configure logging levels.
 */
void ConfigureLogging()
{
  LogComponentEnable("LTEVoipSimulation", LOG_LEVEL_INFO);
  LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
}

/**
 * @brief Configure eNodeB mobility (fixed positions).
 */
void ConfigureEnbMobility(NodeContainer &enbNodes)
{
  MobilityHelper enbMobility;
  Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
  posAlloc->Add(Vector(0.0, 100.0, 30.0));   // eNodeB 0 at 30m height
  posAlloc->Add(Vector(500.0, 100.0, 30.0)); // eNodeB 1 at 30m height
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

  // Set height of UEs (Z-axis)
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    Ptr<MobilityModel> mobility = ueNodes.Get(i)->GetObject<MobilityModel>();
    Vector position = mobility->GetPosition();
    position.z = 1.5; // Set height to 1.5m
    mobility->SetPosition(position);
  }
}


/**
 * @brief Create and connect a remote host to the PGW via a point-to-point link.
 */
Ipv4Address CreateRemoteHost(Ptr<PointToPointEpcHelper> epcHelper, NodeContainer &remoteHostContainer)
{
  // PGW node
  Ptr<Node> pgw = epcHelper->GetPgwNode();

  // Create point-to-point link between remote host and PGW
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));

  NetDeviceContainer devices = p2p.Install(pgw, remoteHostContainer.Get(0));

  // Assign IP
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("1.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

  return interfaces.GetAddress(1); // the remote host's IP
}

/**
 * @brief Install VoIP-like OnOff applications on UEs to send traffic to the remote host.
 */
void InstallVoipApplications(NodeContainer &ueNodes, Ipv4Address remoteAddr, double simTime)
{
  uint16_t port = 5000; // Destination port
  OnOffHelper onOff("ns3::UdpSocketFactory", InetSocketAddress(remoteAddr, port));
  // Typical voice/VoIP parameters
  onOff.SetAttribute("DataRate", StringValue("64kbps"));
  onOff.SetAttribute("PacketSize", UintegerValue(160));
  onOff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  onOff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

  // Install on all UEs
  ApplicationContainer apps = onOff.Install(ueNodes);
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(simTime));

  // If desired, also install a sink (UdpServer or PacketSink) on the remote host
  // to receive these packets. But since it's "the Internet," we'll keep it simple.
}

/**
 * @brief Enable FlowMonitor to collect throughput, delay, packet loss, etc.
 */
Ptr<FlowMonitor> SetupFlowMonitor(FlowMonitorHelper &flowHelper)
{
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
  return flowMonitor;
}

/**
 * @brief Write FlowMonitor stats to file and optionally plot with Gnuplot.
 */
void AnalyzeFlowMonitor(FlowMonitorHelper &flowHelper, Ptr<FlowMonitor> flowMonitor, std::string filePrefix)
{
  flowMonitor->CheckForLostPackets();

  // Instead of flowMonitor->GetClassifier(), we use flowHelper.GetClassifier()
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
  FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();

  // Write to .flowmon file
  flowMonitor->SerializeToXmlFile(filePrefix + ".flowmon", true, true);

  // Example Gnuplot for throughput
  Gnuplot gnuplot(filePrefix + "-throughput.png");
  gnuplot.SetTitle("Throughput vs. Flow ID");
  gnuplot.SetTerminal("png");
  gnuplot.SetLegend("Flow ID", "Throughput (bps)");
  Gnuplot2dDataset dataset;
  dataset.SetStyle(Gnuplot2dDataset::POINTS);

  for (auto &iter : stats)
  {
    double timeFirstTx = iter.second.timeFirstTxPacket.GetSeconds();
    double timeLastRx  = iter.second.timeLastRxPacket.GetSeconds();
    double throughputBps = 0.0;
    if (timeLastRx - timeFirstTx > 0)
    {
      throughputBps = (iter.second.rxBytes * 8.0) / (timeLastRx - timeFirstTx);
    }
    dataset.Add(iter.first, throughputBps);
  }

  gnuplot.AddDataset(dataset);
  std::ofstream plotFile(filePrefix + "-throughput.plt");
  gnuplot.GenerateOutput(plotFile);
  plotFile.close();
}

/**
 * @brief Main function for LTE + VoIP simulation.
 */
int main(int argc, char *argv[])
{
  uint16_t numEnb = 2;
  uint16_t numUe = 5;
  double simTime = 10.0; // seconds
  std::string outputDir = "lte-voip-output";

  CommandLine cmd;
  cmd.AddValue("numEnb", "Number of eNodeBs", numEnb);
  cmd.AddValue("numUe", "Number of UEs", numUe);
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("outputDir", "Directory to save output files", outputDir);
  cmd.Parse(argc, argv);

  // Optional: create directory
  system(("mkdir -p " + outputDir).c_str());

  ConfigureLogging();

  // Create nodes
  NodeContainer enbNodes;
  enbNodes.Create(numEnb);

  NodeContainer ueNodes;
  ueNodes.Create(numUe);

  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);

  // Create LTE & EPC helpers
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);

  // Use a Suburban OkumuraHata path loss model
  // Must use TypeId::LookupByName in older ns-3
  lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::OkumuraHataPropagationLossModel"));
  lteHelper->SetPathlossModelAttribute("Environment", StringValue("SubUrban"));
  lteHelper->SetPathlossModelAttribute("Frequency", DoubleValue(1800.0));

  // Configure Mobility
  ConfigureEnbMobility(enbNodes);
  ConfigureUeMobility(ueNodes);

  // Install LTE devices
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

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

  // Create remote host link and retrieve IP
  Ipv4Address remoteHostAddr = CreateRemoteHost(epcHelper, remoteHostContainer);

  // Install VoIP apps on UEs sending to remote host
  InstallVoipApplications(ueNodes, remoteHostAddr, simTime);

  // Enable PCAP
  lteHelper->EnableTraces();

  // FlowMonitor setup
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = SetupFlowMonitor(flowHelper);

  // Run simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Collect & analyze FlowMonitor stats
  AnalyzeFlowMonitor(flowHelper, flowMonitor, outputDir + "/voip-stats");

  Simulator::Destroy();
  NS_LOG_INFO("VoIP simulation finished!");
  return 0;
}
