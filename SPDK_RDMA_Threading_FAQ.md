# SPDK RDMA and Threading Deep Dive FAQ

## Detailed Answers to Advanced Questions

### Q1: What does "post buffer/requests" to RDMA NIC mean?

**Answer**: "Posting" means registering memory buffers with the RDMA NIC hardware so it can DMA data directly into them.

**Technical Details**:
- SPDK allocates memory buffers in host RAM
- These buffers are registered with the RDMA NIC via `ibv_post_recv()` (wrapped by SPDK)
- The NIC maintains a list of available receive buffers
- When data arrives over the network, the NIC **directly writes** into these buffers via DMA
- No CPU involvement during data transfer (zero-copy)

**Code Location**: [`include/spdk_internal/rdma_provider.h:92-100`](include/spdk_internal/rdma_provider.h:92-100)
```c
/**
 * Append the given recv wr structure to the SRQ's outstanding recv list.
 * This function accepts either a single Work Request or the first WR in a linked list.
 */
bool spdk_rdma_provider_srq_queue_recv_wrs(struct spdk_rdma_provider_srq *rdma_srq,
        struct ibv_recv_wr *first);
```

**Actual posting happens in**: [`lib/nvmf/rdma.c:831`](lib/nvmf/rdma.c:831) - `spdk_rdma_provider_srq_flush_recv_wrs()`

---

### Q2: What are SRQ flush and QP flush?

**SRQ (Shared Receive Queue)**:
- One receive queue shared by multiple Queue Pairs (connections)
- More memory efficient when handling many connections
- All connections share the same pool of receive buffers

**QP (Queue Pair)**:
- Each connection has its own dedicated send/receive queues
- More memory overhead but better isolation

**"Flush" operation**:
- Takes queued Work Requests and actually submits them to the RDMA NIC hardware
- Batching mechanism: queue multiple WRs, then flush them all at once
- Reduces doorbell overhead (hardware notifications)

**Code**:
- SRQ flush: [`lib/nvmf/rdma.c:4580`](lib/nvmf/rdma.c:4580)
- QP flush: [`lib/nvmf/rdma.c:4587`](lib/nvmf/rdma.c:4587)

---

### Q3: Does section 1.1 (buffer posting) happen during create transport step?

**Answer**: Yes, during transport initialization and connection setup.

**Timeline**:
1. **Transport Creation**: [`lib/nvmf/rdma.c:2567-2582`](lib/nvmf/rdma.c:2567-2582) - `nvmf_rdma_opts_init()`
2. **Listen on Address**: [`lib/nvmf/rdma.c:3003-3131`](lib/nvmf/rdma.c:3003-3131) - `nvmf_rdma_listen()`
   - Creates RDMA CM ID
   - Binds to address
   - Starts listening for connections
3. **Connection Accepted**: When client connects, resources are allocated
4. **Buffer Posting**: [`lib/nvmf/rdma.c:830-834`](lib/nvmf/rdma.c:830-834) - Initial receive buffers posted

**Continuous Operation**: Buffers are continuously reposted after each request is processed.

---

### Q4: What are the different queues to process a request in SPDK thread?

**Multiple Queue Levels**:

1. **RDMA Hardware Queues** (in NIC):
   - Send Queue (SQ): Outgoing operations
   - Receive Queue (RQ): Incoming data buffers
   - Completion Queue (CQ): Completed operations

2. **SPDK Software Queues** ([`lib/nvmf/rdma.c:48-100`](lib/nvmf/rdma.c:48-100)):
   - `incoming_queue`: Newly received requests
   - `free_queue`: Available request structures
   - `pending_rdma_read_queue`: Waiting for RDMA READ operations
   - `pending_rdma_send_queue`: Waiting to send responses
   - `active_qpairs`: Queue pairs with pending work

3. **SPDK Thread Queues** ([`lib/thread/thread.c:114-150`](lib/thread/thread.c:114-150)):
   - `active_pollers`: Pollers running every iteration
   - `timed_pollers`: Pollers running periodically
   - `messages`: Cross-thread messages
   - `io_channels`: Per-device I/O contexts

