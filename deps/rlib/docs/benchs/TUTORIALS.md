# Benchmark Tutorials

## Table of Contents

* [Introduction](#intro)
* [Building](#build)
* [Running](#run)
* [Performance](#perform)

<a  name="intro"></a>

## Introduction
Codes in `rib\bench\` are used to evaluate **peak throughput** for one-sided **RDMA Read**.

<a  name="build"></a>

## Building

We use CMake to build rib and provide a script file `make.toml` to simplify the procedure. 

```bash
$cd scripts/
$./bootstrap.py -f benchs/rw_bench/make.toml
```

<a  name="run"></a>

## Running

We provide a script file `run.toml` to simplify the procedure.

```bash
$cd scripts/
$./bootstrap.py -f benchs/rw_bench/run.toml
```

The first host is used as server and the rest are clients. You can change `cmd` in `run.toml` to get running params.

```bash
# server
$cmd = 'cd rib/; ./bench_server -help'
    -magic_num (The magic number read by the client) type: uint64
      default: 3735928495
    -port (Server listener (UDP) port.) type: int64 default: 8888
    -reg_mem_name (The name to register an MR at rctrl.) type: int64
      default: 73
    -reg_nic_name (The name to register an opened NIC at rctrl.) type: int64
      default: 73
    -use_nic_idx (Which NIC to create QP) type: int64 default: 0
```

```bash
# client
$cmd = 'cd rib/; ./bench_client -help'
    -addr (Server address to connect to.) type: string default: "val09:8888"
    -para_factor (#keep <num> queries being processed.) type: int64 default: 20
    -reg_mem_name (The name to register an MR at rctrl.) type: int64
      default: 73
    -reg_nic_name (The name to register an opened NIC at rctrl.) type: int64
      default: 73
    -threads (#Threads used.) type: int64 default: 1
    -use_nic_idx (Which NIC to create QP) type: int64 default: 0
```

> Note: If you change server host, you must add server address parameter to client like this: 

```bash
# client
$cmd = 'cd rib/; ./bench_client -addr [your server IP]'
```

<a  name="perform"></a>

## Performance

```bash
$cd scripts/
$./bootstrap.py -f benchs/make.toml
$./bootstrap.py -f benchs/run.toml
...
[reporter.hh:33] epoch @ 3: thpt: 2.79313e+06 reqs/sec.1.0001e+06 msec passed since last epoch.
[reporter.hh:33] epoch @ 4: thpt: 2.80423e+06 reqs/sec.1.00007e+06 msec passed since last epoch.
[reporter.hh:33] epoch @ 5: thpt: 2.79937e+06 reqs/sec.1.00008e+06 msec passed since last epoch.
...
```

