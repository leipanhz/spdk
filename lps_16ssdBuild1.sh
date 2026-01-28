#!/bin/bash

numactl -N 1 -m 1 sudo  /home/leipan/Workspace/spdk_leifork/build/bin/nvmf_tgt   -m 0x000000000000FFFF   --wait-for-rpc

