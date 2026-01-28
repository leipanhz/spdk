# SPDK RDMA "rdma_bind_addr() failed" - Deep Troubleshooting

## Issue Summary

Even with correct InfiniBand setup and `-f ipv4` flag, you're still getting:
```
[ERROR]: rdma_bind_addr() failed
[ERROR]: Unable to listen on address '10.243.3.20'
```

## Advanced Troubleshooting Steps

### Step 1: Try Wildcard Address (0.0.0.0)

This tells SPDK to listen on ALL RDMA interfaces:

```bash
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t rdma \
    -a 0.0.0.0 \
    -s 4420
```

**If this works**, it means there's an issue with binding to the specific IP.

### Step 2: Check SPDK Process Permissions

```bash
# Check if SPDK is running as root
ps aux | grep nvmf_tgt

# Check RDMA device permissions
ls -l /dev/infiniband/*

# Ensure user has access to RDMA devices
sudo usermod -a -G rdma $USER
```

### Step 3: Check for Port Conflicts

```bash
# Check if port 4420 is already in use
sudo netstat -tulpn | grep 4420
sudo ss -tulpn | grep 4420

# Try a different port
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t rdma \
    -a 10.243.3.20 \
    -s 4421
```

### Step 4: Check RDMA CM (Connection Manager)

```bash
# Check if rdma_cm module is loaded
lsmod | grep rdma_cm

# If not, load it
sudo modprobe rdma_cm

# Check rdma_cm parameters
cat /sys/module/rdma_cm/parameters/*
```

### Step 5: Verify IPoIB Mode

InfiniBand can run in two modes:

```bash
# Check current IPoIB mode
cat /sys/class/net/ibs5f0/mode

# Should show: datagram or connected
# Try switching mode if needed:
echo "datagram" | sudo tee /sys/class/net/ibs5f0/mode
```

### Step 6: Check RDMA Device Capabilities

```bash
# Get detailed device info
ibv_devinfo -v mlx5_0

# Check if device supports RC (Reliable Connection)
# Look for: transport_type: InfiniBand (1)
```

### Step 7: Test RDMA Bind Directly

Create a test to see if rdma_bind_addr works at all:

```bash
# Install rdma-core-devel if needed
sudo yum install rdma-core-devel  # RHEL/CentOS
sudo apt-get install librdmacm-dev  # Ubuntu

# Test with rping
rping -s -a 10.243.3.20 -v -C 1

# If this fails, the issue is at the RDMA level, not SPDK
```

### Step 8: Check Firewall and SELinux

```bash
# Temporarily disable firewall
sudo systemctl stop firewalld

# Check SELinux status
getenforce

# Temporarily set to permissive
sudo setenforce 0

# Try adding listener again
```

### Step 9: Check SPDK RDMA Configuration

```bash
# Get current RDMA transport config
sudo ./scripts/rpc.py nvmf_get_transports

# Try recreating transport with different parameters
sudo ./scripts/rpc.py nvmf_delete_transport -t RDMA
sudo ./scripts/rpc.py nvmf_create_transport -t RDMA \
    -u 4096 \
    -m 64 \
    -c 32 \
    --num-shared-buffers 2048
```

### Step 10: Check for Multiple RDMA Providers

```bash
# List RDMA providers
ls /sys/class/infiniband/*/device/driver

# Check which provider is being used
cat /sys/class/infiniband/mlx5_0/device/driver/module/version
```

## Possible Root Causes

### Cause 1: IPoIB Not Properly Configured

```bash
# Restart IPoIB
sudo ip link set ibs5f0 down
sudo ip link set ibs5f0 up

# Verify IP is reachable
ping -c 3 10.243.3.20

# Check ARP cache
ip neigh show dev ibs5f0
```

### Cause 2: RDMA CM Issue with IPoIB

Some InfiniBand setups have issues with RDMA CM over IPoIB. Try using native IB addressing:

```bash
# Get the GID (Global Identifier)
ibv_devinfo mlx5_0 | grep GID

# Example output:
# GID[0]: fe80:0000:0000:0000:b859:9f03:00c9:bd4c

# Try using GID instead of IP
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t rdma \
    -a fe80::b859:9f03:c9:bd4c \
    -s 4420 \
    -f ipv6
```

### Cause 3: Subnet Manager Issues

```bash
# Check if subnet manager is running
ibstat | grep "SM lid"

# If no SM, InfiniBand won't work properly
# Check opensm service
systemctl status opensm

# Start if needed
sudo systemctl start opensm
```

### Cause 4: SPDK Built Without Proper RDMA Support

```bash
# Check if SPDK was built with RDMA
cd /home/leipan/Workspace/spdk_leifork
grep -r "CONFIG_RDMA" build/

# Rebuild with RDMA if needed
./configure --with-rdma
make clean
make -j$(nproc)
```

## Workaround: Use TCP Transport

