/**
 * @file lte-simulation-assignment.cpp
 * @brief LTE Network Simulation for Suburban Area in NS-3.39
 *
 * This simulation creates an LTE network scenario with:
 * - Multiple eNodeBs and UEs
 * - Suburban propagation loss model
 * - Mobility and traffic generation
 *
 * @version 1.0
 * @date 2024
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

/**
 * @brief Configure mobility for eNodeBs.
 * @param enbNodes NodeContainer for eNodeBs
 */
void ConfigureEnbMobility(NodeContainer &enbNodes) {
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
    enbPositionAlloc->Add(Vector(0.0, 100.0, 0.0));   // eNodeB #1
    enbPositionAlloc->Add(Vector(500.0, 100.0, 0.0)); // eNodeB #2
    enbMobility.SetPositionAllocator(enbPositionAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
}

/**
 * @brief Configure mobility for UEs.
 * @param ueNodes NodeContainer for UEs
 */
void ConfigureUeMobility(NodeContainer &ueNodes) {
    MobilityHelper ueMobility;
    Ptr<PositionAllocator> uePositionAlloc = CreateObject<RandomRectanglePositionAllocator>();
    uePositionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
    uePositionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"));
    ueMobility.SetPositionAllocator(uePositionAlloc);
    ueMobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                "Speed", StringValue("ns3::UniformRandomVariable[Min=2.0|Max=10.0]"),
                                "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                                "PositionAllocator", PointerValue(uePositionAlloc));
    ueMobility.Install(ueNodes);
}

/**
 * @brief Set up the LTE simulation environment.
 * @param numEnbs Number of eNodeBs
 * @param numUes Number of UEs
 * @param simTime Simulation time
 */
void RunLteSimulation(uint16_t numEnbs, uint16_t numUes, double simTime) {
    NS_LOG_INFO("Starting LTE Simulation...");

    // Create LTE and EPC helpers
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);

    // Configure path loss model
    lteHelper->SetPathlossModelType(TypeId::LookupByName("ns3::Cost231PropagationLossModel"));

    // Create nodes
    NodeContainer enbNodes, ueNodes, remoteHostContainer;
    enbNodes.Create(numEnbs);
    ueNodes.Create(numUes);
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);

    // Configure mobility
    ConfigureEnbMobility(enbNodes);
    ConfigureUeMobility(ueNodes);

    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);
    internet.Install(ueNodes);

    // Install LTE devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Assign IPs to UEs
    Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(ueDevs);

    // Attach UEs to eNodeBs
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i) {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(i % numEnbs));
    }

    // Configure remote host link
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer internetDevices = p2p.Install(epcHelper->GetPgwNode(), remoteHost);

    Ipv4AddressHelper ipv4Helper;
    ipv4Helper.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIfaces = ipv4Helper.Assign(internetDevices);

    // Set up routing for remote host
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = Ipv4RoutingHelper::GetRouting<Ipv4StaticRouting>(
        remoteHost->GetObject<Ipv4>()->GetRoutingProtocol());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"),
                                               internetIfaces.GetAddress(1), 1);

    // Install traffic applications
    UdpEchoServerHelper echoServer(9);
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

    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("LTE Simulation Complete.");
}

/**
 * @brief Main function for LTE simulation.
 */
int main(int argc, char *argv[]) {
    uint16_t numEnbs = 2;
    uint16_t numUes = 5;
    double simTime = 20.0;

    CommandLine cmd;
    cmd.AddValue("numEnbs", "Number of eNodeBs", numEnbs);
    cmd.AddValue("numUes", "Number of UEs", numUes);
    cmd.AddValue("simTime", "Simulation time (s)", simTime);
    cmd.Parse(argc, argv);

    RunLteSimulation(numEnbs, numUes, simTime);

    return 0;
}
