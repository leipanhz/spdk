# SPDK InfiniBand RDMA Listener Fix

## Problem Identified

Your setup:
- IP: `10.243.3.20` on interface `ibs5f0`
- RDMA Device: `mlx5_0`
- Interface Type: **InfiniBand** (not Ethernet/RoCE)
- Link type: `link/infiniband`

The issue: SPDK's RDMA listener for InfiniBand requires **GID (Global Identifier)** instead of IP address in some cases.

## Solution Options

### Option 1: Use IPv4 Address Family (Recommended)

For InfiniBand with IPoIB (IP over InfiniBand), explicitly specify IPv4:

```bash
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420 \
    -f ipv4
```

**Full command:**
```bash
NQN="nqn.2025-12.arc.spdk:wolf20"
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420 \
    -f ipv4
```

### Option 2: Use GID Address

InfiniBand uses GID (similar to IPv6) for native addressing:

```bash
# Get GID for mlx5_0 port 1
ibv_devinfo mlx5_0 | grep GID

# Example output:
# GID[0]: fe80:0000:0000:0000:b859:9f03:00c9:bd4c

# Use GID in listener
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma \
    -a fe80::b859:9f03:c9:bd4c \
    -s 4420 \
    -f ipv6
```

### Option 3: Use 0.0.0.0 to Listen on All Interfaces

```bash
# Listen on all RDMA interfaces
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma \
    -a 0.0.0.0 \
    -s 4420
```

## Complete Working Example

```bash
#!/bin/bash
# setup_infiniband_nvmf.sh

NQN="nqn.2025-12.arc.spdk:wolf20"

echo "=== Step 1: Verify InfiniBand Setup ==="
ibv_devices
rdma link
ip addr show ibs5f0 | grep inet

echo "=== Step 2: Start SPDK Target ==="
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
sleep 3

echo "=== Step 3: Create RDMA Transport ==="
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA \
    -u 8192 -m 128 -c 127

echo "=== Step 4: Create AIO Bdev ==="
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

echo "=== Step 5: Create Subsystem ==="
sudo ./scripts/rpc.py nvmf_create_subsystem $NQN -a \
    -s SPDK00000000000001

echo "=== Step 6: Add Namespace ==="
sudo ./scripts/rpc.py nvmf_subsystem_add_ns $NQN aio0

echo "=== Step 7: Add InfiniBand RDMA Listener ==="
# Try with explicit IPv4 address family
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420 \
    -f ipv4

echo "=== Step 8: Verify Setup ==="
sudo ./scripts/rpc.py nvmf_get_subsystems | jq '.[] | select(.nqn=="'$NQN'")'

echo "=== Setup Complete ==="
```

## Verification Commands

```bash
# 1. Check InfiniBand device info
ibv_devinfo mlx5_0

# 2. Check InfiniBand link state
ibstat mlx5_0

# 3. Check IPoIB interface
ip addr show ibs5f0

# 4. Test InfiniBand connectivity (from another node)
ping 10.243.3.20

# 5. Check SPDK subsystem listeners
sudo ./scripts/rpc.py nvmf_get_subsystems | jq '.[].listen_addresses'
```

## Understanding InfiniBand vs RoCE

### InfiniBand (Your Setup)
- Native InfiniBand fabric
- Uses GID addressing
- Interface: `ibs5f0` (IPoIB)
- Link type: `link/infiniband`
- Requires explicit address family specification

### RoCE (Ethernet-based)
- RDMA over Converged Ethernet
- Uses IP addressing
- Interface: `eth0`, `ens1f0`, etc.
- Link type: `link/ether`
- Works with standard IP addresses

## Troubleshooting InfiniBand Issues

### Check InfiniBand State

```bash
# Check IB device state
ibstat

# Should show:
# State: Active
# Physical state: LinkUp
# Rate: 100 Gb/sec (4X EDR)
```

### Check IPoIB Configuration

