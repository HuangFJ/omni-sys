#include "omni.h"
#include <assert.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/amount.h>
#include <core_io.h>
#include <key_io.h>
#include <memory>
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
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <string>
#include <sync.h>
#include <tinyformat.h>
#include <uint256.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/system.cpp>
#include <util/time.h>
#include <validation.h>
#include <vector>

using namespace mastercore;

// Define G_TRANSLATION_FUN symbol in libbitcoinkernel library so users of the
// library aren't required to export this symbol
extern const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;

//! Exodus address (changes based on network)
static std::string exodus_address = "1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P";


void ArgsManager::ForceSetArgs(const std::string& strArg, const std::vector<std::string>& strVector)
{
    LOCK(cs_args);
    UniValue arr = UniValue(UniValue::VARR);
    for (const std::string& str : strVector) {
        arr.push_back(str);
    }
    m_settings.forced_settings[SettingName(strArg)] = arr;
}

static bool fillTxInputCache(CCoinsViewCache& view, const std::vector<Vin>& vin)
{
    for (auto it = vin.begin(); it != vin.end(); ++it) {
        const Vin& vin_one = *it;
        Coin newcoin;
        const COutPoint vin(uint256S(vin_one.txid), vin_one.vout);

        auto pks_data = ParseHexUV(vin_one.prevout.scriptPubKey.hex, "prevout.scriptPubKey.hex");
        CScript scriptPubKey(pks_data.begin(), pks_data.end());
        newcoin.out.scriptPubKey = scriptPubKey;

        const CAmount nValue(vin_one.prevout.value);
        newcoin.out.nValue = nValue;

        newcoin.nHeight = vin_one.prevout.height;

        view.AddCoin(vin, std::move(newcoin), true);
    }

    return true;
}


