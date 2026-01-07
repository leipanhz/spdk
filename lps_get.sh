#!/bin/bash

set -x

sudo scripts/rpc.py bdev_get_bdevs
sudo scripts/rpc.py bdev_nvme_get_controllers
sudo scripts/rpc.py nvmf_get_transports


set +x

