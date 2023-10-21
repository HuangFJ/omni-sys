## prerequisites
#### devcontainer (vscode)
open project in vscode and launch devcontainer.

```bash
apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 clang bear ccache
apt install libevent-dev libboost-dev nlohmann-json3-dev libcpp-httplib-dev
```
#### macos
```bash
brew install automake libtool boost pkg-config libevent nlohmann-json cpp-httplib bear ccache
```

## build
### manual build
#### step 1, config (optional)
In `Makefile`, uncomment `-DFETCH_REMOTE_TX` flag to automatically fetch related transactions from remote bitcoin rpc instead of passing from local function for cache when parse a omni tx.

#### step 2, make omnicore libs
```bash
cd omnicore 
make clean # clean omnicore build

./autogen.sh
./configure CXX=clang++ CC=clang --disable-wallet --disable-zmq --disable-bench --disable-tests --disable-fuzz-binary --without-gui --without-miniupnpc --without-natpmp
make -j8
```

### step 3, make omni-sys libs
```bash
make out # src/omni.out (execution program)
make lib # src/libomni.a (static library)
make clean # clean omni-sys build
```

### step 4, test
- if `-DFETCH_REMOTE_TX` is set then pass tx hex, its block height, bitcoin rpc host, port, username and password
```bash
./src/omni.out \
020000000001018532e79d7625c2e3b4d5cf886f98fbf0ab833528fb3015184b213aaa7a20c1e17900000017160014c6b5514a86e79fe5a3e1537ebbf9df4e32b57975fdffffff03220200000000000017a9142b9190b5daffd6310b828717ab788c9350011ead870000000000000000166a146f6d6e69000000000000001f0000000b21b2b480470607000000000017a914cd93b311b5c132c2f904d9ba40a74faaa40150e18702483045022100d471da15c7ca89b75a95dd165dfb63bdba9e0d67a5cf210a9ea67e9062d1103e02206fde9cf09bf6cb4d1e460b9a6b99954c85d2de3bfea0f24e51b08414c3b04fe80121032bb55b8602969f3fbb061941c614e5e22940cff650bd323a0c2239ff479808d300000000 \
813152 \
127.0.0.1 \
8332 \
username \
passsword
```
- else just pass tx hex, its block height and the vins info
```bash
./src/omni.out \
020000000001018532e79d7625c2e3b4d5cf886f98fbf0ab833528fb3015184b213aaa7a20c1e17900000017160014c6b5514a86e79fe5a3e1537ebbf9df4e32b57975fdffffff03220200000000000017a9142b9190b5daffd6310b828717ab788c9350011ead870000000000000000166a146f6d6e69000000000000001f0000000b21b2b480470607000000000017a914cd93b311b5c132c2f904d9ba40a74faaa40150e18702483045022100d471da15c7ca89b75a95dd165dfb63bdba9e0d67a5cf210a9ea67e9062d1103e02206fde9cf09bf6cb4d1e460b9a6b99954c85d2de3bfea0f24e51b08414c3b04fe80121032bb55b8602969f3fbb061941c614e5e22940cff650bd323a0c2239ff479808d300000000 \
813152 \
'[
    {
        "txid": "185bd2ae0434088d8d53d7a84617161ff805aa29e590245ab0942aed6792159e",
        "vout": 1,
        "prevout": {
            "scriptPubKey": "76a914bb0fce03cd20d2e84c511675688ff83da0a3e0d788ac",
            "value": 100000000
        }
    }
]'
```

### automatic with cargo
- 