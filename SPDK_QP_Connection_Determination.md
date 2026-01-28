# SPDK NVMe-oF: What Determines the Number of QPs/Connections?

## Executive Summary

**The number of Queue Pairs (QPs) / connections is determined by the INITIATOR (client), NOT by the target, number of listeners, or number of reactors.**

Each QP represents one NVMe I/O queue connection from the client to the target. The client decides how many I/O queues to create when connecting.

---

## Key Finding: Client Controls Connection Count

### Code Evidence

**Connection Creation Flow:**

1. **Client initiates connection** with RDMA CM (Connection Manager)
2. **Target receives connection request**: [`lib/nvmf/rdma.c:3751-3752`](lib/nvmf/rdma.c:3751-3752)
   ```c
   case RDMA_CM_EVENT_CONNECT_REQUEST:
       rc = nvmf_rdma_connect(transport, event);
   ```

3. **Target processes connection**: [`lib/nvmf/rdma.c:1292-1407`](lib/nvmf/rdma.c:1292-1407)
   - Reads client's queue parameters from private data
   - Negotiates queue depth based on client's request
   - Creates qpair structure
   - Calls `spdk_nvmf_tgt_new_qpair()` to add to poll group

4. **Target assigns to poll group**: [`lib/nvmf/nvmf.c:1337-1371`](lib/nvmf/nvmf.c:1337-1371)
   - Uses round-robin to distribute connections across poll groups/reactors
   - Each connection stays on its assigned reactor

---

## Client-Side Configuration

### Linux NVMe-oF Initiator

When a Linux client connects using `nvme connect`, it specifies the number of I/O queues:

```bash
sudo nvme connect -t rdma \
    -n nqn.2016-06.io.spdk:cnode1 \
    -a 192.168.1.1 \
    -s 4420 \
    --nr-io-queues=8 \      # <-- THIS determines number of QPs
    --queue-size=128
```

**Key Parameter**: `--nr-io-queues=N`
- Creates N I/O queue pairs (plus 1 admin queue pair)
- Each I/O queue = 1 RDMA QP connection
- Total QPs = N + 1 (admin)

**Example Configurations:**

| Client Command | Admin QPs | I/O QPs | Total QPs |
|----------------|-----------|---------|-----------|
| `--nr-io-queues=1` | 1 | 1 | 2 |
| `--nr-io-queues=4` | 1 | 4 | 5 |
| `--nr-io-queues=8` | 1 | 8 | 9 |
| `--nr-io-queues=16` | 1 | 16 | 17 |
| `--nr-io-queues=32` | 1 | 32 | 33 |

### SPDK NVMe-oF Initiator

When using SPDK as an initiator (via `bdev_nvme_attach_controller` RPC):

```json
{
  "method": "bdev_nvme_attach_controller",
  "params": {
    "name": "Nvme0",
    "trtype": "RDMA",
    "traddr": "192.168.1.1",
    "trsvcid": "4420",
    "subnqn": "nqn.2016-06.io.spdk:cnode1",
    "num_io_queues": 8    // <-- THIS determines number of QPs
  }
}
```

**Reference**: [`CHANGELOG.md:1532-1533`](CHANGELOG.md:1532-1533)
> An new parameter `num_io_queues` is added to `bdev_nvme_attach_controller` RPC to allow specifying amount of requested IO queues.

---

## Target-Side Constraints

While the **client decides** how many connections to create, the **target enforces limits**:

### 1. Maximum Queue Pairs Per Controller

**Configuration**: `max_io_qpairs_per_ctrlr` in transport options

**Code**: [`lib/nvmf/nvmf_rpc.c:2479-2480`](lib/nvmf/nvmf_rpc.c:2479-2480)
```c
{
    "max_io_qpairs_per_ctrlr", offsetof(struct nvmf_rpc_create_transport_ctx, opts.max_qpairs_per_ctrlr),
    nvmf_rpc_decode_max_io_qpairs, true
},
```

