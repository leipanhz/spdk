# SPDK AIO Configuration - Important Correction

## Key Point: AIO is Built by Default

**You are correct!** There is **NO** `--with-aio` configure option in SPDK.

The AIO bdev module is **built by default** and does not require any special configuration flags.

## Correct Build Instructions

### Standard Build (includes AIO)

```bash
# Clone SPDK
cd /opt
sudo git clone https://github.com/spdk/spdk.git
cd spdk
sudo git submodule update --init

# Configure (AIO is included by default)
./configure

# Build
make -j$(nproc)
```

### Build with Additional Features

```bash
# For RDMA support
./configure --with-rdma

# For iSCSI initiator support
./configure --with-iscsi-initiator

# For RBD (Ceph) support
./configure --with-rbd

# For all common features
./configure --with-rdma --with-iscsi-initiator --with-crypto --with-rbd
```

## Why No --with-aio Flag?

The AIO bdev module is a **core component** of SPDK and is always compiled. Looking at the source code:

```
module/bdev/aio/bdev_aio.c  ← Always built
```

The module uses Linux's native AIO (`libaio`) which is a standard Linux feature.

## Available Configure Options

To see all available options:

```bash
./configure --help
```

**AIO-related options you'll see:**
- `--with-aio-fsdev` - Build aio FSDEV component (different from bdev AIO)
- `--without-aio-fsdev` - Disable aio FSDEV component

**Note:** `--with-aio-fsdev` is for the **filesystem device** (FSDEV) layer, NOT the block device (bdev) AIO module.

## Using AIO Bdev

Once SPDK is built, you can use AIO bdev without any special configuration:

```bash
# Start SPDK target
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &

# Create AIO bdev (works immediately, no special setup needed)
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096

# Verify
sudo ./scripts/rpc.py bdev_get_bdevs
```

## Dependencies for AIO

The only requirement is that `libaio-dev` is installed:

```bash
# Ubuntu/Debian
sudo apt-get install libaio-dev

# RHEL/CentOS
sudo yum install libaio-devel
```

This is typically installed as part of the standard SPDK dependencies.

## Summary

| Statement | Correct? |
|-----------|----------|
| "Use `./configure --with-aio`" | ❌ No such option |
| "AIO is built by default" | ✅ Yes |
| "Just use `./configure`" | ✅ Correct |
| "Need `libaio-dev` installed" | ✅ Yes |
| "Can use `bdev_aio_create` immediately" | ✅ Yes |

## Corrected Quick Start

```bash
# 1. Install dependencies (includes libaio-dev)
sudo apt-get install -y build-essential git libaio-dev

# 2. Clone and build SPDK (AIO included automatically)
cd /opt && sudo git clone https://github.com/spdk/spdk.git
cd spdk && sudo git submodule update --init
./configure && make -j$(nproc)

# 3. Setup hugepages
sudo HUGEMEM=2048 scripts/setup.sh

# 4. Start target and use AIO
sudo ./app/nvmf_tgt/nvmf_tgt -m 0x3 &
sleep 2
sudo ./scripts/rpc.py bdev_aio_create /dev/nvme0n1 aio0 4096
```

That's it! No special AIO configuration needed.

---

**Thank you for catching this error!** The documentation has been corrected.