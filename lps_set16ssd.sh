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

sleep 10; 
read -p "Press Enter to continue  \n" </dev/tty

for bdf in $PCIES; do
    echo $bdf; 
    # sudo PCI_ALLOWED=$bdf scripts/setup.sh
    xy=$(echo "$bdf" | cut -d: -f2); 
    sudo scripts/rpc.py bdev_nvme_attach_controller \
        -b spdk_nvme_${xy}\
        -t PCIe \
        -a $bdf;
    sleep 1;
done
sudo scripts/rpc.py bdev_nvme_get_controllers | grep name


sudo scripts/rpc.py nvmf_create_transport -t RDMA -u 8192 -i 131072 -c 8192
#sudo ./scripts/rpc.py nvmf_create_transport -t rdma \
#    --max-io-size 131072 \
#    --io-unit-size 131072 \
#    --num-shared-buffers 65536 \
#    --buf-cache-size 256
sudo scripts/rpc.py nvmf_get_transports

sudo scripts/rpc.py nvmf_create_subsystem $NQN -a -s $SN 
sudo scripts/rpc.py nvmf_get_subsystems


bdevs=$(sudo scripts/rpc.py bdev_get_bdevs | grep \"name\" | cut -d"\"" -f4)

# run only once; if ns is added, will get “Invalid parameters” error
for ns in $bdevs; do \
	sudo scripts/rpc.py nvmf_subsystem_add_ns $NQN $ns; \
done

# look for namespace
sudo scripts/rpc.py nvmf_get_subsystems | less
sudo scripts/rpc.py bdev_nvme_get_controllers | less

read -p "Press Enter to add listener" </dev/tty
sudo scripts/rpc.py nvmf_subsystem_add_listener  $NQN -t rdma -a 10.243.3.20 -s 4420

read -p "Press Enter to save config, otherwise ctrl+C to exit" </dev/tty
sudo scripts/rpc.py save_config > ~/Config/spdk/spdk_config_1215_16ssd.json

set +x
