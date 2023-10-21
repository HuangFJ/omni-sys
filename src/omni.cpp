#include "omni.h"
#include <consensus/amount.h>
#include <univalue.h>
#include <assert.h>
#include <chainparams.h>
#include <coins.h>
#include <core_io.h>
#include <key_io.h>
#include <omnicore/dex.h>
#include <omnicore/mdex.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/rpctxobject.h>
#include <omnicore/rules.h>
#include <omnicore/script.h>
#include <omnicore/tally.h>
#include <omnicore/tx.h>
#include <omnicore/utilsui.h>
#include <optional>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <string>
#include <sync.h>
#include <tinyformat.h>
#include <uint256.h>
#include <unordered_map>
#include <util/strencodings.h>
#include <util/time.h>
#include <validation.h>
#include <vector>


// Define G_TRANSLATION_FUN symbol in libbitcoinkernel library so users of the
// library aren't required to export this symbol
extern const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;

//! Exodus address (changes based on network)
static std::string exodus_address = "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P";

static unsigned int nCacheHits = 0;
static unsigned int nCacheMiss = 0;

using namespace mastercore;

#ifdef FETCH_REMOTE_TX

#include "../jsonrpccxx/client.hpp"
#include <core_read.cpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

class ClientConnector : public jsonrpccxx::IClientConnector
{
public:
    httplib::Client cli;

    ClientConnector(std::string host, int port, std::string username, std::string password) : cli(host, port)
    {
        cli.set_basic_auth(username.c_str(), password.c_str());
    }

    std::string Send(const std::string& request) override
    {
        if (auto res = cli.Post("/", request, "application/json")) {
            if (res->status == 200) {
                return res->body;
            }
            tfm::printfln("json rpc error: %s", res->status);
        } else {
            auto err = res.error();
            tfm::printfln("json rpc error: %s", httplib::to_string(err));
        }
        return "{}";
    }
};

static std::unique_ptr<ClientConnector> connector;

bool fetchTransaction(const uint256& hash, CMutableTransaction& txOut, std::string& blockHash)
{
    LOCK(cs_main);
    jsonrpccxx::JsonRpcClient rpcClient(*connector, jsonrpccxx::version::v2);

    nlohmann::json ret;
    try {
        ret = rpcClient.CallMethod<nlohmann::json>("1", "getrawtransaction", {hash.GetHex(), 1});
        blockHash = ret["blockhash"].get<std::string>();
    } catch (const jsonrpccxx::JsonRpcException& e) {
        tfm::printfln("JsonRpcException: %s", e.what());
        return false;
    }

    return DecodeHexTx(txOut, ret["hex"].get<std::string>());
}

static bool fillTxInputCache(const CTransaction& tx, std::string vins_json)
{
    static const unsigned int nCacheSize = 500000;

    if (view.GetCacheSize() > nCacheSize) {
        tfm::printfln("%s(): clearing cache before insertion [size=%d, hit=%d, miss=%d]\n",
                      __func__, view.GetCacheSize(), nCacheHits, nCacheMiss);
        view.Flush();
    }

    for (std::vector<CTxIn>::const_iterator it = tx.vin.begin(); it != tx.vin.end(); ++it) {
        const CTxIn& txIn = *it;
        unsigned int nOut = txIn.prevout.n;
        const Coin& coin = view.AccessCoin(txIn.prevout);

        if (!coin.IsSpent()) {
            ++nCacheHits;
            continue;
        } else {
            ++nCacheMiss;
        }

        CMutableTransaction txPrev;
        std::string blockHash;
        Coin newcoin;
        std::map<COutPoint, Coin>::const_iterator rm_it;
        if (fetchTransaction(txIn.prevout.hash, txPrev, blockHash)) {
            newcoin.out.scriptPubKey = txPrev.vout[nOut].scriptPubKey;
            newcoin.out.nValue = txPrev.vout[nOut].nValue;
        } else {
            return false;
        }

        view.AddCoin(txIn.prevout, std::move(newcoin), true);
    }

    return true;
}

#else

