## Prerequisites
#### DevContainer (VSCode)
Open project in vscode and launch devcontainer.

```bash
# build system
apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 clang bear ccache git
# dependency libraries
apt install libevent-dev libboost-dev nlohmann-json3-dev libcpp-httplib-dev
```

#### MacOS
```bash
brew install automake libtool boost pkg-config libevent nlohmann-json cpp-httplib bear ccache
```

## Build
### Config (Optional)
In `build.rs`, uncomment `-DFETCH_REMOTE_TX` flag to automatically fetch related transactions from remote bitcoin rpc instead of passing from local function for cache when parse a omni tx. if `-DFETCH_REMOTE_TX` is set then pass tx hex, its block height, bitcoin rpc host, port, username and password. otherwise just pass tx hex, its block height and the vins info.

### Build with cargo
```bash
cargo build
cargo test -- --nocapture
```

## Usage
```
[dependencies]
omni-sys = "0.1"
```

```rust
omni_sys::init();

let raw_str = "{\"txid\":\"79e1c1207aaa3a214b181530fb283583abf0fb986f88cfd5b4e3c225769de732\",\"height\":813152,\"hex\":\"020000000001018532e79d7625c2e3b4d5cf886f98fbf0ab833528fb3015184b213aaa7a20c1e17900000017160014c6b5514a86e79fe5a3e1537ebbf9df4e32b57975fdffffff03220200000000000017a9142b9190b5daffd6310b828717ab788c9350011ead870000000000000000166a146f6d6e69000000000000001f0000000b21b2b480470607000000000017a914cd93b311b5c132c2f904d9ba40a74faaa40150e18702483045022100d471da15c7ca89b75a95dd165dfb63bdba9e0d67a5cf210a9ea67e9062d1103e02206fde9cf09bf6cb4d1e460b9a6b99954c85d2de3bfea0f24e51b08414c3b04fe80121032bb55b8602969f3fbb061941c614e5e22940cff650bd323a0c2239ff479808d300000000\",\"vin\":[{\"txid\":\"e1c1207aaa3a214b181530fb283583abf0fb986f88cfd5b4e3c225769de73285\",\"vout\":121,\"prevout\":{\"scriptPubKey\":{\"hex\":\"a914cd93b311b5c132c2f904d9ba40a74faaa40150e187\"},\"value\":512580}}]}";

let mut ret = omni_sys::parse_tx(raw_str).unwrap();
println!("{:?}", ret.txid());
```