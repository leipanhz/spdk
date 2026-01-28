# SPDK Setup Guide for Storage Cluster with Linux NVMe Block Devices (AIO)

## Table of Contents
1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [System Preparation](#system-preparation)
4. [SPDK Installation](#spdk-installation)
5. [AIO Backend Configuration](#aio-backend-configuration)
6. [NVMe-oF Target Setup](#nvme-of-target-setup)
7. [Client/Initiator Setup](#clientinitiator-setup)
8. [Performance Tuning](#performance-tuning)
9. [Monitoring and Troubleshooting](#monitoring-and-troubleshooting)
10. [Production Deployment](#production-deployment)

---

## Overview

This guide provides step-by-step instructions for setting up SPDK (Storage Performance Development Kit) on a storage cluster using Linux NVMe block devices with AIO (Asynchronous I/O) backend. This configuration is ideal when:

- You want to use existing Linux block devices (NVMe SSDs)
- You need kernel-managed devices for compatibility
- You want to avoid kernel bypass for certain workloads
- You need to share devices between SPDK and other applications

**Architecture:**
```
┌─────────────────────────────────────────────────────────┐
│                    Storage Cluster                       │
├─────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ Target Node 1│  │ Target Node 2│  │ Target Node N│  │
│  │              │  │              │  │              │  │
│  │ SPDK NVMe-oF │  │ SPDK NVMe-oF │  │ SPDK NVMe-oF │  │
│  │   Target     │  │   Target     │  │   Target     │  │
│  │              │  │              │  │              │  │
│  │ AIO Backend  │  │ AIO Backend  │  │ AIO Backend  │  │
│  │   ↓          │  │   ↓          │  │   ↓          │  │
│  │ /dev/nvme0n1 │  │ /dev/nvme0n1 │  │ /dev/nvme0n1 │  │
│  │ /dev/nvme1n1 │  │ /dev/nvme1n1 │  │ /dev/nvme1n1 │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ↓
              Network (RDMA/TCP/FC)
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   Client Nodes                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  Client 1    │  │  Client 2    │  │  Client N    │  │
│  │              │  │              │  │              │  │
│  │ NVMe-oF      │  │ NVMe-oF      │  │ NVMe-oF      │  │
│  │ Initiator    │  │ Initiator    │  │ Initiator    │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## Prerequisites

### Hardware Requirements

**Target Nodes (Storage Servers):**
- CPU: x86_64 with SSE4.2 support (Intel Xeon or AMD EPYC recommended)
- RAM: Minimum 8GB, recommended 16GB+ per node
- Storage: NVMe SSDs (PCIe Gen3/Gen4)
- Network:
  - RDMA: Mellanox ConnectX-5/6/7 or Intel E810 (recommended)
  - TCP: 10GbE or higher
  - FC: 16/32Gb FC HBA

**Client Nodes:**
- CPU: x86_64 architecture
- RAM: 4GB minimum
- Network: Compatible with target network (RDMA/TCP/FC)

### Software Requirements

**Operating System:**
- Linux kernel 4.15+ (5.x recommended)
- Supported distributions:
  - Ubuntu 20.04/22.04 LTS
  - RHEL/CentOS 8/9
  - Debian 11/12

**Required Packages:**
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    git \
    libaio-dev \
    libssl-dev \
    libnuma-dev \
    uuid-dev \
    libiscsi-dev \
    python3 \
    python3-pip \
    pkg-config \
    autoconf \
    automake \
    libtool \
    nasm \
    meson \
    ninja-build

# RHEL/CentOS
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    git \
    libaio-devel \
    openssl-devel \
    numactl-devel \
    libuuid-devel \
    libiscsi-devel \
    python3 \
    python3-pip \
    pkgconfig \
    autoconf \
    automake \
    libtool \
    nasm \
    meson \
    ninja-build
```

**Python Dependencies:**
```bash
pip3 install --user pyelftools
```

---

## System Preparation

### 1. Verify NVMe Devices

```bash
# List all NVMe devices
lsblk -d -o NAME,SIZE,MODEL | grep nvme

# Check NVMe device details
nvme list

# Verify device is not mounted or in use
lsblk /dev/nvme0n1
mount | grep nvme
```

**Example output:**
```
NAME        SIZE MODEL
nvme0n1     1.8T Samsung SSD 980 PRO 2TB
nvme1n1     1.8T Samsung SSD 980 PRO 2TB
```

### 2. Configure Hugepages

SPDK requires hugepages for efficient memory management:

```bash
# Check current hugepage configuration
cat /proc/meminfo | grep Huge

# Allocate 2GB hugepages (adjust based on your needs)
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# Make persistent across reboots
echo "vm.nr_hugepages = 1024" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p

# Verify allocation
grep HugePages /proc/meminfo
```

**Hugepage sizing guidelines:**
- Minimum: 2GB (1024 x 2MB pages)
- Recommended: 4-8GB for production
- Formula: `(Number of connections × 4MB) + (Number of devices × 128MB) + 2GB overhead`

### 3. Configure IOMMU (Optional, for VFIO)

If using VFIO instead of AIO:

```bash
# Edit GRUB configuration
sudo vim /etc/default/grub

# Add to GRUB_CMDLINE_LINUX:
# intel_iommu=on iommu=pt (for Intel)
# amd_iommu=on iommu=pt (for AMD)

# Update GRUB
sudo update-grub  # Ubuntu/Debian
sudo grub2-mkconfig -o /boot/grub2/grub.cfg  # RHEL/CentOS

# Reboot
sudo reboot

# Verify IOMMU is enabled
dmesg | grep -i iommu
```

### 4. Network Configuration

**For RDMA (InfiniBand/RoCE):**

```bash
# Install RDMA packages
# Ubuntu/Debian
sudo apt-get install -y rdma-core libibverbs-dev librdmacm-dev

# RHEL/CentOS
sudo yum install -y rdma-core libibverbs-devel librdmacm-devel

# Load RDMA modules
sudo modprobe ib_core
sudo modprobe ib_uverbs
sudo modprobe rdma_cm
sudo modprobe rdma_ucm

# Verify RDMA devices
ibv_devices
rdma link

# Configure IP addresses on RDMA interfaces
sudo ip addr add 192.168.100.10/24 dev <rdma_interface>
sudo ip link set <rdma_interface> up
```

**For TCP:**

```bash
# Configure network interface
sudo ip addr add 192.168.100.10/24 dev eth0
sudo ip link set eth0 up

# Optimize TCP settings for storage
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 134217728"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 134217728"
```

---

## SPDK Installation

### 1. Clone SPDK Repository

```bash
# Clone the latest stable release
cd /opt
sudo git clone https://github.com/spdk/spdk.git
cd spdk
sudo git checkout v24.01  # Use latest stable version
sudo git submodule update --init
```

### 2. Build SPDK

```bash
# Configure build with AIO support
./configure --with-aio

# Build SPDK
make -j$(nproc)

# Optional: Install system-wide
sudo make install
```

**Build options for specific transports:**
```bash
# For RDMA support
./configure --with-rdma --with-aio

# For iSCSI support
./configure --with-iscsi-initiator --with-aio

# For all features
./configure --with-rdma --with-iscsi-initiator --with-aio --with-crypto
```

### 3. Setup Environment

```bash
# Add SPDK to PATH
echo 'export PATH=$PATH:/opt/spdk/scripts:/opt/spdk/app/nvmf_tgt' >> ~/.bashrc
source ~/.bashrc

# Setup hugepages (if not done earlier)
sudo HUGEMEM=2048 scripts/setup.sh
```

---

## AIO Backend Configuration

### 1. Prepare Block Devices

**Important:** AIO backend uses Linux block devices directly, so devices remain visible to the kernel.

```bash
# Ensure devices are not mounted
sudo umount /dev/nvme0n1 2>/dev/null || true
sudo umount /dev/nvme1n1 2>/dev/null || true

# Optional: Create GPT partition table for SPDK
sudo scripts/spdk-gpt.py /dev/nvme0n1
sudo scripts/spdk-gpt.py /dev/nvme1n1

# Verify devices are accessible
ls -l /dev/nvme*n1
```

### 2. Create AIO Configuration File

Create `/etc/spdk/nvmf.json`:

```json
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "method": "bdev_aio_create",
          "params": {
            "name": "aio0",
            "filename": "/dev/nvme0n1",
            "block_size": 4096
          }
        },
        {
          "method": "bdev_aio_create",
          "params": {
            "name": "aio1",
            "filename": "/dev/nvme1n1",
            "block_size": 4096
          }
        }
      ]
    },
    {
      "subsystem": "nvmf",
      "config": [
        {
          "method": "nvmf_create_transport",
          "params": {
            "trtype": "RDMA",
            "num_shared_buffers": 4096,
            "max_queue_depth": 128,
            "max_qpairs_per_ctrlr": 64,
            "in_capsule_data_size": 4096,
            "max_io_size": 131072,
            "io_unit_size": 131072
          }
        },
        {
          "method": "nvmf_create_subsystem",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "allow_any_host": true,
            "serial_number": "SPDK00000000000001",
            "model_number": "SPDK_AIO_Controller"
          }
        },
        {
          "method": "nvmf_subsystem_add_ns",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "namespace": {
              "nsid": 1,
              "bdev_name": "aio0"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_ns",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "namespace": {
              "nsid": 2,
              "bdev_name": "aio1"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_listener",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "listen_address": {
              "trtype": "RDMA",
              "traddr": "192.168.100.10",
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

**For TCP transport, modify the transport section:**

```json
{
  "method": "nvmf_create_transport",
  "params": {
    "trtype": "TCP",
    "num_shared_buffers": 4096,
    "max_queue_depth": 128,
    "max_qpairs_per_ctrlr": 64,
    "in_capsule_data_size": 4096,
    "max_io_size": 131072,
    "io_unit_size": 131072,
    "sock_priority": 0,
    "acceptor_backlog": 100
  }
}
```

### 3. Alternative: RPC-based Configuration

Instead of JSON, you can configure via RPC commands:

```bash
#!/bin/bash
# configure_aio_target.sh

# Create AIO block devices
rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
rpc.py bdev_aio_create /dev/nvme1n1 aio1 4096

# Create NVMe-oF transport
rpc.py nvmf_create_transport -t RDMA -u 4096 -m 128 -c 64

# Create subsystem
rpc.py nvmf_create_subsystem nqn.2024-01.io.spdk:storage-cluster \
    -a -s SPDK00000000000001 -m SPDK_AIO_Controller

# Add namespaces
rpc.py nvmf_subsystem_add_ns nqn.2024-01.io.spdk:storage-cluster aio0 -n 1
rpc.py nvmf_subsystem_add_ns nqn.2024-01.io.spdk:storage-cluster aio1 -n 2

# Add listener
rpc.py nvmf_subsystem_add_listener nqn.2024-01.io.spdk:storage-cluster \
    -t RDMA -a 192.168.100.10 -s 4420
```

---

## NVMe-oF Target Setup

### 1. Start SPDK NVMe-oF Target

**Using JSON configuration:**

```bash
# Start target with JSON config
sudo /opt/spdk/app/nvmf_tgt/nvmf_tgt -c /etc/spdk/nvmf.json -m 0x3

# Or with systemd service (see below)
```

**Using RPC configuration:**

```bash
# Start target without config
sudo /opt/spdk/app/nvmf_tgt/nvmf_tgt -m 0x3 &

# Wait for target to initialize
sleep 2

# Apply configuration
sudo ./configure_aio_target.sh
```

**Command-line options:**
- `-m 0x3`: CPU core mask (cores 0 and 1)
- `-c`: Configuration file path
- `-r`: RPC socket path (default: /var/tmp/spdk.sock)
- `-s`: Memory size in MB
- `-u`: Disable PCI access (useful for AIO-only setups)

### 2. Create Systemd Service

Create `/etc/systemd/system/spdk-nvmf.service`:

```ini
[Unit]
Description=SPDK NVMe-oF Target
After=network.target

[Service]
Type=simple
ExecStartPre=/opt/spdk/scripts/setup.sh
ExecStart=/opt/spdk/app/nvmf_tgt/nvmf_tgt -c /etc/spdk/nvmf.json -m 0x3
ExecStopPost=/opt/spdk/scripts/setup.sh reset
Restart=on-failure
RestartSec=5
User=root
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Enable and start service:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable spdk-nvmf
sudo systemctl start spdk-nvmf
sudo systemctl status spdk-nvmf
```

### 3. Verify Target Configuration

```bash
# Check if target is running
ps aux | grep nvmf_tgt

# List block devices
rpc.py bdev_get_bdevs

# List NVMe-oF subsystems
rpc.py nvmf_get_subsystems

# Check transport status
rpc.py nvmf_get_transports

# Monitor target logs
sudo journalctl -u spdk-nvmf -f
```

---

## Client/Initiator Setup

### 1. Install NVMe-CLI Tools

```bash
# Ubuntu/Debian
sudo apt-get install -y nvme-cli

# RHEL/CentOS
sudo yum install -y nvme-cli

# Verify installation
nvme version
```

### 2. Load Kernel Modules

**For RDMA:**

```bash
sudo modprobe nvme-rdma
sudo modprobe nvme-fabrics
```

**For TCP:**

```bash
sudo modprobe nvme-tcp
sudo modprobe nvme-fabrics
```

### 3. Discover NVMe-oF Targets

```bash
# Discover targets (RDMA)
sudo nvme discover -t rdma -a 192.168.100.10 -s 4420

# Discover targets (TCP)
sudo nvme discover -t tcp -a 192.168.100.10 -s 4420
```

**Expected output:**
```
Discovery Log Number of Records 1, Generation counter 1
=====Discovery Log Entry 0======
trtype:  rdma
adrfam:  ipv4
subtype: nvme subsystem
treq:    not specified
portid:  0
trsvcid: 4420
subnqn:  nqn.2024-01.io.spdk:storage-cluster
traddr:  192.168.100.10
```

### 4. Connect to Target

**RDMA connection:**

```bash
sudo nvme connect -t rdma \
    -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.10 \
    -s 4420
```

**TCP connection:**

```bash
sudo nvme connect -t tcp \
    -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.10 \
    -s 4420
```

**With additional options:**

```bash
sudo nvme connect -t rdma \
    -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.10 \
    -s 4420 \
    --hostnqn=nqn.2024-01.io.spdk:client01 \
    --hostid=$(uuidgen) \
    --nr-io-queues=8 \
    --queue-size=128 \
    --keep-alive-tmo=30
```

### 5. Verify Connection

```bash
# List connected NVMe devices
sudo nvme list

# Check device details
lsblk | grep nvme

# View connection info
sudo nvme list-subsys
```

**Example output:**
```
nvme-subsys0 - NQN=nqn.2024-01.io.spdk:storage-cluster
\
 +- nvme0 rdma traddr=192.168.100.10 trsvcid=4420 live
    +- nvme0n1 1.82T Linux
    +- nvme0n2 1.82T Linux
```

### 6. Create Filesystem and Mount

```bash
# Create filesystem on namespace 1
sudo mkfs.ext4 /dev/nvme0n1

# Create mount point
sudo mkdir -p /mnt/spdk-storage

# Mount filesystem
sudo mount /dev/nvme0n1 /mnt/spdk-storage

# Verify mount
df -h /mnt/spdk-storage
```

**Make persistent in `/etc/fstab`:**

```bash
echo '/dev/nvme0n1 /mnt/spdk-storage ext4 defaults,_netdev 0 0' | sudo tee -a /etc/fstab
```

---

## Performance Tuning

### 1. CPU Affinity and NUMA

**Pin SPDK target to specific cores:**

```bash
# Start target with specific core mask
sudo /opt/spdk/app/nvmf_tgt/nvmf_tgt -c /etc/spdk/nvmf.json -m 0xFF  # Cores 0-7

# Check NUMA topology
numactl --hardware

# Pin to NUMA node 0
sudo numactl --cpunodebind=0 --membind=0 /opt/spdk/app/nvmf_tgt/nvmf_tgt -c /etc/spdk/nvmf.json
```

### 2. AIO Optimization

**Tune AIO parameters in configuration:**

```json
{
  "method": "bdev_aio_create",
  "params": {
    "name": "aio0",
    "filename": "/dev/nvme0n1",
    "block_size": 4096,
    "readonly": false
  }
}
```

**Kernel AIO tuning:**

```bash
# Increase AIO limits
echo 1048576 | sudo tee /proc/sys/fs/aio-max-nr
echo "fs.aio-max-nr = 1048576" | sudo tee -a /etc/sysctl.conf

# Tune I/O scheduler for NVMe
echo none | sudo tee /sys/block/nvme0n1/queue/scheduler
echo 2 | sudo tee /sys/block/nvme0n1/queue/nomerges
echo 1024 | sudo tee /sys/block/nvme0n1/queue/nr_requests
```

### 3. Network Tuning

**RDMA optimization:**

```bash
# Increase RDMA buffer sizes
sudo sysctl -w net.core.rmem_max=268435456
sudo sysctl -w net.core.wmem_max=268435456

# Tune RDMA CM parameters
echo 8192 | sudo tee /sys/module/rdma_cm/parameters/max_backlog
```

**TCP optimization:**

```bash
# TCP tuning for storage
sudo sysctl -w net.ipv4.tcp_timestamps=1
sudo sysctl -w net.ipv4.tcp_sack=1
sudo sysctl -w net.ipv4.tcp_window_scaling=1
sudo sysctl -w net.core.netdev_max_backlog=5000
sudo sysctl -w net.ipv4.tcp_congestion_control=cubic
```

### 4. Transport Parameters

**Optimize NVMe-oF transport settings:**

```json
{
  "method": "nvmf_create_transport",
  "params": {
    "trtype": "RDMA",
    "num_shared_buffers": 8192,
    "max_queue_depth": 256,
    "max_qpairs_per_ctrlr": 128,
    "in_capsule_data_size": 8192,
    "max_io_size": 262144,
    "io_unit_size": 262144,
    "max_aq_depth": 128,
    "num_cqe": 4096
  }
}
```

### 5. Client-Side Tuning

```bash
# Increase number of I/O queues
sudo nvme connect -t rdma \
    -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.10 \
    -s 4420 \
    --nr-io-queues=16 \
    --queue-size=256

# Tune NVMe multipath
echo "options nvme_core multipath=Y" | sudo tee /etc/modprobe.d/nvme.conf
```

---

## Monitoring and Troubleshooting

### 1. Monitor Target Performance

```bash
# Get real-time statistics
rpc.py bdev_get_iostat

# Monitor specific bdev
rpc.py bdev_get_iostat -b aio0

# Get subsystem statistics
rpc.py nvmf_get_stats
```

### 2. Enable Debug Logging

```bash
# Start target with debug logging
sudo /opt/spdk/app/nvmf_tgt/nvmf_tgt -c /etc/spdk/nvmf.json -m 0x3 -L all

# Set log level via RPC
rpc.py log_set_level DEBUG

# Set log level for specific component
rpc.py log_set_flag nvmf
```

### 3. Common Issues and Solutions

**Issue: "Failed to allocate hugepages"**
```bash
# Solution: Increase hugepage allocation
echo 2048 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

**Issue: "Cannot open /dev/nvme0n1: Device or resource busy"**
```bash
# Solution: Ensure device is not mounted or in use
sudo umount /dev/nvme0n1
sudo fuser -k /dev/nvme0n1
```

**Issue: "RDMA connection failed"**
```bash
# Solution: Verify RDMA modules and connectivity
sudo modprobe rdma_cm rdma_ucm
ibv_devices
rdma link
ping 192.168.100.10
```

**Issue: "Keep-alive timeout"**
```bash
# Solution: Increase keep-alive timeout
sudo nvme connect -t rdma \
    -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.10 \
    -s 4420 \
    --keep-alive-tmo=60
```

### 4. Performance Benchmarking

**Using fio:**

```bash
# Install fio
sudo apt-get install -y fio

# Random read test
sudo fio --name=randread \
    --ioengine=libaio \
    --iodepth=32 \
    --rw=randread \
    --bs=4k \
    --direct=1 \
    --size=10G \
    --numjobs=4 \
    --runtime=60 \
    --group_reporting \
    --filename=/dev/nvme0n1

# Sequential write test
sudo fio --name=seqwrite \
    --ioengine=libaio \
    --iodepth=32 \
    --rw=write \
    --bs=128k \
    --direct=1 \
    --size=10G \
    --numjobs=1 \
    --runtime=60 \
    --group_reporting \
    --filename=/dev/nvme0n1
```

**Using SPDK's perf tool:**

```bash
# Build perf tool
cd /opt/spdk/examples/nvme/perf
make

# Run performance test
sudo ./perf -q 128 -o 4096 -w randread -t 60 \
    -r "trtype:RDMA adrfam:IPv4 traddr:192.168.100.10 trsvcid:4420 subnqn:nqn.2024-01.io.spdk:storage-cluster"
```

---

## Production Deployment

### 1. High Availability Setup

**Multi-path configuration:**

```bash
# Enable NVMe multipath
echo "options nvme_core multipath=Y" | sudo tee /etc/modprobe.d/nvme.conf
sudo modprobe -r nvme_core
sudo modprobe nvme_core

# Connect to multiple targets
sudo nvme connect -t rdma -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.10 -s 4420
sudo nvme connect -t rdma -n nqn.2024-01.io.spdk:storage-cluster \
    -a 192.168.100.11 -s 4420

# Verify multipath
sudo nvme list-subsys
```

### 2. Security Configuration

**Enable authentication:**

```json
{
  "method": "nvmf_subsystem_add_host",
  "params": {
    "nqn": "nqn.2024-01.io.spdk:storage-cluster",
    "host": "nqn.2024-01.io.spdk:client01",
    "dhchap_key": "DHHC-1:00:base64encodedkey",
    "dhchap_ctrlr_key": "DHHC-1:00:base64encodedkey"
  }
}
```

**TLS encryption (for TCP):**

```bash
# Generate certificates
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes

# Configure TLS in transport
rpc.py nvmf_create_transport -t TCP --tls-version 1.3
```

### 3. Monitoring and Alerting

**Prometheus exporter:**

```bash
# Enable metrics endpoint
rpc.py framework_start_init
rpc.py framework_wait_init

# Scrape metrics
curl http://localhost:9090/metrics
```

**Health check script:**

```bash
#!/bin/bash
# health_check.sh

# Check if target is running
if ! pgrep -x nvmf_tgt > /dev/null; then
    echo "ERROR: SPDK target not running"
    exit 1
fi

# Check RPC connectivity
if ! rpc.py spdk_get_version > /dev/null 2>&1; then
    echo "ERROR: Cannot connect to RPC"
    exit 1
fi

# Check bdev status
BDEVS=$(rpc.py bdev_get_bdevs | jq -r '.[].name')
if [ -z "$BDEVS" ]; then
    echo "ERROR: No block devices found"
    exit 1
fi

echo "OK: All checks passed"
exit 0
```

### 4. Backup and Recovery

**Configuration backup:**

```bash
# Backup current configuration
rpc.py save_config > /etc/spdk/nvmf_backup_$(date +%Y%m%d).json

# Backup script
#!/bin/bash
BACKUP_DIR=/var/backups/spdk
mkdir -p $BACKUP_DIR
rpc.py save_config > $BACKUP_DIR/nvmf_$(date +%Y%m%d_%H%M%S).json
find $BACKUP_DIR -name "nvmf_*.json" -mtime +30 -delete
```

### 5. Cluster Management Script

```bash
#!/bin/bash
# cluster_manager.sh

NODES=("192.168.100.10" "192.168.100.11" "192.168.100.12")
ACTION=$1

case $ACTION in
    start)
        for node in "${NODES[@]}"; do
            ssh root@$node "systemctl start spdk-nvmf"
        done
        ;;
    stop)
        for node in "${NODES[@]}"; do
            ssh root@$node "systemctl stop spdk-nvmf"
        done
        ;;
    status)
        for node in "${NODES[@]}"; do
            echo "=== Node: $node ==="
            ssh root@$node "systemctl status spdk-nvmf"
        done
        ;;
    *)
        echo "Usage: $0 {start|stop|status}"
        exit 1
        ;;
esac
```

---

## Appendix

### A. Complete Example Configuration

**Full production configuration (`/etc/spdk/nvmf_production.json`):**

```json
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "method": "bdev_set_options",
          "params": {
            "bdev_io_pool_size": 65536,
            "bdev_io_cache_size": 256
          }
        },
        {
          "method": "bdev_aio_create",
          "params": {
            "name": "aio_nvme0",
            "filename": "/dev/nvme0n1",
            "block_size": 4096
          }
        },
        {
          "method": "bdev_aio_create",
          "params": {
            "name": "aio_nvme1",
            "filename": "/dev/nvme1n1",
            "block_size": 4096
          }
        }
      ]
    },
    {
      "subsystem": "nvmf",
      "config": [
        {
          "method": "nvmf_set_max_subsystems",
          "params": {
            "max_subsystems": 1024
          }
        },
        {
          "method": "nvmf_set_crdt",
          "params": {
            "crdt1": 0,
            "crdt2": 0,
            "crdt3": 0
          }
        },
        {
          "method": "nvmf_create_transport",
          "params": {
            "trtype": "RDMA",
            "num_shared_buffers": 8192,
            "max_queue_depth": 256,
            "max_qpairs_per_ctrlr": 128,
            "in_capsule_data_size": 8192,
            "max_io_size": 262144,
            "io_unit_size": 262144,
            "max_aq_depth": 128,
            "num_cqe": 4096,
            "acceptor_backlog": 100
          }
        },
        {
          "method": "nvmf_create_subsystem",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "allow_any_host": false,
            "serial_number": "SPDK00000000000001",
            "model_number": "SPDK_AIO_Controller",
            "max_namespaces": 256,
            "min_cntlid": 1,
            "max_cntlid": 65519
          }
        },
        {
          "method": "nvmf_subsystem_add_ns",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "namespace": {
              "nsid": 1,
              "bdev_name": "aio_nvme0",
              "uuid": "11111111-1111-1111-1111-111111111111"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_ns",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "namespace": {
              "nsid": 2,
              "bdev_name": "aio_nvme1",
              "uuid": "22222222-2222-2222-2222-222222222222"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_listener",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "listen_address": {
              "trtype": "RDMA",
              "traddr": "192.168.100.10",
              "trsvcid": "4420",
              "adrfam": "IPv4"
            }
          }
        },
        {
          "method": "nvmf_subsystem_add_host",
          "params": {
            "nqn": "nqn.2024-01.io.spdk:storage-cluster",
            "host": "nqn.2