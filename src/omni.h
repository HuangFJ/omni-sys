#pragma once

#include "univalue.h"
#include <string>

struct Vin {
    std::string txid;
    unsigned int vout;
    struct PrevOut {
        uint64_t value;
        struct ScriptPubKey {
            std::string hex;
        } scriptPubKey;
    } prevout;
};

struct RawTx {
    std::string txid;
    std::string hex;
    std::vector<Vin> vin;
    unsigned int height;

    static RawTx loads(std::string rawStr)
    {
        RawTx rawTx;

        UniValue value(UniValue::VOBJ);
        value.read(rawStr);

        rawTx.txid = value["txid"].get_str();
        rawTx.hex = value["hex"].get_str();
        rawTx.height = value["height"].getInt<unsigned int>();

        for (auto v : value["vin"].getValues()) {
            Vin vin;
            vin.txid = v["txid"].get_str();
            vin.vout = v["vout"].getInt<unsigned int>();
            vin.prevout.value = v["prevout"]["value"].getInt<uint64_t>();
            vin.prevout.scriptPubKey.hex = v["prevout"]["scriptPubKey"]["hex"].get_str();

            rawTx.vin.push_back(vin);
        }
        return rawTx;
    }

    std::string dumps()
    {
        UniValue value(UniValue::VOBJ);
        value.pushKV("txid", txid);
        value.pushKV("hex", hex);
        value.pushKV("height", height);

        UniValue vinValue(UniValue::VARR);
        for (auto v : vin) {
            UniValue vinItem(UniValue::VOBJ);
            vinItem.pushKV("txid", v.txid);
            vinItem.pushKV("vout", v.vout);

            UniValue prevoutValue(UniValue::VOBJ);
            prevoutValue.pushKV("value", v.prevout.value);

            UniValue scriptPubKey(UniValue::VOBJ);
            scriptPubKey.pushKV("hex", v.prevout.scriptPubKey.hex);

            prevoutValue.pushKV("scriptPubKey", scriptPubKey);
            vinItem.pushKV("prevout", prevoutValue);

            vinValue.push_back(vinItem);
        }
        value.pushKV("vin", vinValue);

        return value.write();
    }
};


struct OmniTx {
    std::string txid;
    std::string fee;
    std::string sendingaddress;
    std::string referenceaddress;
    unsigned short version;
    unsigned int type_int;
    std::string type;
    uint64_t amount;
    unsigned int propertyid;


    std::string dumps()
    {
        UniValue value(UniValue::VOBJ);
        value.pushKV("txid", txid);
        value.pushKV("fee", fee);
        value.pushKV("sendingaddress", sendingaddress);
        value.pushKV("referenceaddress", referenceaddress);
        value.pushKV("version", version);
        value.pushKV("type_int", type_int);
        value.pushKV("type", type);
        value.pushKV("amount", amount);
        value.pushKV("propertyid", propertyid);

        return value.write();
    }
};

void Init(std::string host = "127.0.0.1", int port = 8332, std::string username = "", std::string password = "");
// unique_ptr is smart pointer, it will own the object that it points to.
// T& is reference, a reference is a type that refers to another object. 
// It's essentially an alias for an existing object. 
// Unlike a pointer, a reference cannot be null and must be initialized when declared. 
// And has no address
std::unique_ptr<OmniTx> ParseTx(const RawTx& rawTx);