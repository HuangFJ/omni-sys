use anyhow::Result;
use autocxx::prelude::*;
use serde::{Deserialize, Serialize};

include_cpp! {
    #include "omni.h"
    safety!(unsafe) // see details of unsafety policies described in the 'safety' section of the book

    generate!("Init")
    generate!("ParseTx")
}

pub struct OmniLayer;

#[derive(Serialize, Deserialize, Debug)]
pub struct OmniTx {
    pub txid: String,
    pub fee: String,
    pub sendingaddress: String,
    pub referenceaddress: Option<String>,
    pub version: i32,
    pub type_int: i32,
    pub r#type: String,
    pub amount: i64,
    pub propertyid: i32,
}

#[derive(Serialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PrevOut {
    pub script_pub_key: String,
    pub value: i64,
}

#[derive(Serialize, Debug)]
pub struct Vin {
    pub txid: String,
    pub vout: i32,
    pub prevout: PrevOut,
}

impl OmniLayer {
    pub fn new(host: &str, port: i32, username: &str, password: &str) -> Self {
        ffi::Init(host, c_int(port), username, password);
        Self
    }
    pub fn dfault() -> Self {
        ffi::Init("127.0.0.1", c_int(8332), "", "");
        Self
    }
    pub fn parse_tx(&self, tx_hex: &str, block_height: i32, vins: Vec<Vin>) -> Result<OmniTx> {
        let result = ffi::ParseTx(tx_hex, c_int(block_height), serde_json::to_string(&vins)?);
        if result.is_null() {
            return Err(anyhow::anyhow!("ParseTx failed"));
        }

        Ok(serde_json::from_str(result.to_str()?)?)
    }
}
