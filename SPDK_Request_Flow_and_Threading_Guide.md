# SPDK NVMe-oF Request Flow and Threading Model Guide

## Overview

This guide explains the end-to-end work request process in SPDK NVMe-oF and the reactor-based threading model that coordinates request processing across CPU cores.

---

## 1. End-to-End Request Flow (Read Data Example)

### 1.1 Request Reception at RDMA Transport Layer

The journey of a read request from a remote client begins at the RDMA transport layer:

#### Step 1: RDMA Receive Buffers Posted

**Before any requests arrive**, SPDK posts receive buffers to the RDMA NIC:

- Location: [`lib/nvmf/rdma.c:830-834`](lib/nvmf/rdma.c:830-834) - During resource initialization
- Function: `spdk_rdma_provider_srq_flush_recv_wrs()` or `spdk_rdma_provider_qp_flush_recv_wrs()`
- These functions post receive Work Requests (WRs) to the RDMA NIC
- The RDMA NIC uses these buffers to store incoming NVMe commands from remote clients

**Continuous Buffer Replenishment**:
- Location: [`lib/nvmf/rdma.c:4896`](lib/nvmf/rdma.c:4896) - `_poller_submit_recvs()`
- After processing each request, buffers are reposted to maintain a pool of available receive buffers

#### Step 2: Remote Client Sends Command

When a remote client issues an NVMe command (e.g., READ):
1. Client's RDMA NIC performs **RDMA SEND** operation
2. Command travels over InfiniBand/RoCE network
3. Target's RDMA NIC receives the data into one of the pre-posted receive buffers
4. RDMA NIC generates a **completion event** in the Completion Queue (CQ)

#### Step 3: SPDK Polls Completion Queue

**Entry Point: RDMA Completion Queue Polling**
- Location: [`lib/nvmf/rdma.c:4968`](lib/nvmf/rdma.c:4968) - `nvmf_rdma_poll_group_poll()`
- Each poll group polls its RDMA pollers to check for incoming work completions
- This is called repeatedly by the reactor's main loop

**Work Completion Processing**
- Location: [`lib/nvmf/rdma.c:4800-4818`](lib/nvmf/rdma.c:4800-4818)
- When `IBV_WC_RECV` completion arrives (RDMA_WR_TYPE_RECV):
  - The RDMA receive buffer contains the NVMe command from the remote client
  - Request is queued to `incoming_queue` for processing
  - Queue pair is added to `active_qpairs` list
  - Queue depth is incremented

### 1.2 Request State Machine

Requests progress through a well-defined state machine defined in [`lib/nvmf/rdma.c:48-100`](lib/nvmf/rdma.c:48-100):

```
RDMA_REQUEST_STATE_NEW
  ↓
RDMA_REQUEST_STATE_NEED_BUFFER (if buffer needed)
  ↓
RDMA_REQUEST_STATE_HAVE_BUFFER
  ↓
RDMA_REQUEST_STATE_TRANSFERRING_HOST_TO_CONTROLLER (for writes)
  ↓
RDMA_REQUEST_STATE_READY_TO_EXECUTE
  ↓
RDMA_REQUEST_STATE_EXECUTING
  ↓
RDMA_REQUEST_STATE_EXECUTED
  ↓
RDMA_REQUEST_STATE_TRANSFERRING_CONTROLLER_TO_HOST (for reads)
  ↓
RDMA_REQUEST_STATE_COMPLETING
  ↓
RDMA_REQUEST_STATE_COMPLETED
```

**State Processing**: [`lib/nvmf/rdma.c:2500-2547`](lib/nvmf/rdma.c:2500-2547) - `nvmf_rdma_request_process()`

### 1.3 Request Execution at Block Device Layer

**Command Routing**:
- Admin commands: [`lib/nvmf/ctrlr.c`](lib/nvmf/ctrlr.c) - `nvmf_ctrlr_process_admin_cmd()`
- I/O commands: [`lib/nvmf/ctrlr.c`](lib/nvmf/ctrlr.c) - `nvmf_ctrlr_process_io_cmd()`

**Block Device I/O**:
- Read operation: [`lib/nvmf/ctrlr_bdev.c`](lib/nvmf/ctrlr_bdev.c) - `nvmf_bdev_ctrlr_read_cmd()`
- Completion callback: [`lib/nvmf/ctrlr_bdev.c:71-112`](lib/nvmf/ctrlr_bdev.c:71-112) - `nvmf_bdev_ctrlr_complete_cmd()`

**For a READ request**:
1. RDMA READ operation transfers data from controller to host memory
2. Completion work request (RDMA_WR_TYPE_DATA) arrives at [`lib/nvmf/rdma.c:4819-4862`](lib/nvmf/rdma.c:4819-4862)
3. Request transitions to READY_TO_COMPLETE state
4. Response is sent back to client

### 1.4 Data Flow Summary

