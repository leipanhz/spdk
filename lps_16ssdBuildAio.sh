#!/bin/bash


set -x

NQN="nqn.2025-12.arc.spdk:wolf20"
SN="SPDK202512ARCWOFL20"

# get all  pcie ids
PCIES=`lspci -D | grep -i "non-volatile memory controller" | awk '{print $1}'`

# set buffer options
sudo scripts/rpc.py iobuf_set_options \
  --small-pool-count 32768 \
  --large-pool-count 8192
sudo scripts/rpc.py framework_start_init

sleep 5; 
read -p "Press Enter to continue  \n" </dev/tty


# for i in {0..15}; do sudo ./scripts/rpc.py bdev_aio_create /dev/nvme${i}n1 aio${i} 4096; sleep 2; done
for i in {0..15}; do sudo ./scripts/rpc.py bdev_aio_create /dev/nvme${i}n1 aio${i} 512; sleep 2; done

sudo scripts/rpc.py nvmf_create_transport -t RDMA -u 8192 -i 131072 -c 8192
sudo scripts/rpc.py nvmf_get_transports

sudo scripts/rpc.py nvmf_create_subsystem $NQN -a -s $SN
sudo scripts/rpc.py nvmf_get_subsystems


bdevs=$(sudo scripts/rpc.py bdev_get_bdevs | grep \"name\" | cut -d"\"" -f4)
# Add namespace
for ns in $bdevs; do \
	sudo scripts/rpc.py nvmf_subsystem_add_ns $NQN $ns; \
done

# look for namespace
sudo scripts/rpc.py nvmf_get_subsystems | less
sudo scripts/rpc.py bdev_nvme_get_controllers | less

read -p "Press Enter to add listener" </dev/tty
sudo scripts/rpc.py nvmf_subsystem_add_listener  $NQN -t rdma -a 10.243.3.20 -s 4420
#sudo scripts/rpc.py nvmf_subsystem_add_listener  $NQN -t rdma -a 10.243.3.20 -s 4421

read -p "Press Enter to save config, otherwise ctrl+C to exit" </dev/tty
sudo scripts/rpc.py save_config > ~/Config/spdk/spdk_config_0123_16ssd_aio.json

set +x