If RDMA continues to fail, use TCP as a workaround:

```bash
# Delete RDMA transport
sudo ./scripts/rpc.py nvmf_delete_transport -t RDMA

# Create TCP transport
sudo ./scripts/rpc.py nvmf_create_transport -t TCP

# Add TCP listener (this should work)
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 \
    -t tcp \
    -a 10.243.3.20 \
    -s 4420

# Clients connect via TCP
sudo nvme connect -t tcp -n nqn.2025-12.arc.spdk:wolf20 \
    -a 10.243.3.20 -s 4420
```

## Diagnostic Script

Run this comprehensive diagnostic:

```bash
#!/bin/bash
# rdma_diagnostic.sh

echo "=== RDMA Diagnostic Report ==="
echo

echo "1. InfiniBand Devices:"
ibv_devices
echo

echo "2. RDMA Links:"
rdma link
echo

echo "3. InfiniBand Status:"
ibstat
echo

echo "4. Network Interface with Target IP:"
ip addr show | grep -A 5 "10.243.3.20"
echo

echo "5. IPoIB Mode:"
cat /sys/class/net/ibs5f0/mode 2>/dev/null || echo "N/A"
echo

echo "6. RDMA Modules:"
lsmod | grep -E "rdma|ib_"
echo

echo "7. RDMA Device Info:"
ibv_devinfo mlx5_0 | head -30
echo

echo "8. Port in Use Check:"
sudo netstat -tulpn | grep 4420
echo

echo "9. SPDK Process:"
ps aux | grep nvmf_tgt | grep -v grep
echo

echo "10. SPDK RDMA Transport:"
sudo ./scripts/rpc.py nvmf_get_transports 2>/dev/null | jq '.[] | select(.trtype=="RDMA")'
echo

echo "11. Test RDMA Bind (rping):"
timeout 5 rping -s -a 10.243.3.20 -v -C 1 2>&1 || echo "rping test failed or timed out"
echo

echo "=== End of Diagnostic ==="
```

## Known Issues

### Issue: Mellanox Cards with IPoIB

Some Mellanox cards have issues with RDMA CM over IPoIB in certain configurations.

**Solution:**
```bash
# Use native IB addressing (GID) instead of IPoIB
# Or use RoCE mode if card supports it
```

### Issue: Kernel Version Compatibility

Some older kernels have RDMA CM bugs with InfiniBand.

**Check kernel version:**
```bash
uname -r

# Recommended: 5.x or newer
# If older, consider upgrading kernel
```

## Alternative Approaches

### Approach 1: Use RoCE Instead of IPoIB

If your Mellanox card supports RoCE (RDMA over Converged Ethernet):

```bash
# Check if RoCE is supported
ibv_devinfo mlx5_0 | grep -i roce

# Enable RoCE mode (if supported)
# This varies by card model
```

### Approach 2: Use SR-IOV Virtual Functions

```bash
# Create VF (Virtual Function)
echo 1 | sudo tee /sys/class/infiniband/mlx5_0/device/sriov_numvfs

# Use VF for SPDK
```

### Approach 3: Use Different SPDK Version

```bash
# Try an older/newer SPDK version
cd /opt
sudo git clone https://github.com/spdk/spdk.git spdk-v23.09
cd spdk-v23.09
sudo git checkout v23.09
sudo git submodule update --init
./configure --with-rdma
make -j$(nproc)
```

## Summary of Things to Try

1. ✅ **Try wildcard address**: `0.0.0.0` instead of `10.243.3.20`
2. ✅ **Try different port**: `4421` instead of `4420`
3. ✅ **Try GID addressing**: Use IPv6 GID instead of IPv4 IP
4. ✅ **Check IPoIB mode**: Ensure it's in correct mode
5. ✅ **Restart IPoIB interface**: `ip link set ibs5f0 down/up`
6. ✅ **Check subnet manager**: Ensure opensm is running
7. ✅ **Test with rping**: Verify RDMA bind works at all
8. ✅ **Use TCP transport**: As a working alternative

## Most Likely Solutions

Based on your setup, try these in order:

### Solution 1: Use Wildcard Address
```bash
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 -t rdma -a 0.0.0.0 -s 4420
```

### Solution 2: Use GID (Native IB)
```bash
# Get GID
GID=$(ibv_devinfo mlx5_0 | grep "GID\[0\]" | awk '{print $2}')
echo "Using GID: $GID"

# Add listener with GID
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 -t rdma -a $GID -s 4420 -f ipv6
```

### Solution 3: Switch to TCP
```bash
sudo ./scripts/rpc.py nvmf_delete_transport -t RDMA
sudo ./scripts/rpc.py nvmf_create_transport -t TCP
sudo ./scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2025-12.arc.spdk:wolf20 -t tcp -a 10.243.3.20 -s 4420
```

Try these solutions and let me know which one works!