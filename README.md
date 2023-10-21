## prerequisites
#### devcontainer (vscode)
open project in vscode and launch devcontainer.

```bash
# build system
apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 clang bear ccache git
# dependency libraries
apt install libevent-dev libboost-dev nlohmann-json3-dev libcpp-httplib-dev
```
#### macos
```bash
brew install automake libtool boost pkg-config libevent nlohmann-json cpp-httplib bear ccache
```

## build
### manual build
#### step 1, config (optional)
In `Makefile`, uncomment `-DFETCH_REMOTE_TX` flag to automatically fetch related transactions from remote bitcoin rpc instead of passing from local function for cache when parse a omni tx. if `-DFETCH_REMOTE_TX` is set then pass tx hex, its block height, bitcoin rpc host, port, username and password. otherwise just pass tx hex, its block height and the vins info.

#### step 2, make omnicore libs
```bash
cd omnicore 
make clean # clean omnicore build

./autogen.sh
./configure CXX=clang++ CC=clang --disable-wallet --disable-zmq --disable-bench --disable-tests --disable-fuzz-binary --without-gui --without-miniupnpc --without-natpmp
make -j8
```

#### step 3, make omni-sys libs
```bash
make out # src/omni.out (execution program)
make lib # src/libomni.a (static library)
make clean # clean omni-sys build
```

### automatic build with cargo
set `-DFETCH_REMOTE_TX` flag in `build.rs` then run

```bash
cargo build
```

### test
```bash
cargo test
```