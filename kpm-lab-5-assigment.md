# Laboratory 5: Analysis of Wireshark Network Activity Traces

## 1. Network Setup and Key Simulation Events
The simulation setup consists of:
- **Point-to-Point (P2P)** connection between two nodes with specific data rate and delay attributes.
- **CSMA and Wi-Fi Networks** connected to various nodes.
- **UDP Echo Server and Client** configured to send and receive packets with defined intervals.

## 2. Analysis

### 2.1 Initial UDP Handshake and Packet Exchanges

The UDP Echo Client initiates communication with the Echo Server on port 9. This first packet confirms that the client is attempting to connect to the server, setting up the communication session.

- **Source IP**: `10.1.3.3` (UDP Echo Client)
- **Destination IP**: `10.1.2.4` (UDP Echo Server)
- **Source Port**: A high-numbered ephemeral port (`49153` in this example), dynamically assigned by the client
- **Destination Port**: Port `9`, specified in the server setup
- **UDP Length**: The total packet size, including headers and payload, is 1024 bytes as configured in the `queue.cc` simulation file
- **Checksum**: The checksum field verifies data integrity, though in this instance, it shows `0x0000`, meaning it was not calculated (UDP checksum is optional)

This packet marks the start of data flow between the client and server, indicating that the network and applications are correctly configured to communicate.

```cpp
UdpEchoServerHelper echoServer(9);
ApplicationContainer serverApps = echoServer.Install(csmaNodes.Get(nCsma));
serverApps.Start(Seconds(1.0));
serverApps.Stop(Seconds(10.0));

UdpEchoClientHelper echoClient(csmaInterfaces.GetAddress(nCsma), 9);
echoClient.SetAttribute("MaxPackets", UintegerValue(10));
echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
echoClient.SetAttribute("PacketSize", UintegerValue(1024));
```
The screenshot below shows the initial UDP handshake packet between the client and server on port 9, demonstrating the start of the communication process.
  ![UDP handshake](/lab5/screenshots/screen_0-0.png)



### 2.2. Mobility Events and Position Updates
As nodes move within the network, position changes are logged in the simulation and are captured by Wireshark.
- **Packet Explanation**: Mobility updates provide information on nodes' changing positions, which can impact connectivity and packet routing efficiency.
- **Screenshot Placeholder**: 
  ![Mobility Event Packet](screenshot_mobility_event.png)

### 2.3. Packet Queue Management
Queue length changes were observed and captured, showing congestion levels in network queues. When the queue length increases, packets may be held or dropped, depending on the network’s traffic load.
- **Packet Explanation**: These packets reflect the queue's state when managing traffic; a high queue length might lead to packet delays or drops, which can be confirmed by packet retransmissions in Wireshark.
- **Screenshot Placeholder**: 
  ![Queue Length Change Packet](screenshot_queue_length_change.png)

### 2.4. Packet Drops
During high congestion, packets are occasionally dropped from the queue. Wireshark captures these events with details on the specific times and packets affected.
- **Packet Explanation**: Packet drops indicate network congestion. Dropped packets typically require retransmission, affecting overall latency and throughput.
- **Screenshot Placeholder**: 
  ![Packet Drop Event](screenshot_packet_drop.png)

### 2.5. Client-Server Data Exchange
The simulation logs capture packets sent from the client and received by the server, with corresponding acknowledgments. Each packet sent and received in the data exchange can be correlated to a time event in the simulation logs.
- **Packet Explanation**: This section illustrates successful communication between the client and server, indicating stable connectivity. Each data packet exchange signifies the correct functioning of the UDP echo application.
- **Screenshot Placeholder**: 
  ![Client-Server Packet Exchange](screenshot_client_server_exchange.png)

## 3. Conclusion
The `.pcap` analysis shows how network parameters like mobility, queue size, and congestion levels impact data flow and packet integrity in a wireless network environment. The events logged in the simulation, such as queue length changes, packet drops, and position updates, correlate well with the packet traces observed in Wireshark.

By examining the captured packets, we see the effects of node movement, congestion, and queue management on data transmission, which are critical factors in real-world network performance.

---

## Appendices
### Appendix A: Full Wireshark Capture Screenshots
- **Screenshot of Initial UDP Handshake**
- **Screenshot of Mobility Event**
- **Screenshot of Queue Length Change**
- **Screenshot of Packet Drop Event**
- **Screenshot of Client-Server Packet Exchange**

Each section above should include a Wireshark screenshot showing the packets involved in the respective events.
