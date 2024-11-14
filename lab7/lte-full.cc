/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Modified by: [Your Name]
 */

#include <fstream>
#include <string>

#include "ns3/lte-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store-module.h"

#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/data-rate.h"
#include "ns3/gnuplot.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("lte-full-modified");

int main(int argc, char *argv[])
{
  // *** Modification 1: Increased number of eNodeBs and UEs ***
  uint16_t numberOfNodes = 15;     // Increased from 5
  uint16_t numberOf_eNodeBs = 3;   // Increased from 1
  double simTime = 30.0;           // *** Modification 2: Increased simulation time ***

  double distance = 500.0;         // Increased distance between eNodeBs for wider coverage
  double interPacketInterval = 100;
  bool useCa = true;               // Carrier Aggregation enabled

  // Command line arguments
  CommandLine cmd;
  cmd.AddValue("numberOfNodes", "Number of UE nodes", numberOfNodes);
  cmd.AddValue("numberOf_eNodeBs", "Number of eNodeB nodes", numberOf_eNodeBs);
  cmd.AddValue("simTime", "Total duration of the simulation [s]", simTime);
  cmd.AddValue("distance", "Distance between eNBs [m]", distance);
  cmd.AddValue("interPacketInterval", "Inter packet interval [ms]", interPacketInterval);
  cmd.AddValue("useCa", "Whether to use carrier aggregation.", useCa);
  cmd.Parse(argc, argv);

  if (useCa)
  {
    Config::SetDefault("ns3::LteHelper::UseCa", BooleanValue(useCa));
    Config::SetDefault("ns3::LteHelper::NumberOfComponentCarriers", UintegerValue(2));
    Config::SetDefault("ns3::LteHelper::EnbComponentCarrierManager", StringValue("ns3::RrComponentCarrierManager"));
  }

  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults();

  // Parse again so you can override default values from the command line
  cmd.Parse(argc, argv);

  // *** Modification 6: Configure QoS parameters ***
  // Configure different QoS classes
  // The following line is commented out because it causes an error in ns-3.39
  // Config::SetDefault("ns3::LteUeRrc::RrcConnectionReleaseOnIdle", BooleanValue(false));

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);

  // Set eNodeB bandwidth settings *** Modification 7 ***
  lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(50)); // Increased downlink bandwidth
  lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(50)); // Increased uplink bandwidth

  Ptr<Node> pgw = epcHelper->GetPgwNode();

  // Create Remote Host
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // Create Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
  p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
  p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));
  NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
      ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                             Ipv4Mask("255.0.0.0"), 1);

  NodeContainer ueNodes;
  NodeContainer enbNodes;
  enbNodes.Create(numberOf_eNodeBs); // Create multiple eNB nodes
  ueNodes.Create(numberOfNodes);     // Create increased number of UEs

  // Install Mobility Model *** Modification 3 ***
  MobilityHelper mobility;

  // Configure Mobility for eNodeBs (Constant Position)
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  // Position eNodeBs in a grid based on distance
  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
  for (uint16_t i = 0; i < numberOf_eNodeBs; ++i)
  {
    enbPositionAlloc->Add(Vector(distance * i, 0, 0));
  }
  mobility.SetPositionAllocator(enbPositionAlloc);
  mobility.Install(enbNodes);

  // Configure Mobility for UEs (Random Walk)
  mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                            "Bounds", RectangleValue(Rectangle(-1000, 1000, -1000, 1000)),
                            "Speed", StringValue("ns3::UniformRandomVariable[Min=1|Max=5]"),
                            "Distance", DoubleValue(100.0));
  mobility.Install(ueNodes);

  // Install LTE Devices to the nodes
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

  // Install the IP stack on the UEs
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevs));

  // Set default gateway for UEs
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<Node> ueNode = ueNodes.Get(u);
    Ptr<Ipv4StaticRouting> ueStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }

  // Attach UEs to eNodeBs in a round-robin fashion
  for (uint16_t i = 0; i < numberOfNodes; i++)
  {
    lteHelper->Attach(ueLteDevs.Get(i), enbLteDevs.Get(i % numberOf_eNodeBs));
  }

  // *** Modification 4: Change traffic pattern to UDP ***
  // Create UdpEchoServer on remote host
  uint16_t udpPort = 8080;
  UdpEchoServerHelper echoServer(udpPort);
  ApplicationContainer serverApps = echoServer.Install(remoteHost);
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(simTime));

  // Create UdpEchoClient on UEs
  UdpEchoClientHelper echoClient(remoteHostAddr, udpPort);
  echoClient.SetAttribute("MaxPackets", UintegerValue(10000));
  echoClient.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
  echoClient.SetAttribute("PacketSize", UintegerValue(1024));

  ApplicationContainer clientApps;
  for (uint16_t i = 0; i < numberOfNodes; i++)
  {
    clientApps.Add(echoClient.Install(ueNodes.Get(i)));
  }
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(simTime));

  // *** Modification 5: Enable Logging and Tracing ***
  // Enable logging for LTE and EPC modules
  // Note: Uncomment the following lines if you want to enable logging
  // LogComponentEnable("LteHelper", LOG_LEVEL_INFO);
  // LogComponentEnable("EpcHelper", LOG_LEVEL_INFO);

  // Enable PCAP tracing
  p2ph.EnablePcapAll("lte-full-modified");

  // Animation definition
  unsigned long long testValue = 0xFFFFFFFFFFFFFFFF;
  AnimationInterface anim("lte-full-modified.xml");
  anim.SetMobilityPollInterval(Seconds(1));
  anim.EnablePacketMetadata(true);
  anim.SetMaxPktsPerTraceFile(testValue);

  // Assign positions to PGW and Remote Host
  AnimationInterface::SetConstantPosition(pgw, -500, 0);
  AnimationInterface::SetConstantPosition(remoteHost, -600, 0);
  anim.UpdateNodeDescription(pgw, "PGW");
  anim.UpdateNodeDescription(remoteHost, "RemoteHost");

  // Update eNodeBs in the animation
  for (uint32_t e = 0; e < enbNodes.GetN(); ++e)
  {
    Ptr<MobilityModel> mob = enbNodes.Get(e)->GetObject<MobilityModel>();
    Vector pos = mob->GetPosition();
    anim.UpdateNodeDescription(enbNodes.Get(e), "eNodeB_" + std::to_string(e));
    anim.UpdateNodeColor(enbNodes.Get(e), 0, 255, 0); // Green
    anim.SetConstantPosition(enbNodes.Get(e), pos.x, pos.y);
  }

  // Update UEs in the animation
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    anim.UpdateNodeDescription(ueNodes.Get(u), "UE_" + std::to_string(u));
    anim.UpdateNodeColor(ueNodes.Get(u), 0, 0, 255); // Blue
    // UEs are mobile; positions will be updated automatically
  }

  // Flow Monitor setup
  Ptr<FlowMonitor> monitor;
  FlowMonitorHelper flowMonHelper;
  monitor = flowMonHelper.InstallAll();

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // GnuPlot for Delay
  std::string delayFile = "delay-modified";
  std::string graphicsFileDelay = delayFile + ".png";
  std::string plotFileDelay = delayFile + ".plt";
  std::string plotTitleDelay = "Average Delay";
  std::string dataTitleDelay = "Delay [ms]";
  Gnuplot gnuplot_delay(graphicsFileDelay);
  gnuplot_delay.SetTitle(plotTitleDelay);
  gnuplot_delay.SetTerminal("png");
  gnuplot_delay.SetLegend("Flow ID", dataTitleDelay);
  gnuplot_delay.AppendExtra("set xrange [1:" + std::to_string(numberOfNodes) + "]");
  gnuplot_delay.AppendExtra("set yrange [0:100]");
  gnuplot_delay.AppendExtra("set grid");
  Gnuplot2dDataset dataset_delay;

  // GnuPlot for Data Rate
  std::string dataRateFile = "datarate-modified";
  std::string graphicsFileDR = dataRateFile + ".png";
  std::string plotFileDR = dataRateFile + ".plt";
  std::string plotTitleDR = "Data Rate for Flows";
  std::string dataTitleDR = "Data rate [kbps]";
  Gnuplot gnuplot_DR(graphicsFileDR);
  gnuplot_DR.SetTitle(plotTitleDR);
  gnuplot_DR.SetTerminal("png");
  gnuplot_DR.SetLegend("Flow ID", dataTitleDR);
  gnuplot_DR.AppendExtra("set xrange [1:" + std::to_string(numberOfNodes) + "]");
  gnuplot_DR.AppendExtra("set yrange [0:1000]");
  gnuplot_DR.AppendExtra("set grid");
  Gnuplot2dDataset dataset_rate;

  // Analyze Flow Monitor Statistics
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  monitor->SerializeToXmlFile("lte-full-modified.flowmon", true, true);

  std::cout << std::endl << "*** Flow monitor statistics ***" << std::endl;
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
  {
    double Delay, DataRate;
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
    std::cout << "Flow ID: " << i->first << std::endl;
    std::cout << "Src Addr: " << t.sourceAddress << " -> Dst Addr: " << t.destinationAddress << std::endl;
    std::cout << "Src Port: " << t.sourcePort << " -> Dst Port: " << t.destinationPort << std::endl;
    std::cout << "Tx Packets/Bytes: " << i->second.txPackets << "/" << i->second.txBytes << std::endl;
    std::cout << "Rx Packets/Bytes: " << i->second.rxPackets << "/" << i->second.rxBytes << std::endl;
    std::cout << "Throughput: "
              << i->second.rxBytes * 8.0 /
                     (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1024
              << " kbps" << std::endl;
    std::cout << "Delay Sum: " << i->second.delaySum.GetMilliSeconds() << " ms" << std::endl;
    std::cout << "Mean Delay: " << (i->second.delaySum.GetSeconds() / i->second.rxPackets) * 1000 << " ms" << std::endl;

    // Gnuplot Delay
    Delay = (i->second.delaySum.GetSeconds() / i->second.rxPackets) * 1000;
    DataRate = i->second.rxBytes * 8.0 /
               (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1024;

    dataset_delay.Add((double)i->first, (double)Delay);
    dataset_rate.Add((double)i->first, (double)DataRate);

    std::cout << "Jitter Sum: " << i->second.jitterSum.GetMilliSeconds() << " ms" << std::endl;
    std::cout << "Mean Jitter: "
              << (i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1)) * 1000 << " ms" << std::endl;
    std::cout << "Lost Packets: " << i->second.txPackets - i->second.rxPackets << std::endl;
    std::cout << "Packet Loss: "
              << (((i->second.txPackets - i->second.rxPackets) * 1.0) / i->second.txPackets) * 100 << "%" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
  }

  // Gnuplot - Delay
  gnuplot_delay.AddDataset(dataset_delay);
  std::ofstream plotFileDelayOut(plotFileDelay.c_str());
  gnuplot_delay.GenerateOutput(plotFileDelayOut);
  plotFileDelayOut.close();

  // Gnuplot - Data Rate
  gnuplot_DR.AddDataset(dataset_rate);
  std::ofstream plotFileDROut(plotFileDR.c_str());
  gnuplot_DR.GenerateOutput(plotFileDROut);
  plotFileDROut.close();

  Simulator::Destroy();
  return 0;
}
