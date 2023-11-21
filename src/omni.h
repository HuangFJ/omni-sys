#pragma once

#include "univalue.h"
#include <memory>
#include <string>

struct Vin {
    std::string txid;
    unsigned int vout;
    struct PrevOut {
        unsigned int height;
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
    //block property
    unsigned int height;
    unsigned int time;
    unsigned int idx;

    RawTx(std::string rawStr)
    {
        UniValue value(UniValue::VOBJ);
        value.read(rawStr);

        this->txid = value["txid"].get_str();
        this->hex = value["hex"].get_str();
        this->height = value["height"].getInt<unsigned int>();
        this->time = value["time"].getInt<unsigned int>();
        this->idx = value["idx"].getInt<unsigned int>();

        for (auto v : value["vin"].getValues()) {
            Vin vin;
            vin.txid = v["txid"].get_str();
            vin.vout = v["vout"].getInt<unsigned int>();
            vin.prevout.value = v["prevout"]["value"].getInt<uint64_t>();
            vin.prevout.height = v["prevout"]["height"].getInt<unsigned int>();
            vin.prevout.scriptPubKey.hex = v["prevout"]["scriptPubKey"]["hex"].get_str();

            this->vin.push_back(vin);
        }
    }

    std::string dumps()
    {
        UniValue value(UniValue::VOBJ);
        value.pushKV("txid", txid);
        value.pushKV("hex", hex);
        value.pushKV("height", height);
        value.pushKV("time", time);
        value.pushKV("idx", idx);

        UniValue vinValue(UniValue::VARR);
        for (auto v : vin) {
            UniValue vinItem(UniValue::VOBJ);
            vinItem.pushKV("txid", v.txid);
            vinItem.pushKV("vout", v.vout);

            UniValue prevoutValue(UniValue::VOBJ);
            prevoutValue.pushKV("value", v.prevout.value);
            prevoutValue.pushKV("height", v.prevout.height);

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

    std::string get_txid()
    {
        return txid;
    }
    std::string get_fee()
    {
        return fee;
    }
    std::string get_sendingaddress()
    {
        return sendingaddress;
    }
    std::string get_referenceaddress()
    {
        return referenceaddress;
    }
    std::string get_type()
    {
        return type;
    }
    uint64_t get_amount()
    {
        return amount;
    }
    unsigned int get_propertyid()
    {
        return propertyid;
    }
    unsigned short get_version()
    {
        return version;
    }
    unsigned int get_type_int()
    {
        return type_int;
    }

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
std::unique_ptr<OmniTx> ParseTx(const RawTx& rawTx);