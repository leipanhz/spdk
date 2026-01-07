#!/bin/bash

echo "0000:31:00.0" | sudo tee /sys/bus/pci/drivers/nvme/unbind
ls /dev/nvme0*
sudo modprobe vfio-pci
echo "8086 2701" | sudo tee /sys/bus/pci/drivers/vfio-pci/new_id


