
# Lab 7: NS3 LENA Basic and EPC Scenarios

## Course Information
- **Course:** MPA-KPM 24/25Z
- **Lab Date:** October 30, 2024
- **Presented by:** MSc. Balaji Kirubakaran
- **Faculty:** BUT FEEC, Department of Telecommunications

## Lab Exercises


### 1. Print Attribute Values

In this exercise, we will print the values of the following attributes in the ns-3 LTE module:

- `ns3::LteUePowerControl::ClosedLoop`
- `ns3::LteUePowerControl::OpenLoop`

#### Code and Execution

Below is the code to create a simple ns-3 LTE simulation and print the value of the `ClosedLoop` attribute for the UE (User Equipment).

link

**Explanation:**

- **Simulation Setup:**
  - Creates one eNodeB (base station) and one UE (user equipment).
  - Assigns constant positions to both nodes.

- **Configuration:**
  - Sets the `ClosedLoop` attribute of `ns3::LteUePowerControl` to `false`, enabling Open Loop Power Control. To enable Closed Loop Power Control, set this attribute to `true`.

- **Printing Attribute Values:**
  - Accesses the `LteUePowerControl` instance from the UE's PHY layer.
  - Retrieves the `ClosedLoop` attribute value using `GetAttribute`.
  - Prints the value and infers the power control mode based on it.

#### Expected Output

When you run the simulation, you should see output similar to:

```
UE 0 ClosedLoop: false
UE 0 is using Open Loop Power Control.
```

If you set `ClosedLoop` to `true` in the code:

```cpp
Config::SetDefault ("ns3::LteUePowerControl::ClosedLoop", BooleanValue (true));
```

The output will be:

```
UE 0 ClosedLoop: true
UE 0 is using Closed Loop Power Control.
```

#### Conclusion

We examined the `ClosedLoop` attribute in NS-3's LTE module to understand power control modes for LTE User Equipment (UE):

1. **Closed Loop Power Control**:
- When `ClosedLoop` is set to `true`, the UE adjusts its transmission power based on feedback from the eNodeB, optimizing link quality and minimizing interference. This dynamic control provides more efficient power management, especially in environments with variable interference.

2. **Open Loop Power Control**:
- When `ClosedLoop` is set to `false`, the UE operates in Open Loop mode, maintaining a fixed transmission power without network feedback. This mode is simpler and suitable for scenarios where consistent power is preferred or feedback is limited.
---

## 2. Run Emulated Script `lena-simple-epc-emu.cc`

### Setup for script
 1. The network interfaces were created using commands
```
 sudo ip link add name veth0 type veth peer name veth1
 sudo ip link set veth0 promisc on
 sudo ip link set veth1 promisc on
```

 2. Following lines were added to the `lena-simple-epc-emu.cc` script
```
    // Set interface names for emulation
    epcHelper->SetAttribute("SgwDeviceName", StringValue("veth0")); // Interface for SGW
    epcHelper->SetAttribute("EnbDeviceName", StringValue("veth1")); // Interface for eNB
```

Upon running the script, we observed outputs indicating the initialization of the LTE components and network setups. Notable parts of the output included:

- **Initialization of eNBs and UEs**: The script sets up eNBs and UEs with assigned IP addresses and establishes the S1-U interface.
- **Data Transfer Initialization**: Displayed logs for uplink and downlink data traffic across the emulated LTE network.
### Interface Configuration Check

The virtual interfaces created for this emulation were verified using the `ip link show` command. The output was as follows:

```
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
2: enp0s3: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP mode DEFAULT group default qlen 1000
    link/ether 08:00:27:94:3e:28 brd ff:ff:ff:ff:ff:ff
3: lxcbr0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc noqueue state DOWN mode DEFAULT group default qlen 1000
    link/ether 00:16:3e:00:00:00 brd ff:ff:ff:ff:ff:ff
4: veth1@veth0: <BROADCAST,MULTICAST,PROMISC,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DEFAULT group default qlen 1000
    link/ether b6:36:25:f1:11:73 brd ff:ff:ff:ff:ff:ff
5: veth0@veth1: <BROADCAST,MULTICAST,PROMISC,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DEFAULT group default qlen 1000
    link/ether 7e:5c:b5:cd:20:78 brd ff:ff:ff:ff:ff:ff
```

This output confirms that the interfaces `veth0` and `veth1` are configured and up, allowing for the emulated LTE communication setup.

### Wireshark Observations

We captured network traffic on the virtual interfaces (`veth0` and `veth1`) using Wireshark. Observed traffic included ARP requests, GTP packets for tunneling, and LTE-specific control messages.

- **Screenshot Placeholder**: Insert Wireshark capture screenshot here.

### Conclusions

This experiment demonstrated a functional LTE network setup using ns-3's emulated EPC helper, allowing real-time packet exchange in an emulated environment. Observing the network traffic in Wireshark validated the successful configuration of virtual interfaces and LTE components.

---

## 3. Mean Jitter Calculation

In the `lte-full.cc` file, the mean jitter for each flow is calculated based on the variations in packet delay times. Jitter refers to the difference in delay between consecutive packets in a flow.

### Calculation Method

The jitter calculation is performed by measuring the difference between the delay of consecutive packets as they arrive. The sum of these delay differences is then divided by the total number of packets to obtain the **mean jitter**.

For example, if packet delays vary between consecutive packets, this delay difference (jitter) is accumulated for each pair of packets. At the end of the transmission, the sum is divided by the total packet count to get the average jitter:

`Mean Jitter = Jitter Sum / Total Packets`

### Example from Output

Based on the simulation output:

| Flow ID | Source Address | Destination Address | Mean Jitter (ms) |
|---------|----------------|---------------------|------------------|
| 1       | 7.0.0.2       | 1.0.0.2            | 1.77016         |
| 2       | 7.0.0.3       | 1.0.0.2            | 1.74256         |
| 3       | 7.0.0.4       | 1.0.0.2            | 1.89999         |
| 4       | 7.0.0.5       | 1.0.0.2            | 1.88299         |
| 5       | 7.0.0.6       | 1.0.0.2            | 1.75107         |
| 6       | 1.0.0.2       | 7.0.0.2            | 2.11324         |
| 7       | 1.0.0.2       | 7.0.0.3            | 2.14041         |
| 8       | 1.0.0.2       | 7.0.0.4            | 2.26133         |
| 9       | 1.0.0.2       | 7.0.0.5            | 2.32207         |
| 10      | 1.0.0.2       | 7.0.0.6            | 2.11826         |

These values indicate the average variation in packet delay for each flow.

### Summary

Mean jitter calculation provides insight into network stability for real-time applications. Lower jitter values are preferable, as they indicate more consistent packet arrival times.

---

## 4. Program Modifications and Output Comparison
#### Changes in `lte-full.cc`
- **Change 1:** Adjusted the number of eNBs and UEs.
- **Change 2:** Enabled/disabled power control options to study their effects.

#### Output Comparison
**Before Changes:**
```
```

**After Changes:**
```
```

#### Analysis

---


