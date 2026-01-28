# SPDK AIO Setup Verification Guide

## Important: `bdev_nvme_get_controllers` Will Be Empty for AIO!

### This is CORRECT and EXPECTED! ✅

When using AIO backend, `bdev_nvme_get_controllers` returns empty because:
- **AIO doesn't use NVMe controllers** - it uses Linux block devices
- **NVMe controllers** are only for VFIO/kernel bypass setup
- **AIO bdevs** are verified with a different command

## Correct Verification Commands for AIO

### ✅ Use `bdev_get_bdevs` (NOT `bdev_nvme_get_controllers`)

```bash
# This is the CORRECT command for AIO verification
sudo ./scripts/rpc.py bdev_get_bdevs

# Expected output:
[
  {
    "name": "aio0",
    "aliases": [],
    "product_name": "AIO disk",
    "block_size": 4096,
    "num_blocks": 488397168,
    "uuid": "...",
    "assigned_rate_limits": {},
    "claimed": false,
    "zoned": false,
    "supported_io_types": {
      "read": true,
      "write": true,
      "unmap": true,
      "write_zeroes": true,
      "flush": true,
      "reset": true,
      "compare": false,
      "compare_and_write": false,
      "abort": true,
      "nvme_admin": false,
      "nvme_io": false
    },
    "driver_specific": {
      "aio": {
        "filename": "/dev/nvme0n1"
      }
    }
  }
]
```

### ❌ `bdev_nvme_get_controllers` Returns Empty (This is Normal!)

```bash
# This will be EMPTY for AIO setup - that's correct!
sudo ./scripts/rpc.py bdev_nvme_get_controllers

# Output: []
# This is EXPECTED because AIO doesn't use NVMe controllers
```

## Complete Verification Workflow

### Step 1: Create AIO Bdev

```bash
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
```

### Step 2: Verify with Correct Command

```bash
# ✅ CORRECT: Check bdevs (block devices)
sudo ./scripts/rpc.py bdev_get_bdevs

# Look for:
# - "name": "aio0"
# - "product_name": "AIO disk"
# - "filename": "/dev/nvme0n1"
```

### Step 3: Additional Verification

```bash
# Check I/O statistics
sudo ./scripts/rpc.py bdev_get_iostat

# List all bdev names
sudo ./scripts/rpc.py bdev_get_bdevs | jq -r '.[].name'

# Get specific bdev info
sudo ./scripts/rpc.py bdev_get_bdevs -b aio0
```

## Understanding the Difference

### AIO Backend Architecture

```
┌─────────────────────────────────────────┐
│  SPDK RPC Commands for AIO              │
├─────────────────────────────────────────┤
│  ✅ bdev_get_bdevs        → Shows AIO   │
│  ✅ bdev_aio_create       → Creates AIO │
│  ✅ bdev_aio_delete       → Deletes AIO │
│  ✅ bdev_get_iostat       → Shows stats │
│  ❌ bdev_nvme_get_controllers → Empty   │
│  ❌ bdev_nvme_attach_controller → N/A   │
└─────────────────────────────────────────┘
```

### NVMe Driver Architecture (VFIO)

```
┌─────────────────────────────────────────┐
│  SPDK RPC Commands for NVMe Driver      │
├─────────────────────────────────────────┤
│  ✅ bdev_get_bdevs        → Shows NVMe  │
│  ✅ bdev_nvme_get_controllers → Shows   │
│  ✅ bdev_nvme_attach_controller → Adds  │
│  ✅ bdev_nvme_detach_controller → Removes│
│  ❌ bdev_aio_create       → N/A         │
└─────────────────────────────────────────┘
```

## Comparison Table

| Command | AIO Setup | VFIO Setup |
|---------|-----------|------------|
| `bdev_get_bdevs` | ✅ Shows AIO bdevs | ✅ Shows NVMe bdevs |
| `bdev_nvme_get_controllers` | ❌ Empty (normal) | ✅ Shows controllers |
| `bdev_aio_create` | ✅ Used | ❌ Not used |
| `bdev_nvme_attach_controller` | ❌ Not used | ✅ Used |

## Complete Example with Verification

