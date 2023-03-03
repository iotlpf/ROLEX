# Installing RLib

This document provides install guide for how to run `RLib`' examples and benchmarks.
Actually there is nothing to install about RLib itself because it is a header-only library :). 

## Table of Contents

* [Preparing and Downloading](#dl)
* [Satisfying Dependences](#deps)
* [Building and Running](#run)

<br>
<a name="dl"></a>
## Preparing and Downloading

You can download rlib directly from the Github Repository. If you are one of developers, it is recommended to **fork** your own repository and develop on it afterwards.

```bash
$git clone https://ipads.se.sjtu.edu.cn:1312/weixd/rib.git --recursive
$cd rib
```

<br>
<a name="deps"></a>
## Satisfying Dependences

#### Install RLib dependecies on one of your cluster machines

We use gitmodules (i.e., `.gitmodules`) to download and install required dependencies automatically within the certain sub-directory.
Note, the dependenices is only required for examples and benchmarks. 

> Currently, we requires gflags.

```bash
# install all submodule
$git submodule update
```


#### Copy rlib codes to all machines involved or use distributed file systems like NFS. 

1) Setup password-less SSH between the master node and all other machines.

Verify it is possible to ssh without password between any pairs of machines. These [instructions](http://www.linuxproblem.org/art_9.html) explain how to setup ssh without passswords.

Before proceeding, verify that this is setup correctly; check that the following connects to the remote machine without prompting for a password:

```bash
$ssh user@node
```

2) Edit `scripts/sync_to_server.sh` with username and name of all machines in your cluster. For example:

```bash
$cat scripts/sync_to_server.sh
user="xxx"
target=("xxx01" "xxx02")
...
```
If you want to use IP address as machines' hostname, you can change this file as you need.

3) Run the following commands to copy necessities to the rest machines.

```bash
$cd scripts
$./sync_to_server.sh
```


<br>
<a name="run"></a>
## Building and Running

#### Compile rlib from remote

1) Make sure you have installed python3 and required packages for `scripts/bootstrap.py`.

```bash
$pip3 install toml ...
```

2) Use your own username and password for ssh. You can also change your ssh_config file path if using proxy.

```python
...
with open(os.path.expanduser("/etc/ssh/ssh_config")) as _file:
    config.parse(_file)
...
user = config.get("user","xxx")
pwd  = config.get("pwd","***")
...
```

3) We use CMake to build rlib and provide a script file `make.toml` to simplify the procedure. You can learn more in detailed `TUTORIALS.md`.
```bash
$cd scripts/
$./bootstrap.py -f make.toml
```


#### Launch example executable file (server + client)

You can learn more in detailed `TUTORIALS.md`.

```bash
$cd scripts/
$./bootstrap.py -f run.toml
...
```
