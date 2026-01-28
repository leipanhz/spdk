# AIO vs NVMe Bdev: Understanding the Difference

## Quick Answer

**For AIO setup:** Use `bdev_aio_create` - NO need for `bdev_nvme_attach_controller`

**For VFIO setup:** Use `bdev_nvme_attach_controller` - NO need for `bdev_aio_create`

## The Confusion Explained

There are **two different ways** to access NVMe devices in SPDK:

### Method 1: AIO Backend (Kernel-based)
```bash
# Uses Linux block device (/dev/nvme0n1)
rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
```

### Method 2: NVMe Driver (Kernel bypass)
```bash
# Uses PCIe address directly
rpc.py bdev_nvme_attach_controller -b nvme0 -t pcie -a 0000:01:00.0
```

## Detailed Comparison

### AIO Backend Setup

```
┌─────────────────────────────────────────┐
│  Step 1: Device stays in kernel         │
│  /dev/nvme0n1 is visible                │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  Step 2: Create AIO bdev                │
│  rpc.py bdev_aio_create /dev/nvme0n1    │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  Result: SPDK uses kernel's NVMe driver │
│  via Linux AIO interface                │
└─────────────────────────────────────────┘
```

**Commands:**
```bash
# That's it! Just one command:
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

# NO bdev_nvme_attach_controller needed!
```

### NVMe Driver Setup (VFIO)

```
┌─────────────────────────────────────────┐
│  Step 1: Unbind device from kernel      │
│  scripts/setup.sh                       │
│  /dev/nvme0n1 disappears                │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  Step 2: Attach via PCIe address        │
│  rpc.py bdev_nvme_attach_controller     │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  Result: SPDK directly controls NVMe    │
│  hardware (kernel bypass)               │
└─────────────────────────────────────────┘
```

**Commands:**
```bash
# Step 1: Unbind from kernel
sudo ./scripts/setup.sh

# Step 2: Attach controller
sudo ./scripts/rpc.py bdev_nvme_attach_controller \
    -b nvme0 -t pcie -a 0000:01:00.0

# NO bdev_aio_create needed!
```

## Side-by-Side Comparison

| Aspect | AIO Backend | NVMe Driver |
|--------|-------------|-------------|
| **RPC Command** | `bdev_aio_create` | `bdev_nvme_attach_controller` |
| **Input** | Block device path (`/dev/nvme0n1`) | PCIe address (`0000:01:00.0`) |
| **Device Binding** | Stays in kernel | Unbound from kernel |
| **Setup Script** | Not needed | `scripts/setup.sh` required |
| **Device Visibility** | Visible in `/dev/` | Not visible in `/dev/` |
| **Access Method** | Through kernel driver | Direct hardware access |

## Complete Examples

### Example 1: AIO Setup (Simple)

```bash
#!/bin/bash
# aio_setup.sh

# Start SPDK target
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
sleep 2

# Create AIO bdev - ONE command, that's all!
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme1n1 aio1 4096

# Create NVMe-oF subsystem
sudo ./scripts/rpc.py nvmf_create_transport -t TCP
sudo ./scripts/rpc.py nvmf_create_subsystem nqn.2024-01.io.spdk:cnode1 -a
sudo ./scripts/rpc.py nvmf_subsystem_add_ns nqn.2024-01.io.spdk:cnode1 aio0
sudo ./scripts/rpc.py nvmf_subsystem_add_listener nqn.2024-01.io.spdk:cnode1 \
    -t TCP -a 192.168.1.10 -s 4420

echo "AIO setup complete!"
```

### Example 2: NVMe Driver Setup (VFIO)

