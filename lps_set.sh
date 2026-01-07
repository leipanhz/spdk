#!/bin/bash


set -x

PCIE="0000:31:00.0"
NQN="nqn.2025-12.arc.spdk:wolf20"
CTROLLER="spdk_controller_w20_nvme0n"

#sudo scripts/rpc.py nvmf_create_transport -t RDMA -u 8192 -i 131072 -c 8192
sudo ./scripts/rpc.py nvmf_create_transport -t rdma \
    --max-io-size 131072 \
    --io-unit-size 131072 \
    --num-shared-buffers 65536 \
    --buf-cache-size 256

sudo scripts/rpc.py bdev_nvme_attach_controller -b spdk_controller_w20_nvme0n -t pcie -a $PCIE
sudo scripts/rpc.py bdev_malloc_create -b Malloc0 512 512
sudo scripts/rpc.py nvmf_create_subsystem $NQN -a -s SPDK202512ARCWOFL20 -d $CTROLLER
sudo scripts/rpc.py nvmf_subsystem_add_ns $NQN Malloc0
# sudo scripts/rpc.py nvmf_subsystem_add_listener  $NQN -t rdma -a 10.244.3.20 -s 4420


set +x