```
Remote Client
    ↓ (RDMA SEND - NVMe Command)
RDMA NIC → Completion Queue
    ↓
nvmf_rdma_poll_group_poll() [lib/nvmf/rdma.c:4968]
    ↓
nvmf_rdma_poller_poll() - Process work completions
    ↓
Request State Machine [lib/nvmf/rdma.c:2500-2547]
    ↓
nvmf_ctrlr_process_io_cmd() [lib/nvmf/ctrlr.c]
    ↓
nvmf_bdev_ctrlr_read_cmd() [lib/nvmf/ctrlr_bdev.c]
    ↓
Block Device Layer (AIO/NVMe/etc.)
    ↓
nvmf_bdev_ctrlr_complete_cmd() [lib/nvmf/ctrlr_bdev.c:71]
    ↓
RDMA READ (Data Transfer to Host)
    ↓
RDMA SEND (Completion Response)
    ↓
Remote Client
```

---

## 2. SPDK Reactor/Threading Model

### 2.1 Core Concepts

**Reactor = Thread per CPU Core**

SPDK uses a **reactor model** where each reactor is a lightweight thread pinned to a specific CPU core. This is fundamentally different from traditional multi-threaded models.

**Key Files**:
- [`lib/event/reactor.c`](lib/event/reactor.c) - Reactor implementation
- [`lib/thread/thread.c`](lib/thread/thread.c) - SPDK thread abstraction
- [`include/spdk/thread.h`](include/spdk/thread.h) - Thread API

### 2.2 Reactor Initialization

**Global Reactor Array**: [`lib/event/reactor.c:35-36`](lib/event/reactor.c:35-36)
```c
static struct spdk_reactor *g_reactors;
static uint32_t g_reactor_count;
```

**Answer to Question 2**: Yes, if there are N CPU cores allocated to SPDK, there will be N reactors (threads), one per core.

**Reactor Structure** (from [`lib/event/reactor.c`](lib/event/reactor.c)):
- Each reactor has its own event queue
- Each reactor manages multiple SPDK threads
- Each reactor has pollers for I/O operations

### 2.3 SPDK Thread vs OS Thread vs Reactor

**This is a critical distinction that often causes confusion:**

#### Reactor (OS Thread)
- **One reactor = One OS pthread** pinned to a specific CPU core
- If you have N CPU cores allocated to SPDK, you get N reactors (N OS threads)
- Each reactor runs continuously in a polling loop
- Location: [`lib/event/reactor.c:1008-1036`](lib/event/reactor.c:1008-1036) - `reactor_run()`

#### SPDK Thread (Lightweight Context)
- **Multiple SPDK threads can run on a single reactor**
- SPDK threads are NOT OS threads - they are lightweight, stackless execution contexts
- Think of them as "virtual threads" or "coroutines"
- They are cooperatively scheduled within a reactor
- Location: [`include/spdk/thread.h:38`](include/spdk/thread.h:38)

**Analogy**:
- Reactor = Factory worker (OS thread)
- SPDK Thread = Task assigned to that worker
- One worker can handle multiple tasks, switching between them cooperatively

**SPDK Thread Structure** ([`lib/thread/thread.c:114-150`](lib/thread/thread.c:114-150)):
```c
struct spdk_thread {
    TAILQ_HEAD(active_pollers_head, spdk_poller) active_pollers;
    RB_HEAD(timed_pollers_tree, spdk_poller) timed_pollers;
    TAILQ_HEAD(paused_pollers_head, spdk_poller) paused_pollers;
    struct spdk_ring *messages;
    RB_HEAD(io_channel_tree, spdk_io_channel) io_channels;
};
```

**Example Configuration**:
```
4 CPU cores → 4 Reactors (4 OS threads)
Each reactor can run multiple SPDK threads:
  Reactor 0 (Core 0): SPDK Thread A, SPDK Thread B
  Reactor 1 (Core 1): SPDK Thread C
  Reactor 2 (Core 2): SPDK Thread D, SPDK Thread E, SPDK Thread F
  Reactor 3 (Core 3): SPDK Thread G
```

### 2.4 Work Distribution and Coordination

**Poll Groups** ([`lib/nvmf/nvmf_internal.h:183-195`](lib/nvmf/nvmf_internal.h:183-195)):
```c
struct spdk_nvmf_subsystem_poll_group {
    struct spdk_nvmf_subsystem_pg_ns_info *ns_info;
    uint32_t num_ns;
    enum spdk_nvmf_subsystem_state state;
    uint64_t mgmt_io_outstanding;
    TAILQ_HEAD(, spdk_nvmf_request) queued;
};
```

**How Reactors Coordinate**:

1. **Event-Based Communication** ([`lib/event/reactor.c:532-592`](lib/event/reactor.c:532-592)):
   - `spdk_event_allocate()` - Create event for target core
   - `spdk_event_call()` - Send event to target reactor
   - Events are enqueued to target reactor's ring buffer
   - Target reactor processes events in `event_queue_run_batch()`

   **Who calls `spdk_event_call()`?**
   - Any code that needs to execute a function on a specific reactor/core
   - Examples:
     - Scheduler moving SPDK threads between cores: [`lib/event/reactor.c:1181`](lib/event/reactor.c:1181)
     - Subsystem operations requiring specific core execution
     - Cross-core notifications and synchronization

   **How is the target thread decided?**
   - The caller explicitly specifies the target core number in `spdk_event_allocate(lcore, fn, arg1, arg2)`
   - The `lcore` parameter determines which reactor will execute the event
   - Example: To run function on core 2, call `spdk_event_allocate(2, my_function, arg1, arg2)`

