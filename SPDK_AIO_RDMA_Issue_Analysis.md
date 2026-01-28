# SPDK AIO + RDMA Issue: Root Cause Analysis

## Critical Discovery

You mentioned: **"The same hardware/network setup works with SPDK/VFIO and Linux NVMe-oF"**

This is the key insight! The issue is **NOT** hardware/network - it's specific to **SPDK with AIO backend + RDMA**.

## Root Cause: SPDK Process Not Running with Proper Privileges

When using AIO backend, SPDK might not have the necessary capabilities to bind RDMA addresses.

### The Issue

Looking at the SPDK RDMA code (`lib/nvmf/rdma.c:3078`):
```c
rc = rdma_bind_addr(port->id, res->ai_addr);
if (rc < 0) {
    SPDK_ERRLOG("rdma_bind_addr() failed\n");
    ...
}
```

The `rdma_bind_addr()` call fails, which typically happens when:
1. Process doesn't have CAP_NET_BIND_SERVICE capability
2. Process doesn't have access to RDMA devices
3. RDMA event channel is not properly initialized

## Solution: Ensure SPDK Runs with Proper Privileges

### Check Current SPDK Process

```bash
# Check how SPDK is running
ps aux | grep nvmf_tgt

# Check process capabilities
sudo cat /proc/$(pgrep nvmf_tgt)/status | grep Cap
```

### Solution 1: Run SPDK as Root (Simplest)

```bash
# Stop current SPDK
sudo killall nvmf_tgt

# Start SPDK as root
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &

# Wait for startup
sleep 3

# Now try adding listener
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420
```

### Solution 2: Grant Capabilities to SPDK Binary

```bash
# Grant necessary capabilities
sudo setcap 'cap_net_bind_service,cap_sys_resource,cap_ipc_lock,cap_net_admin+ep' \
    ./app/nvmf_tgt/nvmf_tgt

# Start SPDK (can run as non-root now)
./app/nvmf_tgt/nvmf_tgt -m 0x3 &
```

### Solution 3: Check RDMA Device Permissions

```bash
# Check RDMA device permissions
ls -l /dev/infiniband/*

# Should show:
# crw-rw---- 1 root rdma ... /dev/infiniband/rdma_cm
# crw-rw---- 1 root rdma ... /dev/infiniband/uverbs0

# Add user to rdma group
sudo usermod -a -G rdma $USER

# Logout and login, or:
newgrp rdma
```

## Comparison: Why VFIO Works but AIO Doesn't

### VFIO Setup (Works)
```
1. scripts/setup.sh runs as root
2. Unbinds devices, sets up VFIO
3. SPDK runs with full device access
4. RDMA works ✓
```

### AIO Setup (Fails)
```
1. No setup.sh needed
2. Devices stay in kernel
3. SPDK might run with limited privileges
4. RDMA bind fails ✗
```

## Complete Working Setup for AIO + RDMA

```bash
#!/bin/bash
# aio_rdma_setup.sh

NQN="nqn.2025-12.arc.spdk:wolf20"

echo "=== Step 1: Ensure Running as Root ==="
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root or with sudo"
    exit 1
fi

echo "=== Step 2: Check RDMA Setup ==="
ibv_devices
rdma link

echo "=== Step 3: Stop Any Existing SPDK ==="
killall nvmf_tgt 2>/dev/null || true
sleep 2

echo "=== Step 4: Start SPDK as Root ==="
./app/nvmf_tgt/nvmf_tgt -m 0x3 &
SPDK_PID=$!
echo "SPDK PID: $SPDK_PID"
sleep 3

echo "=== Step 5: Verify SPDK is Running ==="
if ! ps -p $SPDK_PID > /dev/null; then
    echo "ERROR: SPDK failed to start"
    exit 1
fi

echo "=== Step 6: Create RDMA Transport ==="
./scripts/rpc.py nvmf_create_transport -t RDMA \
    -u 8192 -m 128 -c 127

echo "=== Step 7: Create AIO Bdev ==="
./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

echo "=== Step 8: Create Subsystem ==="
./scripts/rpc.py nvmf_create_subsystem $NQN -a \
    -s SPDK00000000000001

echo "=== Step 9: Add Namespace ==="
./scripts/rpc.py nvmf_subsystem_add_ns $NQN aio0

echo "=== Step 10: Add RDMA Listener ==="
./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420

echo "=== Step 11: Verify Setup ==="
./scripts/rpc.py nvmf_get_subsystems | jq '.[] | select(.nqn=="'$NQN'")'

echo "=== Setup Complete ==="
```

