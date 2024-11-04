# LTE Network Simulation Report

## 1. GTPv2 Session Creation

In LTE networks, **GTPv2 (GPRS Tunneling Protocol version 2)** operates at the control plane and is essential for managing sessions between the User Equipment (UE) and core network components. GTPv2 enables the setup, modification, and deletion of sessions to facilitate efficient data transfer.

### Session Setup Process

The session setup starts with a **Create Session Request** from the core network, followed by a **Create Session Response**. Once the session is established, data packets are tunneled using GTP-U (User Plane) encapsulation.

The initial frames in the trace file illustrate this process:
- **Frame 3**: **Create Session Request** from `14.0.0.6` to `14.0.0.5`, which includes subscriber and network information.
- **Frame 4**: **Create Session Response** from `14.0.0.5` to `14.0.0.6`, confirming session setup.

Following these setup frames, GTP-U packets are visible, indicating active user data transfer. These packets, such as **Frame 15**, use different port numbers (e.g., `49153 → 2001`) to signify different data flows and potential QoS levels.

**![Session Creation and Data Packets](lab6/screenshots/screen_1.png)**

A closer look at the **Create Session Request** in Frame 3 reveals fields like:
- **IMSI**: Identifies the subscriber for authentication and tracking.
- **ULI**: Provides UE location information for routing.
- **F-TEID**: Specifies the tunnel endpoint for data transfer, including SGW IP and TEID.

These fields are crucial for setting up the GTP tunnel between the UE and core network, enabling seamless data transmission.

**![Create Session Request Details](lab6/screenshots/screen_2.png)**
### Layer Information
- **GTPv2 Control Messages** (e.g., Create Session Request) operate at the **control plane layer**.
- **GTP-U Data Packets** function at the **user plane layer**, carrying user data.
---

## 2. UDP Data Analysis

The UDP protocol, seen in the pcap files, is used to carry encapsulated user data between the core network and UE over the GTP-U tunnel. Each UDP packet represents a segment of user data being tunneled across the LTE network.

### Analysis of UDP Packets

**![UDP Data Packets](lab6/screenshots/screen_udp.png)**

### Layer Information
- **UDP Packets** in this context are part of the **user plane** layer, encapsulating data transferred between the UE and core network.

---

### 3.1 PHY Layer

PHY Layer Analysis: SINR and RSRP

In LTE networks, **SINR (Signal-to-Interference-plus-Noise Ratio)** and **RSRP (Reference Signal Received Power)** are crucial metrics for understanding signal quality and connection stability at the physical layer.


The **SINR** values in the uplink (UL) trace file `UlSinrStats.txt` provide insights into the quality of the uplink signal received by the eNodeB. Higher SINR values indicate a better signal quality, which is essential for achieving higher data rates and reliable communication.

**Sample Data:**
```
% time	cellId	IMSI	RNTI	rsrp	sinr	ComponentCarrierId
0.000214285	1	1	0	0.00333333	7.02728e+12	0
0.000214285	2	2	0	0.00333333	7.02728e+12	0
0.00121428	1	1	0	0.00333333	7.02728e+12	0
0.00121428	2	2	0	0.00333333	7.02728e+12	0
0.00221428	1	1	0	0.00333333	7.02728e+12	0
0.00221428	2	2	0	0.00333333	7.02728e+12	0
0.00321428	1	1	0	0.00333333	7.02728e+12	0
0.00321428	2	2	0	0.00333333	7.02728e+12	0
0.00421428	1	1	0	0.00333333	7.02728e+12	0
0.00421428	2	2	0	0.00333333	7.02728e+12	0
0.00521428	1	1	0	0.00333333	7.02728e+12	0
```
- **Time**: The timestamp of each SINR measurement.
- **cellId**: The ID of the cell receiving the uplink transmission.
- **IMSI**: The unique identifier for the UE.
- **sinrLinear**: The SINR value in linear scale. In this sample, the SINR values are consistently high (e.g., `1.64215e+06`), indicating minimal interference and good signal quality.
- **componentCarrierId**: The carrier component ID used for multi-carrier setups.


### Analysis of Downlink RSRP and SINR Data