```bash
#!/bin/bash
# vfio_setup.sh

# Step 1: Setup VFIO (unbind devices)
sudo HUGEMEM=4096 ./scripts/setup.sh

# Step 2: Start SPDK target
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
sleep 2

# Step 3: Attach NVMe controllers via PCIe
sudo ./scripts/rpc.py bdev_nvme_attach_controller \
    -b nvme0 -t pcie -a 0000:01:00.0
sudo ./scripts/rpc.py bdev_nvme_attach_controller \
    -b nvme1 -t pcie -a 0000:02:00.0

# Step 4: Create NVMe-oF subsystem
sudo ./scripts/rpc.py nvmf_create_transport -t TCP
sudo ./scripts/rpc.py nvmf_create_subsystem nqn.2024-01.io.spdk:cnode1 -a
sudo ./scripts/rpc.py nvmf_subsystem_add_ns nqn.2024-01.io.spdk:cnode1 nvme0n1
sudo ./scripts/rpc.py nvmf_subsystem_add_listener nqn.2024-01.io.spdk:cnode1 \
    -t TCP -a 192.168.1.10 -s 4420

echo "VFIO setup complete!"
```

## Why the Confusion?

The confusion often arises because:

1. **Both access NVMe devices** - but in completely different ways
2. **Documentation sometimes mixes them** - showing both methods
3. **Similar naming** - both create "bdevs" (block devices in SPDK)

## JSON Configuration Examples

### AIO Configuration

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
        }
      ]
    }
  ]
}
```

**Note:** No `bdev_nvme_attach_controller` in AIO config!

### NVMe Driver Configuration

```json
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "method": "bdev_nvme_attach_controller",
          "params": {
            "name": "nvme0",
            "trtype": "PCIe",
            "traddr": "0000:01:00.0"
          }
        }
      ]
    }
  ]
}
```

**Note:** No `bdev_aio_create` in NVMe driver config!

## Common Mistakes

### ❌ Wrong: Mixing Both Methods

```bash
# DON'T DO THIS!
sudo ./scripts/setup.sh  # Unbinds device
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096  # FAILS! Device gone
```

**Error:** Device no longer exists in `/dev/` after unbinding.

### ❌ Wrong: Using Wrong Command

```bash
# DON'T DO THIS!
sudo ./scripts/rpc.py bdev_nvme_attach_controller \
    -b nvme0 -t pcie -a /dev/nvme0n1  # WRONG! Not a PCIe address
```

**Error:** `/dev/nvme0n1` is not a PCIe address.

### ✅ Correct: Choose One Method

**Option A: AIO**
```bash
# Device stays in kernel
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
```

**Option B: NVMe Driver**
```bash
# Device unbound from kernel
sudo ./scripts/setup.sh
sudo ./scripts/rpc.py bdev_nvme_attach_controller -b nvme0 -t pcie -a 0000:01:00.0
```

## How to Find PCIe Address (if needed for VFIO)

```bash
# Method 1: lspci
lspci | grep -i nvme
# Output: 01:00.0 Non-Volatile memory controller: Samsung...

# Method 2: From /dev/nvme0n1
ls -l /sys/block/nvme0n1
# Follow symlink to find PCIe address

# Method 3: nvme list with verbose
nvme list -v
```

## Decision Tree

```
Do you want to use AIO?
    ↓
   Yes → Use bdev_aio_create with /dev/nvmeXnY
    │    NO bdev_nvme_attach_controller needed
    │
   No → Want kernel bypass (VFIO)?
         ↓
        Yes → Use bdev_nvme_attach_controller with PCIe address
              NO bdev_aio_create needed
```

## Summary Table

| Setup Type | Command to Use | Input Required | Other Command Needed? |
|------------|----------------|----------------|----------------------|
| **AIO** | `bdev_aio_create` | `/dev/nvme0n1` | ❌ No |
| **VFIO** | `bdev_nvme_attach_controller` | `0000:01:00.0` | ❌ No |

## Key Takeaway

**For AIO setup:**
- ✅ Use `bdev_aio_create /dev/nvme0n1`
- ❌ Do NOT use `bdev_nvme_attach_controller`
- ❌ Do NOT run `scripts/setup.sh` (keeps devices in kernel)

**For VFIO setup:**
- ✅ Use `bdev_nvme_attach_controller -t pcie -a 0000:01:00.0`
- ❌ Do NOT use `bdev_aio_create`
- ✅ DO run `scripts/setup.sh` (unbinds devices)

They are **mutually exclusive** - choose one method, not both!

---

**Bottom Line:** If you're doing AIO setup, you only need `bdev_aio_create`. The `bdev_nvme_attach_controller` command is for a completely different setup method (VFIO/kernel bypass).