static bool fillTxInputCache(const CTransaction& tx, std::string vins_json)
{
    //
    // vins_json : [
    //     {
    //         "txid": "185bd2ae0434088d8d53d7a84617161ff805aa29e590245ab0942aed6792159e",
    //         "vout": 1,
    //         "prevout": {
    //             "scriptPubKey": "76a914bb0fce03cd20d2e84c511675688ff83da0a3e0d788ac",
    //             "value": 100000000
    //         }
    //     }
    // ]
    //
    static const unsigned int nCacheSize = 500000;

    if (view.GetCacheSize() > nCacheSize) {
        tfm::printfln("%s(): clearing cache before insertion [size=%d, hit=%d, miss=%d]\n",
                      __func__, view.GetCacheSize(), nCacheHits, nCacheMiss);
        view.Flush();
    }

    UniValue vins(UniValue::VARR);
    vins.read(vins_json);
    auto vins_arr = vins.getValues();
    for (auto it = vins_arr.begin(); it != vins_arr.end(); ++it) {
        const UniValue& vin_obj = *it;
        Coin newcoin;
        const COutPoint vin(uint256S(vin_obj["txid"].get_str()), vin_obj["vout"].getInt<uint32_t>());
        const UniValue prevout_obj = vin_obj["prevout"];
        newcoin.out.scriptPubKey = ParseScript(prevout_obj["scriptPubKey"].get_str());
        const CAmount nValue(prevout_obj["value"].getInt<int64_t>());
        newcoin.out.nValue = nValue;

        view.AddCoin(vin, std::move(newcoin), true);
    }

    return true;
}

#endif

