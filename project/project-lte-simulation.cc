/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Enhanced LTE Network Simulation (NS-3.39)
 * Demonstrates:
 *  - eNodeB + UE setup
 *  - Separate position allocators (one for eNodeBs, one for UEs)
 *  - RandomWayPoint Mobility for UEs
 *  - Cost231 Path Loss (suburban)
 *  - Dedicated EPS bearer with QCI=1 for voice
 *  - FlowMonitor for throughput, delay, packet loss
 *  - NetAnim visualization
 *  - PCAP tracing on P2P link (PGW <-> RemoteHost)
 *
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
#include "ns3/cost231-propagation-loss-model.h"

using namespace ns3;

// Logging for debugging
NS_LOG_COMPONENT_DEFINE("EnhancedLteSimulation");

// (1) Instead of setting "ns3::LteBearersManager::DefaultQci" via Config::SetDefault,
//     we explicitly create & activate a dedicated bearer with QCI=1 after attaching.

void ActivateVoiceBearer(Ptr<LteHelper> lteHelper, NetDeviceContainer ueDevs)
{
  // EpsBearer::GBR_CONV_VOICE typically corresponds to QCI=1
  EpsBearer voiceBearer(EpsBearer::GBR_CONV_VOICE);
  voiceBearer.arp.priorityLevel = 1;
  voiceBearer.arp.preemptionCapability = true;
  voiceBearer.arp.preemptionVulnerability = false;

  // Activate on each UE device
  for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
  {
    Ptr<NetDevice> ueDevice = ueDevs.Get(i);
    lteHelper->ActivateDedicatedEpsBearer(ueDevice, voiceBearer, EpcTft::Default());
  }
}

// (2) Optional Carrier Aggregation (two component carriers)
void EnableCarrierAggregation(Ptr<LteHelper> lteHelper)
{
  Config::SetDefault("ns3::LteHelper::UseCa", BooleanValue(true));
  Config::SetDefault("ns3::LteHelper::NumberOfComponentCarriers", UintegerValue(2));
  Config::SetDefault("ns3::LteHelper::EnbComponentCarrierManager", StringValue("ns3::RrComponentCarrierManager"));
}

// (3) Cost231 for suburban path-loss
void ConfigurePathLossModel(Ptr<LteHelper> lteHelper)
{
  Ptr<PropagationLossModel> lossModel = CreateObject<Cost231PropagationLossModel>();
  lteHelper->SetAttribute("PathlossModel", PointerValue(lossModel));
}

// (4) Enable PCAP on the P2P link
void EnableP2pTracing(PointToPointHelper &p2p, NetDeviceContainer devices)
{
  p2p.EnablePcap("enhanced-lte-p2p", devices.Get(0), false);
  p2p.EnablePcap("enhanced-lte-p2p", devices.Get(1), false);
}

// (5) FlowMonitor for stats
Ptr<FlowMonitor> SetupFlowMonitor()
{
  FlowMonitorHelper flowHelper;
  return flowHelper.InstallAll();
}

// (6) NetAnim: node layout & color
void SetupAnimation(NodeContainer enbNodes, NodeContainer ueNodes, Ptr<Node> pgw, Ptr<Node> remoteHost)
{
  AnimationInterface anim("enhanced-lte.xml");
  anim.SetMobilityPollInterval(Seconds(1));

  anim.UpdateNodeDescription(pgw, "PGW");
  anim.UpdateNodeDescription(remoteHost, "RemoteHost");

  // eNodeBs => green
  for (uint32_t i = 0; i < enbNodes.GetN(); ++i)
  {
    anim.UpdateNodeDescription(enbNodes.Get(i), "eNodeB_" + std::to_string(i + 1));
    anim.UpdateNodeColor(enbNodes.Get(i), 0, 255, 0);
  }
  // UEs => blue
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    anim.UpdateNodeDescription(ueNodes.Get(i), "UE_" + std::to_string(i + 1));
    anim.UpdateNodeColor(ueNodes.Get(i), 0, 0, 255);
  }

  // Fix PGW & remote host in place for clarity
  anim.SetConstantPosition(pgw, -500, 0);
  anim.SetConstantPosition(remoteHost, -600, 0);
}

// (7) Simple UDP Echo for voice-like traffic
void SetupUdpEchoTraffic(NodeContainer ueNodes, Ptr<Node> remoteHost, Ipv4Address hostAddr,
                         uint16_t port, double startTime, double stopTime)
{
  // Server on remoteHost
  UdpEchoServerHelper echoServer(port);
  ApplicationContainer serverApps = echoServer.Install(remoteHost);
  serverApps.Start(Seconds(startTime));
  serverApps.Stop(Seconds(stopTime));

  // Clients on UEs
  UdpEchoClientHelper echoClient(hostAddr, port);
  echoClient.SetAttribute("MaxPackets", UintegerValue(10000));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(512));

  ApplicationContainer clientApps;
  for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
  {
    clientApps.Add(echoClient.Install(ueNodes.Get(i)));
  }
  clientApps.Start(Seconds(startTime + 1.0));
  clientApps.Stop(Seconds(stopTime));
}

