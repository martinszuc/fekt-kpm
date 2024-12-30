/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * LTE Network Simulation (NS-3.39)
 * Demonstrates:
 *   - Multiple eNodeBs (>=2) and UEs (>=5)
 *   - Separate position allocators for eNodeBs, UEs
 *   - RandomWaypointMobilityModel (no invalid Bounds attribute)
 *   - Cost231PropagationLossModel for suburban
 *   - Dedicated EPS bearer (QCI=1) for voice
 *   - FlowMonitor for throughput, delay, packet loss
 *   - NetAnim visualization
 *   - PCAP tracing on P2P link
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

NS_LOG_COMPONENT_DEFINE("LteFinalAssignment");

// (1) Activate QCI=1 bearer on UEs (voice priority)
void
ActivateVoiceQciOneBearer(Ptr<LteHelper> lteHelper, NetDeviceContainer ueDevices)
{
  EpsBearer voiceBearer(EpsBearer::GBR_CONV_VOICE); // QCI=1
  voiceBearer.arp.priorityLevel = 1;
  voiceBearer.arp.preemptionCapability = true;
  voiceBearer.arp.preemptionVulnerability = false;

  for (uint32_t i = 0; i < ueDevices.GetN(); ++i)
  {
    lteHelper->ActivateDedicatedEpsBearer(ueDevices.Get(i), voiceBearer, EpcTft::Default());
  }
}

// (2) Optional Carrier Aggregation (two component carriers)
void
EnableCarrierAggregation(Ptr<LteHelper> lteHelper)
{
  Config::SetDefault("ns3::LteHelper::UseCa", BooleanValue(true));
  Config::SetDefault("ns3::LteHelper::NumberOfComponentCarriers", UintegerValue(2));
  Config::SetDefault("ns3::LteHelper::EnbComponentCarrierManager", StringValue("ns3::RrComponentCarrierManager"));
}

// (3) Configure Cost231 path loss (via TypeId lookup)
void
ConfigureCost231Pathloss(Ptr<LteHelper> lteHelper)
{
  TypeId cost231Id = TypeId::LookupByName("ns3::Cost231PropagationLossModel");
  lteHelper->SetPathlossModelType(cost231Id);
}

// (4) PCAP tracing on the P2P link
void
EnableP2pTracing(PointToPointHelper &p2p, NetDeviceContainer devices)
{
  p2p.EnablePcap("lte-project-p2p", devices.Get(0), false);
  p2p.EnablePcap("lte-project-p2p", devices.Get(1), false);
}

// (5) FlowMonitor for throughput, delay, packet loss
Ptr<FlowMonitor>
SetupFlowMonitor()
{
  FlowMonitorHelper flowHelper;
  return flowHelper.InstallAll();
}

// (6) NetAnim setup
void
SetupNetAnim(NodeContainer enbNodes, NodeContainer ueNodes,
             Ptr<Node> pgw, Ptr<Node> remoteHost)
{
  AnimationInterface anim("lte-project.xml");
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

  // Fix PGW & remote host positions for clarity
  anim.SetConstantPosition(pgw, -500, 0);
  anim.SetConstantPosition(remoteHost, -600, 0);
}

