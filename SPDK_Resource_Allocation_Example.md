# SPDK Resource Allocation: Concrete Example

## Scenario: 96 Cores, 16 SSDs, 2 InfiniBand Ports, 1 Host

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SPDK NVMe-oF Target Server                          │
│                              (96 CPU Cores)                                  │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                            Network Layer (RDMA)                              │
├─────────────────────────────────┬───────────────────────────────────────────┤
│   InfiniBand HCA 0              │   InfiniBand HCA 1                        │
│   Port 0: 192.168.1.1:4420      │   Port 1: 192.168.1.2:4420                │
│   ┌──────────────────────┐      │   ┌──────────────────────┐                │
│   │ SRQ 0 (Shared)       │      │   │ SRQ 1 (Shared)       │                │
│   │ 4096 recv buffers    │      │   │ 4096 recv buffers    │                │
│   └──────────────────────┘      │   └──────────────────────┘                │
└─────────────────────────────────┴───────────────────────────────────────────┘
         ▲                                    ▲
         │                                    │
         │  8 Client Connections (QPs)        │
         │  QP0, QP1, QP4, QP5               │  QP2, QP3, QP6, QP7
         │                                    │
┌────────┴────────────────────────────────────┴─────────────────────────────┐
│                         Poll Groups & Reactors                             │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ Core 0 (Reactor 0)                                                   │  │
│  │ ┌─────────────────────────────────────────────────────────────────┐ │  │
│  │ │ Poll Group 0                                                     │ │  │
│  │ │  ┌──────────────────┐  ┌──────────────────┐                     │ │  │
│  │ │  │ RDMA Poller 0-0  │  │ RDMA Poller 0-1  │                     │ │  │
│  │ │  │ (IB Port 0)      │  │ (IB Port 1)      │                     │ │  │
│  │ │  │ - CQ 0-0         │  │ - CQ 0-1         │                     │ │  │
│  │ │  │ - Monitors QP0   │  │ - No QPs         │                     │ │  │
│  │ │  └──────────────────┘  └──────────────────┘                     │ │  │
│  │ │  ┌────────────────────────────────────────────────────────────┐ │ │  │
│  │ │  │ I/O Channels: SSD0, SSD1, ..., SSD15                       │ │ │  │
│  │ │  └────────────────────────────────────────────────────────────┘ │ │  │
│  │ └─────────────────────────────────────────────────────────────────┘ │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ Core 1 (Reactor 1)                                                   │  │
│  │ ┌─────────────────────────────────────────────────────────────────┐ │  │
│  │ │ Poll Group 1                                                     │ │  │
│  │ │  ┌──────────────────┐  ┌──────────────────┐                     │ │  │
│  │ │  │ RDMA Poller 1-0  │  │ RDMA Poller 1-1  │                     │ │  │
│  │ │  │ (IB Port 0)      │  │ (IB Port 1)      │                     │ │  │
│  │ │  │ - CQ 1-0         │  │ - CQ 1-1         │                     │ │  │
│  │ │  │ - Monitors QP1   │  │ - No QPs         │                     │ │  │
│  │ │  └──────────────────┘  └──────────────────┘                     │ │  │
│  │ │  ┌────────────────────────────────────────────────────────────┐ │ │  │
│  │ │  │ I/O Channels: SSD0, SSD1, ..., SSD15                       │ │ │  │
│  │ │  └────────────────────────────────────────────────────────────┘ │ │  │
│  │ └─────────────────────────────────────────────────────────────────┘ │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ Core 2 (Reactor 2)                                                   │  │
│  │ ┌─────────────────────────────────────────────────────────────────┐ │  │
│  │ │ Poll Group 2                                                     │ │  │
│  │ │  ┌──────────────────┐  ┌──────────────────┐                     │ │  │
│  │ │  │ RDMA Poller 2-0  │  │ RDMA Poller 2-1  │                     │ │  │
│  │ │  │ (IB Port 0)      │  │ (IB Port 1)      │                     │ │  │
│  │ │  │ - CQ 2-0         │  │ - CQ 2-1         │                     │ │  │
│  │ │  │ - No QPs         │  │ - Monitors QP2   │                     │ │  │
│  │ │  └──────────────────┘  └──────────────────┘                     │ │  │
│  │ │  ┌────────────────────────────────────────────────────────────┐ │ │  │
│  │ │  │ I/O Channels: SSD0, SSD1, ..., SSD15                       │ │ │  │
│  │ │  └────────────────────────────────────────────────────────────┘ │ │  │
│  │ └─────────────────────────────────────────────────────────────────┘ │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│  ... (Cores 3-94 similar structure) ...                                    │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ Core 95 (Reactor 95)                                                 │  │
│  │ ┌─────────────────────────────────────────────────────────────────┐ │  │
│  │ │ Poll Group 95                                                    │ │  │
│  │ │  ┌──────────────────┐  ┌──────────────────┐                     │ │  │
│  │ │  │ RDMA Poller 95-0 │  │ RDMA Poller 95-1 │                     │ │  │
│  │ │  │ (IB Port 0)      │  │ (IB Port 1)      │                     │ │  │
│  │ │  │ - CQ 95-0        │  │ - CQ 95-1        │                     │ │  │
│  │ │  │ - No QPs         │  │ - No QPs         │                     │ │  │
│  │ │  └──────────────────┘  └──────────────────┘                     │ │  │
│  │ │  ┌────────────────────────────────────────────────────────────┐ │ │  │
│  │ │  │ I/O Channels: SSD0, SSD1, ..., SSD15                       │ │ │  │
│  │ │  └────────────────────────────────────────────────────────────┘ │ │  │
│  │ └─────────────────────────────────────────────────────────────────┘ │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                          Storage Layer (Block Devices)                       │
│                                                                              │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐ ┌──────┐ ┌──────┐       │
│  │ SSD0 │ │ SSD1 │ │ SSD2 │ │ SSD3 │ ... │ SSD13│ │ SSD14│ │ SSD15│       │
│  └──────┘ └──────┘ └──────┘ └──────┘     └──────┘ └──────┘ └──────┘       │
│     ▲        ▲        ▲        ▲             ▲        ▲        ▲            │
│     └────────┴────────┴────────┴─────────────┴────────┴────────┘            │
│              Accessible from all 96 cores via I/O channels                  │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                              Remote Client Host                              │
│                                                                              │
│  Creates 8 NVMe-oF connections:                                             │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ │
│  │ QP 0 │ │ QP 1 │ │ QP 2 │ │ QP 3 │ │ QP 4 │ │ QP 5 │ │ QP 6 │ │ QP 7 │ │
│  └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ │
│     │        │        │        │        │        │        │        │      │
│     └────────┴────────┼────────┼────────┴────────┴────────┼────────┼──────┘
│                       │        │                           │        │
│                  To Port 0  To Port 1                 To Port 0  To Port 1
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Simplified Connection Mapping Diagram

