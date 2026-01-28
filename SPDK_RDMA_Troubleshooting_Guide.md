# SPDK RDMA Listener Error: "rdma_bind_addr() failed"

## Error Analysis

```
[ERROR]: rdma_bind_addr() failed
[ERROR]: Unable to listen on address '10.243.3.20'
```

This error means SPDK cannot bind to the RDMA address. Common causes:

1. **IP address not configured on RDMA interface**
2. **RDMA kernel modules not loaded**
3. **No RDMA-capable network interface**
4. **IP address belongs to wrong interface**
5. **RDMA transport not created**

## Step-by-Step Troubleshooting

### Step 1: Verify RDMA Devices Exist

```bash
# Check for RDMA devices
ibv_devices

# Expected output (if RDMA hardware exists):
# device                 node GUID
# ------              ----------------
# mlx5_0              248a0703007a2f40
# mlx5_1              248a0703007a2f41

# If empty or command not found, you don't have RDMA hardware/drivers
```

**If `ibv_devices` is empty or command not found:**
- You don't have RDMA-capable hardware, OR
- RDMA drivers are not installed

### Step 2: Check RDMA Kernel Modules

```bash
# Check if RDMA modules are loaded
lsmod | grep -E "rdma|ib_"

# Should see modules like:
# rdma_cm
# rdma_ucm
# ib_core
# ib_uverbs
# mlx5_core (for Mellanox)
# irdma (for Intel E810)
```

**If modules are missing, load them:**

```bash
# Load RDMA modules
sudo modprobe rdma_cm
sudo modprobe rdma_ucm
sudo modprobe ib_core
sudo modprobe ib_uverbs
sudo modprobe ib_umad

# For Mellanox cards
sudo modprobe mlx5_core
sudo modprobe mlx5_ib

# For Intel E810 cards
sudo modprobe irdma roce_ena=1
```

### Step 3: Verify IP Address Configuration

```bash
# List all RDMA-capable network interfaces
rdma link

# Expected output:
# link mlx5_0/1 state ACTIVE physical_state LINK_UP netdev ens1f0
# link mlx5_1/1 state ACTIVE physical_state LINK_UP netdev ens1f1

# Check if your IP is configured on the RDMA interface
ip addr show

# Look for 10.243.3.20 in the output
```

**If IP is not configured:**

```bash
# Find the RDMA interface name (e.g., ens1f0, ib0, etc.)
rdma link | grep ACTIVE

# Configure IP on the RDMA interface
sudo ip addr add 10.243.3.20/24 dev <interface_name>
sudo ip link set <interface_name> up

# Example:
sudo ip addr add 10.243.3.20/24 dev ens1f0
sudo ip link set ens1f0 up

# Verify
ip addr show ens1f0 | grep 10.243.3.20
```

### Step 4: Verify RDMA Transport is Created

```bash
# Check if RDMA transport exists in SPDK
sudo ./scripts/rpc.py nvmf_get_transports

# Should show RDMA transport:
# [
#   {
#     "trtype": "RDMA",
#     ...
#   }
# ]
```

**If RDMA transport is missing, create it:**

```bash
# Create RDMA transport BEFORE adding listener
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA

# Or with more options:
sudo ./scripts/rpc.py nvmf_create_transport \
    -t RDMA \
    -u 4096 \
    -m 128 \
    -c 64
```

### Step 5: Test RDMA Connectivity

```bash
# Test if RDMA interface is working
# On server (10.243.3.20):
ib_write_bw -d mlx5_0

# On client (from another machine):
ib_write_bw -d mlx5_0 10.243.3.20

# If this fails, RDMA hardware/network has issues
```

## Complete Working Example

```bash
#!/bin/bash
# setup_rdma_nvmf.sh

echo "=== Step 1: Load RDMA Modules ==="
sudo modprobe rdma_cm
sudo modprobe rdma_ucm
sudo modprobe ib_core
sudo modprobe ib_uverbs
sudo modprobe mlx5_ib  # For Mellanox

echo "=== Step 2: Verify RDMA Devices ==="
ibv_devices
rdma link

echo "=== Step 3: Configure IP on RDMA Interface ==="
# Find RDMA interface name
RDMA_IF=$(rdma link | grep ACTIVE | head -1 | awk '{print $NF}')
echo "RDMA Interface: $RDMA_IF"

# Configure IP
sudo ip addr add 10.243.3.20/24 dev $RDMA_IF
sudo ip link set $RDMA_IF up

# Verify
ip addr show $RDMA_IF | grep 10.243.3.20

echo "=== Step 4: Start SPDK Target ==="
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
sleep 3

echo "=== Step 5: Create RDMA Transport ==="
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA -u 4096 -m 128 -c 64

echo "=== Step 6: Create AIO Bdev ==="
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

echo "=== Step 7: Create Subsystem ==="
NQN="nqn.2025-12.arc.spdk:wolf20"
sudo ./scripts/rpc.py nvmf_create_subsystem $NQN -a -s SPDK00000000000001

echo "=== Step 8: Add Namespace ==="
sudo ./scripts/rpc.py nvmf_subsystem_add_ns $NQN aio0

echo "=== Step 9: Add RDMA Listener ==="
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 10.243.3.20 -s 4420

echo "=== Setup Complete ==="
sudo ./scripts/rpc.py nvmf_get_subsystems
```

