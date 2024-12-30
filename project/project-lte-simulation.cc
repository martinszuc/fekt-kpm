/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Enhanced LTE Network Simulation (NS-3.39)
 * Demonstrates eNodeB + UE setup, QoS, Cost231, RandomWayPoint, FlowMonitor, etc.
 * Author: Martin Szuc
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/lte-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/cost231-propagation-loss-model.h" // For Cost231PropagationLossModel

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EnhancedLteSimulation");

// Configure QoS: prioritize voice traffic (QCI=1)
void ConfigureQoS()
{
  Config::SetDefault("ns3::LteBearersManager::DefaultQci", UintegerValue(1));
}

// Optionally enable Carrier Aggregation (two component carriers)
void EnableCarrierAggregation(Ptr<LteHelper> lteHelper)
{
  Config::SetDefault("ns3::LteHelper::UseCa", BooleanValue(true));
  Config::SetDefault("ns3::LteHelper::NumberOfComponentCarriers", UintegerValue(2));
  Config::SetDefault("ns3::LteHelper::EnbComponentCarrierManager", StringValue("ns3::RrComponentCarrierManager"));
}

// Configure Cost231 path loss for suburban environment
void ConfigurePathLossModel(Ptr<LteHelper> lteHelper)
{
  Ptr<PropagationLossModel> lossModel = CreateObject<Cost231PropagationLossModel>();
  lteHelper->SetAttribute("PathlossModel", PointerValue(lossModel));
}

// Enable PCAP & ASCII tracing on LTE devices and the P2P link
void EnableTracing(Ptr<LteHelper> lteHelper, PointToPointHelper &p2p)
{
  lteHelper->EnablePcapAll("enhanced-lte");    // pcap for LTE
  p2p.EnablePcapAll("enhanced-lte-p2p");       // pcap for P2P link

  AsciiTraceHelper ascii;
  lteHelper->EnableAsciiAll(ascii.CreateFileStream("enhanced-lte.tr")); // ASCII
}

// Install FlowMonitor to track throughput, delay, packet loss, etc.
Ptr<FlowMonitor> SetupFlowMonitor()
{
  FlowMonitorHelper flowHelper;
  return flowHelper.InstallAll();
}

// Use NetAnim to visualize nodes and movement
void SetupAnimation(NodeContainer enbNodes, NodeContainer ueNodes, Ptr<Node> pgw, Ptr<Node> remoteHost)
{
  AnimationInterface anim("enhanced-lte.xml");
  anim.SetMobilityPollInterval(Seconds(1.0));

  // Label PGW & remote host
  anim.UpdateNodeDescription(pgw, "PGW");
  anim.UpdateNodeDescription(remoteHost, "RemoteHost");

  // eNodeBs: green
  for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
  {
    anim.UpdateNodeDescription(enbNodes.Get(i), "eNodeB_" + std::to_string(i+1));
    anim.UpdateNodeColor(enbNodes.Get(i), 0, 255, 0);
  }

  // UEs: blue
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    anim.UpdateNodeDescription(ueNodes.Get(i), "UE_" + std::to_string(i+1));
    anim.UpdateNodeColor(ueNodes.Get(i), 0, 0, 255);
  }

  // Fix PGW & remote host in place for clarity
  anim.SetConstantPosition(pgw, -500, 0);
  anim.SetConstantPosition(remoteHost, -600, 0);
}

// Simple UDP echo traffic (voice-like)
void SetupUdpEchoTraffic(NodeContainer ueNodes, Ptr<Node> remoteHost, Ipv4Address hostAddr,
                         uint16_t port, double startTime, double stopTime)
{
  // Server on remote host
  UdpEchoServerHelper echoServer(port);
  ApplicationContainer serverApps = echoServer.Install(remoteHost);
  serverApps.Start(Seconds(startTime));
  serverApps.Stop(Seconds(stopTime));

  // Client on UEs
  UdpEchoClientHelper echoClient(hostAddr, port);
  echoClient.SetAttribute("MaxPackets", UintegerValue(10000));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(512)); // smaller packet size for voice

  ApplicationContainer clientApps;
  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
  {
    clientApps.Add(echoClient.Install(ueNodes.Get(i)));
  }
  clientApps.Start(Seconds(startTime + 1.0));
  clientApps.Stop(Seconds(stopTime));
}