```bash
# Verify IPoIB module is loaded
lsmod | grep ib_ipoib

# If not loaded:
sudo modprobe ib_ipoib

# Check IPoIB interface
ip link show ibs5f0
```

### Check Subnet Manager

```bash
# InfiniBand requires a subnet manager
# Check if opensm is running
systemctl status opensm

# Or check for subnet manager
ibstat | grep "SM lid"
```

### Test RDMA Connectivity

```bash
# On server (10.243.3.20):
rping -s -a 10.243.3.20 -v

# On client (from another node):
rping -c -a 10.243.3.20 -v -C 10
```

## Client Connection (InfiniBand)

When connecting from clients:

```bash
# Discover targets
sudo nvme discover -t rdma -a 10.243.3.20 -s 4420

# Connect to subsystem
sudo nvme connect -t rdma \
    -n nqn.2025-12.arc.spdk:wolf20 \
    -a 10.243.3.20 \
    -s 4420

# Verify connection
sudo nvme list
```

## Alternative: Use Both IB Interfaces

You have two InfiniBand interfaces. You can add listeners on both:

```bash
# Add listener on mlx5_0 (ibs5f0 - 10.243.3.20)
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 10.243.3.20 -s 4420 -f ipv4

# Add listener on mlx5_1 (ibs5f1 - 10.244.3.20)
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 10.244.3.20 -s 4420 -f ipv4
```

## Common InfiniBand Errors and Fixes

### Error: "rdma_bind_addr() failed"

**Fix 1: Specify address family**
```bash
# Add -f ipv4 flag
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 10.243.3.20 -s 4420 -f ipv4
```

**Fix 2: Check IPoIB is configured**
```bash
# Ensure IPoIB interface is up
sudo ip link set ibs5f0 up

# Verify IP is configured
ip addr show ibs5f0 | grep inet
```

**Fix 3: Use wildcard address**
```bash
# Listen on all interfaces
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 0.0.0.0 -s 4420
```

### Error: "No route to host"

```bash
# Check InfiniBand link is active
ibstat mlx5_0 | grep State

# Check subnet manager
ibstat mlx5_0 | grep "SM lid"

# Test connectivity
ping 10.243.3.20
```

## JSON Configuration for InfiniBand

```json
{
  "subsystems": [
    {
      "subsystem": "nvmf",
      "config": [
        {
          "method": "nvmf_create_transport",
          "params": {
            "trtype": "RDMA",
            "max_queue_depth": 128,
            "num_shared_buffers": 4095
          }
        },
        {
          "method": "nvmf_create_subsystem",
          "params": {
            "nqn": "nqn.2025-12.arc.spdk:wolf20",
            "allow_any_host": true,
            "serial_number": "SPDK00000000000001"
          }
        },
        {
          "method": "nvmf_subsystem_add_ns",
          "params": {
            "nqn": "nqn.2025-12.arc.spdk:wolf20",
            "namespace": {
              "nsid": 1,
              "bdev_name": "aio0"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_listener",
          "params": {
            "nqn": "nqn.2025-12.arc.spdk:wolf20",
            "listen_address": {
              "trtype": "RDMA",
              "traddr": "10.243.3.20",
              "trsvcid": "4420",
              "adrfam": "IPv4"
            }
          }
        }
      ]
    }
  ]
}
```

## Summary

**Your specific fix:**

```bash
# Add the -f ipv4 flag for InfiniBand IPoIB
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420 \
    -f ipv4
```

**Why this is needed:**
- InfiniBand uses native GID addressing
- IPoIB (IP over InfiniBand) requires explicit IPv4 specification
- Without `-f ipv4`, SPDK may try to use native IB addressing

**Key differences from Ethernet RDMA:**
- InfiniBand: Requires `-f ipv4` for IPoIB
- RoCE/Ethernet: Works without address family specification

Try the command with `-f ipv4` flag and it should work!