2. **Message Passing** ([`lib/thread/thread.c:136`](lib/thread/thread.c:136)):
   - Each SPDK thread has a message ring
   - Cross-thread communication via `spdk_thread_send_msg()`
   - Messages are processed during thread polling

3. **I/O Channel Abstraction**:
   - Each SPDK thread has per-device I/O channels
   - Provides thread-local context for I/O operations
   - Eliminates need for locks in fast path

### 2.5 Request Processing Flow Across Threads

**Scenario**: Request arrives on Core 0, needs processing on Core 2

```
Core 0 (Reactor 0)
    ↓
RDMA Poller detects incoming request
    ↓
Request assigned to Poll Group on Core 0
    ↓
If subsystem/namespace requires different core:
    ↓
spdk_event_call() to Core 2
    ↓
Core 2 (Reactor 2)
    ↓
Event processed, request executed
    ↓
Completion callback
    ↓
If response needs Core 0:
    ↓
spdk_event_call() back to Core 0
    ↓
Core 0 sends RDMA response to client
```

### 2.6 Polling Model

**Reactor Main Loop** ([`lib/event/reactor.c`](lib/event/reactor.c)):
```
while (reactor running) {
    1. Process events from event queue
    2. Poll all SPDK threads on this reactor
       - Run active pollers
       - Process messages
       - Handle I/O completions
    3. Check for scheduling decisions
    4. Yield if in interrupt mode
}
```

**NVMf Transport Polling** ([`lib/nvmf/rdma.c:4968`](lib/nvmf/rdma.c:4968)):
- Each poll group polls its RDMA completion queues
- Processes work completions (receives, sends, RDMA operations)
- Advances request state machines
- Submits new work requests

### 2.7 Lock-Free Design

**Key Principle**: Each reactor operates independently on its own data structures without locks in the fast path.

**Mechanisms**:
- **Thread affinity**: Data structures accessed by single thread only
- **Message passing**: Cross-thread communication without shared state
- **I/O channels**: Thread-local device contexts
- **Lock-free rings**: For event/message queues

**Example**: RDMA qpair processing ([`lib/nvmf/rdma.c:4978-4987`](lib/nvmf/rdma.c:4978-4987))
- Each poller processes its own qpairs
- No locks needed during request processing
- Completion callbacks execute on same thread

---

## 3. Threading Model Advantages

### 3.1 Performance Benefits

1. **No Context Switching**: Cooperative scheduling within reactor
2. **No Lock Contention**: Lock-free fast path
3. **Cache Efficiency**: Thread-local data structures
4. **Predictable Latency**: No preemption in critical paths

### 3.2 Scalability

- **Linear Scaling**: Each core processes independently
- **NUMA Awareness**: Can assign reactors to specific NUMA nodes
- **Dynamic Load Balancing**: Scheduler can move threads between reactors

---

## 4. Code References Summary

### Request Flow
- RDMA polling: [`lib/nvmf/rdma.c:4968`](lib/nvmf/rdma.c:4968)
- Request state machine: [`lib/nvmf/rdma.c:48-100`](lib/nvmf/rdma.c:48-100), [`lib/nvmf/rdma.c:2500-2547`](lib/nvmf/rdma.c:2500-2547)
- Block device I/O: [`lib/nvmf/ctrlr_bdev.c:71-112`](lib/nvmf/ctrlr_bdev.c:71-112)

### Threading Model
- Reactor core: [`lib/event/reactor.c:35-36`](lib/event/reactor.c:35-36)
- SPDK thread: [`lib/thread/thread.c:114-150`](lib/thread/thread.c:114-150)
- Event system: [`lib/event/reactor.c:532-592`](lib/event/reactor.c:532-592)
- Thread API: [`include/spdk/thread.h`](include/spdk/thread.h)

### NVMf Subsystem
- Internal structures: [`lib/nvmf/nvmf_internal.h`](lib/nvmf/nvmf_internal.h)
- Controller: [`lib/nvmf/ctrlr.c`](lib/nvmf/ctrlr.c)
- Transport abstraction: [`lib/nvmf/transport.h`](lib/nvmf/transport.h)

---

## 5. Key Takeaways

1. **Request Flow**: Remote client → RDMA NIC → Completion Queue → Reactor Poll → State Machine → Block Device → Completion → Response
2. **Threading**: N cores = N reactors (OS threads), each running multiple SPDK threads (lightweight contexts)
3. **Coordination**: Event-based message passing between reactors, lock-free within reactor
4. **Performance**: Lock-free fast path, cooperative scheduling, thread-local data structures

This architecture enables SPDK to achieve extremely high performance and low latency for NVMe-oF storage operations.