The `DlRsrpSinrStats.txt` file captures key metrics related to signal strength and quality in the downlink (DL) path of the LTE simulation. Below is a breakdown of the primary metrics:

- **RSRP (Reference Signal Received Power)**: Indicates the received power of the reference signal sent by the eNodeB (cell tower) to the UE (User Equipment). In this dataset, the RSRP values are consistently low (`0.00333333`), likely due to simulated ideal conditions or controlled signal interference.

- **SINR (Signal-to-Interference-plus-Noise Ratio)**: A measure of signal quality, reflecting the relationship between the received signal and interference/noise. The high SINR values (e.g., `7.02728e+12`) suggest minimal interference in the simulation, indicative of an ideal network setup. High SINR ensures a better-quality connection, supporting higher data rates.

#### Sample Data

The following table illustrates the consistent pattern in RSRP and SINR values across timestamps:

**Sample Data:**
```

```

#### PHY Layer

The following data from the downlink (`DlTxPhyStats.txt`) shows the modulation and coding scheme (MCS), packet size, and transmission characteristics for each transmission instance. Consistent MCS values (e.g., `28`) and packet sizes (`2196`) indicate stable network conditions with optimal resource usage for downlink transmission.

**[PHY Layer Downlink Stats]**

```
% time	cellId	IMSI	RNTI	layer	mcs	size	rv	ndi	ccId
513	1	1	1	0	28	2196	0	1	0
513	2	2	1	0	28	2196	0	1	0
515	1	1	1	0	28	2196	0	1	0
515	2	2	1	0	28	2196	0	1	0
613	1	1	1	0	28	2196	0	1	0
613	2	2	1	0	28	2196	0	1	0
614	1	1	1	0	28	2196	0	1	0
614	2	2	1	0	28	2196	0	1	0
713	1	1	1	0	28	2196	0	1	0
713	2	2	1	0	28	2196	0	1	0
714	1	1	1	0	28	2196	0	1	0
714	2	2	1	0	28	2196	0	1	0
```
**[PHY Layer Uplink Stats]**
```
% time	cellId	IMSI	RNTI	layer	mcs	size	rv	ndi	ccId
512	1	1	1	0	28	2292	0	1	0
512	2	2	1	0	28	2292	0	1	0
611	1	1	1	0	28	2292	0	1	0
611	2	2	1	0	28	2292	0	1	0
711	1	1	1	0	28	2292	0	1	0
711	2	2	1	0	28	2292	0	1	0
811	1	1	1	0	28	2292	0	1	0
811	2	2	1	0	28	2292	0	1	0
911	1	1	1	0	28	2292	0	1	0
911	2	2	1	0	28	2292	0	1	0
1011	1	1	1	0	28	2292	0	1	0
1011	2	2	1	0	28	2292	0	1	0
```

### 3.2 MAC Layer (e.g., `UlMacStats.txt` and `DlMacStats.txt`)

- **Key Metrics**: Resource block allocations and MCS (Modulation and Coding Scheme).
- **Insights**: Efficient RB allocation and high MCS suggest optimal data flow. Variations can indicate network congestion or adaptive modulation in response to changing link quality.

**[MAC Layer Uplink Stats]**
```
% time	cellId	IMSI	frame	sframe	RNTI	mcsTb1	sizeTb1	mcsTb2	sizeTb2	ccId
0.511	1	1	52	2	1	28	2196	0	0	0
0.511	2	2	52	2	1	28	2196	0	0	0
0.513	1	1	52	4	1	28	2196	0	0	0
0.513	2	2	52	4	1	28	2196	0	0	0
0.611	1	1	62	2	1	28	2196	0	0	0
0.611	2	2	62	2	1	28	2196	0	0	0
0.612	1	1	62	3	1	28	2196	0	0	0
0.612	2	2	62	3	1	28	2196	0	0	0
0.711	1	1	72	2	1	28	2196	0	0	0
0.711	2	2	72	2	1	28	2196	0	0	0
0.712	1	1	72	3	1	28	2196	0	0	0
0.712	2	2	72	3	1	28	2196	0	0	0
0.811	1	1	82	2	1	28	2196	0	0	0
```

