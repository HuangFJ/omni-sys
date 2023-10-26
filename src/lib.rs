use anyhow::Result;
use autocxx::prelude::*;
// use serde::{Deserialize, Serialize};

include_cpp! {
    #include "omni.h"
    safety!(unsafe) // see details of unsafety policies described in the 'safety' section of the book

    generate!("Init")
    generate!("ParseTx")
    generate!("OmniTx")
    generate!("RawTx")
}

pub use ffi::{OmniTx, RawTx};

// #[bridge]
// mod ffi {
//     use autocxx::include_cpp;

//     #[derive(Debug, Default)]
//     pub struct OmniTx {
//         pub txid: String,
//         pub fee: String,
//         pub sendingaddress: String,
//         pub referenceaddress: String,
//         pub version: u16,
//         pub type_int: u32,
//         pub r#type: String,
//         pub amount: u64,
//         pub propertyid: u32,
//     }

//     #[derive(Debug, Clone)]
//     pub struct RawTx {
//         pub txid: String,
//         pub height: u32,
//         pub hex: String,
//         pub vin: Vec<Vin>,
//     }

//     #[derive(Debug, Clone)]
//     pub struct Vin {
//         pub txid: String,
//         pub vout: u32,
//         pub prevout: PrevOut,
//     }

//     #[derive(Debug, Clone)]
//     pub struct PrevOut {
//         pub value: u64,
//         pub scriptPubKey: ScriptPubKey,
//     }

//     #[derive(Debug, Clone)]
//     pub struct ScriptPubKey {
//         pub hex: String,
//     }

//     unsafe extern "C++" {
//         include!("omni.h");

//         fn Init(host: &str, port: i32, username: &str, password: &str);
//         fn ParseTx(tx: &RawTx) -> OmniTx;
//     }
// }

pub fn init_with(host: &str, port: i32, username: &str, password: &str) {
    ffi::Init(host, c_int(port), username, password);
}

pub fn init() {
    ffi::Init("127.0.0.1", c_int(8332), "", "");
}

pub struct OmniTransaction(cxx::UniquePtr<ffi::OmniTx>);

pub fn parse_tx(tx: &ffi::RawTx) -> Result<OmniTransaction> {

    let mut result = ffi::ParseTx(tx);
    
    if result.is_null() {
        Err(anyhow::anyhow!("invalid omni tx"))
    } else {
        let json = result.as_mut().unwrap().dumps();

        println!("{:?}",json);
        Ok(OmniTransaction(result))
    }
}