// Print FlowMonitor results (throughput, delay, packet loss)
void AnalyzeData(Ptr<FlowMonitor> monitor)
{
  monitor->CheckForLostPackets();
  auto classifier = DynamicCast<Ipv4FlowClassifier>(FlowMonitorHelper().GetClassifier());
  auto stats = monitor->GetFlowStats();

  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    double timeFirst = flow.second.timeFirstTxPacket.GetSeconds();
    double timeLast = flow.second.timeLastRxPacket.GetSeconds();
    double duration = (timeLast - timeFirst);

    double rxBytes = flow.second.rxBytes * 8.0; // bits
    double throughputKbps = (rxBytes / duration) / 1024.0;

    double meanDelayMs = (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000.0;
    double lossRatio = 0.0;
    if (flow.second.txPackets > 0)
    {
      lossRatio = (double)(flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets * 100.0;
    }

    std::cout << "Flow ID: " << flow.first
              << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
              << "  TxPackets: " << flow.second.txPackets
              << "  RxPackets: " << flow.second.rxPackets << "\n"
              << "  Throughput: " << throughputKbps << " kbps\n"
              << "  Mean Delay: " << meanDelayMs << " ms\n"
              << "  Packet Loss: " << lossRatio << " %\n\n";
  }
  monitor->SerializeToXmlFile("flow-results.xml", true, true);
}

int main(int argc, char *argv[])
{
  // Defaults
  uint16_t numEnbs = 2;       // At least 2 eNodeBs
  uint16_t numUes = 5;        // At least 5 UEs
  double   simTime = 20.0;    // Simulation time
  bool     useCa = false;     // Carrier Aggregation toggle

  CommandLine cmd(__FILE__);
  cmd.AddValue("numEnbs",  "Number of eNodeBs", numEnbs);
  cmd.AddValue("numUes",   "Number of UEs", numUes);
  cmd.AddValue("simTime",  "Total simulation time (s)", simTime);
  cmd.AddValue("useCa",    "Enable carrier aggregation", useCa);
  cmd.Parse(argc, argv);

  LogComponentEnable("EnhancedLteSimulation", LOG_LEVEL_INFO);

  // Create LTE + EPC
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);
  epcHelper->Initialize();

  // QoS for voice
  ConfigureQoS();

  // Optional carrier aggregation
  if (useCa)
  {
    EnableCarrierAggregation(lteHelper);
  }

  // Path loss for suburban
  ConfigurePathLossModel(lteHelper);

  // Create eNodeBs + UEs
  NodeContainer enbNodes, ueNodes;
  enbNodes.Create(numEnbs);
  ueNodes.Create(numUes);

  // Mobility
  MobilityHelper mobility;
  // eNodeBs stationary
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(enbNodes);

  // UEs random waypoint in a 500x500 region
  mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"),
                                "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"));
  mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                            "Speed",  StringValue("ns3::UniformRandomVariable[Min=1|Max=10]"),
                            "Pause",  StringValue("ns3::ConstantRandomVariable[Constant=0]"),
                            "Bounds", RectangleValue(Rectangle(0, 500, 0, 500)));
  mobility.Install(ueNodes);

  // Install LTE devices
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

  // Attach UEs to eNodeBs
  for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
  {
    lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % enbDevs.GetN()));
  }

  // Internet stack on UEs
  InternetStackHelper stack;
  stack.Install(ueNodes);

  // Assign IP addresses to UEs
  epcHelper->AssignUeIpv4Address(ueDevs);

  // Default route for UEs
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
  {
    Ptr<Node> ue = ueNodes.Get(i);
    Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ue->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }

  // Remote host + PGW link
  Ptr<Node> pgw = epcHelper->GetPgwNode();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  stack.Install(remoteHostContainer);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));
  NetDeviceContainer internetDevices = p2p.Install(pgw, remoteHost);

  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer ifaces = ipv4h.Assign(internetDevices);
  Ipv4Address remoteHostAddr = ifaces.GetAddress(1);

  // Route on remote host => route to UE subnets
  Ptr<Ipv4StaticRouting> remoteHostStatic = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStatic->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // Tracing
  EnableTracing(lteHelper, p2p);

  // FlowMonitor
  Ptr<FlowMonitor> monitor = SetupFlowMonitor();

  // NetAnim
  SetupAnimation(enbNodes, ueNodes, pgw, remoteHost);

  // Simple UDP echo simulating voice
  uint16_t echoPort = 9999;
  SetupUdpEchoTraffic(ueNodes, remoteHost, remoteHostAddr, echoPort, 1.0, simTime);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Results
  AnalyzeData(monitor);

  Simulator::Destroy();
  return 0;
}
