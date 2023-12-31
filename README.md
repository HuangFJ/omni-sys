## Prerequisites
#### DevContainer (VSCode)
Open project in vscode and launch devcontainer.

```bash
# build system
apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 clang bear ccache git
# dependency libraries
apt install libevent-dev libboost-dev
```

#### MacOS
```bash
brew install automake libtool boost pkg-config libevent bear ccache git
```

## Build
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
omni_sys::init(omni_sys::Chain::Main, false);

let raw_str = "{\"txid\":\"41864b9e4c0d8499b785a47d48ddc0d18b57fd7948513a595ad8d4e7e7399237\",\"height\":817811,\"time\":1700577787,\"idx\":204,\"hex\":\"020000000163d95cfb3d235666cc9f7978217efe6aaade37912be4721ac61ddac713c52e38010000006a473044022042aef05b0fd6ab7d47dd4b9bf03e9311144f17e4159cf90a96ff0d692b698697022025da0f74e0234fe0f5cf56c4af009e6629c180b1a778c8acd513cabc058d94d20121030888863fcb4cdf5b7d33b40e613af35df8f39d576e7972238b0d396cd3fcc3f2feffffff030000000000000000166a146f6d6e6900000000000000030000000000002e9a6f2d0600000000001976a91488d924f51033b74a895863a5fb57fd545529df7d88ac22020000000000001976a914e4ef869ab7e62584be0c004f20155eefdc64789288ac6a7a0c00\",\"vin\":[{\"txid\":\"382ec513c7da1dc61a72e42b9137deaa6afe7e2178799fcc6656233dfb5cd963\",\"vout\":1,\"prevout\":{\"scriptPubKey\":{\"hex\":\"76a91488d924f51033b74a895863a5fb57fd545529df7d88ac\"},\"value\":433748,\"height\":817809}}]}";

let mut ret = omni_sys::parse_tx(raw_str).unwrap();
println!("{}", ret.dumps());
```