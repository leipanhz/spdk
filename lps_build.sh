#!/bin/bash

sudo scripts/pkgdep.sh --rdma
./configure --with-rdma
make

echo "make is complete, run build/bin/nvmf_tgt with needed arguments to start"
#sudo build/bin/nvmf_tgt

