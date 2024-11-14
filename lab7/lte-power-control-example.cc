#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"

using namespace ns3;

int main (int argc, char *argv[])
{
    // Create Node containers for eNodeB and UEs
    NodeContainer enbNodes;
    NodeContainer ueNodes;
    enbNodes.Create (1);
    ueNodes.Create (1);

    // Install Mobility Models
    MobilityHelper mobility;
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

    // Install mobility for eNodeB
    mobility.Install (enbNodes);
    enbNodes.Get (0)->GetObject<MobilityModel> ()->SetPosition (Vector (0.0, 0.0, 0.0));

    // Install mobility for UE
    mobility.Install (ueNodes);
    ueNodes.Get (0)->GetObject<MobilityModel> ()->SetPosition (Vector (10.0, 0.0, 0.0));

    // Create LTE Helper
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();

    // Set ClosedLoop to true or false
    Config::SetDefault ("ns3::LteUePowerControl::ClosedLoop", BooleanValue (false)); // Set to true for Closed Loop

    // Install LTE Devices to the nodes
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (ueNodes);

    // Attach UEs to the eNodeB
    lteHelper->Attach (ueDevs, enbDevs.Get (0));

    // Access the LteUePowerControl instance and print the attribute value
    for (uint32_t i = 0; i < ueDevs.GetN (); ++i)
    {
        Ptr<NetDevice> netDevice = ueDevs.Get (i);
        Ptr<LteUeNetDevice> ueNetDevice = netDevice->GetObject<LteUeNetDevice> ();
        Ptr<LteUePhy> uePhy = ueNetDevice->GetPhy ();
        Ptr<LteUePowerControl> uePowerControl = uePhy->GetUplinkPowerControl ();

        // Retrieve and print ClosedLoop
        BooleanValue closedLoopValue;
        uePowerControl->GetAttribute ("ClosedLoop", closedLoopValue);
        std::cout << "UE " << i << " ClosedLoop: " << (closedLoopValue.Get () ? "true" : "false") << std::endl;

        // Infer the power control mode
        if (!closedLoopValue.Get ())
        {
            std::cout << "UE " << i << " is using Open Loop Power Control." << std::endl;
        }
        else
        {
            std::cout << "UE " << i << " is using Closed Loop Power Control." << std::endl;
        }
    }

    // Run the simulation for a short duration
    Simulator::Stop (Seconds (1.0));
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;
}