// idx is position within the block, 0-based
// int msc_tx_push(const CTransaction &wtx, int nBlock, unsigned int idx)
// INPUT: bRPConly -- set to true to avoid moving funds; to be called from various RPC calls like this
// RETURNS: 0 if parsed a MP TX
// RETURNS: < 0 if a non-MP-TX or invalid
// RETURNS: >0 if 1 or more payments have been made
static int parseTx(bool bRPConly, CCoinsViewCache& view, const CTransaction& wtx, int nBlock, unsigned int idx, CMPTransaction& mp_tx, unsigned int nTime, const std::vector<Vin>& vin)
{
    assert(bRPConly == mp_tx.isRpcOnly());

    // ### CLASS IDENTIFICATION AND MARKER CHECK ###
    int omniClass = GetEncodingClass(wtx, nBlock);
    if (omniClass == NO_MARKER) {
        return -1; // No Exodus/Omni marker, thus not a valid Omni transaction
    }

    mp_tx.Set(wtx.GetHash(), nBlock, idx, nTime);

    if (!bRPConly || msc_debug_parser_readonly) {
        PrintToLog("____________________________________________________________________________________________________________________________________\n");
        PrintToLog("%s(block=%d, %s idx= %d); txid: %s\n", __func__, nBlock, FormatISO8601DateTime(nTime), idx, wtx.GetHash().GetHex());
    }

    // ### SENDER IDENTIFICATION ###
    std::string strSender;
    int64_t inAll = 0;

    bool forceOverride = true;
    bool isCoinbase = wtx.IsCoinBase();
    const uint256& txid = wtx.GetHash();
    for (size_t i = 0; i < wtx.vout.size(); ++i)
        view.AddCoin(COutPoint(txid, i), Coin(wtx.vout[i], nBlock, isCoinbase), forceOverride);

    // Add previous transaction inputs to the cache
    if (!fillTxInputCache(view, vin)) {
        PrintToLog("%s() ERROR: failed to get inputs for %s\n", __func__, wtx.GetHash().GetHex());
        return -101;
    }

    assert(view.HaveInputs(wtx));

    if (omniClass != OMNI_CLASS_C) {
        // OLD LOGIC - collect input amounts and identify sender via "largest input by sum"
        std::map<std::string, int64_t> inputs_sum_of_values;

        for (unsigned int i = 0; i < wtx.vin.size(); ++i) {
            if (msc_debug_vin) PrintToLog("vin=%d:%s\n", i, ScriptToAsmStr(wtx.vin[i].scriptSig));

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
                if (msc_debug_exo) PrintToLog("looking for The Sender: %s , nMax=%lu, nTemp=%d\n", strSender, nMax, nTemp);
                nMax = nTemp;
            }
        }
    } else {
        // NEW LOGIC - the sender is chosen based on the first vin

        // determine the sender, but invalidate transaction, if the input is not accepted
        {
            unsigned int vin_n = 0; // the first input
            if (msc_debug_vin) PrintToLog("vin=%d:%s\n", vin_n, ScriptToAsmStr(wtx.vin[vin_n].scriptSig));

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

    int64_t outAll = wtx.GetValueOut();
    int64_t txFee = inAll - outAll; // miner fee

    if (!strSender.empty()) {
        if (msc_debug_verbose) PrintToLog("The Sender: %s : fee= %s\n", strSender, FormatDivisibleMP(txFee));
    } else {
        PrintToLog("The sender is still EMPTY !!! txid: %s\n", wtx.GetHash().GetHex());
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
                if (msc_debug_parser_data) PrintToLog("saving address_data #%d: %s:%s\n", n, EncodeDestination(dest), ScriptToAsmStr(wtx.vout[n].scriptPubKey));
            }
        }
    }
    if (msc_debug_parser_data) PrintToLog(" address_data.size=%lu\n script_data.size=%lu\n value_data.size=%lu\n", address_data.size(), script_data.size(), value_data.size());

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
                    if (msc_debug_parser_data) PrintToLog("Data Address located - data[%d]:%s: %s (%s)\n", k, script_data[k], address_data[k], FormatDivisibleMP(value_data[k]));
                } else {                    // invalidate - Class A cannot be more than one data packet - possible collision, treat as default (BTC payment)
                    strDataAddress.clear(); // empty strScriptData to block further parsing
                    if (msc_debug_parser_data) PrintToLog("Multiple Data Addresses found (collision?) Class A invalidated, defaulting to BTC payment\n");
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
                        if (msc_debug_parser_data) PrintToLog("Reference Address located via seqnum - data[%d]:%s: %s (%s)\n", k, script_data[k], address_data[k], FormatDivisibleMP(value_data[k]));
                    } else {                   // can't trust sequence numbers to provide reference address, there is a collision with >1 address with expected seqnum
                        strRefAddress.clear(); // blank ref address
                        if (msc_debug_parser_data) PrintToLog("Reference Address sequence number collision, will fall back to evaluating matching output amounts\n");
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
                                    if (msc_debug_parser_data) PrintToLog("Reference Address located via matching amounts - data[%d]:%s: %s (%s)\n", k, script_data[k], address_data[k], FormatDivisibleMP(value_data[k]));
                                } else {
                                    strRefAddress.clear();
                                    if (msc_debug_parser_data) PrintToLog("Reference Address collision, multiple potential candidates. Class A invalidated, defaulting to BTC payment\n");
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
            if (msc_debug_parser_data) PrintToLog("valid Class A:from=%s:to=%s:data=%s\n", strSender, strReference, strScriptData);
            packet_size = PACKET_SIZE_CLASS_A;
            memcpy(single_pkt, &ParseHex(strScriptData)[0], packet_size);
        } else {
            if ((!bRPConly || msc_debug_parser_readonly) && msc_debug_parser_dex) {
                PrintToLog("!! sender: %s , receiver: %s\n", strSender, strReference);
                PrintToLog("!! this may be the BTC payment for an offer !!\n");
            }
        }
    }
    // ### CLASS B / CLASS C PARSING ###
    if ((omniClass == OMNI_CLASS_B) || (omniClass == OMNI_CLASS_C)) {
        if (msc_debug_parser_data) PrintToLog("Beginning reference identification\n");
        bool referenceFound = false;                         // bool to hold whether we've found the reference yet
        bool changeRemoved = false;                          // bool to hold whether we've ignored the first output to sender as change
        unsigned int potentialReferenceOutputs = 0;          // int to hold number of potential reference outputs
        for (unsigned k = 0; k < address_data.size(); ++k) { // how many potential reference outputs do we have, if just one select it right here
            const std::string& addr = address_data[k];
            if (msc_debug_parser_data) PrintToLog("ref? data[%d]:%s: %s (%s)\n", k, script_data[k], addr, FormatIndivisibleMP(value_data[k]));
            if (addr != exodus_address) {
                ++potentialReferenceOutputs;
                if (1 == potentialReferenceOutputs) {
                    strReference = addr;
                    referenceFound = true;
                    if (msc_debug_parser_data) PrintToLog("Single reference potentially id'd as follows: %s \n", strReference);
                } else {                  // as soon as potentialReferenceOutputs > 1 we need to go fishing
                    strReference.clear(); // avoid leaving strReference populated for sanity
                    referenceFound = false;
                    if (msc_debug_parser_data) PrintToLog("More than one potential reference candidate, blanking strReference, need to go fishing\n");
                }
            }
        }
        if (!referenceFound) { // do we have a reference now? or do we need to dig deeper
            if (msc_debug_parser_data) PrintToLog("Reference has not been found yet, going fishing\n");
            for (unsigned k = 0; k < address_data.size(); ++k) {
                const std::string& addr = address_data[k];
                if (addr != exodus_address) { // removed strSender restriction, not to spec
                    if (addr == strSender && !changeRemoved) {
                        changeRemoved = true; // per spec ignore first output to sender as change if multiple possible ref addresses
                        if (msc_debug_parser_data) PrintToLog("Removed change\n");
                    } else {
                        strReference = addr; // this may be set several times, but last time will be highest vout
                        if (msc_debug_parser_data) PrintToLog("Resetting strReference as follows: %s \n ", strReference);
                    }
                }
            }
        }
        if (msc_debug_parser_data) PrintToLog("Ending reference identification\nFinal decision on reference identification is: %s\n", strReference);

        // ### CLASS B SPECIFIC PARSING ###
        if (omniClass == OMNI_CLASS_B) {
            std::vector<std::string> multisig_script_data;

            // ### POPULATE MULTISIG SCRIPT DATA ###
            for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
                TxoutType whichType;
                std::vector<CTxDestination> vDest;
                int nRequired;
                if (msc_debug_script) PrintToLog("scriptPubKey: %s\n", HexStr(wtx.vout[i].scriptPubKey));
                if (!ExtractDestinations(wtx.vout[i].scriptPubKey, whichType, vDest, nRequired)) {
                    continue;
                }
                if (whichType == TxoutType::MULTISIG) {
                    if (msc_debug_script) {
                        PrintToLog(" >> multisig: ");
                        for (const CTxDestination& dest : vDest) {
                            PrintToLog("%s ; ", EncodeDestination(dest));
                        }
                        PrintToLog("\n");
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
                PrintToLog("limiting number of packets to %d [extracted=%d]\n", nPackets, multisig_script_data.size());
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

                if (msc_debug_parser_data) {
                    CPubKey key(ParseHex(multisig_script_data[k]));
                    std::string strAddress = EncodeDestination(PKHash(key));
                    PrintToLog("multisig_data[%d]:%s: %s\n", k, multisig_script_data[k], strAddress);
                }
                if (msc_debug_parser) {
                    if (!packet.empty()) {
                        std::string strPacket = HexStr(packet.begin(), packet.end());
                        PrintToLog("packet #%d: %s\n", mdata_count, strPacket);
                    }
                }
            }
            packet_size = mdata_count * (PACKET_SIZE - 1);
            assert(packet_size <= sizeof(single_pkt));

            // ### FINALIZE CLASS B ###
            for (unsigned int m = 0; m < mdata_count; ++m) { // now decode mastercoin packets
                if (msc_debug_parser) PrintToLog("m=%d: %s\n", m, HexStr(packets[m], PACKET_SIZE + packets[m]));

                // check to ensure the sequence numbers are sequential and begin with 01 !
                if (1 + m != packets[m][0]) {
                    if (msc_debug_spec) PrintToLog("Error: non-sequential seqnum ! expected=%d, got=%d\n", 1 + m, packets[m][0]);
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

                            if (msc_debug_parser_data) {
                                PrintToLog("Class C transaction detected: %s parsed to %s at vout %d\n", wtx.GetHash().GetHex(), vstrPushes[0], n);
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
                        PrintToLog("limiting payload size to %d byte\n", packet_size + payload_size);
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
    if (msc_debug_verbose) PrintToLog("single_pkt: %s\n", HexStr(single_pkt, packet_size + single_pkt));
    mp_tx.Set(strSender, strReference, 0, wtx.GetHash(), nBlock, idx, (unsigned char*)&single_pkt, packet_size, omniClass, (inAll - outAll));

    // TODO: the following is a bit awful
    // Provide a hint for DEx payments
    if (omniClass == OMNI_CLASS_A && packet_size == 0) {
        return 1;
    }

    return 0;
}

// chain: main, test, signet, regtest
void Init(std::string chain, bool debug)
{
    if (debug) {
        gArgs.ForceSetArgs("-omnidebug", std::vector<std::string>{"vin", "parser_data", "parser", "script", "exo", "parser_dex", "spec", "verbose", "parser_readonly"});
        gArgs.SoftSetBoolArg("-printtoconsole", true);
        InitDebugLogLevels();
    }
    SelectParams(chain);
}

std::unique_ptr<OmniTx> ParseTx(const RawTx& rawTx)
{
    auto hexTx = rawTx.hex;

    CMutableTransaction tx;
    if (!DecodeHexTx(tx, hexTx)) {
        if (msc_debug_verbose) PrintToLog("decode hexTx failed: %s", hexTx);
        return nullptr;
    }

    CCoinsViewCacheOnly view;
    CMPTransaction mp_obj;
    int parseRC = parseTx(true, view, CTransaction(tx), rawTx.height, rawTx.idx, mp_obj, rawTx.time, rawTx.vin);
    if (parseRC < 0) {
        if (msc_debug_verbose) PrintToLog("parse Tx failed with code: %d", parseRC);
        return nullptr;
    }

    if (!mp_obj.interpret_Transaction()) {
        if (msc_debug_verbose) PrintToLog("interpret omniTx failed");
        return nullptr;
    }

    auto txOmni = new OmniTx();
    txOmni->txid = mp_obj.getHash().GetHex();
    txOmni->fee = FormatDivisibleMP(mp_obj.getFeePaid());
    txOmni->sendingaddress = TryEncodeOmniAddress(mp_obj.getSender());
    txOmni->version = mp_obj.getVersion();
    txOmni->type_int = mp_obj.getType();
    txOmni->type = mp_obj.getTypeString();
    txOmni->amount = mp_obj.getNewAmount();
    txOmni->propertyid = mp_obj.getProperty();
    if (showRefForTx(mp_obj.getType()))
        txOmni->referenceaddress = TryEncodeOmniAddress(mp_obj.getReceiver());

    if (msc_debug_verbose) PrintToLog("parse Tx success: %s", txOmni->dumps());
    return std::unique_ptr<OmniTx>(txOmni);
}