// (8) FlowMonitor results: throughput, delay, packet loss
void AnalyzeData(Ptr<FlowMonitor> monitor)
{
  monitor->CheckForLostPackets();
  auto classifier = DynamicCast<Ipv4FlowClassifier>(FlowMonitorHelper().GetClassifier());
  auto stats = monitor->GetFlowStats();

  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    double start  = flow.second.timeFirstTxPacket.GetSeconds();
    double finish = flow.second.timeLastRxPacket.GetSeconds();
    double duration = finish - start;

    double rxBits = flow.second.rxBytes * 8.0;
    double throughputKbps = (duration > 0) ? (rxBits / duration / 1024.0) : 0.0;

    double meanDelayMs = (flow.second.rxPackets > 0)
      ? ((flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000.0)
      : 0.0;

    double lossPct = 0.0;
    if (flow.second.txPackets > 0)
    {
      lossPct = (flow.second.txPackets - flow.second.rxPackets) / (double)flow.second.txPackets * 100.0;
    }

    std::cout << "Flow ID: " << flow.first << " ("
              << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
              << "  TxPackets: " << flow.second.txPackets
              << "  RxPackets: " << flow.second.rxPackets << "\n"
              << "  Throughput: " << throughputKbps << " kbps\n"
              << "  Mean Delay: " << meanDelayMs << " ms\n"
              << "  Packet Loss: " << lossPct << " %\n\n";
  }
  monitor->SerializeToXmlFile("flow-results.xml", true, true);
}

// Main
int main(int argc, char *argv[])
{
  // Default settings
  uint16_t numEnbs  = 2;   // eNodeBs
  uint16_t numUes   = 5;   // UEs
  double   simTime  = 20;  // seconds
  bool     useCa    = false; // Toggle carrier aggregation

  CommandLine cmd(__FILE__);
  cmd.AddValue("numEnbs",  "Number of eNodeBs", numEnbs);
  cmd.AddValue("numUes",   "Number of UEs", numUes);
  cmd.AddValue("simTime",  "Simulation time (s)", simTime);
  cmd.AddValue("useCa",    "Enable Carrier Aggregation", useCa);
  cmd.Parse(argc, argv);

  // Optional debug logs
  LogComponentEnable("EnhancedLteSimulation", LOG_LEVEL_INFO);

  // Create LTE + EPC
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);
  epcHelper->Initialize();

  // Carrier Aggregation if toggled
  if (useCa)
  {
    EnableCarrierAggregation(lteHelper);
  }

  // Suburban path loss
  ConfigurePathLossModel(lteHelper);

  // Create eNodeB & UE nodes
  NodeContainer enbNodes;
  enbNodes.Create(numEnbs);
  NodeContainer ueNodes;
  ueNodes.Create(numUes);

  // eNodeBs: simple list placement + constant
  MobilityHelper enbMobility;
  Ptr<ListPositionAllocator> enbPosAlloc = CreateObject<ListPositionAllocator>();
  for (uint16_t i = 0; i < numEnbs; i++)
  {
    enbPosAlloc->Add(Vector(100.0 * i, 200.0, 0.0));
  }
  enbMobility.SetPositionAllocator(enbPosAlloc);
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.Install(enbNodes);

  // UEs: random waypoint in 500x500
  MobilityHelper ueMobility;
  ueMobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                  "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"),
                                  "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"));
  ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                              "Speed",  StringValue("ns3::UniformRandomVariable[Min=1|Max=10]"),
                              "Pause",  StringValue("ns3::ConstantRandomVariable[Constant=0]"),
                              "Bounds", RectangleValue(Rectangle(0, 500, 0, 500)));
  ueMobility.Install(ueNodes);

  // LTE devices
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

  // Attach UEs round-robin
  for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
  {
    lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % enbDevs.GetN()));
  }

  // IP stack on UEs
  InternetStackHelper stack;
  stack.Install(ueNodes);

  // Assign IP to UEs
  epcHelper->AssignUeIpv4Address(ueDevs);

  // Default gateway
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
  {
    Ptr<Ipv4StaticRouting> ueStatic = ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(i)->GetObject<Ipv4>());
    ueStatic->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }

  // PGW <-> RemoteHost link
  Ptr<Node> pgw = epcHelper->GetPgwNode();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  stack.Install(remoteHostContainer);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));
  NetDeviceContainer p2pDevices = p2p.Install(pgw, remoteHost);

  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer ifaces = ipv4h.Assign(p2pDevices);
  Ipv4Address remoteHostAddr = ifaces.GetAddress(1);

  // Route on remote host => reach 7.0.0.0 for UEs
  Ptr<Ipv4StaticRouting> remoteHostStatic = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStatic->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // PCAP tracing
  EnableP2pTracing(p2p, p2pDevices);

  // FlowMonitor
  Ptr<FlowMonitor> monitor = SetupFlowMonitor();

  // NetAnim
  SetupAnimation(enbNodes, ueNodes, pgw, remoteHost);

  // (IMPORTANT) Activate dedicated voice bearer (QCI=1) for each UE
  ActivateVoiceBearer(lteHelper, ueDevs);

  // Voice-like UDP echo
  SetupUdpEchoTraffic(ueNodes, remoteHost, remoteHostAddr, 9999, 1.0, simTime);

  // Run
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // FlowMonitor results
  AnalyzeData(monitor);

  Simulator::Destroy();
  return 0;
}