**Request Flow Through Queues**:
```
RDMA RQ → incoming_queue → Request State Machine →
Block Device → Completion → pending_rdma_send_queue → RDMA SQ
```

---

### Q5: Who creates RDMA poll group and poller? When are they created?

**Poll Group Creation**:
- **Who**: NVMf transport layer
- **When**: During subsystem initialization and when adding transport to poll group
- **Code**: [`lib/nvmf/transport.h:19-20`](lib/nvmf/transport.h:19-20) - `nvmf_transport_poll_group_create()`

**Poller Creation**:
- **Who**: Each reactor creates pollers for its assigned devices
- **When**: When poll group is added to a reactor
- **Code**: Search for `nvmf_rdma_poll_group_create` in [`lib/nvmf/rdma.c`](lib/nvmf/rdma.c)

**Relationship**:
```
Target → Transport → Poll Groups (one per reactor) → Pollers (one per device/port)
```

---

### Q6: Work request in completion queue - is it processed? What does completion mean?

**Critical Clarification**: Work Requests (WRs) are NOT in the Completion Queue!

**Correct Flow**:
1. **Work Request (WR)**: Posted to Send/Receive Queue
   - Describes operation to perform (SEND, RECV, RDMA_READ, RDMA_WRITE)

2. **RDMA NIC Processes WR**: Hardware executes the operation

3. **Work Completion (WC)**: Generated when operation finishes
   - Posted to Completion Queue by hardware
   - Contains status and metadata

4. **SPDK Polls CQ**: [`lib/nvmf/rdma.c:4968-4989`](lib/nvmf/rdma.c:4968-4989)
   - Retrieves Work Completions
   - Processes based on completion type

**"Completion" means**: The RDMA operation finished (successfully or with error)

**Code Processing Completions**: [`lib/nvmf/rdma.c:4800-4887`](lib/nvmf/rdma.c:4800-4887)
```c
switch (rdma_wr->type) {
case RDMA_WR_TYPE_RECV:  // Received command from client
case RDMA_WR_TYPE_SEND:  // Sent response to client
case RDMA_WR_TYPE_DATA:  // RDMA READ/WRITE completed
}
```

---

### Q7: Is RDMA NIC logic in SPDK code?

**Answer**: No, RDMA NIC logic is in hardware and kernel drivers.

**SPDK's Role**:
- Uses **libibverbs** library to communicate with RDMA hardware
- Wraps libibverbs in [`include/spdk_internal/rdma_provider.h`](include/spdk_internal/rdma_provider.h)
- Provides higher-level abstractions

**Architecture**:
```
SPDK Code (User Space)
    ↓ (libibverbs API)
Kernel RDMA Drivers (mlx5, irdma, etc.)
    ↓ (PCIe)
RDMA NIC Hardware (Mellanox, Intel, etc.)
```

**SPDK does NOT**:
- Implement RDMA protocol
- Handle network packets
- Manage NIC hardware directly

**SPDK DOES**:
- Post Work Requests to NIC
- Poll Completion Queues
- Manage memory registration
- Handle connection management

---

### Q8: Can each reactor have multiple pollers? How do they coordinate?

**Answer**: Yes, each reactor typically has multiple pollers.

**Poller Types**:
1. **RDMA Pollers**: One per RDMA device/port
2. **Block Device Pollers**: One per bdev
3. **Timer Pollers**: Periodic operations
4. **Custom Pollers**: Application-specific

**Coordination**:
- **No explicit coordination needed** - they run sequentially
- Reactor polls each poller in round-robin fashion
- Each poller returns BUSY or IDLE
- No locks needed (single-threaded execution)

**Code**: [`lib/event/reactor.c:1008-1036`](lib/event/reactor.c:1008-1036)
```c
while (1) {
    // Poll all pollers on this reactor
    _reactor_run(reactor);

    // Check for scheduling
    if (time_for_scheduling) {
        _reactors_scheduler_gather_metrics();
    }
}
```