```
Client Host                    SPDK Target (96 Cores)
┌──────────┐
│          │                   ┌─────────────────────────────────────┐
│  QP 0 ───┼──── Port 0 ──────▶│ Core 0: Poller 0-0 → CQ 0-0        │
│          │                   └─────────────────────────────────────┘
│  QP 1 ───┼──── Port 0 ──────▶│ Core 1: Poller 1-0 → CQ 1-0        │
│          │                   └─────────────────────────────────────┘
│  QP 2 ───┼──── Port 1 ──────▶│ Core 2: Poller 2-1 → CQ 2-1        │
│          │                   └─────────────────────────────────────┘
│  QP 3 ───┼──── Port 1 ──────▶│ Core 3: Poller 3-1 → CQ 3-1        │
│          │                   └─────────────────────────────────────┘
│  QP 4 ───┼──── Port 0 ──────▶│ Core 4: Poller 4-0 → CQ 4-0        │
│          │                   └─────────────────────────────────────┘
│  QP 5 ───┼──── Port 0 ──────▶│ Core 5: Poller 5-0 → CQ 5-0        │
│          │                   └─────────────────────────────────────┘
│  QP 6 ───┼──── Port 1 ──────▶│ Core 6: Poller 6-1 → CQ 6-1        │
│          │                   └─────────────────────────────────────┘
│  QP 7 ───┼──── Port 1 ──────▶│ Core 7: Poller 7-1 → CQ 7-1        │
└──────────┘                   └─────────────────────────────────────┘

                               Cores 8-95: Pollers ready, no QPs assigned yet
```

---

