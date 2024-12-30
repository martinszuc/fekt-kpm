/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * NS-3.39 LTE Simulation
 * Assignment 04: LTE Network Simulation in Suburban Area
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/lte-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/cost231-propagation-loss-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LteSimulationAssignment");

int main(int argc, char *argv[]) {
    uint16_t numEnbs = 2;
    uint16_t numUes = 5;
    double simTime   = 20.0;

    CommandLine cmd;
    cmd.AddValue("numEnbs", "Number of eNodeBs", numEnbs);
    cmd.AddValue("numUes",  "Number of UEs", numUes);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.Parse(argc, argv);

    NS_LOG_INFO("Starting LTE Simulation...");

    // Create LTE and EPC helper
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Configure Path Loss Model
    lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::Cost231PropagationLossModel"));

    // Create Nodes
    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(numEnbs);
    ueNodes.Create(numUes);
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);

    // Install Internet Stack
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);
    internet.Install(ueNodes);

    // eNodeB Mobility
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
    enbPositionAlloc->Add(Vector(0.0, 100.0, 0.0));   // eNodeB #1
    enbPositionAlloc->Add(Vector(500.0, 100.0, 0.0)); // eNodeB #2
    enbMobility.SetPositionAllocator(enbPositionAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);

    // UE Mobility
    MobilityHelper ueMobility;
    ueMobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                    "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"),
                                    "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"));
    ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                "Speed", StringValue("ns3::UniformRandomVariable[Min=2.0|Max=10.0]"),
                                "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    ueMobility.Install(ueNodes);

    // Install LTE Devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

    // Assign IPs to UEs
    Ipv4InterfaceContainer ueIpIfaces =
        epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

    // Attach each UE to an eNB (round-robin)
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i) {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % numEnbs));
    }

    // Create RemoteHost <-> PGW link
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer internetDevices = p2p.Install(epcHelper->GetPgwNode(), remoteHost);

    // Assign IP addresses on the above link
    Ipv4AddressHelper ipv4Helper;
    ipv4Helper.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIfaces = ipv4Helper.Assign(internetDevices);

    // Set default route on remoteHost
    // 1) Get Ipv4 pointer from remoteHost
    Ptr<Ipv4> remoteHostIpv4 = remoteHost->GetObject<Ipv4>();
    // 2) Get the Ipv4RoutingProtocol from that Ipv4
    Ptr<Ipv4RoutingProtocol> rhRoutingProtocol = remoteHostIpv4->GetRoutingProtocol();
    // 3) Ask Ipv4RoutingHelper for the Ipv4StaticRouting interface
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(rhRoutingProtocol);
    // 4) Add route to reach the UE network
    remoteHostStaticRouting->AddNetworkRouteTo(
        Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), // UE network
        internetIfaces.GetAddress(1),                 // Next hop
        1                                             // Interface index on remoteHost
    );

    // Install a UDP Echo application (server on remoteHost, clients on UEs)
    UdpEchoServerHelper echoServer(9); // UDP port
    ApplicationContainer serverApps = echoServer.Install(remoteHost);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));

    UdpEchoClientHelper echoClient(internetIfaces.GetAddress(0), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(0.01)));
    echoClient.SetAttribute("PacketSize", UintegerValue(512));
    ApplicationContainer clientApps = echoClient.Install(ueNodes);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simTime));

    // Run the simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("LTE Simulation Complete.");
    return 0;
}