**Example Reactor with Multiple Pollers**:
```
Reactor 0 (Core 0):
  - RDMA Poller (port 0)
  - RDMA Poller (port 1)
  - NVMe Bdev Poller (nvme0)
  - NVMe Bdev Poller (nvme1)
  - Timer Poller (keep-alive)
```

---

### Q9: Which module chooses reactor for a given WR and schedules it? How does request arrive on a reactor if there are many reactors?

**Answer**: The RDMA transport assigns connections to reactors using round-robin.

**Connection Assignment**:
- **Code**: Look for `next_poll_group` in [`lib/nvmf/nvmf_internal.h:107`](lib/nvmf/nvmf_internal.h:107)
- When new connection arrives, it's assigned to next poll group
- Each poll group is associated with one reactor
- Round-robin ensures load balancing

**Request Arrival**:
1. Client connects to target
2. Connection assigned to Poll Group X (on Reactor Y)
3. **All requests from that connection** are processed by Reactor Y
4. RDMA NIC delivers completions to the CQ monitored by Reactor Y

**Key Point**: Connection affinity - once assigned, all requests from that connection stay on the same reactor.

**No per-request scheduling**: The reactor that handles the connection handles all its requests.

---

### Q10: Why would a WR arriving on a core need to be processed on another core?

**Answer**: Usually it doesn't! But there are specific cases:

**Case 1: Block Device Affinity**
- Some block devices (NVMe SSDs) are assigned to specific cores
- If request arrives on Core 0 but SSD is on Core 2
- Request must be forwarded to Core 2

**Case 2: Subsystem Thread Assignment**
- NVMf subsystems can be pinned to specific threads/cores
- If connection is on Core 0 but subsystem is on Core 1
- Request forwarded via `spdk_event_call()`

**Case 3: Load Balancing**
- Scheduler may move SPDK threads between reactors
- Ongoing requests follow the thread to new core

**Code for Cross-Core Forwarding**: [`lib/event/reactor.c:558-592`](lib/event/reactor.c:558-592) - `spdk_event_call()`

**Most Common Case**: Request stays on the same core for efficiency!

---

### Q11: If there are multiple SSDs, how are they associated with cores/reactors? Is it one SSD per core?

**Answer**: Flexible assignment, NOT one-to-one.

**I/O Channel Model**:
- Each SPDK thread gets an I/O channel to each device it uses
- Multiple threads can access the same SSD
- Each thread has its own queue to the SSD (no contention)

**Example Configurations**:

**Configuration A: Dedicated Assignment**
```
Core 0 → SSD 0
Core 1 → SSD 1
Core 2 → SSD 2
Core 3 → SSD 3
```

**Configuration B: Shared Access**
```
Core 0 → SSD 0, SSD 1
Core 1 → SSD 0, SSD 1
Core 2 → SSD 2, SSD 3
Core 3 → SSD 2, SSD 3
```

**Configuration C: All-to-All**
```
Each core has I/O channel to each SSD
Maximum flexibility, potential for contention
```

**NUMA Awareness**:
- Best practice: Assign SSDs to cores on same NUMA node
- Reduces memory access latency
- Code considers PCIe topology

**I/O Channel Code**: [`lib/thread/thread.c:266-282`](lib/thread/thread.c:266-282)

**Key Point**: Assignment is flexible and configurable, optimized for workload.

---

### Q12: If callbacks are executed on the same thread, will the thread be idling/waiting?

**Answer**: No! SPDK uses **asynchronous, non-blocking I/O** with polling.

**How It Works**:

1. **Submit I/O Operation**:
   - Thread submits I/O to block device
   - Returns immediately (non-blocking)
   - No waiting!

2. **Thread Continues Working**:
   - Processes other requests
   - Polls for completions
   - Handles new incoming requests

3. **Completion Callback**:
   - When I/O completes, callback is invoked
   - Callback runs on same thread (no context switch)
   - Processes completion and moves to next state

