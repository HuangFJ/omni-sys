use anyhow::Result;
use autocxx::prelude::*;
pub use ffi::{OmniTx, RawTx};

include_cpp! {
    #include "omni.h"
    safety!(unsafe)

    generate!("Init")
    generate!("ParseTx")
    generate!("OmniTx")
    generate!("RawTx")
}

pub struct OmniTransaction(pub cxx::UniquePtr<OmniTx>);
unsafe impl Send for OmniTransaction {}

impl OmniTransaction {
    pub fn txid(&mut self) -> String {
        self.0.pin_mut().get_txid().to_string()
    }
    pub fn fee(&mut self) -> String {
        self.0.pin_mut().get_fee().to_string()
    }
    pub fn amount(&mut self) -> u64 {
        self.0.pin_mut().get_amount()
    }
    pub fn propertyid(&mut self) -> u32 {
        self.0.pin_mut().get_propertyid().into()
    }
    pub fn referenceaddress(&mut self) -> String {
        self.0.pin_mut().get_referenceaddress().to_string()
    }
    pub fn sendingaddress(&mut self) -> String {
        self.0.pin_mut().get_sendingaddress().to_string()
    }
    pub fn version(&mut self) -> u16 {
        self.0.pin_mut().get_version().into()
    }
    pub fn type_int(&mut self) -> u32 {
        self.0.pin_mut().get_type_int().into()
    }
    pub fn r#type(&mut self) -> String {
        self.0.pin_mut().get_type().to_string()
    }

    pub fn dumps(&mut self) -> String {
        self.0.pin_mut().dumps().to_string()
    }
}

pub fn init_with(host: &str, port: i32, username: &str, password: &str) {
    ffi::Init(host, c_int(port), username, password);
}

pub fn init() {
    ffi::Init("127.0.0.1", c_int(8332), "", "");
}

pub fn parse_tx(raw_str: &str) -> Result<OmniTransaction> {
    moveit! {
        let mut raw_tx = RawTx::new(raw_str);
    }

    let result = ffi::ParseTx(&raw_tx);

    if result.is_null() {
        Err(anyhow::anyhow!("invalid omni tx"))
    } else {
        Ok(OmniTransaction(result))
    }
}