## Request Flow Diagram (Example: Request on Core 5)

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 1: Client sends NVMe READ command via QP 5                         │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 2: RDMA NIC (IB Port 0)                                            │
│  - Receives packet over network                                         │
│  - DMAs data to buffer from SRQ 0                                       │
│  - Posts work completion to CQ 5-0                                      │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 3: Core 5 - Reactor Main Loop                                      │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ while (1) {                                                         │ │
│  │   poll_rdma_poller_5_0();  ◄─── Finds completion in CQ 5-0         │ │
│  │   poll_rdma_poller_5_1();                                           │ │
│  │   poll_ssd_channels();                                              │ │
│  │ }                                                                   │ │
│  └────────────────────────────────────────────────────────────────────┘ │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 4: Core 5 - RDMA Poller 5-0                                        │
│  - Extracts NVMe command from buffer                                    │
│  - Queues to incoming_queue                                             │
│  - Request State Machine: NEW → NEED_BUFFER → HAVE_BUFFER               │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 5: Core 5 - Submit I/O to SSD 3                                    │
│  - Uses I/O channel to SSD 3                                            │
│  - Submits non-blocking READ                                            │
│  - Returns immediately (no waiting!)                                    │
│  - State: READY_TO_EXECUTE → EXECUTING                                  │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 6: Core 5 - Continue polling (processes other requests)            │
│  - Polls SSD I/O channels                                               │
│  - Finds completion from SSD 3                                          │
│  - Invokes callback on Core 5                                           │
│  - State: EXECUTED → READY_TO_COMPLETE                                  │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 7: Core 5 - Send RDMA Response                                     │
│  - Post RDMA READ (DMA data from SSD buffer to client memory)          │
│  - Post SEND (NVMe completion response)                                 │
│  - Submit to QP 5's Send Queue                                          │
│  - State: TRANSFERRING_CONTROLLER_TO_HOST → COMPLETING                  │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 8: RDMA NIC completes operations                                   │
│  - DMAs data to client                                                  │
│  - Sends completion                                                     │
│  - Posts completions to CQ 5-0                                          │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Step 9: Core 5 - Poll and process completions                           │
│  - RDMA Poller 5-0 finds completions                                    │
│  - Marks request as COMPLETED                                           │
│  - Frees resources                                                      │
└─────────────────────────────────────────────────────────────────────────┘

                    *** Entire request processed on Core 5 ***
                    *** No cross-core communication needed! ***
```

---

### Hardware Configuration
- **Target Server**: 96 CPU cores
- **Storage**: 16 NVMe SSDs
- **Network**: 2 InfiniBand HCAs (Host Channel Adapters), each with 1 port
  - IB Port 0: 192.168.1.1:4420
  - IB Port 1: 192.168.1.2:4420
- **Client Host**: 1 remote host connecting via RDMA

---

## Device/Port Terminology Clarification

**"Device" in RDMA context** = InfiniBand HCA (Network Card)
**"Port" in RDMA context** = Physical port on the IB card

In your case:
- 2 RDMA devices (2 IB HCAs)
- 2 RDMA ports (one per HCA)

**NOT the SSDs** - SSDs are separate block devices accessed via different pollers.

---

## Resource Allocation Breakdown

### 1. Poll Groups (One per Reactor)

**Total Poll Groups**: 96 (one for each core)

```
Poll Group 0  → Reactor 0 (Core 0)
Poll Group 1  → Reactor 1 (Core 1)
Poll Group 2  → Reactor 2 (Core 2)
...
Poll Group 95 → Reactor 95 (Core 95)
```

**Code**: Each poll group created during transport initialization
- Location: [`lib/nvmf/transport.h:19-20`](lib/nvmf/transport.h:19-20)

---

### 2. RDMA Pollers (One per IB Port per Poll Group)

**Total RDMA Pollers**: 96 × 2 = **192 pollers**

Each poll group has one poller for each IB port:

```
Poll Group 0 (Core 0):
  ├─ RDMA Poller for IB Port 0 (192.168.1.1:4420)
  └─ RDMA Poller for IB Port 1 (192.168.1.2:4420)

Poll Group 1 (Core 1):
  ├─ RDMA Poller for IB Port 0
  └─ RDMA Poller for IB Port 1

...

Poll Group 95 (Core 95):
  ├─ RDMA Poller for IB Port 0
  └─ RDMA Poller for IB Port 1
```

**Each RDMA Poller**:
- Monitors one Completion Queue (CQ)
- Handles connections on that port assigned to this core
- Polls for RDMA work completions

---

### 3. Shared Receive Queue (SRQ) - Optional

**Configuration Option A: With SRQ (Memory Efficient)**

**Total SRQs**: 2 (one per IB port, shared across all cores)

```
SRQ for IB Port 0:
  - Shared by all 96 pollers monitoring Port 0
  - Contains pool of receive buffers
  - Size: e.g., 4096 buffers