**Enforcement**: [`lib/nvmf/ctrlr.c:466`](lib/nvmf/ctrlr.c:466)
```c
ctrlr->qpair_mask = spdk_bit_array_create(transport->opts.max_qpairs_per_ctrlr);
```

**Example RPC Configuration:**
```json
{
  "method": "nvmf_create_transport",
  "params": {
    "trtype": "RDMA",
    "max_io_qpairs_per_ctrlr": 64,  // Maximum I/O queues per controller
    "max_queue_depth": 128
  }
}
```

If a client requests more I/O queues than this limit, the target will reject additional connections.

### 2. Queue Depth Negotiation

**Code**: [`lib/nvmf/rdma.c:1335-1384`](lib/nvmf/rdma.c:1335-1384)

The target negotiates queue depth considering:
1. Target's `max_queue_depth` configuration
2. Local NIC hardware limits (`max_qp_wr`)
3. Remote NIC hardware limits (from client's connection request)
4. Client's requested queue sizes (`hrqsize`, `hsqsize`)

```c
/* Start with the maximum queue depth allowed by the target */
max_queue_depth = rtransport->transport.opts.max_queue_depth;

/* Check local NIC hardware limitations */
max_queue_depth = spdk_min(max_queue_depth, port->device->attr.max_qp_wr);

/* Check remote NIC hardware limitations */
max_read_depth = spdk_min(max_read_depth, rdma_param->initiator_depth);

/* Check client's requested sizes */
max_queue_depth = spdk_min(max_queue_depth, private_data->hrqsize);
max_queue_depth = spdk_min(max_queue_depth, private_data->hsqsize + 1);
```

---

## Connection Distribution Across Reactors

### Round-Robin Assignment

**Code**: [`lib/nvmf/nvmf.c:1342-1354`](lib/nvmf/nvmf.c:1342-1354)

```c
group = spdk_nvmf_get_optimal_poll_group(qpair);
if (group == NULL) {
    if (tgt->next_poll_group == NULL) {
        tgt->next_poll_group = TAILQ_FIRST(&tgt->poll_groups);
    }
    group = tgt->next_poll_group;
    tgt->next_poll_group = TAILQ_NEXT(group, link);  // Round-robin
}
```

**Distribution Example** (8 QPs, 96 reactors):
```
QP 0 → Reactor 0
QP 1 → Reactor 1
QP 2 → Reactor 2
QP 3 → Reactor 3
QP 4 → Reactor 4
QP 5 → Reactor 5
QP 6 → Reactor 6
QP 7 → Reactor 7
```

Each connection is assigned to a poll group (reactor) and stays on that reactor for its lifetime.

---

## What Does NOT Determine QP Count

### ❌ Number of Listeners

**Listeners** are RDMA CM listening endpoints (IP:port combinations). They accept incoming connections but don't determine how many connections are created.

**Example:**
- Target has 2 listeners: `192.168.1.1:4420` and `192.168.1.2:4420`
- Client connects to `192.168.1.1:4420` with `--nr-io-queues=8`
- Result: 8 I/O QPs created (all to the same listener)

### ❌ Number of Reactors

**Reactors** (CPU cores) don't determine QP count. They only determine how connections are distributed.

**Example:**
- Target has 96 reactors
- Client connects with `--nr-io-queues=4`
- Result: Only 4 I/O QPs created (assigned to reactors 0-3)

### ❌ Number of SSDs

**SSDs** are accessed via separate I/O channels, independent of RDMA QPs.

**Example:**
- Target has 16 SSDs
- Client connects with `--nr-io-queues=8`
- Result: 8 I/O QPs created, each can access all 16 SSDs via I/O channels

---

## Practical Examples

### Example 1: Single Client, Multiple Queues

**Setup:**
- Target: 96 cores, 2 IB ports, 16 SSDs
- Client: Connects with `--nr-io-queues=8`

**Result:**
- 1 admin QP + 8 I/O QPs = 9 total QPs
- QPs distributed across reactors 0-8 (round-robin)
- Each QP can access all 16 SSDs

### Example 2: Multiple Clients

**Setup:**
- Target: 96 cores, 2 IB ports, 16 SSDs
- Client A: Connects with `--nr-io-queues=4`
- Client B: Connects with `--nr-io-queues=8`

**Result:**
- Client A: 1 admin + 4 I/O = 5 QPs (reactors 0-4)
- Client B: 1 admin + 8 I/O = 9 QPs (reactors 5-13)
- Total: 14 QPs across 14 reactors

### Example 3: High Queue Count

**Setup:**
- Target: 96 cores, configured with `max_io_qpairs_per_ctrlr=64`
- Client: Connects with `--nr-io-queues=32`

**Result:**
- 1 admin + 32 I/O = 33 QPs
- QPs distributed across reactors 0-32
- Remaining 64 reactors available for other clients or idle

---

## Performance Considerations

### Optimal Queue Count

**General Rule**: Match I/O queues to client CPU cores for best performance.

```bash
# On a 16-core client machine
sudo nvme connect -t rdma \
    -n nqn.2016-06.io.spdk:cnode1 \
    -a 192.168.1.1 \
    -s 4420 \
    --nr-io-queues=16    # One queue per core
```

### Why More Queues?

1. **Parallelism**: Each queue can submit I/O independently
2. **CPU Affinity**: Each client CPU core can have its own queue
3. **Reduced Contention**: No lock contention between cores
4. **Better Throughput**: More parallel paths to storage

### Why Fewer Queues?

1. **Resource Conservation**: Each QP consumes memory on both sides
2. **Simpler Management**: Fewer connections to monitor
3. **Lower Overhead**: Less polling overhead on target

---

## Summary Table

| Factor | Determines QP Count? | Role |
|--------|---------------------|------|
| **Client `--nr-io-queues`** | ✅ YES | Primary determinant |
| **Client `num_io_queues` (SPDK)** | ✅ YES | Primary determinant |
| **Target `max_io_qpairs_per_ctrlr`** | ⚠️ LIMIT | Maximum allowed |
| Number of Listeners | ❌ NO | Accept connections |
| Number of Reactors | ❌ NO | Distribute connections |
| Number of SSDs | ❌ NO | Storage backend |
| Number of IB Ports | ❌ NO | Network interfaces |

---

## References

### Code Locations

1. **Connection Request Handling**: [`lib/nvmf/rdma.c:1292-1412`](lib/nvmf/rdma.c:1292-1412)
2. **QP Assignment to Poll Group**: [`lib/nvmf/nvmf.c:1337-1371`](lib/nvmf/nvmf.c:1337-1371)
3. **Transport Configuration**: [`lib/nvmf/nvmf_rpc.c:2479-2480`](lib/nvmf/nvmf_rpc.c:2479-2480)
4. **Controller QP Mask**: [`lib/nvmf/ctrlr.c:466`](lib/nvmf/ctrlr.c:466)

### Documentation

1. **Client Configuration**: [`SPDK_AIO_Setup_Guide.md:628`](SPDK_AIO_Setup_Guide.md:628)
2. **SPDK Initiator**: [`CHANGELOG.md:1532-1533`](CHANGELOG.md:1532-1533)

### External Resources

- NVMe-oF Specification: Defines I/O queue model
- Linux NVMe Driver: `nvme-cli` documentation for `--nr-io-queues`
- RDMA CM API: `rdma_cm(7)` man page for connection management

---

## Conclusion

**The number of QPs/connections is determined by the INITIATOR (client)** through the `--nr-io-queues` parameter (Linux) or `num_io_queues` parameter (SPDK). The target accepts these connections (up to configured limits) and distributes them across reactors using round-robin assignment. This design gives clients control over their I/O parallelism while allowing targets to enforce resource limits.