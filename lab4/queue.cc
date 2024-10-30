/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TraceCallbackExample");

// Callback function to log course changes (mobility events) without context
void CourseChangeWithoutContext(Ptr<const MobilityModel> model)
{
    Vector position = model->GetPosition();
    NS_LOG_UNCOND("CourseChangeWithoutContext - x = " << position.x << ", y = " << position.y);
}

// Callback function to log course changes (mobility events) with context
void CourseChangeWithContext(std::string context, Ptr<const MobilityModel> model)
{
    Vector position = model->GetPosition();
    NS_LOG_UNCOND(context << " - x = " << position.x << ", y = " << position.y);
}

// Callback function to log packet drops
void PacketDrop(Ptr<const QueueDiscItem> item)
{
    std::cout << "Packet dropped at " << Simulator::Now().GetSeconds() << "s" << std::endl;
}

// Callback function to log queue length changes without context
void QueueLengthWithoutContext(uint32_t oldVal, uint32_t newVal)
{
    std::cout << "Queue length changed from " << oldVal << " to " << newVal << " at " << Simulator::Now().GetSeconds() << "s" << std::endl;
}

// Callback function to log queue length changes with context
void QueueLengthWithContext(std::string context, uint32_t oldVal, uint32_t newVal)
{
    std::cout << "Context: " << context << " - Queue length changed from " << oldVal << " to " << newVal << " at " << Simulator::Now().GetSeconds() << "s" << std::endl;
}

int main(int argc, char *argv[])
{
    bool verbose = true;
    uint32_t nCsma = 3;
    uint32_t nWifi = 3;
    bool tracing = true;

    CommandLine cmd;
    cmd.AddValue("nCsma", "Number of \"extra\" CSMA nodes/devices", nCsma);
    cmd.AddValue("nWifi", "Number of wifi STA devices", nWifi);
    cmd.AddValue("verbose", "Tell echo applications to log if true", verbose);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);
    cmd.Parse(argc, argv);

    if (nWifi > 18)
    {
        std::cout << "nWifi should be 18 or less; otherwise grid layout exceeds the bounding box" << std::endl;
        return 1;
    }

    if (verbose)
    {
        LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
        LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    }

    // Create point-to-point nodes and set up devices
    NodeContainer p2pNodes;
    p2pNodes.Create(2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));
    pointToPoint.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("50p"));

    NetDeviceContainer p2pDevices;
    p2pDevices = pointToPoint.Install(p2pNodes);

    // Create CSMA nodes and set up devices
    NodeContainer csmaNodes;
    csmaNodes.Add(p2pNodes.Get(1));
    csmaNodes.Create(nCsma);

    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    NetDeviceContainer csmaDevices;
    csmaDevices = csma.Install(csmaNodes);

    // Create Wi-Fi nodes and set up devices
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create(nWifi);
    NodeContainer wifiApNode = p2pNodes.Get(0);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");

    WifiMacHelper mac;
    Ssid ssid = Ssid("ns-3-ssid");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));

    NetDeviceContainer staDevices;
    staDevices = wifi.Install(phy, mac, wifiStaNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));

    NetDeviceContainer apDevices;
    apDevices = wifi.Install(phy, mac, wifiApNode);

    // Set up mobility for Wi-Fi nodes
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(0.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(5.0),
                                  "DeltaY", DoubleValue(10.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds", RectangleValue(Rectangle(-50, 50, -50, 50)));
    mobility.Install(wifiStaNodes);

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(wifiApNode);

    // Install internet stack on nodes
    InternetStackHelper stack;
    stack.Install(csmaNodes);
    stack.Install(wifiApNode);
    stack.Install(wifiStaNodes);

    // Set up traffic control (queue management)
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::CoDelQueueDisc", "MaxSize", StringValue("1000p"));
    Ptr<QueueDisc> queueDisc = tch.Install(p2pDevices.Get(0)).Get(0);

    // Connect trace sources for packet drops and queue length changes
    queueDisc->TraceConnectWithoutContext("Drop", MakeCallback(&PacketDrop));
    queueDisc->TraceConnectWithoutContext("BytesInQueue", MakeCallback(&QueueLengthWithoutContext));

    // Connect trace sources for queue length changes with context
    queueDisc->TraceConnect("BytesInQueue", "", MakeCallback(&QueueLengthWithContext));

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterfaces = address.Assign(p2pDevices);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces = address.Assign(csmaDevices);

    address.SetBase("10.1.3.0", "255.255.255.0");
    address.Assign(staDevices);
    address.Assign(apDevices);

    // Set up UDP echo server and client
    UdpEchoServerHelper echoServer(9);
    ApplicationContainer serverApps = echoServer.Install(csmaNodes.Get(nCsma));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));

    UdpEchoClientHelper echoClient(csmaInterfaces.GetAddress(nCsma), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(10));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(wifiStaNodes.Get(nWifi - 1));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));

    // Enable routing and tracing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    Simulator::Stop(Seconds(10.0));

    if (tracing)
    {
        pointToPoint.EnablePcapAll("trace-example");
        phy.EnablePcap("trace-example", apDevices.Get(0));
        csma.EnablePcap("trace-example", csmaDevices.Get(0), true);
    }

    // Connect trace sources for mobility course changes with context
    std::ostringstream oss;
    oss << "/NodeList/" << wifiStaNodes.Get(nWifi - 1)->GetId() << "/$ns3::MobilityModel/CourseChange";
    Config::Connect(oss.str(), MakeCallback(&CourseChangeWithContext));

    // Connect trace sources for mobility course changes without context
    Config::ConnectWithoutContext(oss.str(), MakeCallback(&CourseChangeWithoutContext));

    // Run the simulation
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