## Debugging Steps

### Step 1: Check SPDK Process Owner

```bash
ps aux | grep nvmf_tgt | grep -v grep

# Should show:
# root ... ./app/nvmf_tgt/nvmf_tgt -m 0x3
```

### Step 2: Check RDMA Event Channel

```bash
# Check if RDMA event channel is created
sudo lsof -p $(pgrep nvmf_tgt) | grep rdma

# Should show rdma_cm device
```

### Step 3: Enable SPDK Debug Logging

```bash
# Start SPDK with debug logging
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 -L all &

# Check logs
sudo ./scripts/rpc.py log_set_level DEBUG
sudo ./scripts/rpc.py log_set_flag nvmf
```

### Step 4: Test RDMA Bind Manually

Create a simple test:

```bash
# Test if rdma_bind_addr works for your user
rping -s -a 10.243.3.20 -v

# If this fails with permission error, it's a privilege issue
```

## Key Differences: AIO vs VFIO

| Aspect | VFIO (Works) | AIO (Fails) |
|--------|--------------|-------------|
| **Device Access** | Direct via VFIO | Via kernel |
| **Setup Script** | `scripts/setup.sh` (as root) | Not needed |
| **Typical Privileges** | Runs as root | May run as user |
| **RDMA Access** | Full access | May be restricted |
| **Device Binding** | Unbound from kernel | Stays in kernel |

## Most Likely Fix

The issue is almost certainly **privilege-related**. Try this:

```bash
# 1. Stop SPDK
sudo killall nvmf_tgt

# 2. Start SPDK explicitly as root
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &

# 3. Verify it's running as root
ps aux | grep nvmf_tgt

# 4. Try adding listener again
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t rdma \
    -a 10.243.3.20 \
    -s 4420
```

## Alternative: Check for Conflicting Processes

```bash
# Check if another process is using RDMA on this address
sudo lsof -i :4420
sudo netstat -tulpn | grep 4420

# Check if another RDMA listener exists
sudo lsof | grep rdma_cm
```

## Verification After Fix

Once the listener is added successfully:

```bash
# 1. Verify listener is active
sudo ./scripts/rpc.py nvmf_get_subsystems | \
    jq '.[] | select(.nqn=="nqn.2025-12.arc.spdk:wolf20") | .listen_addresses'

# Should show:
# [
#   {
#     "trtype": "RDMA",
#     "traddr": "10.243.3.20",
#     "trsvcid": "4420",
#     "adrfam": "IPv4"
#   }
# ]

# 2. Test from client
sudo nvme discover -t rdma -a 10.243.3.20 -s 4420

# 3. Connect from client
sudo nvme connect -t rdma -n nqn.2025-12.arc.spdk:wolf20 \
    -a 10.243.3.20 -s 4420
```

## Summary

**Root Cause:** SPDK with AIO backend not running with sufficient privileges to bind RDMA addresses.

**Solution:** Ensure SPDK runs as root or with proper capabilities.

**Why VFIO works:** The `scripts/setup.sh` script runs as root and sets up everything with proper privileges.

**Why AIO fails:** No setup script, so SPDK might start with limited privileges.

**Quick Fix:**
```bash
sudo killall nvmf_tgt
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
sudo ./scripts/rpc.py nvmf_subsystem_add_listener <nqn> -t rdma -a 10.243.3.20 -s 4420
```

Try running SPDK explicitly as root and the RDMA listener should work!