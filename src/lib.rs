use anyhow::Result;
use autocxx::prelude::*;
use serde::{Deserialize, Serialize};

include_cpp! {
    #include "omni.h"
    safety!(unsafe) // see details of unsafety policies described in the 'safety' section of the book

    generate!("Init")
    generate!("ParseTx")
}

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

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct RawTx {
    pub txid: String,
    pub height: i32,
    pub hex: String,
    pub vin: Vec<Vin>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Vin {
    pub txid: String,
    pub vout: i32,
    pub prevout: PrevOut,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PrevOut {
    pub value: i64,
    pub script_pub_key: ScriptPubKey,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ScriptPubKey {
    pub hex: String,
}

pub fn init_with(host: &str, port: i32, username: &str, password: &str) {
    ffi::Init(host, c_int(port), username, password);
}

pub fn init() {
    ffi::Init("127.0.0.1", c_int(8332), "", "");
}

pub fn parse_tx(tx: &RawTx) -> Result<OmniTx> {
    let result = ffi::ParseTx(serde_json::to_string(tx)?);
    if result.is_null() {
        return Err(anyhow::anyhow!("invalid omni tx"));
    }
    if let Some(omni_tx) = serde_json::from_str::<Option<OmniTx>>(result.to_str()?)? {
        Ok(omni_tx)
    } else {
        Err(anyhow::anyhow!("invalid omni tx"))
    }
}