SRQ for IB Port 1:
  - Shared by all 96 pollers monitoring Port 1
  - Contains pool of receive buffers
  - Size: e.g., 4096 buffers
```

**Configuration Option B: Without SRQ (Per-QP Queues)**

Each Queue Pair has its own dedicated receive queue.

**For this example, assume SRQ is used** (typical for many connections).

---

### 4. Queue Pairs (QPs) - Per Client Connection

**Scenario**: 1 host creates multiple connections

Let's say the host creates **8 connections** (typical for multi-queue):

**Total QPs**: 8

```
Connection 0: QP 0 → Assigned to Poll Group 0 (Core 0), IB Port 0
Connection 1: QP 1 → Assigned to Poll Group 1 (Core 1), IB Port 0
Connection 2: QP 2 → Assigned to Poll Group 2 (Core 2), IB Port 1
Connection 3: QP 3 → Assigned to Poll Group 3 (Core 3), IB Port 1
Connection 4: QP 4 → Assigned to Poll Group 4 (Core 4), IB Port 0
Connection 5: QP 5 → Assigned to Poll Group 5 (Core 5), IB Port 0
Connection 6: QP 6 → Assigned to Poll Group 6 (Core 6), IB Port 1
Connection 7: QP 7 → Assigned to Poll Group 7 (Core 7), IB Port 1
```

**Assignment Algorithm**: Round-robin across poll groups
- Code: [`lib/nvmf/nvmf_internal.h:107`](lib/nvmf/nvmf_internal.h:107) - `next_poll_group`

**Each QP has**:
- Send Queue (SQ): For sending responses
- Receive Queue (RQ): Uses SRQ if enabled, or dedicated queue
- Completion Queue (CQ): Monitored by the poller on assigned core

---

### 5. Completion Queues (CQs)

**Total CQs**: 192 (one per RDMA poller)

```
Core 0:
  ├─ CQ for IB Port 0 (monitors QP 0)
  └─ CQ for IB Port 1

Core 1:
  ├─ CQ for IB Port 0 (monitors QP 1)
  └─ CQ for IB Port 1

Core 2:
  ├─ CQ for IB Port 0
  └─ CQ for IB Port 1 (monitors QP 2)

...
```

**Each CQ**:
- Receives work completions from QPs assigned to this core
- Polled by the RDMA poller on this core
- Size: e.g., 4096 entries

---

### 6. Block Device (SSD) Pollers

**Total SSD Pollers**: Depends on configuration

**Option A: All cores access all SSDs**
```
Each of 96 cores has I/O channels to all 16 SSDs
Total I/O channels: 96 × 16 = 1,536
```

**Option B: Dedicated assignment (more common)**
```
Cores 0-5   → SSD 0
Cores 6-11  → SSD 1
Cores 12-17 → SSD 2
...
Cores 90-95 → SSD 15

Each group of 6 cores shares access to one SSD
```

**Option C: NUMA-aware (best performance)**
```
NUMA Node 0 (Cores 0-47):
  - SSDs 0-7 (connected to PCIe on NUMA 0)

NUMA Node 1 (Cores 48-95):
  - SSDs 8-15 (connected to PCIe on NUMA 1)
```

---

## Complete Resource Map for Your Scenario

### Summary Table

| Resource Type | Total Count | Per Core | Notes |
|---------------|-------------|----------|-------|
| **Reactors (OS Threads)** | 96 | 1 | One per CPU core |
| **Poll Groups** | 96 | 1 | One per reactor |
| **RDMA Pollers** | 192 | 2 | One per IB port per core |
| **SRQs** | 2 | - | One per IB port (shared) |
| **Queue Pairs (QPs)** | 8 | - | One per client connection |
| **Completion Queues** | 192 | 2 | One per RDMA poller |
| **SSD I/O Channels** | 1,536 | 16 | Assuming all-to-all access |

---

## Detailed Example: Request Flow on Core 5

Let's trace a request arriving on **Core 5**:

### Setup on Core 5:
```
Reactor 5 (Core 5):
  ├─ Poll Group 5
  │   ├─ RDMA Poller for IB Port 0
  │   │   ├─ Monitors CQ 5-0
  │   │   └─ Handles QP 5 (Connection 5)
  │   └─ RDMA Poller for IB Port 1
  │       └─ Monitors CQ 5-1
  │
  ├─ SSD Pollers (I/O channels to SSDs)
  │   ├─ I/O Channel to SSD 0
  │   ├─ I/O Channel to SSD 1
  │   └─ ... (to all 16 SSDs)
  │
  └─ SPDK Threads (if any assigned to this core)