**[MAC Layer Downlink  Stats]**
```
% time	cellId	IMSI	frame	sframe	RNTI	mcsTb1	sizeTb1	mcsTb2	sizeTb2	ccId
0.511	1	1	52	2	1	28	2196	0	0	0
0.511	2	2	52	2	1	28	2196	0	0	0
0.513	1	1	52	4	1	28	2196	0	0	0
0.513	2	2	52	4	1	28	2196	0	0	0
0.611	1	1	62	2	1	28	2196	0	0	0
0.611	2	2	62	2	1	28	2196	0	0	0
0.612	1	1	62	3	1	28	2196	0	0	0
0.612	2	2	62	3	1	28	2196	0	0	0
0.711	1	1	72	2	1	28	2196	0	0	0
0.711	2	2	72	2	1	28	2196	0	0	0
0.712	1	1	72	3	1	28	2196	0	0	0
0.712	2	2	72	3	1	28	2196	0	0	0
0.811	1	1	82	2	1	28	2196	0	0	0
0.811	2	2	82	2	1	28	2196	0	0	0
0.812	1	1	82	3	1	28	2196	0	0	0
0.812	2	2	82	3	1	28	2196	0	0	0
0.911	1	1	92	2	1	28	2196	0	0	0
0.911	2	2	92	2	1	28	2196	0	0	0
0.912	1	1	92	3	1	28	2196	0	0	0
0.912	2	2	92	3	1	28	2196	0	0	0
1.011	1	1	102	2	1	28	2196	0	0	0
1.011	2	2	102	2	1	28	2196	0	0	0
1.012	1	1	102	3	1	28	2196	0	0	0
1.012	2	2	102	3	1	28	2196	0	0	0
```

### 3.3 RLC Layer (e.g., `UlRlcStats.txt` and `DlRlcStats.txt`)

- **Key Metrics**: Packet delays and retransmissions.
- **Insights**: Low delay and minimal retransmissions indicate a reliable connection. High retransmissions can suggest packet loss due to interference.

**[RLC Layer Uplink Stats]**
```
% start	end	CellId	IMSI	RNTI	LCID	nTxPDUs	TxBytes	nRxPDUs	RxBytes	delay	stdDev	min	max	PduSize	stdDev	min	max
0.5	0.75	1	1	1	3	3	6336	3	6336	0.00471429	0	0.00471429	0.00471429	2112	0	2112	2112	
0.5	0.75	2	2	1	3	3	6336	3	6336	0.00471429	0	0.00471429	0.00471429	2112	0	2112	2112	
0.75	1	1	1	1	3	2	4224	2	4224	0.00471429	0	0.00471429	0.00471429	2112	0	2112	2112	
0.75	1	2	2	1	3	2	4224	2	4224	0.00471429	0	0.00471429	0.00471429	2112	0	2112	2112	
1	1.25	1	1	1	3	1	2112	1	2112	0.00471429	0	0.00471429	0.00471429	2112	0	2112	2112	
1	1.25	2	2	1	3	1	2112	1	2112	0.00471429	0	0.00471429	0.00471429	2112	0	2112	2112	
```
**[RLC Layer Downlink Stats]**
```
% start	end	CellId	IMSI	RNTI	LCID	nTxPDUs	TxBytes	nRxPDUs	RxBytes	delay	stdDev	min	max	PduSize	stdDev	min	max
0.5	0.75	1	1	1	3	6	6336	6	6336	0.003	0	0.003	0.003	1056	0	1056	1056	
0.5	0.75	2	2	1	3	6	6336	6	6336	0.003	0	0.003	0.003	1056	0	1056	1056	
0.75	1	1	1	1	3	4	4224	4	4224	0.003	0	0.003	0.003	1056	0	1056	1056	
0.75	1	2	2	1	3	4	4224	4	4224	0.003	0	0.003	0.003	1056	0	1056	1056	
1	1.25	1	1	1	3	2	2112	2	2112	0.003	0	0.003	0.003	1056	0	1056	1056	
1	1.25	2	2	1	3	2	2112	2	2112	0.003	0	0.003	0.003	1056	0	1056	1056	
```
### 3.4 PDCP Layer (e.g., `UlPdcpStats.txt` and `DlPdcpStats.txt`)

