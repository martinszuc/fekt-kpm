/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/log.h"

// Default Network Topology
//
//       10.1.1.0
// n0 -------------- n1
//    point-to-point
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FirstScriptExample");

int
main(int argc, char* argv[])
{
    // LOG FUNCTION: Log the start of the main function
    NS_LOG_FUNCTION_NOARGS();
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);
    NS_LOG_INFO("Time resolution set to nanoseconds"); // LOG INFO
    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);
    NS_LOG_INFO("Logging enabled for UdpEcho applications"); // LOG INFO

    NodeContainer nodes;
    nodes.Create(2);
    NS_LOG_INFO("Created two nodes for the point-to-point network"); // LOG INFO

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));
    NS_LOG_DEBUG("Configured Point-to-Point attributes: DataRate=5Mbps, Delay=2ms"); // LOG DEBUG

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);
    NS_LOG_LOGIC("Installed Point-to-Point devices on both nodes"); // LOG LOGIC

    InternetStackHelper stack;
    stack.Install(nodes);
    NS_LOG_INFO("Internet stack installed on nodes"); // LOG INFO

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign(devices);
    NS_LOG_INFO("Assigned IP addresses to devices"); // LOG INFO

    UdpEchoServerHelper echoServer(9);

    ApplicationContainer serverApps = echoServer.Install(nodes.Get(1));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));
    NS_LOG_INFO("UdpEchoServer installed on node 1 with start and stop times"); // LOG INFO

    UdpEchoClientHelper echoClient(interfaces.GetAddress(1), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));
    NS_LOG_DEBUG("Configured UdpEchoClient attributes: MaxPackets=1, Interval=1s, PacketSize=1024 bytes"); // LOG DEBUG

    ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));
    NS_LOG_INFO("UdpEchoClient installed on node 0 with start and stop times"); // LOG INFO

    NS_LOG_WARN("Starting the simulation"); // LOG WARN
    Simulator::Run();
    Simulator::Destroy();
    NS_LOG_INFO("Simulation finished and resources destroyed"); // LOG INFO
    return 0;
}