// (7) Simple UDP echo (voice-like)
void
SetupUdpEchoVoice(NodeContainer ueNodes, Ptr<Node> remoteHost,
                  Ipv4Address hostAddr, uint16_t port,
                  double startTime, double stopTime)
{
  // Server on remote host
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

// (8) Print FlowMonitor results
void
AnalyzeFlowMonitor(Ptr<FlowMonitor> monitor)
{
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(FlowMonitorHelper().GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    double start  = flow.second.timeFirstTxPacket.GetSeconds();
    double finish = flow.second.timeLastRxPacket.GetSeconds();
    double duration = finish - start;

    double rxBits = flow.second.rxBytes * 8.0;
    double throughputKbps = (duration > 0) ? (rxBits / duration / 1024.0) : 0.0;

    double meanDelayMs = 0.0;
    if (flow.second.rxPackets > 0)
    {
      meanDelayMs = (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000.0;
    }

    double lossPct = 0.0;
    if (flow.second.txPackets > 0)
    {
      lossPct = (flow.second.txPackets - flow.second.rxPackets)
                / (double)flow.second.txPackets * 100.0;
    }

    std::cout << "Flow ID: " << flow.first
              << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
              << "  TxPackets: " << flow.second.txPackets
              << "  RxPackets: " << flow.second.rxPackets << "\n"
              << "  Throughput: " << throughputKbps << " kbps\n"
              << "  Mean Delay: " << meanDelayMs << " ms\n"
              << "  Packet Loss: " << lossPct << " %\n\n";
  }
  monitor->SerializeToXmlFile("flow-results.xml", true, true);
}

// -----------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------
int
main(int argc, char *argv[])
{
  // Default config
  uint16_t numEnbs  = 2;    // eNodeBs
  uint16_t numUes   = 5;    // UEs
  double   simTime  = 20;   // seconds
  bool     useCa    = false;

  CommandLine cmd(__FILE__);
  cmd.AddValue("numEnbs",  "Number of eNodeBs", numEnbs);
  cmd.AddValue("numUes",   "Number of UEs",     numUes);
  cmd.AddValue("simTime",  "Simulation time",   simTime);
  cmd.AddValue("useCa",    "Enable CA",         useCa);
  cmd.Parse(argc, argv);

  LogComponentEnable("LteFinalAssignment", LOG_LEVEL_INFO);

  // A. Create LTE + EPC
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);
  epcHelper->Initialize();

  // B. Optional CA
  if (useCa)
  {
    EnableCarrierAggregation(lteHelper);
  }

  // C. Suburban path-loss
  ConfigureCost231Pathloss(lteHelper);

  // D. Create nodes
  NodeContainer enbNodes, ueNodes;
  enbNodes.Create(numEnbs);
  ueNodes.Create(numUes);

  // E. eNodeBs: distinct position allocator + constant mobility
  MobilityHelper enbMobility;
  Ptr<ListPositionAllocator> enbPosAlloc = CreateObject<ListPositionAllocator>();
  for (uint16_t i = 0; i < numEnbs; ++i)
  {
    // Place each eNodeB in a line, spaced out
    enbPosAlloc->Add(Vector(100.0 * i, 200.0, 0.0));
  }
  enbMobility.SetPositionAllocator(enbPosAlloc);
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.Install(enbNodes);

  // F. UEs: separate position allocator + RandomWaypoint (no invalid Bounds)
  MobilityHelper ueMobility;
  ueMobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                  "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"),
                                  "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"));
  ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                              "Speed",  StringValue("ns3::UniformRandomVariable[Min=1.0|Max=10.0]"),
                              "Pause",  StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
  ueMobility.Install(ueNodes);

  // G. LTE devices
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

  // H. Install IP stack on UEs
  InternetStackHelper ipStack;
  ipStack.Install(ueNodes);

  // I. Assign IP addresses after UE devices are created
  epcHelper->AssignUeIpv4Address(ueDevs);

  // J. Default route for UEs
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  for (uint32_t i = 0; i < ueNodes.GetN(); i++)
  {
    Ptr<Ipv4StaticRouting> ueStatic =
        ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(i)->GetObject<Ipv4>());
    ueStatic->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }

  // K. Attach UEs to eNodeBs (round-robin)
  for (uint32_t i = 0; i < ueDevs.GetN(); ++i)
  {
    lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % enbDevs.GetN()));
  }

  // L. PGW <-> Remote Host link
  Ptr<Node> pgw = epcHelper->GetPgwNode();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  ipStack.Install(remoteHostContainer);

  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));
  NetDeviceContainer p2pDevices = p2p.Install(pgw, remoteHost);

  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer ifaces = ipv4h.Assign(p2pDevices);
  Ipv4Address remoteHostAddr = ifaces.GetAddress(1);

  // M. Route from remote host to UEs
  Ptr<Ipv4StaticRouting> remoteHostStatic =
      ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStatic->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                      Ipv4Mask("255.0.0.0"),
                                      1);

  // N. PCAP on P2P
  EnableP2pTracing(p2p, p2pDevices);

  // O. FlowMonitor
  Ptr<FlowMonitor> monitor = SetupFlowMonitor();

  // P. NetAnim
  SetupNetAnim(enbNodes, ueNodes, pgw, remoteHost);

  // Q. Activate QCI=1 for voice (after IP is assigned + attach done)
  ActivateVoiceQciOneBearer(lteHelper, ueDevs);

  // R. Voice-like UDP Echo (start=1s, end=simTime)
  SetupUdpEchoVoice(ueNodes, remoteHost, remoteHostAddr, 9999, 1.0, simTime);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // FlowMonitor analysis
  AnalyzeFlowMonitor(monitor);

  Simulator::Destroy();
  return 0;
}