## Common Issues and Solutions

### Issue 1: No RDMA Hardware

**Symptom:**
```bash
$ ibv_devices
# (empty output)
```

**Solution:**
You don't have RDMA-capable hardware. Options:
1. Use TCP transport instead of RDMA
2. Install RDMA-capable NIC (Mellanox, Intel E810, etc.)
3. Use software RoCE (RXE) for testing

**Using TCP instead:**
```bash
# Use TCP transport
sudo ./scripts/rpc.py nvmf_create_transport -t TCP

# Add TCP listener
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t tcp -a 10.243.3.20 -s 4420
```

### Issue 2: IP on Wrong Interface

**Symptom:**
```bash
$ ip addr show | grep 10.243.3.20
# Shows IP on eth0, but RDMA is on ens1f0
```

**Solution:**
```bash
# Remove IP from wrong interface
sudo ip addr del 10.243.3.20/24 dev eth0

# Add to correct RDMA interface
sudo ip addr add 10.243.3.20/24 dev ens1f0
sudo ip link set ens1f0 up
```

### Issue 3: RDMA Transport Not Created

**Symptom:**
```bash
$ sudo ./scripts/rpc.py nvmf_get_transports
[]  # Empty
```

**Solution:**
```bash
# Create RDMA transport FIRST
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA

# Then add listener
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 10.243.3.20 -s 4420
```

### Issue 4: Firewall Blocking

**Symptom:**
Listener adds successfully but clients can't connect

**Solution:**
```bash
# Allow RDMA port
sudo firewall-cmd --add-port=4420/tcp --permanent
sudo firewall-cmd --reload

# Or disable firewall for testing
sudo systemctl stop firewalld
```

### Issue 5: SELinux Blocking

**Symptom:**
Permission denied errors

**Solution:**
```bash
# Temporarily disable SELinux for testing
sudo setenforce 0

# Or add SELinux policy (production)
sudo setsebool -P nis_enabled 1
```

## Software RoCE (RXE) for Testing

If you don't have RDMA hardware, you can use software RoCE for testing:

```bash
# Install RXE
sudo modprobe rdma_rxe

# Create RXE device on ethernet interface
sudo rdma link add rxe0 type rxe netdev eth0

# Verify
ibv_devices
# Should show: rxe0

# Configure IP
sudo ip addr add 10.243.3.20/24 dev eth0
sudo ip link set eth0 up

# Now you can use RDMA transport with RXE
```

## Diagnostic Commands

```bash
# 1. Check RDMA devices
ibv_devices
ibv_devinfo

# 2. Check RDMA links
rdma link
rdma link show

# 3. Check network interfaces
ip addr show
ip link show

# 4. Check RDMA modules
lsmod | grep rdma
lsmod | grep ib_

# 5. Check SPDK transports
sudo ./scripts/rpc.py nvmf_get_transports

# 6. Check SPDK subsystems
sudo ./scripts/rpc.py nvmf_get_subsystems

# 7. Test RDMA connectivity
rping -s -a 10.243.3.20  # Server
rping -c -a 10.243.3.20  # Client

# 8. Check SPDK logs
sudo journalctl -u spdk-nvmf -f
```

## Correct Order of Operations

```bash
# 1. Load RDMA modules
sudo modprobe rdma_cm rdma_ucm ib_core ib_uverbs

# 2. Configure IP on RDMA interface
sudo ip addr add 10.243.3.20/24 dev <rdma_interface>
sudo ip link set <rdma_interface> up

# 3. Start SPDK target
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &

# 4. Create RDMA transport (IMPORTANT!)
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA

# 5. Create bdev
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

# 6. Create subsystem
sudo ./scripts/rpc.py nvmf_create_subsystem $NQN -a

# 7. Add namespace
sudo ./scripts/rpc.py nvmf_subsystem_add_ns $NQN aio0

# 8. Add listener (NOW it should work)
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t rdma -a 10.243.3.20 -s 4420
```

## Alternative: Use TCP Transport

If RDMA is not available or causing issues, use TCP:

```bash
# Create TCP transport instead
sudo ./scripts/rpc.py nvmf_create_transport -t TCP

# Add TCP listener
sudo ./scripts/rpc.py nvmf_subsystem_add_listener $NQN \
    -t tcp -a 10.243.3.20 -s 4420

# Clients connect via TCP
sudo nvme connect -t tcp -n $NQN -a 10.243.3.20 -s 4420
```

## Summary Checklist

Before adding RDMA listener, verify:

- [ ] RDMA hardware exists (`ibv_devices` shows devices)
- [ ] RDMA modules loaded (`lsmod | grep rdma`)
- [ ] IP configured on RDMA interface (`ip addr show`)
- [ ] RDMA interface is UP (`rdma link` shows ACTIVE)
- [ ] SPDK target is running (`ps aux | grep nvmf_tgt`)
- [ ] RDMA transport created (`nvmf_get_transports` shows RDMA)
- [ ] Subsystem exists (`nvmf_get_subsystems`)

If any of these fail, you'll get `rdma_bind_addr() failed` error.

---

**Most Common Fix:** Create RDMA transport before adding listener!

```bash
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA