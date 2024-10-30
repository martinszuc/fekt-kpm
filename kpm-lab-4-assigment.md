# Code
Edited code for `first.cc` with changes can be seen and compared here: [Compare changes for `first.cc` on GitHub](https://github.com/martinszuc/fekt-kpm/commit/172104372e50b587f524b6568307a09059f8ba57)

Or complete file `first.cc` [Complete first.cc file](https://github.com/martinszuc/fekt-kpm/blob/main/lab4/first.cc)


# Simulation Output and Explanation

```bash
$ NS_LOG="FirstScriptExample=level_all" ./ns3 run scratch/first.cc > log_output.txt 2>&1

FirstScript:main()
Time resolution set to nanoseconds
Logging enabled for UdpEcho applications
Created two nodes for the point-to-point network
Configured Point-to-Point attributes: DataRate=5Mbps, Delay=2ms
Installed Point-to-Point devices on both nodes
Internet stack installed on nodes
Assigned IP addresses to devices
UdpEchoServer installed on node 1 with start and stop times
Configured UdpEchoClient attributes: MaxPackets=1, Interval=1s, PacketSize=1024 bytes
UdpEchoClient installed on node 0 with start and stop times
Starting the simulation
At time +2s client sent 1024 bytes to 10.1.1.2 port 9
At time +2.00369s server received 1024 bytes from 10.1.1.1 port 49153
At time +2.00369s server sent 1024 bytes to 10.1.1.1 port 49153
At time +2.00737s client received 1024 bytes from 10.1.1.2 port 9
Simulation finished and resources destroyed
```

## Explanation
- `$ NS_LOG="FirstScript=level_all" ./ns3 run scratch/first.cc > log_output.txt`
  - This command runs the `first.cc` script with all log levels enabled for `FirstScript`, and redirects the output to `log_output.txt`.

- `FirstScript:main()`
  - Entry into the `main` function of the simulation.

- `Time resolution set to nanoseconds`
  - The simulation time resolution is configured to nanoseconds for higher precision.

- `Logging enabled for UdpEcho applications`
  - Logging is activated for both `UdpEchoClientApplication` and `UdpEchoServerApplication`.

- `Created two nodes for the point-to-point network`
  - Two nodes (`n0` and `n1`) are created to form the point-to-point network.

- `Configured Point-to-Point attributes: DataRate=5Mbps, Delay=2ms`
  - The point-to-point link is set with a data rate of 5 Mbps and a delay of 2 milliseconds.

- `Installed Point-to-Point devices on both nodes`
  - Network devices are installed on the nodes to enable communication over the point-to-point link.

- `Internet stack installed on nodes`
  - The TCP/IP stack is installed on both nodes to support internet protocols.

- `Assigned IP addresses to devices`
  - IP addresses are assigned to the network interfaces of the nodes.

- `UdpEchoServer installed on node 1 with start and stop times`
  - The UDP Echo Server application is installed on node 1, set to run from 1 to 10 seconds.

- `Configured UdpEchoClient attributes: MaxPackets=1, Interval=1s, PacketSize=1024 bytes`
  - The UDP Echo Client is configured to send 1 packet of 1024 bytes after an interval of 1 second.

- `UdpEchoClient installed on node 0 with start and stop times`
  - The UDP Echo Client application is installed on node 0, set to run from 2 to 10 seconds.

- `Starting the simulation`
  - Indicates that the simulation is starting.

- `At time +2s client sent 1024 bytes to 10.1.1.2 port 9`
  - The client sends a packet to the server at time 2 seconds.

- `At time +2.00369s server received 1024 bytes from 10.1.1.1 port 49153`
  - The server receives the packet from the client.

- `At time +2.00369s server sent 1024 bytes to 10.1.1.1 port 49153`
  - The server sends a reply back to the client.

- `At time +2.00737s client received 1024 bytes from 10.1.1.2 port 9`
  - The client receives the reply from the server.

- `Simulation finished and resources destroyed`
  - The simulation ends, and all allocated resources are cleaned up.