**Example Flow**:
```
Time 0: Submit READ for Request A → returns immediately
Time 1: Process new Request B
Time 2: Submit WRITE for Request C → returns immediately
Time 3: Poll completion queue
Time 4: Request A completed! → callback invoked
Time 5: Process Request A callback
Time 6: Continue with other work
```

**Code Example**: [`lib/nvmf/ctrlr_bdev.c:71-112`](lib/nvmf/ctrlr_bdev.c:71-112)
```c
static void
nvmf_bdev_ctrlr_complete_cmd(struct spdk_bdev_io *bdev_io, bool success,
                             void *cb_arg)
{
    struct spdk_nvmf_request *req = cb_arg;
    // This callback runs on the same thread that submitted the I/O
    // No blocking, no waiting - just process the completion
    spdk_nvmf_request_complete(req);
    spdk_bdev_free_io(bdev_io);
}
```

**Reactor Loop** ([`lib/event/reactor.c:1008-1036`](lib/event/reactor.c:1008-1036)):
```c
while (1) {
    // Never blocks! Always polling
    process_events();
    poll_all_threads();
    poll_all_devices();
    check_completions();
    // Loop continues immediately
}
```

**Benefits**:
- **No context switches**: Callback on same thread
- **No blocking**: Thread always doing useful work
- **Low latency**: Immediate response to completions
- **High throughput**: Continuous processing

**Contrast with Traditional Blocking I/O**:
```
Traditional:
  submit_io() → BLOCKS → waits → callback → returns

SPDK:
  submit_io() → returns immediately
  (thread continues other work)
  poll() → finds completion → callback invoked
```

---

## Summary Diagram: Complete Request Flow

```
┌─────────────────────────────────────────────────────────────┐
│ Remote Client                                                │
│   Issues NVMe READ command                                   │
└────────────────┬────────────────────────────────────────────┘
                 │ RDMA SEND
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ RDMA NIC (Hardware)                                          │
│   - Receives packet                                          │
│   - DMA to pre-posted buffer                                 │
│   - Generates completion in CQ                               │
└────────────────┬────────────────────────────────────────────┘
                 │ Completion Event
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ Reactor (OS Thread on Core X)                                │
│   Main Loop:                                                 │
│   1. Poll RDMA CQ → finds completion                         │
│   2. Extract request from buffer                             │
│   3. Queue to incoming_queue                                 │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ Request State Machine (same thread)                          │
│   NEW → NEED_BUFFER → HAVE_BUFFER → READY_TO_EXECUTE         │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ Block Device Layer (same thread)                             │
│   - Submit READ to SSD (non-blocking)                        │
│   - Returns immediately                                      │
│   - Thread continues other work                              │
└────────────────┬────────────────────────────────────────────┘
                 │ (thread polls for completion)
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ Completion Callback (same thread)                            │
│   - Data ready in buffer                                     │
│   - Transition to READY_TO_COMPLETE                          │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ RDMA Response (same thread)                                  │
│   - Post RDMA READ (DMA data to client)                      │
│   - Post SEND (completion response)                          │
└────────────────┬────────────────────────────────────────────┘
                 │ RDMA operations
                 ↓
┌─────────────────────────────────────────────────────────────┐
│ Remote Client                                                │
│   Receives data and completion                               │
└─────────────────────────────────────────────────────────────┘
```

**Key Insights**:
1. **Single thread handles entire request** (usually)
2. **No blocking** - always polling
3. **Zero-copy** - RDMA NIC DMAs directly
4. **Lock-free** - thread-local data structures
5. **Asynchronous** - callbacks on same thread

---

## Additional Resources

- Main Guide: [`SPDK_Request_Flow_and_Threading_Guide.md`](SPDK_Request_Flow_and_Threading_Guide.md)
- SPDK Documentation: https://spdk.io/doc/
- RDMA Programming: https://www.rdmamojo.com/

---

*This FAQ addresses deep technical questions about SPDK's RDMA transport and threading architecture with specific code references.*
All cores can access all SSDs