int parseTx(const CTransaction& wtx, int nBlock, CMPTransaction& mp_tx, std::string vins_json)
{
    unsigned int idx(0);
    unsigned int nTime(0);

    mp_tx.Set(wtx.GetHash(), nBlock, idx, nTime);

    // ### CLASS IDENTIFICATION AND MARKER CHECK ###
    int omniClass = mastercore::GetEncodingClass(wtx, nBlock);

    if (omniClass == NO_MARKER) {
        return -1; // No Exodus/Omni marker, thus not a valid Omni transaction
    }

    tfm::printfln("____________________________________________________________________________________________________________________________________\n");
    tfm::printfln("%s block: %d, txid: %s\n", __FUNCTION__, nBlock, wtx.GetHash().GetHex());

    // ### SENDER IDENTIFICATION ###
    std::string strSender;
    int64_t inAll = 0;

    { // needed to ensure the cache isn't cleared in the meantime when doing parallel queries
        // To avoid potential dead lock warning
        // cs_main for FillTxInputCache() > GetTransaction()
        // mempool.cs for FillTxInputCache() > GetTransaction() > mempool.get()
        // auto mempool = ::ChainstateActive().GetMempool();
        // assert(mempool);
        LOCK2(cs_main, cs_tx_cache);

        // Add previous transaction inputs to the cache
        if (!fillTxInputCache(wtx, vins_json)) {
            tfm::printfln("%s() ERROR: failed to get inputs for %s\n", __func__, wtx.GetHash().GetHex());
            return -101;
        }

        assert(view.HaveInputs(wtx));

        if (omniClass != OMNI_CLASS_C) {
            // OLD LOGIC - collect input amounts and identify sender via "largest input by sum"
            std::map<std::string, int64_t> inputs_sum_of_values;

            for (unsigned int i = 0; i < wtx.vin.size(); ++i) {
                tfm::printfln("vin=%d:%s\n", i, ScriptToAsmStr(wtx.vin[i].scriptSig));

                const CTxIn& txIn = wtx.vin[i];
                const Coin& coin = view.AccessCoin(txIn.prevout);
                const CTxOut& txOut = coin.out;

                assert(!txOut.IsNull());

                CTxDestination source;
                TxoutType whichType;
                if (!GetOutputType(txOut.scriptPubKey, whichType)) {
                    return -104;
                }
                if (!IsAllowedInputType(whichType, nBlock)) {
                    return -105;
                }
                if (ExtractDestination(txOut.scriptPubKey, source)) { // extract the destination of the previous transaction's vout[n] and check it's allowed type
                    inputs_sum_of_values[EncodeDestination(source)] += txOut.nValue;
                } else
                    return -106;
            }

            int64_t nMax = 0;
            for (std::map<std::string, int64_t>::iterator it = inputs_sum_of_values.begin(); it != inputs_sum_of_values.end(); ++it) { // find largest by sum
                int64_t nTemp = it->second;
                if (nTemp > nMax) {
                    strSender = it->first;
                    tfm::printfln("looking for The Sender: %s , nMax=%lu, nTemp=%d\n", strSender, nMax, nTemp);
                    nMax = nTemp;
                }
            }
        } else {
            // NEW LOGIC - the sender is chosen based on the first vin

            // determine the sender, but invalidate transaction, if the input is not accepted
            {
                unsigned int vin_n = 0; // the first input
                tfm::printfln("vin=%d:%s\n", vin_n, ScriptToAsmStr(wtx.vin[vin_n].scriptSig));

                const CTxIn& txIn = wtx.vin[vin_n];
                const Coin& coin = view.AccessCoin(txIn.prevout);
                const CTxOut& txOut = coin.out;

                assert(!txOut.IsNull());

                TxoutType whichType;
                if (!GetOutputType(txOut.scriptPubKey, whichType)) {
                    return -108;
                }
                if (!IsAllowedInputType(whichType, nBlock)) {
                    return -109;
                }
                CTxDestination source;
                if (ExtractDestination(txOut.scriptPubKey, source)) {
                    strSender = EncodeDestination(source);
                } else
                    return -110;
            }
        }

        inAll = view.GetValueIn(wtx);

    } // end of LOCK(cs_tx_cache)

    int64_t outAll = wtx.GetValueOut();
    int64_t txFee = inAll - outAll; // miner fee

    if (!strSender.empty()) {
        tfm::printfln("The Sender: %s : fee= %s\n", strSender, FormatDivisibleMP(txFee));
    } else {
        tfm::printfln("The sender is still EMPTY !!! txid: %s\n", wtx.GetHash().GetHex());
        return -5;
    }

    // ### DATA POPULATION ### - save output addresses, values and scripts
    std::string strReference;
    unsigned char single_pkt[MAX_PACKETS * PACKET_SIZE];
    unsigned int packet_size = 0;
    std::vector<std::string> script_data;
    std::vector<std::string> address_data;
    std::vector<int64_t> value_data;

    for (size_t n = 0; n < wtx.vout.size(); ++n) {
        TxoutType whichType;
        if (!GetOutputType(wtx.vout[n].scriptPubKey, whichType)) {
            continue;
        }
        if (!IsAllowedOutputType(whichType, nBlock)) {
            continue;
        }
        CTxDestination dest;
        if (ExtractDestination(wtx.vout[n].scriptPubKey, dest)) {
            if (!(dest == ExodusAddress())) {
                // saving for Class A processing or reference
                GetScriptPushes(wtx.vout[n].scriptPubKey, script_data);
                std::string address = EncodeDestination(dest);
                address_data.push_back(address);
                mp_tx.addValidStmAddress(n, address);
                value_data.push_back(wtx.vout[n].nValue);
                tfm::printfln("saving address_data #%d: %s:%s\n", n, EncodeDestination(dest), ScriptToAsmStr(wtx.vout[n].scriptPubKey));
            }
        }
    }
    tfm::printfln(" address_data.size=%lu\n script_data.size=%lu\n value_data.size=%lu\n", address_data.size(), script_data.size(), value_data.size());

    // ### CLASS A PARSING ###
    if (omniClass == OMNI_CLASS_A) {
        std::string strScriptData;
        std::string strDataAddress;
        std::string strRefAddress;
        unsigned char dataAddressSeq = 0xFF;
        unsigned char seq = 0xFF;
        int64_t dataAddressValue = 0;
        for (unsigned k = 0; k < script_data.size(); ++k) {                                // Step 1, locate the data packet
            std::string strSub = script_data[k].substr(2, 16);                             // retrieve bytes 1-9 of packet for peek & decode comparison
            seq = (ParseHex(script_data[k].substr(0, 2)))[0];                              // retrieve sequence number
            if ("0000000000000001" == strSub || "0000000000000002" == strSub) {            // peek & decode comparison
                if (strScriptData.empty()) {                                               // confirm we have not already located a data address
                    strScriptData = script_data[k].substr(2 * 1, 2 * PACKET_SIZE_CLASS_A); // populate data packet
                    strDataAddress = address_data[k];                                      // record data address
                    dataAddressSeq = seq;                                                  // record data address seq num for reference matching
                    dataAddressValue = value_data[k];                                      // record data address amount for reference matching
                    tfm::printfln("Data Address located - data[%d]:%s: %s (%s)\n", k, script_data[k], address_data[k], FormatDivisibleMP(value_data[k]));
                } else {                    // invalidate - Class A cannot be more than one data packet - possible collision, treat as default (BTC payment)
                    strDataAddress.clear(); // empty strScriptData to block further parsing
                    tfm::printfln("Multiple Data Addresses found (collision?) Class A invalidated, defaulting to BTC payment\n");
                    break;
                }
            }
        }
        if (!strDataAddress.empty()) { // Step 2, try to locate address with seqnum = DataAddressSeq+1 (also verify Step 1, we should now have a valid data packet)
            unsigned char expectedRefAddressSeq = dataAddressSeq + 1;
            for (unsigned k = 0; k < script_data.size(); ++k) {                                                                     // loop through outputs
                seq = (ParseHex(script_data[k].substr(0, 2)))[0];                                                                   // retrieve sequence number
                if ((address_data[k] != strDataAddress) && (address_data[k] != exodus_address) && (expectedRefAddressSeq == seq)) { // found reference address with matching sequence number
                    if (strRefAddress.empty()) {                                                                                    // confirm we have not already located a reference address
                        strRefAddress = address_data[k];                                                                            // set ref address
                        tfm::printfln("Reference Address located via seqnum - data[%d]:%s: %s (%s)\n", k, script_data[k], address_data[k], FormatDivisibleMP(value_data[k]));
                    } else {                   // can't trust sequence numbers to provide reference address, there is a collision with >1 address with expected seqnum
                        strRefAddress.clear(); // blank ref address
                        tfm::printfln("Reference Address sequence number collision, will fall back to evaluating matching output amounts\n");
                        break;
                    }
                }
            }
            std::vector<int64_t> ExodusValues;
            for (unsigned int n = 0; n < wtx.vout.size(); ++n) {
                CTxDestination dest;
                if (ExtractDestination(wtx.vout[n].scriptPubKey, dest)) {
                    if (dest == ExodusAddress()) {
                        ExodusValues.push_back(wtx.vout[n].nValue);
                    }
                }
            }
            if (strRefAddress.empty()) {                                                                                                     // Step 3, if we still don't have a reference address, see if we can locate an address with matching output amounts
                for (unsigned k = 0; k < script_data.size(); ++k) {                                                                          // loop through outputs
                    if ((address_data[k] != strDataAddress) && (address_data[k] != exodus_address) && (dataAddressValue == value_data[k])) { // this output matches data output, check if matches exodus output
                        for (unsigned int exodus_idx = 0; exodus_idx < ExodusValues.size(); exodus_idx++) {
                            if (value_data[k] == ExodusValues[exodus_idx]) { // this output matches data address value and exodus address value, choose as ref
                                if (strRefAddress.empty()) {
                                    strRefAddress = address_data[k];
                                    tfm::printfln("Reference Address located via matching amounts - data[%d]:%s: %s (%s)\n", k, script_data[k], address_data[k], FormatDivisibleMP(value_data[k]));
                                } else {
                                    strRefAddress.clear();
                                    tfm::printfln("Reference Address collision, multiple potential candidates. Class A invalidated, defaulting to BTC payment\n");
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        } // end if (!strDataAddress.empty())
        if (!strRefAddress.empty()) {
            strReference = strRefAddress; // populate expected var strReference with chosen address (if not empty)
        }
        if (strRefAddress.empty()) {
            strDataAddress.clear(); // last validation step, if strRefAddress is empty, blank strDataAddress so we default to BTC payment
        }
        if (!strDataAddress.empty()) { // valid Class A packet almost ready
            tfm::printfln("valid Class A:from=%s:to=%s:data=%s\n", strSender, strReference, strScriptData);
            packet_size = PACKET_SIZE_CLASS_A;
            memcpy(single_pkt, &ParseHex(strScriptData)[0], packet_size);
        } else {
            tfm::printfln("!! sender: %s , receiver: %s\n", strSender, strReference);
            tfm::printfln("!! this may be the BTC payment for an offer !!\n");
        }
    }
    // ### CLASS B / CLASS C PARSING ###
    if ((omniClass == OMNI_CLASS_B) || (omniClass == OMNI_CLASS_C)) {
        tfm::printfln("Beginning reference identification\n");
        bool referenceFound = false;                         // bool to hold whether we've found the reference yet
        bool changeRemoved = false;                          // bool to hold whether we've ignored the first output to sender as change
        unsigned int potentialReferenceOutputs = 0;          // int to hold number of potential reference outputs
        for (unsigned k = 0; k < address_data.size(); ++k) { // how many potential reference outputs do we have, if just one select it right here
            const std::string& addr = address_data[k];
            tfm::printfln("ref? data[%d]:%s: %s (%s)\n", k, script_data[k], addr, FormatIndivisibleMP(value_data[k]));
            if (addr != exodus_address) {
                ++potentialReferenceOutputs;
                if (1 == potentialReferenceOutputs) {
                    strReference = addr;
                    referenceFound = true;
                    tfm::printfln("Single reference potentially id'd as follows: %s \n", strReference);
                } else {                  // as soon as potentialReferenceOutputs > 1 we need to go fishing
                    strReference.clear(); // avoid leaving strReference populated for sanity
                    referenceFound = false;
                    tfm::printfln("More than one potential reference candidate, blanking strReference, need to go fishing\n");
                }
            }
        }
        if (!referenceFound) { // do we have a reference now? or do we need to dig deeper
            tfm::printfln("Reference has not been found yet, going fishing\n");
            for (unsigned k = 0; k < address_data.size(); ++k) {
                const std::string& addr = address_data[k];
                if (addr != exodus_address) { // removed strSender restriction, not to spec
                    if (addr == strSender && !changeRemoved) {
                        changeRemoved = true; // per spec ignore first output to sender as change if multiple possible ref addresses
                        tfm::printfln("Removed change\n");
                    } else {
                        strReference = addr; // this may be set several times, but last time will be highest vout
                        tfm::printfln("Resetting strReference as follows: %s \n ", strReference);
                    }
                }
            }
        }
        tfm::printfln("Ending reference identification\nFinal decision on reference identification is: %s\n", strReference);

        // ### CLASS B SPECIFIC PARSING ###
        if (omniClass == OMNI_CLASS_B) {
            std::vector<std::string> multisig_script_data;

            // ### POPULATE MULTISIG SCRIPT DATA ###
            for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
                TxoutType whichType;
                std::vector<CTxDestination> vDest;
                int nRequired;
                tfm::printfln("scriptPubKey: %s\n", HexStr(wtx.vout[i].scriptPubKey));
                if (!ExtractDestinations(wtx.vout[i].scriptPubKey, whichType, vDest, nRequired)) {
                    continue;
                }
                if (whichType == TxoutType::MULTISIG) {
                    {
                        tfm::printfln(" >> multisig: ");
                        for (const CTxDestination& dest : vDest) {
                            tfm::printfln("%s ; ", EncodeDestination(dest));
                        }
                        tfm::printfln("\n");
                    }
                    // ignore first public key, as it should belong to the sender
                    // and it be used to avoid the creation of unspendable dust
                    GetScriptPushes(wtx.vout[i].scriptPubKey, multisig_script_data, true);
                }
            }

            // The number of packets is limited to MAX_PACKETS,
            // which allows, at least in theory, to add 1 byte
            // sequence numbers to each packet.

            // Transactions with more than MAX_PACKET packets
            // are not invalidated, but trimmed.

            unsigned int nPackets = multisig_script_data.size();
            if (nPackets > MAX_PACKETS) {
                nPackets = MAX_PACKETS;
                tfm::printfln("limiting number of packets to %d [extracted=%d]\n", nPackets, multisig_script_data.size());
            }

            // ### PREPARE A FEW VARS ###
            std::string strObfuscatedHashes[1 + MAX_SHA256_OBFUSCATION_TIMES];
            PrepareObfuscatedHashes(strSender, 1 + nPackets, strObfuscatedHashes);
            unsigned char packets[MAX_PACKETS][32];
            unsigned int mdata_count = 0; // multisig data count

            // ### DEOBFUSCATE MULTISIG PACKETS ###
            for (unsigned int k = 0; k < nPackets; ++k) {
                assert(mdata_count < MAX_PACKETS);
                assert(mdata_count < MAX_SHA256_OBFUSCATION_TIMES);

                std::vector<unsigned char> hash = ParseHex(strObfuscatedHashes[mdata_count + 1]);
                std::vector<unsigned char> packet = ParseHex(multisig_script_data[k].substr(2 * 1, 2 * PACKET_SIZE));
                for (unsigned int i = 0; i < packet.size(); i++) { // this is a data packet, must deobfuscate now
                    packet[i] ^= hash[i];
                }
                memcpy(&packets[mdata_count], &packet[0], PACKET_SIZE);
                ++mdata_count;

                {
                    CPubKey key(ParseHex(multisig_script_data[k]));
                    std::string strAddress = EncodeDestination(PKHash(key));
                    tfm::printfln("multisig_data[%d]:%s: %s\n", k, multisig_script_data[k], strAddress);
                }
                {
                    if (!packet.empty()) {
                        std::string strPacket = HexStr(packet.begin(), packet.end());
                        tfm::printfln("packet #%d: %s\n", mdata_count, strPacket);
                    }
                }
            }
            packet_size = mdata_count * (PACKET_SIZE - 1);
            assert(packet_size <= sizeof(single_pkt));

            // ### FINALIZE CLASS B ###
            for (unsigned int m = 0; m < mdata_count; ++m) { // now decode mastercoin packets
                tfm::printfln("m=%d: %s\n", m, HexStr(packets[m], PACKET_SIZE + packets[m]));

                // check to ensure the sequence numbers are sequential and begin with 01 !
                if (1 + m != packets[m][0]) {
                    tfm::printfln("Error: non-sequential seqnum ! expected=%d, got=%d\n", 1 + m, packets[m][0]);
                }

                memcpy(m * (PACKET_SIZE - 1) + single_pkt, 1 + packets[m], PACKET_SIZE - 1); // now ignoring sequence numbers for Class B packets
            }
        }

        // ### CLASS C SPECIFIC PARSING ###
        if (omniClass == OMNI_CLASS_C) {
            std::vector<std::string> op_return_script_data;

            // ### POPULATE OP RETURN SCRIPT DATA ###
            for (unsigned int n = 0; n < wtx.vout.size(); ++n) {
                TxoutType whichType;
                if (!GetOutputType(wtx.vout[n].scriptPubKey, whichType)) {
                    continue;
                }
                if (!IsAllowedOutputType(whichType, nBlock)) {
                    continue;
                }
                if (whichType == TxoutType::NULL_DATA) {
                    // only consider outputs, which are explicitly tagged
                    std::vector<std::string> vstrPushes;
                    if (!GetScriptPushes(wtx.vout[n].scriptPubKey, vstrPushes)) {
                        continue;
                    }
                    // TODO: maybe encapsulate the following sort of messy code
                    if (!vstrPushes.empty()) {
                        std::vector<unsigned char> vchMarker = GetOmMarker();
                        std::vector<unsigned char> vchPushed = ParseHex(vstrPushes[0]);
                        if (vchPushed.size() < vchMarker.size()) {
                            continue;
                        }
                        if (std::equal(vchMarker.begin(), vchMarker.end(), vchPushed.begin())) {
                            size_t sizeHex = vchMarker.size() * 2;
                            // strip out the marker at the very beginning
                            vstrPushes[0] = vstrPushes[0].substr(sizeHex);
                            // add the data to the rest
                            op_return_script_data.insert(op_return_script_data.end(), vstrPushes.begin(), vstrPushes.end());

                            {
                                tfm::printfln("Class C transaction detected: %s parsed to %s at vout %d\n", wtx.GetHash().GetHex(), vstrPushes[0], n);
                            }
                        }
                    }
                }
            }
            // ### EXTRACT PAYLOAD FOR CLASS C ###
            for (unsigned int n = 0; n < op_return_script_data.size(); ++n) {
                if (!op_return_script_data[n].empty()) {
                    assert(IsHex(op_return_script_data[n])); // via GetScriptPushes()
                    std::vector<unsigned char> vch = ParseHex(op_return_script_data[n]);
                    unsigned int payload_size = vch.size();
                    if (packet_size + payload_size > MAX_PACKETS * PACKET_SIZE) {
                        payload_size = MAX_PACKETS * PACKET_SIZE - packet_size;
                        tfm::printfln("limiting payload size to %d byte\n", packet_size + payload_size);
                    }
                    if (payload_size > 0) {
                        memcpy(single_pkt + packet_size, &vch[0], payload_size);
                        packet_size += payload_size;
                    }
                    if (MAX_PACKETS * PACKET_SIZE == packet_size) {
                        break;
                    }
                }
            }
        }
    }

    // ### SET MP TX INFO ###
    tfm::printfln("single_pkt: %s\n", HexStr(single_pkt, packet_size + single_pkt));
    mp_tx.Set(strSender, strReference, 0, wtx.GetHash(), nBlock, idx, (unsigned char*)&single_pkt, packet_size, omniClass, (inAll - outAll));

    // TODO: the following is a bit awful
    // Provide a hint for DEx payments
    if (omniClass == OMNI_CLASS_A && packet_size == 0) {
        return 1;
    }

    return 0;
}

void Init(std::string host, int port, std::string username, std::string password)
{
#ifdef FETCH_REMOTE_TX
    connector = std::unique_ptr<ClientConnector>(new ClientConnector(host, port, username, password));
#endif
    SelectParams("main");
}

std::string ParseTx(std::string hexTx, int blockHeight, std::string vinsJson)
{
    CMutableTransaction tx;
    if (!DecodeHexTx(tx, hexTx)) {
        tfm::printfln("decode hexTx failed: %s", hexTx);
        return "";
    }

    CMPTransaction mp_obj;
    int parseRC = parseTx(CTransaction(tx), blockHeight, mp_obj, vinsJson);
    if (parseRC < 0) {
        tfm::printfln("parse Tx failed with code: %d", parseRC);
        return "";
    }

    if (!mp_obj.interpret_Transaction()) {
        tfm::printfln("interpret omniTx failed");
        return "";
    }

    UniValue txobj(UniValue::VOBJ);
    txobj.pushKV("txid", mp_obj.getHash().GetHex());
    txobj.pushKV("fee", FormatDivisibleMP(mp_obj.getFeePaid()));
    txobj.pushKV("sendingaddress", TryEncodeOmniAddress(mp_obj.getSender()));
    if (showRefForTx(mp_obj.getType()))
        txobj.pushKV("referenceaddress", TryEncodeOmniAddress(mp_obj.getReceiver()));
    txobj.pushKV("version", (uint64_t)mp_obj.getVersion());
    txobj.pushKV("type_int", (uint64_t)mp_obj.getType());
    txobj.pushKV("type", mp_obj.getTypeString());
    txobj.pushKV("amount", mp_obj.getNewAmount());
    txobj.pushKV("propertyid", mp_obj.getProperty());

    return txobj.write();
}

int main(int argc, char const* argv[])
{
#ifdef FETCH_REMOTE_TX
    Init(argv[3], std::stoi(argv[4]), argv[5], argv[6]);
    std::string rawTx = argv[1];
    int blockHeight = std::stoi(argv[2]);

    auto ret = ParseTx(rawTx, blockHeight);
    tfm::printfln("%s", ret);
#endif
    Init();
    std::string rawTx = argv[1];
    int blockHeight = std::stoi(argv[2]);
    std::string vinsJson = argv[3];
    auto ret = ParseTx(rawTx, blockHeight, vinsJson);
    tfm::printfln("%s", ret);

    return 0;
}