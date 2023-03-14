## ROLEX
ROLEX: A Scalable RDMA-oriented Learned Key-Value Store for Disaggregated Memory Systems

### Build
1. Dependency
```
MLNX_OFED_LINUX-5.4-1.0.3.0-ubuntu18.04-x86_64
Boost_1.65
Gflags_2.2.2
```
2. CMake
```
$ mkdir build
$ cd build
$ cmake ..
$ make
```
3. Create HugePage
### Run
```
./rolex --nkeys=100000 --non_nkeys=100000
```
   
| parameters | descriptions |
|  ----  | ----  |
| --nkeys  | the number of trained data |
| --non_nkeys  | the number of new data |
| --threads  | the number of threads |
| --coros  | the number of coroutines |
| --workloads  | workloads |
| --read_ratio  | the read ratio |
| --insert_ratio  | the write ratio |
| --update_ratio  | the update ratio |.
