use omni_sys::OmniLayer;
use omni_sys::Vin;
use omni_sys::PrevOut;

#[test]
fn test_omni() {
    let ol = OmniLayer::dfault();
    let hex = "020000000001018532e79d7625c2e3b4d5cf886f98fbf0ab833528fb3015184b213aaa7a20c1e17900000017160014c6b5514a86e79fe5a3e1537ebbf9df4e32b57975fdffffff03220200000000000017a9142b9190b5daffd6310b828717ab788c9350011ead870000000000000000166a146f6d6e69000000000000001f0000000b21b2b480470607000000000017a914cd93b311b5c132c2f904d9ba40a74faaa40150e18702483045022100d471da15c7ca89b75a95dd165dfb63bdba9e0d67a5cf210a9ea67e9062d1103e02206fde9cf09bf6cb4d1e460b9a6b99954c85d2de3bfea0f24e51b08414c3b04fe80121032bb55b8602969f3fbb061941c614e5e22940cff650bd323a0c2239ff479808d300000000";
    let height = 813152;
    let vin = Vin {
        txid: "79e1c1207aaa3a214b180130fb2833abf0fb986f85cfd5b4e3c225769de73285".to_string(),
        vout: 0,
        prevout: PrevOut {
            script_pub_key: "a9142b9190b5daffd6310b828717ab788c9350011ead87".to_string(),
            value: "0.00000547".to_string(),
        },
    };
    let ret = ol.parse_tx(hex, height, vec![vin]).unwrap();
    println!("{:?}", ret);
}