```

### Request Processing:

1. **Client sends NVMe READ command** via Connection 5 (QP 5)

2. **RDMA NIC** (IB Port 0):
   - Receives packet
   - DMAs to buffer from SRQ for Port 0
   - Posts completion to CQ 5-0

3. **Reactor 5 Main Loop**:
   ```
   while (1) {
       // Poll RDMA Poller for Port 0
       poll_rdma_port_0();  // Finds completion in CQ 5-0

       // Poll RDMA Poller for Port 1
       poll_rdma_port_1();  // No completions

       // Poll SSD I/O channels
       poll_ssd_channels();

       // Process other work
   }
   ```

4. **RDMA Poller for Port 0** (on Core 5):
   - Polls CQ 5-0
   - Finds work completion for QP 5
   - Extracts NVMe command
   - Queues to incoming_queue

5. **Request State Machine** (on Core 5):
   - Processes request
   - Determines target SSD (e.g., SSD 3)

6. **Submit I/O to SSD 3** (on Core 5):
   - Uses I/O channel to SSD 3
   - Submits non-blocking READ
   - Returns immediately

7. **Continue Polling** (on Core 5):
   - Polls SSD I/O channels
   - Finds completion from SSD 3
   - Invokes callback

8. **Send Response** (on Core 5):
   - Posts RDMA READ (DMA data to client)
   - Posts SEND (completion)
   - Submits to QP 5's Send Queue

9. **RDMA NIC** completes operations:
   - Posts completions to CQ 5-0
   - Core 5 polls and processes completions

**Entire request processed on Core 5** - no cross-core communication needed!

---

## Why This Architecture?

### Benefits:

1. **Load Distribution**:
   - 8 connections spread across 8 cores
   - Each core handles ~1/8 of the traffic

2. **No Lock Contention**:
   - Each QP assigned to one core
   - That core exclusively handles all requests from that QP

3. **Cache Efficiency**:
   - Request data stays in L1/L2 cache of one core
   - No cache line bouncing between cores

4. **Scalability**:
   - Add more client connections → spread across more cores
   - Linear scaling up to 96 cores

5. **NUMA Awareness**:
   - Can assign connections to cores near their target SSDs
   - Minimizes cross-NUMA memory access

---

## Configuration in Practice

### SPDK Configuration File Example:

```json
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "method": "bdev_nvme_attach_controller",
          "params": {
            "name": "Nvme0",
            "trtype": "PCIe",
            "traddr": "0000:01:00.0"
          }
        }
        // ... repeat for all 16 SSDs
      ]
    },
    {
      "subsystem": "nvmf",
      "config": [
        {
          "method": "nvmf_create_transport",
          "params": {
            "trtype": "RDMA",
            "max_queue_depth": 128,
            "num_shared_buffers": 4095,
            "no_srq": false  // Use SRQ
          }
        },
        {
          "method": "nvmf_subsystem_add_listener",
          "params": {
            "nqn": "nqn.2016-06.io.spdk:cnode1",
            "listen_address": {
              "trtype": "RDMA",
              "traddr": "192.168.1.1",  // IB Port 0
              "trsvcid": "4420"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_listener",
          "params": {
            "nqn": "nqn.2016-06.io.spdk:cnode1",
            "listen_address": {
              "trtype": "RDMA",
              "traddr": "192.168.1.2",  // IB Port 1
              "trsvcid": "4420"
            }
          }
        }
      ]
    }
  ]
}
```

### Runtime Allocation:

When SPDK starts:
1. Creates 96 reactors (one per core)
2. Creates 96 poll groups (one per reactor)
3. Creates 2 RDMA pollers per poll group (one per IB port) = 192 total
4. Creates 2 SRQs (one per IB port)
5. Creates 192 CQs (one per RDMA poller)

When client connects:
1. Client creates 8 connections (QPs)
2. Each QP assigned round-robin to poll groups 0-7
3. Each QP monitored by the CQ on its assigned core

---

## Key Takeaways

1. **"Device/Port" = InfiniBand HCA and its physical port**, NOT SSDs
2. **RDMA Pollers** = One per IB port per core = 96 cores × 2 ports = 192
3. **QPs** = One per client connection = 8 in this example
4. **SRQ** = Shared across all cores for each IB port = 2 total
5. **Request stays on one core** from arrival to completion
6. **SSDs accessed via separate I/O channels**, independent of RDMA pollers

This architecture enables SPDK to efficiently handle high-throughput, low-latency storage traffic across many cores without lock contention.