- **Key Metrics**: End-to-end packet delay and data throughput.
- **Insights**: Consistent delays indicate stable data flow. Spikes in delay may suggest network congestion affecting service quality.

**[PDCP Layer Uplink Stats]**
```
% start	end	CellId	IMSI	RNTI	LCID	nTxPDUs	TxBytes	nRxPDUs	RxBytes	delay	stdDev	min	max	PduSize	stdDev	min	max
0.5	0.75	1	1	1	3	6	6324	6	6324	0.0122619	0.000516398	0.0119286	0.0129286	1054	0	1054	1054	
0.5	0.75	2	2	1	3	6	6324	6	6324	0.0122619	0.000516398	0.0119286	0.0129286	1054	0	1054	1054	
0.75	1	1	1	1	3	4	4216	4	4216	0.0119286	0	0.0119286	0.0119286	1054	0	1054	1054	
0.75	1	2	2	1	3	4	4216	4	4216	0.0119286	0	0.0119286	0.0119286	1054	0	1054	1054	
1	1.25	1	1	1	3	2	2108	2	2108	0.0119286	0	0.0119286	0.0119286	1054	0	1054	1054	
1	1.25	2	2	1	3	2	2108	2	2108	0.0119286	0	0.0119286	0.0119286	1054	0	1054	1054	
```
**[PDCP Layer Downlink Stats]**
```
% start	end	CellId	IMSI	RNTI	LCID	nTxPDUs	TxBytes	nRxPDUs	RxBytes	delay	stdDev	min	max	PduSize	stdDev	min	max
0.5	0.75	1	1	1	3	6	6324	6	6324	0.00353261	0.00050999	0.00306705	0.00399817	1054	0	1054	1054	
0.5	0.75	2	2	1	3	6	6324	6	6324	0.00353173	0.00050999	0.00306618	0.00399729	1054	0	1054	1054	
0.75	1	1	1	1	3	4	4216	4	4216	0.00353261	0.000537577	0.00306705	0.00399817	1054	0	1054	1054	
0.75	1	2	2	1	3	4	4216	4	4216	0.00353173	0.000537577	0.00306618	0.00399729	1054	0	1054	1054	
1	1.25	1	1	1	3	2	2108	2	2108	0.00353261	0.000658395	0.00306705	0.00399817	1054	0	1054	1054	
1	1.25	2	2	1	3	2	2108	2	2108	0.00353173	0.000658395	0.00306618	0.00399729	1054	0	1054	1054	
```
---

## 4. Fundamental Cell Parameters Analysis with Simulation Proofs

Here, we analyze key cell parameters—SINR, RSRP, Throughput, and Packet Delay—based on the LTE simulation.

### SINR (Signal-to-Interference-plus-Noise Ratio)
- **Significance**: High SINR allows better data rates and reliable transmission.
- **Proof**: Show SINR values from the PHY layer trace to demonstrate signal quality.


### RSRP (Reference Signal Received Power)
- **Significance**: Reflects signal strength received by UEs, crucial for connection quality.
- **Proof**: RSRP values from the PHY layer trace indicate how power levels vary within the LTE cell environment.


### Throughput
- **Significance**: Measures successful data transfer over time, essential for evaluating network capacity.
- **Proof**: Throughput values from MAC or PDCP layer traces, showing consistent data flow.


### Packet Delay
- **Significance**: Low delay is important for real-time applications, while higher delays may indicate congestion.
- **Proof**: Delay values from RLC or PDCP traces, showing network latency behavior.


---

## 5. Explanation of the GTP Protocol with Proof

The GTP protocol operates at two levels in LTE:
- **GTPv2 (Control Plane)**: Manages session creation, modification, and deletion. The **Create Session Request** and **Create Session Response** messages are responsible for setting up the session between UE and core network. (See **screen_2** for a close-up of the Create Session Request.)
- **GTP-U (User Plane)**: Encapsulates and tunnels user data. GTP-U packets in the pcap file, such as **Frame 15**, demonstrate the ongoing data transfer over the established session.

The GTP protocol thus enables both control and user data flows, supporting efficient LTE communication.


---