```bash
#!/bin/bash
# aio_setup_and_verify.sh

echo "=== Starting SPDK Target ==="
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
SPDK_PID=$!
sleep 3

echo "=== Creating AIO Bdev ==="
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

echo "=== Verification ==="

echo "1. Check bdevs (should show aio0):"
sudo ./scripts/rpc.py bdev_get_bdevs | jq -r '.[].name'

echo "2. Check NVMe controllers (will be empty - this is CORRECT):"
CONTROLLERS=$(sudo ./scripts/rpc.py bdev_nvme_get_controllers)
if [ "$CONTROLLERS" == "[]" ]; then
    echo "   ✅ Empty as expected for AIO setup"
else
    echo "   ⚠️  Unexpected: $CONTROLLERS"
fi

echo "3. Get detailed bdev info:"
sudo ./scripts/rpc.py bdev_get_bdevs -b aio0 | jq '{name, product_name, block_size, driver_specific}'

echo "4. Check I/O statistics:"
sudo ./scripts/rpc.py bdev_get_iostat -b aio0

echo "=== Setup Complete ==="
```

## Expected Output

```
=== Starting SPDK Target ===
Starting SPDK v24.01 / DPDK 23.11.0 initialization...

=== Creating AIO Bdev ===

=== Verification ===
1. Check bdevs (should show aio0):
aio0

2. Check NVMe controllers (will be empty - this is CORRECT):
   ✅ Empty as expected for AIO setup

3. Get detailed bdev info:
{
  "name": "aio0",
  "product_name": "AIO disk",
  "block_size": 4096,
  "driver_specific": {
    "aio": {
      "filename": "/dev/nvme0n1"
    }
  }
}

4. Check I/O statistics:
{
  "tick_rate": 2400000000,
  "ticks": 7200000000,
  "bdevs": [
    {
      "name": "aio0",
      "bytes_read": 0,
      "num_read_ops": 0,
      "bytes_written": 0,
      "num_write_ops": 0,
      ...
    }
  ]
}

=== Setup Complete ===
```

## Troubleshooting

### Issue: `bdev_get_bdevs` is Empty

```bash
# Check if bdev was created
sudo ./scripts/rpc.py bdev_get_bdevs

# If empty, check:
# 1. Is device accessible?
ls -l /dev/nvme0n1

# 2. Is device mounted?
mount | grep nvme0n1

# 3. Check SPDK logs
sudo journalctl -u spdk-nvmf -n 50

# 4. Try creating again with verbose output
sudo ./scripts/rpc.py -v bdev_aio_create /dev/nvme0n1 aio0 4096
```

### Issue: "Cannot open /dev/nvme0n1"

```bash
# Solution: Unmount device
sudo umount /dev/nvme0n1

# Kill processes using it
sudo fuser -k /dev/nvme0n1

# Try again
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
```

### Issue: Confused About Empty Controllers

```bash
# This is NORMAL for AIO!
sudo ./scripts/rpc.py bdev_nvme_get_controllers
# Output: []

# ✅ This is the correct verification for AIO:
sudo ./scripts/rpc.py bdev_get_bdevs
# Should show your AIO bdevs
```

## Quick Reference Card

### For AIO Setup:

| What to Check | Command | Expected Result |
|---------------|---------|-----------------|
| **Block devices** | `bdev_get_bdevs` | ✅ Shows aio0 |
| **NVMe controllers** | `bdev_nvme_get_controllers` | ✅ Empty [] |
| **I/O stats** | `bdev_get_iostat` | ✅ Shows aio0 stats |
| **Device file** | `ls /dev/nvme0n1` | ✅ Exists |

### For VFIO Setup:

| What to Check | Command | Expected Result |
|---------------|---------|-----------------|
| **Block devices** | `bdev_get_bdevs` | ✅ Shows nvme0n1 |
| **NVMe controllers** | `bdev_nvme_get_controllers` | ✅ Shows nvme0 |
| **I/O stats** | `bdev_get_iostat` | ✅ Shows nvme0n1 stats |
| **Device file** | `ls /dev/nvme0n1` | ❌ Not found (unbound) |

## Summary

**Key Points:**

1. ✅ `bdev_nvme_get_controllers` being **empty is CORRECT** for AIO setup
2. ✅ Use `bdev_get_bdevs` to verify AIO bdevs
3. ✅ AIO bdevs show `"product_name": "AIO disk"`
4. ✅ AIO bdevs show `"driver_specific": {"aio": {"filename": "/dev/nvme0n1"}}`
5. ❌ Don't expect NVMe controllers when using AIO backend

**Remember:**
- **AIO** = Block device backend → Use `bdev_get_bdevs`
- **NVMe Driver** = Controller backend → Use `bdev_nvme_get_controllers`

Your empty `bdev_nvme_get_controllers` output confirms you're correctly using AIO! ✅

---

**Bottom Line:** If `bdev_nvme_get_controllers` is empty but `bdev_get_bdevs` shows your AIO devices, everything is working perfectly!