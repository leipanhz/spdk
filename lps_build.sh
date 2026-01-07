#!/bin/bash

sudo scripts/pkgdep.sh --rdma
./configure --with-rdma
make
sudo build/bin/nvmf_tgt

