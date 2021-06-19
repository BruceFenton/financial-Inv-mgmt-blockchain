// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "validation.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "primitives/transaction.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#endif

#include <stdint.h>
#include "assets/assets.h"

#include <univalue.h>
#include <tinyformat.h>

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, bool expanded = false)
{
    // Call into TxToUniv() in raven-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in raven-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry, true, RPCSerializationFlags());

    if (expanded) {
        uint256 txid = tx.GetHash();
        if (!(tx.IsCoinBase())) {
            const UniValue& oldVin = entry["vin"];
            UniValue newVin(UniValue::VARR);
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const CTxIn& txin = tx.vin[i];
                UniValue in = oldVin[i];

                // Add address and value info if spentindex enabled
                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
                if (GetSpentIndex(spentKey, spentInfo)) {
                    in.pushKV("value", ValueFromAmount(spentInfo.satoshis));
                    in.pushKV("valueSat", spentInfo.satoshis);
                    if (spentInfo.addressType == 1) {
                        in.pushKV("address", CRavenAddress(CKeyID(spentInfo.addressHash)).ToString());
                    } else if (spentInfo.addressType == 2) {
                        in.pushKV("address", CRavenAddress(CScriptID(spentInfo.addressHash)).ToString());
                    }
                }
                newVin.push_back(in);
            }
            entry.pushKV("vin", newVin);
        }

        const UniValue& oldVout = entry["vout"];
        UniValue newVout(UniValue::VARR);
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut& txout = tx.vout[i];
            UniValue out = oldVout[i];

            // Add spent information if spentindex is enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txid, i);
            if (GetSpentIndex(spentKey, spentInfo)) {
                out.pushKV("spentTxId", spentInfo.txid.GetHex());
                out.pushKV("spentIndex", (int)spentInfo.inputIndex);
                out.pushKV("spentHeight", spentInfo.blockHeight);
            }

            out.pushKV("valueSat", txout.nValue);
            newVout.push_back(out);
        }
        entry.pushKV("vout", newVout);
    }

    if (!hashBlock.IsNull()) {
        entry.pushKV("blockhash", hashBlock.GetHex());
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.pushKV("height", pindex->nHeight);
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            } else {
                entry.pushKV("height", -1);
                entry.pushKV("confirmations", 0);
            }
        }
    }
}

UniValue getrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions.\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (bool, optional, default=false) If false, return a string, otherwise return a json object\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"address\"        (string) raven address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
        );

    LOCK(cs_main);

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        if (request.params[1].isNum()) {
            if (request.params[1].get_int() != 0) {
                fVerbose = true;
            }
        }
        else if(request.params[1].isBool()) {
            if(request.params[1].isTrue()) {
                fVerbose = true;
            }
        }
        else {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid type provided. Verbose parameter must be a boolean.");
        }
    }

    CTransactionRef tx;

    uint256 hashBlock;
    if (!GetTransaction(hash, tx, GetParams().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string(fTxIndex ? "No such mempool or blockchain transaction"
            : "No such mempool transaction. Use -txindex to enable blockchain transaction queries") +
            ". Use gettransaction for wallet transactions.");

    if (!fVerbose)
        return EncodeHexTx(*tx, RPCSerializationFlags());

    UniValue result(UniValue::VOBJ);
    TxToJSON(*tx, hashBlock, result, true);

    return result;
}

UniValue gettxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included manually (by blockhash).\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n"
        );

    std::set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = request.params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = nullptr;

    uint256 hashBlock;
    if (!request.params[1].isNull())
    {
        hashBlock = uint256S(request.params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        // Loop through txids and try to find which block they're in. Exit loop once a block is found.
        for (const auto& tx : setTxids) {
            const Coin& coin = AccessByTxid(*pcoinsTip, tx);
            if (!coin.IsSpent()) {
                pblockindex = chainActive[coin.nHeight];
                break;
            }
        }
    }

    if (pblockindex == nullptr)
    {
        CTransactionRef tx;
        if (!GetTransaction(oneTxid, tx, GetParams().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex, GetParams().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto& tx : block.vtx)
        if (setTxids.count(tx->GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not all transactions found in specified or retrieved block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid\n"
        );

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;



    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);

    if (!mapBlockIndex.count(merkleBlock.header.GetHash()) || (!chainActive.Contains(mapBlockIndex[merkleBlock.header.GetHash()])))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    for (const uint256& hash : vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":(amount or object),\"data\":\"hex\",...}\n"
            "                     ( locktime ) ( replaceable )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs are addresses (paired with a RVN amount, data or object specifying an asset operation) or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nPaying for Asset Operations:\n"
            "  Some operations require an amount of RVN to be sent to a burn address:\n"
            "\n"
            "    Operation          Amount + Burn Address\n"
            "    transfer                 0\n"
            "    transferwithmessage      0\n"
            "    issue                  " + i64tostr(GetBurnAmount(AssetType::ROOT) / COIN) + " to " + GetBurnAddress(AssetType::ROOT) + "\n"
            "    issue (subasset)       " + i64tostr(GetBurnAmount(AssetType::SUB) / COIN) + " to " + GetBurnAddress(AssetType::SUB) + "\n"
            "    issue_unique             " + i64tostr(GetBurnAmount(AssetType::UNIQUE) / COIN) + " to " + GetBurnAddress(AssetType::UNIQUE) + "\n"
            "    reissue                " + i64tostr(GetBurnAmount(AssetType::REISSUE) / COIN) + " to " + GetBurnAddress(AssetType::REISSUE) + "\n"
            "    issue_restricted      " + i64tostr(GetBurnAmount(AssetType::RESTRICTED) / COIN) + " to " + GetBurnAddress(AssetType::RESTRICTED) + "\n"
            "    reissue_restricted     " + i64tostr(GetBurnAmount(AssetType::REISSUE) / COIN) + " to " + GetBurnAddress(AssetType::REISSUE) + "\n"
            "    issue_qualifier       " + i64tostr(GetBurnAmount(AssetType::QUALIFIER) / COIN) + " to " + GetBurnAddress(AssetType::QUALIFIER) + "\n"
            "    issue_qualifier (sub)  " + i64tostr(GetBurnAmount(AssetType::SUB_QUALIFIER) / COIN) + " to " + GetBurnAddress(AssetType::SUB_QUALIFIER) + "\n"
            "    tag_addresses          " + "0.1 to " + GetBurnAddress(AssetType::NULL_ADD_QUALIFIER) + " (per address)\n"
            "    untag_addresses        " + "0.1 to " + GetBurnAddress(AssetType::NULL_ADD_QUALIFIER) + " (per address)\n"
            "    freeze_addresses         0\n"
            "    unfreeze_addresses       0\n"
            "    freeze_asset             0\n"
            "    unfreeze_asset           0\n"

            "\nAssets For Authorization:\n"
            "  These operations require a specific asset input for authorization:\n"
            "    Root Owner Token:\n"
            "      reissue\n"
            "      issue_unique\n"
            "      issue_restricted\n"
            "      reissue_restricted\n"
            "      freeze_addresses\n"
            "      unfreeze_addresses\n"
            "      freeze_asset\n"
            "      unfreeze_asset\n"
            "    Root Qualifier Token:\n"
            "      issue_qualifier (when issuing subqualifier)\n"
            "    Qualifier Token:\n"
            "      tag_addresses\n"
            "      untag_addresses\n"

            "\nOutput Ordering:\n"
            "  Asset operations require the following:\n"
            "    1) All coin outputs come first (including the burn output).\n"
            "    2) The owner token change output comes next (if required).\n"
            "    3) An issue, reissue, or any number of transfers comes last\n"
            "       (different types can't be mixed in a single transaction).\n"

            "\nArguments:\n"
            "1. \"inputs\"                                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",                      (string, required) The transaction id\n"
            "         \"vout\":n,                         (number, required) The output number\n"
            "         \"sequence\":n                      (number, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"                               (object, required) a json object with outputs\n"
            "     {\n"
            "       \"address\":                          (string, required) The destination raven address.\n"
            "                                               Each output must have a different address.\n"
            "         x.xxx                             (number or string, required) The RVN amount\n"
            "           or\n"
            "         {                                 (object) A json object of assets to send\n"
            "           \"transfer\":\n"
            "             {\n"
            "               \"asset-name\":               (string, required) asset name\n"
            "               asset-quantity              (number, required) the number of raw units to transfer\n"
            "               ,...\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object of describing the transfer and message contents to send\n"
            "           \"transferwithmessage\":\n"
            "             {\n"
            "               \"asset-name\":              (string, required) asset name\n"
            "               asset-quantity,            (number, required) the number of raw units to transfer\n"
            "               \"message\":\"hash\",          (string, required) ipfs hash or a txid hash\n"
            "               \"expire_time\": n           (number, required) utc time in seconds to expire the message\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing new assets to issue\n"
            "           \"issue\":\n"
            "             {\n"
            "               \"asset_name\":\"asset-name\",  (string, required) new asset name\n"
            "               \"asset_quantity\":n,         (number, required) the number of raw units to issue\n"
            "               \"units\":[1-8],              (number, required) display units, between 1 (integral) to 8 (max precision)\n"
            "               \"reissuable\":[0-1],         (number, required) 1=reissuable asset\n"
            "               \"has_ipfs\":[0-1],           (number, required) 1=passing ipfs_hash\n"
            "               \"ipfs_hash\":\"hash\"          (string, optional) an ipfs hash for discovering asset metadata\n"
            // TODO if we decide to remove the consensus check from issue 675 https://github.com/RavenProject/Ravencoin/issues/675
   //TODO"               \"custom_owner_address\": \"addr\" (string, optional) owner token will get sent to this address if set\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing new unique assets to issue\n"
            "           \"issue_unique\":\n"
            "             {\n"
            "               \"root_name\":\"root-name\",         (string, required) name of the asset the unique asset(s) \n"
            "                                                      are being issued under\n"
            "               \"asset_tags\":[\"asset_tag\", ...], (array, required) the unique tag for each asset which is to be issued\n"
            "               \"ipfs_hashes\":[\"hash\", ...],     (array, optional) ipfs hashes corresponding to each supplied tag \n"
            "                                                      (should be same size as \"asset_tags\")\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing follow-on asset issue.\n"
            "           \"reissue\":\n"
            "             {\n"
            "               \"asset_name\":\"asset-name\", (string, required) name of asset to be reissued\n"
            "               \"asset_quantity\":n,          (number, required) the number of raw units to issue\n"
            "               \"reissuable\":[0-1],          (number, optional) default is 1, 1=reissuable asset\n"
            "               \"ipfs_hash\":\"hash\",        (string, optional) An ipfs hash for discovering asset metadata, \n"
            "                                                Overrides the current ipfs hash if given\n"
            "               \"owner_change_address\"       (string, optional) the address where the owner token will be sent to. \n"
            "                                                If not given, it will be sent to the output address\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing how restricted asset to issue\n"
            "           \"issue_restricted\":\n"
            "             {\n"
            "               \"asset_name\":\"asset-name\",(string, required) new asset name\n"
            "               \"asset_quantity\":n,         (number, required) the number of raw units to issue\n"
            "               \"verifier_string\":\"text\", (string, required) the verifier string to be used for a restricted \n"
            "                                               asset transfer verification\n"
            "               \"units\":[0-8],              (number, required) display units, between 0 (integral) and 8 (max precision)\n"
            "               \"reissuable\":[0-1],         (number, required) 1=reissuable asset\n"
            "               \"has_ipfs\":[0-1],           (number, required) 1=passing ipfs_hash\n"
            "               \"ipfs_hash\":\"hash\",       (string, optional) an ipfs hash for discovering asset metadata\n"
            "               \"owner_change_address\"      (string, optional) the address where the owner token will be sent to. \n"
            "                                               If not given, it will be sent to the output address\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing follow-on asset issue.\n"
            "           \"reissue_restricted\":\n"
            "             {\n"
            "               \"asset_name\":\"asset-name\", (string, required) name of asset to be reissued\n"
            "               \"asset_quantity\":n,          (number, required) the number of raw units to issue\n"
            "               \"reissuable\":[0-1],          (number, optional) default is 1, 1=reissuable asset\n"
            "               \"verifier_string\":\"text\",  (string, optional) the verifier string to be used for a restricted asset \n"
            "                                                transfer verification\n"
            "               \"ipfs_hash\":\"hash\",        (string, optional) An ipfs hash for discovering asset metadata, \n"
            "                                                Overrides the current ipfs hash if given\n"
            "               \"owner_change_address\"       (string, optional) the address where the owner token will be sent to. \n"
            "                                                If not given, it will be sent to the output address\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing a new qualifier to issue.\n"
            "           \"issue_qualifier\":\n"
            "             {\n"
            "               \"asset_name\":\"asset_name\", (string, required) a qualifier name (starts with '#')\n"
            "               \"asset_quantity\":n,          (numeric, optional, default=1) the number of units to be issued (1 to 10)\n"
            "               \"has_ipfs\":[0-1],            (boolean, optional, default=false), whether ifps hash is going \n"
            "                                                to be added to the asset\n"
            "               \"ipfs_hash\":\"hash\",        (string, optional but required if has_ipfs = 1), an ipfs hash or a \n"
            "                                                txid hash once RIP5 is activated\n"
            "               \"root_change_address\"        (string, optional) Only applies when issuing subqualifiers.\n"
            "                                                The address where the root qualifier will be sent.\n"
            "                                                If not specified, it will be sent to the output address.\n"
            "               \"change_quantity\":\"qty\"    (numeric, optional) the asset change amount (defaults to 1)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be tagged.\n"
            "                                             The address in the key will used as the asset change address.\n"
            "           \"tag_addresses\":\n"
            "             {\n"
            "               \"qualifier\":\"qualifier\",          (string, required) a qualifier name (starts with '#')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be tagged (up to 10)\n"
            "               \"change_quantity\":\"qty\",          (numeric, optional) the asset change amount (defaults to 1)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be untagged.\n"
            "                                             The address in the key will be used as the asset change address.\n"
            "           \"untag_addresses\":\n"
            "             {\n"
            "               \"qualifier\":\"qualifier\",          (string, required) a qualifier name (starts with '#')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be untagged (up to 10)\n"
            "               \"change_quantity\":\"qty\",          (numeric, optional) the asset change amount (defaults to 1)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be frozen.\n"
            "                                             The address in the key will used as the owner change address.\n"
            "           \"freeze_addresses\":\n"
            "             {\n"
            "               \"asset_name\":\"asset_name\",        (string, required) a restricted asset name (starts with '$')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be frozen (up to 10)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be frozen.\n"
            "                                             The address in the key will be used as the owner change address.\n"
            "           \"unfreeze_addresses\":\n"
            "             {\n"
            "               \"asset_name\":\"asset_name\",        (string, required) a restricted asset name (starts with '$')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be untagged (up to 10)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing an asset to be frozen.\n"
            "                                             The address in the key will used as the owner change address.\n"
            "           \"freeze_asset\":\n"
            "             {\n"
            "               \"asset_name\":\"asset_name\",        (string, required) a restricted asset name (starts with '$')\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing an asset to be frozen.\n"
            "                                             The address in the key will be used as the owner change address.\n"
            "           \"unfreeze_asset\":\n"
            "             {\n"
            "               \"asset_name\":\"asset_name\",        (string, required) a restricted asset name (starts with '$')\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "       \"data\": \"hex\"                       (string, required) The key is \"data\", the value is hex encoded data\n"
            "       ,...\n"
            "     }\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
//            "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
//            "                                        Allows this transaction to be replaced by a transaction with higher fees.\n"
//            "                                        If provided, it is an error if explicit sequence numbers are incompatible.\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueAssetXXXXXXXXXXXXXXXXXhhZGt\\\":500,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue\\\":{\\\"asset_name\\\":\\\"MYASSET\\\",\\\"asset_quantity\\\":1000000,\\\"units\\\":1,\\\"reissuable\\\":0,\\\"has_ipfs\\\":1,\\\"ipfs_hash\\\":\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\"}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueRestrictedXXXXXXXXXXXXzJZ1q\\\":1500,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue_restricted\\\":{\\\"asset_name\\\":\\\"$MYASSET\\\",\\\"asset_quantity\\\":1000000,\\\"verifier_string\\\":\\\"#TAG & !KYC\\\",\\\"units\\\":1,\\\"reissuable\\\":0,\\\"has_ipfs\\\":1,\\\"ipfs_hash\\\":\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\"}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueUniqueAssetXXXXXXXXXXWEAe58\\\":20,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue_unique\\\":{\\\"root_name\\\":\\\"MYASSET\\\",\\\"asset_tags\\\":[\\\"ALPHA\\\",\\\"BETA\\\"],\\\"ipfs_hashes\\\":[\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\",\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\"]}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"myasset\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":{\\\"transfer\\\":{\\\"MYASSET\\\":50}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"myasset\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":{\\\"transferwithmessage\\\":{\\\"MYASSET\\\":50,\\\"message\\\":\\\"hash\\\",\\\"expire_time\\\": utc_time}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"myownership\\\",\\\"vout\\\":0}]\" \"{\\\"issuer_address\\\":{\\\"reissue\\\":{\\\"asset_name\\\":\\\"MYASSET\\\",\\\"asset_quantity\\\":2000000}}}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ, UniValue::VNUM}, true);
    if (request.params[0].isNull() || request.params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = request.params[0].get_array();
    UniValue sendTo = request.params[1].get_obj();

    CMutableTransaction rawTx;

    if (!request.params[2].isNull()) {
        int64_t nLockTime = request.params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

//    bool rbfOptIn = request.params[3].isTrue();

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence;
//        if (rbfOptIn) {
//            nSequence = MAX_BIP125_RBF_SEQUENCE;
//        } else if (rawTx.nLockTime) {
//            nSequence = std::numeric_limits<uint32_t>::max() - 1;
//        } else {
//            nSequence = std::numeric_limits<uint32_t>::max();
//        }

        if (rawTx.nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    auto currentActiveAssetCache = GetCurrentAssetCache();

    std::set<CTxDestination> destinations;
    std::vector<std::string> addrList = sendTo.getKeys();
    for (const std::string& name_ : addrList) {

        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(),"Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination = DecodeDestination(name_);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raven address: ") + name_);
            }

            if (!destinations.insert(destination).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }

            CScript scriptPubKey = GetScriptForDestination(destination);
            CScript ownerPubKey = GetScriptForDestination(destination);


            if (sendTo[name_].type() == UniValue::VNUM || sendTo[name_].type() == UniValue::VSTR) {
                CAmount nAmount = AmountFromValue(sendTo[name_]);
                CTxOut out(nAmount, scriptPubKey);
                rawTx.vout.push_back(out);
            }
            /** RVN COIN START **/
            else if (sendTo[name_].type() == UniValue::VOBJ) {
                auto asset_ = sendTo[name_].get_obj();
                auto assetKey_ = asset_.getKeys()[0];

                if (assetKey_ == "issue")
                {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue\": {\"key\": value}, ...}"));

                    // Get the asset data object from the json
                    auto assetData = asset_.getValues()[0].get_obj();

                    /**-------Process the assets data-------**/
                    const UniValue& asset_name = find_value(assetData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_name");

                    const UniValue& asset_quantity = find_value(assetData, "asset_quantity");
                    if (!asset_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_quantity");

                    const UniValue& units = find_value(assetData, "units");
                    if (!units.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: units");

                    const UniValue& reissuable = find_value(assetData, "reissuable");
                    if (!reissuable.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: reissuable");

                    const UniValue& has_ipfs = find_value(assetData, "has_ipfs");
                    if (!has_ipfs.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: has_ipfs");
// TODO, if we decide to remove the consensus check https://github.com/RavenProject/Ravencoin/issues/675, remove or add the code (requires consensus change)
//                    const UniValue& custom_owner_address = find_value(assetData, "custom_owner_address");
//                    if (!custom_owner_address.isNull()) {
//                        CTxDestination dest = DecodeDestination(custom_owner_address.get_str());
//                        if (!IsValidDestination(dest)) {
//                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, invalid destination: custom_owner_address");
//                        }
//
//                        ownerPubKey = GetScriptForDestination(dest);
//                    }


                    UniValue ipfs_hash = "";
                    if (has_ipfs.get_int() == 1) {
                        ipfs_hash = find_value(assetData, "ipfs_hash");
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: has_ipfs");
                    }


                    if (IsAssetNameAnRestricted(asset_name.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, asset_name can't be a restricted asset name. Please use issue_restricted with the correct parameters");

                    CAmount nAmount = AmountFromValue(asset_quantity);

                    // Create a new asset
                    CNewAsset asset(asset_name.get_str(), nAmount, units.get_int(), reissuable.get_int(), has_ipfs.get_int(), DecodeAssetData(ipfs_hash.get_str()));

                    // Verify that data
                    std::string strError = "";
                    if (!ContextualCheckNewAsset(currentActiveAssetCache, asset, strError)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                    }

                    // Construct the asset transaction
                    asset.ConstructTransaction(scriptPubKey);

                    AssetType type;
                    if (IsAssetNameValid(asset.strName, type)) {
                        if (type != AssetType::UNIQUE && type != AssetType::MSGCHANNEL) {
                            asset.ConstructOwnerTransaction(ownerPubKey);

                            // Push the scriptPubKey into the vouts.
                            CTxOut ownerOut(0, ownerPubKey);
                            rawTx.vout.push_back(ownerOut);
                        }
                    } else {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, ("Invalid parameter, invalid asset name"));
                    }

                    // Push the scriptPubKey into the vouts.
                    CTxOut out(0, scriptPubKey);
                    rawTx.vout.push_back(out);

                }
                else if (assetKey_ == "issue_unique")
                {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_unique\": {\"root_name\": value}, ...}"));

                    // Get the asset data object from the json
                    auto assetData = asset_.getValues()[0].get_obj();

                    /**-------Process the assets data-------**/
                    const UniValue& root_name = find_value(assetData, "root_name");
                    if (!root_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: root_name");

                    const UniValue& asset_tags = find_value(assetData, "asset_tags");
                    if (!asset_tags.isArray() || asset_tags.size() < 1)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_tags");

                    const UniValue& ipfs_hashes = find_value(assetData, "ipfs_hashes");
                    if (!ipfs_hashes.isNull()) {
                        if (!ipfs_hashes.isArray() || ipfs_hashes.size() != asset_tags.size()) {
                            if (!ipfs_hashes.isNum())
                                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                                   "Invalid parameter, missing asset metadata for key: units");
                        }
                    }

                    // Create the scripts for the change of the ownership token
                    CScript scriptTransferOwnerAsset = GetScriptForDestination(destination);
                    CAssetTransfer assetTransfer(root_name.get_str() + OWNER_TAG, OWNER_ASSET_AMOUNT);
                    assetTransfer.ConstructTransaction(scriptTransferOwnerAsset);

                    // Create the CTxOut for the owner token
                    CTxOut out(0, scriptTransferOwnerAsset);
                    rawTx.vout.push_back(out);

                    // Create the assets
                    for (int i = 0; i < (int)asset_tags.size(); i++) {

                        // Create a new asset
                        CNewAsset asset;
                        if (ipfs_hashes.isNull()) {
                            asset = CNewAsset(GetUniqueAssetName(root_name.get_str(), asset_tags[i].get_str()),
                                              UNIQUE_ASSET_AMOUNT,  UNIQUE_ASSET_UNITS, UNIQUE_ASSETS_REISSUABLE, 0, "");
                        } else {
                            asset = CNewAsset(GetUniqueAssetName(root_name.get_str(), asset_tags[i].get_str()),
                                              UNIQUE_ASSET_AMOUNT, UNIQUE_ASSET_UNITS, UNIQUE_ASSETS_REISSUABLE,
                                              1, DecodeAssetData(ipfs_hashes[i].get_str()));
                        }

                        // Verify that data
                        std::string strError = "";
                        if (!ContextualCheckNewAsset(currentActiveAssetCache, asset, strError))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                        // Construct the asset transaction
                        scriptPubKey = GetScriptForDestination(destination);
                        asset.ConstructTransaction(scriptPubKey);

                        // Push the scriptPubKey into the vouts.
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);

                    }
                }
                else if (assetKey_ == "reissue")
                {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"reissue\": {\"key\": value}, ...}"));

                    // Get the asset data object from the json
                    auto reissueData = asset_.getValues()[0].get_obj();

                    CReissueAsset reissueObj;

                    /**-------Process the reissue data-------**/
                    const UniValue& asset_name = find_value(reissueData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing reissue data for key: asset_name");

                    const UniValue& asset_quantity = find_value(reissueData, "asset_quantity");
                    if (!asset_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing reissue data for key: asset_quantity");

                    const UniValue& reissuable = find_value(reissueData, "reissuable");
                    if (!reissuable.isNull()) {
                        if (!reissuable.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: reissuable");

                        int nReissuable = reissuable.get_int();
                        if (nReissuable > 1 || nReissuable < 0)
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, reissuable data must be a 0 or 1");

                        reissueObj.nReissuable = int8_t(nReissuable);
                    }

                    const UniValue& ipfs_hash = find_value(reissueData, "ipfs_hash");
                    if (!ipfs_hash.isNull()) {
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: ipfs_hash");
                        reissueObj.strIPFSHash = DecodeAssetData(ipfs_hash.get_str());
                    }

                    bool fHasOwnerChange = false;
                    const UniValue& owner_change_address = find_value(reissueData, "owner_change_address");
                    if (!owner_change_address.isNull()) {
                        if (!owner_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, owner_change_address must be a string");
                        fHasOwnerChange = true;
                    }

                    if (fHasOwnerChange && !IsValidDestinationString(owner_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, owner_change_address is not a valid Ravencoin address");

                    if (IsAssetNameAnRestricted(asset_name.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, asset_name can't be a restricted asset name. Please use reissue_restricted with the correct parameters");

                    // Add the received data into the reissue object
                    reissueObj.strName = asset_name.get_str();
                    reissueObj.nAmount = AmountFromValue(asset_quantity);

                    // Validate the the object is valid
                    std::string strError;
                    if (!ContextualCheckReissueAsset(currentActiveAssetCache, reissueObj, strError))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                    // Create the scripts for the change of the ownership token
                    CScript owner_asset_transfer_script;
                    if (fHasOwnerChange)
                        owner_asset_transfer_script = GetScriptForDestination(DecodeDestination(owner_change_address.get_str()));
                    else
                        owner_asset_transfer_script = GetScriptForDestination(destination);

                    CAssetTransfer transfer_owner(asset_name.get_str() + OWNER_TAG, OWNER_ASSET_AMOUNT);
                    transfer_owner.ConstructTransaction(owner_asset_transfer_script);

                    // Create the scripts for the reissued assets
                    CScript scriptReissueAsset = GetScriptForDestination(destination);
                    reissueObj.ConstructTransaction(scriptReissueAsset);

                    // Create the CTxOut for the owner token
                    CTxOut out(0, owner_asset_transfer_script);
                    rawTx.vout.push_back(out);

                    // Create the CTxOut for the reissue asset
                    CTxOut out2(0, scriptReissueAsset);
                    rawTx.vout.push_back(out2);

                } else if (assetKey_ == "transfer") {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"transfer\": {\"asset_name\": amount, ...} }"));

                    UniValue transferData = asset_.getValues()[0].get_obj();

                    auto keys = transferData.getKeys();

                    if (keys.size() == 0)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"transfer\": {\"asset_name\": amount, ...} }"));

                    UniValue asset_quantity;
                    for (auto asset_name : keys) {
                        asset_quantity = find_value(transferData, asset_name);

                        if (!asset_quantity.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing or invalid quantity");

                        CAmount nAmount = AmountFromValue(asset_quantity);

                        // Create a new transfer
                        CAssetTransfer transfer(asset_name, nAmount);

                        // Verify
                        std::string strError = "";
                        if (!transfer.IsValid(strError)) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                        }

                        // Construct transaction
                        CScript scriptPubKey = GetScriptForDestination(destination);
                        transfer.ConstructTransaction(scriptPubKey);

                        // Push into vouts
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);
                    }
                } else if (assetKey_ == "transferwithmessage") {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                "Invalid parameter, the format must follow { \"transferwithmessage\": {\"asset_name\": amount, \"message\": messagehash, \"expire_time\": utc_time} }"));

                    UniValue transferData = asset_.getValues()[0].get_obj();

                    auto keys = transferData.getKeys();

                    if (keys.size() == 0)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                "Invalid parameter, the format must follow { \"transferwithmessage\": {\"asset_name\": amount, \"message\": messagehash, \"expire_time\": utc_time} }"));

                    UniValue asset_quantity;
                    std::string asset_name = keys[0];

                    if (!IsAssetNameValid(asset_name)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, missing valid asset name to transferwithmessage");

                        const UniValue &asset_quantity = find_value(transferData, asset_name);
                        if (!asset_quantity.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing or invalid quantity");

                        const UniValue &message = find_value(transferData, "message");
                        if (!message.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue data for key: message");

                        const UniValue &expire_time = find_value(transferData, "expire_time");
                        if (!expire_time.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue data for key: expire_time");

                        CAmount nAmount = AmountFromValue(asset_quantity);

                        // Create a new transfer
                        CAssetTransfer transfer(asset_name, nAmount, DecodeAssetData(message.get_str()),
                                                expire_time.get_int64());

                        // Verify
                        std::string strError = "";
                        if (!transfer.IsValid(strError)) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                        }

                        // Construct transaction
                        CScript scriptPubKey = GetScriptForDestination(destination);
                        transfer.ConstructTransaction(scriptPubKey);

                        // Push into vouts
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);
                    }
                } else if (assetKey_ == "issue_restricted") {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_restricted\": {\"key\": value}, ...}"));

                    // Get the asset data object from the json
                    auto assetData = asset_.getValues()[0].get_obj();

                    /**-------Process the assets data-------**/
                    const UniValue& asset_name = find_value(assetData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_name");

                    const UniValue& asset_quantity = find_value(assetData, "asset_quantity");
                    if (!asset_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_quantity");

                    const UniValue& verifier_string = find_value(assetData, "verifier_string");
                    if (!verifier_string.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset_data for key: verifier_string");

                    const UniValue& units = find_value(assetData, "units");
                    if (!units.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: units");

                    const UniValue& reissuable = find_value(assetData, "reissuable");
                    if (!reissuable.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: reissuable");

                    const UniValue& has_ipfs = find_value(assetData, "has_ipfs");
                    if (!has_ipfs.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: has_ipfs");

                    bool fHasOwnerChange = false;
                    const UniValue& owner_change_address = find_value(assetData, "owner_change_address");
                    if (!owner_change_address.isNull()) {
                        if (!owner_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, owner_change_address must be a string");
                        fHasOwnerChange = true;
                    }

                    if (fHasOwnerChange && !IsValidDestinationString(owner_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, owner_change_address is not a valid Ravencoin address");

                    UniValue ipfs_hash = "";
                    if (has_ipfs.get_int() == 1) {
                        ipfs_hash = find_value(assetData, "ipfs_hash");
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: has_ipfs");
                    }

                    std::string strAssetName = asset_name.get_str();

                    if (!IsAssetNameAnRestricted(strAssetName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, asset_name must be a restricted asset name. e.g $ASSET_NAME");

                    CAmount nAmount = AmountFromValue(asset_quantity);

                    // Strip the white spaces from the verifier string
                    std::string strippedVerifierString = GetStrippedVerifierString(verifier_string.get_str());

                    // Check the restricted asset destination address, and make sure it validates with the verifier string
                    std::string strError = "";
                    if (!ContextualCheckVerifierString(currentActiveAssetCache, strippedVerifierString, EncodeDestination(destination), strError))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parmeter, verifier string is not. Please check the syntax. Error Msg - " + strError));


                    // Create a new asset
                    CNewAsset asset(strAssetName, nAmount, units.get_int(), reissuable.get_int(), has_ipfs.get_int(), DecodeAssetData(ipfs_hash.get_str()));

                    // Verify the new asset data
                    if (!ContextualCheckNewAsset(currentActiveAssetCache, asset, strError)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                    }

                    // Construct the restricted issuance script
                    CScript restricted_issuance_script = GetScriptForDestination(destination);
                    asset.ConstructTransaction(restricted_issuance_script);

                    // Construct the owner change script
                    CScript owner_asset_transfer_script;
                    if (fHasOwnerChange)
                        owner_asset_transfer_script = GetScriptForDestination(DecodeDestination(owner_change_address.get_str()));
                    else
                        owner_asset_transfer_script = GetScriptForDestination(destination);

                    CAssetTransfer transfer_owner(strAssetName.substr(1, strAssetName.size()) + OWNER_TAG, OWNER_ASSET_AMOUNT);
                    transfer_owner.ConstructTransaction(owner_asset_transfer_script);

                    // Construct the verifier string script
                    CScript verifier_string_script;
                    CNullAssetTxVerifierString verifierString(strippedVerifierString);
                    verifierString.ConstructTransaction(verifier_string_script);

                    // Create the CTxOut for each script we need to issue a restricted asset
                    CTxOut resissue(0, restricted_issuance_script);
                    CTxOut owner_change(0, owner_asset_transfer_script);
                    CTxOut verifier(0, verifier_string_script);

                    // Push the scriptPubKey into the vouts.
                    rawTx.vout.push_back(verifier);
                    rawTx.vout.push_back(owner_change);
                    rawTx.vout.push_back(resissue);

                } else if (assetKey_ == "reissue_restricted") {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                "Invalid parameter, the format must follow { \"reissue_restricted\": {\"key\": value}, ...}"));

                    // Get the asset data object from the json
                    auto reissueData = asset_.getValues()[0].get_obj();

                    CReissueAsset reissueObj;

                    /**-------Process the reissue data-------**/
                    const UniValue &asset_name = find_value(reissueData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, missing reissue data for key: asset_name");

                    const UniValue &asset_quantity = find_value(reissueData, "asset_quantity");
                    if (!asset_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, missing reissue data for key: asset_quantity");

                    const UniValue &reissuable = find_value(reissueData, "reissuable");
                    if (!reissuable.isNull()) {
                        if (!reissuable.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: reissuable");

                        int nReissuable = reissuable.get_int();
                        if (nReissuable > 1 || nReissuable < 0)
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, reissuable data must be a 0 or 1");

                        reissueObj.nReissuable = int8_t(nReissuable);
                    }

                    bool fHasVerifier = false;
                    const UniValue &verifier = find_value(reissueData, "verifier_string");
                    if (!verifier.isNull()) {
                        if (!verifier.isStr()) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, verifier_string must be a string");
                        }
                        fHasVerifier = true;
                    }

                    const UniValue &ipfs_hash = find_value(reissueData, "ipfs_hash");
                    if (!ipfs_hash.isNull()) {
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: ipfs_hash");
                        reissueObj.strIPFSHash = DecodeAssetData(ipfs_hash.get_str());
                    }

                    bool fHasOwnerChange = false;
                    const UniValue &owner_change_address = find_value(reissueData, "owner_change_address");
                    if (!owner_change_address.isNull()) {
                        if (!owner_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, owner_change_address must be a string");
                        fHasOwnerChange = true;
                    }

                    if (fHasOwnerChange && !IsValidDestinationString(owner_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, owner_change_address is not a valid Ravencoin address");

                    std::string strAssetName = asset_name.get_str();

                    if (!IsAssetNameAnRestricted(strAssetName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, asset_name must be a restricted asset name. e.g $ASSET_NAME");

                    std::string strippedVerifierString;
                    if (fHasVerifier) {
                        // Strip the white spaces from the verifier string
                        strippedVerifierString = GetStrippedVerifierString(verifier.get_str());

                        // Check the restricted asset destination address, and make sure it validates with the verifier string
                        std::string strError = "";
                        if (!ContextualCheckVerifierString(currentActiveAssetCache, strippedVerifierString,
                                                           EncodeDestination(destination), strError))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                    "Invalid parmeter, verifier string is not. Please check the syntax. Error Msg - " +
                                    strError));
                    }

                    // Add the received data into the reissue object
                    reissueObj.strName = asset_name.get_str();
                    reissueObj.nAmount = AmountFromValue(asset_quantity);

                    // Validate the the object is valid
                    std::string strError;
                    if (!ContextualCheckReissueAsset(currentActiveAssetCache, reissueObj, strError))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                    // Create the scripts for the change of the ownership token
                    CScript owner_asset_transfer_script;
                    if (fHasOwnerChange)
                        owner_asset_transfer_script = GetScriptForDestination(
                                DecodeDestination(owner_change_address.get_str()));
                    else
                        owner_asset_transfer_script = GetScriptForDestination(destination);

                    CAssetTransfer transfer_owner(RestrictedNameToOwnerName(asset_name.get_str()), OWNER_ASSET_AMOUNT);
                    transfer_owner.ConstructTransaction(owner_asset_transfer_script);

                    // Create the scripts for the reissued assets
                    CScript scriptReissueAsset = GetScriptForDestination(destination);
                    reissueObj.ConstructTransaction(scriptReissueAsset);

                    // Construct the verifier string script
                    CScript verifier_string_script;
                    if (fHasVerifier) {
                        CNullAssetTxVerifierString verifierString(strippedVerifierString);
                        verifierString.ConstructTransaction(verifier_string_script);
                    }

                    // Create the CTxOut for the verifier script
                    CTxOut out_verifier(0, verifier_string_script);
                    rawTx.vout.push_back(out_verifier);

                    // Create the CTxOut for the owner token
                    CTxOut out_owner(0, owner_asset_transfer_script);
                    rawTx.vout.push_back(out_owner);

                    // Create the CTxOut for the reissue asset
                    CTxOut out_reissuance(0, scriptReissueAsset);
                    rawTx.vout.push_back(out_reissuance);

                } else if (assetKey_ == "issue_qualifier") {
                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_qualifier\": {\"key\": value}, ...}"));

                    // Get the asset data object from the json
                    auto assetData = asset_.getValues()[0].get_obj();

                    /**-------Process the assets data-------**/
                    const UniValue& asset_name = find_value(assetData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_name");

                    const UniValue& asset_quantity = find_value(assetData, "asset_quantity");
                    if (!asset_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset data for key: asset_quantity");

                    const UniValue& has_ipfs = find_value(assetData, "has_ipfs");
                    if (!has_ipfs.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: has_ipfs");

                    bool fHasIpfs = false;
                    UniValue ipfs_hash = "";
                    if (has_ipfs.get_int() == 1) {
                        fHasIpfs = true;
                        ipfs_hash = find_value(assetData, "ipfs_hash");
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing asset metadata for key: has_ipfs");
                    }

                    std::string strAssetName = asset_name.get_str();
                    if (!IsAssetNameAQualifier(strAssetName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, asset_name must be a qualifier or subqualifier name. e.g #MY_QUALIFIER or #MY_ROOT/#MY_SUB");
                    bool isSubQualifier = IsAssetNameASubQualifier(strAssetName);

                    bool fHasRootChange = false;
                    const UniValue& root_change_address = find_value(assetData, "root_change_address");
                    if (!root_change_address.isNull()) {
                        if (!isSubQualifier)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, root_change_address only allowed when issuing a subqualifier.");
                        if (!root_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, root_change_address must be a string");
                        fHasRootChange = true;
                    }

                    if (fHasRootChange && !IsValidDestinationString(root_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, root_change_address is not a valid Ravencoin address");

                    CAmount nAmount = AmountFromValue(asset_quantity);
                    if (nAmount < QUALIFIER_ASSET_MIN_AMOUNT || nAmount > QUALIFIER_ASSET_MAX_AMOUNT)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, qualifiers are only allowed to be issued in quantities between 1 and 10.");

                    CAmount changeQty = COIN;
                    const UniValue& change_qty = find_value(assetData, "change_quantity");
                    if (!change_qty.isNull()) {
                        if (!change_qty.isNum() || AmountFromValue(change_qty) < COIN)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, change_amount must be a positive number");
                        changeQty = AmountFromValue(change_qty);
                    }

                    int units = 0;
                    bool reissuable = false;

                    // Create a new qualifier asset
                    CNewAsset asset(strAssetName, nAmount, units, reissuable ? 1 : 0, fHasIpfs ? 1 : 0, DecodeAssetData(ipfs_hash.get_str()));

                    // Verify the new asset data
                    std::string strError = "";
                    if (!ContextualCheckNewAsset(currentActiveAssetCache, asset, strError)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                    }

                    // Construct the issuance script
                    CScript issuance_script = GetScriptForDestination(destination);
                    asset.ConstructTransaction(issuance_script);

                    // Construct the root change script if issuing subqualifier
                    CScript root_asset_transfer_script;
                    if (isSubQualifier) {
                        if (fHasRootChange)
                            root_asset_transfer_script = GetScriptForDestination(
                                    DecodeDestination(root_change_address.get_str()));
                        else
                            root_asset_transfer_script = GetScriptForDestination(destination);

                        CAssetTransfer transfer_root(GetParentName(strAssetName), changeQty);
                        transfer_root.ConstructTransaction(root_asset_transfer_script);
                    }

                    // Create the CTxOut for each script we need to issue
                    CTxOut issue(0, issuance_script);
                    CTxOut root_change;
                    if (isSubQualifier)
                        root_change = CTxOut(0, root_asset_transfer_script);

                    // Push the scriptPubKey into the vouts.
                    if (isSubQualifier)
                        rawTx.vout.push_back(root_change);
                    rawTx.vout.push_back(issue);

                } else if (assetKey_ == "tag_addresses" || assetKey_ == "untag_addresses") {
                    int8_t tag_op = assetKey_ == "tag_addresses" ? 1 : 0;

                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"[tag|untag]_addresses\": {\"key\": value}, ...}"));
                    auto assetData = asset_.getValues()[0].get_obj();

                    const UniValue& qualifier = find_value(assetData, "qualifier");
                    if (!qualifier.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing data for key: qualifier");
                    std::string strQualifier = qualifier.get_str();
                    if (!IsAssetNameAQualifier(strQualifier))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, a valid qualifier name must be provided, e.g. #MY_QUALIFIER");

                    const UniValue& addresses = find_value(assetData, "addresses");
                    if (!addresses.isArray() || addresses.size() < 1 || addresses.size() > 10)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, value for key address must be an array of size 1 to 10");
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        if (!IsValidDestinationString(addresses[i].get_str()))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, supplied address is not a valid Ravencoin address");
                    }

                    CAmount changeQty = COIN;
                    const UniValue& change_qty = find_value(assetData, "change_quantity");
                    if (!change_qty.isNull()) {
                        if (!change_qty.isNum() || AmountFromValue(change_qty) < COIN)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, change_amount must be a positive number");
                        changeQty = AmountFromValue(change_qty);
                    }

                    // change
                    CScript change_script = GetScriptForDestination(destination);
                    CAssetTransfer transfer_change(strQualifier, changeQty);
                    transfer_change.ConstructTransaction(change_script);
                    CTxOut out_change(0, change_script);
                    rawTx.vout.push_back(out_change);

                    // tagging
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        CScript tag_string_script = GetScriptForNullAssetDataDestination(DecodeDestination(addresses[i].get_str()));
                        CNullAssetTxData tagString(strQualifier, tag_op);
                        tagString.ConstructTransaction(tag_string_script);
                        CTxOut out_tag(0, tag_string_script);
                        rawTx.vout.push_back(out_tag);
                    }
                } else if (assetKey_ == "freeze_addresses" || assetKey_ == "unfreeze_addresses") {
                    int8_t freeze_op = assetKey_ == "freeze_addresses" ? 1 : 0;

                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"[freeze|unfreeze]_addresses\": {\"key\": value}, ...}"));
                    auto assetData = asset_.getValues()[0].get_obj();

                    const UniValue& asset_name = find_value(assetData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing data for key: asset_name");
                    std::string strAssetName = asset_name.get_str();
                    if (!IsAssetNameAnRestricted(strAssetName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, a valid restricted asset name must be provided, e.g. $MY_ASSET");

                    const UniValue& addresses = find_value(assetData, "addresses");
                    if (!addresses.isArray() || addresses.size() < 1 || addresses.size() > 10)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, value for key address must be an array of size 1 to 10");
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        if (!IsValidDestinationString(addresses[i].get_str()))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, supplied address is not a valid Ravencoin address");
                    }

                    // owner change
                    CScript change_script = GetScriptForDestination(destination);
                    CAssetTransfer transfer_change(RestrictedNameToOwnerName(strAssetName), OWNER_ASSET_AMOUNT);
                    transfer_change.ConstructTransaction(change_script);
                    CTxOut out_change(0, change_script);
                    rawTx.vout.push_back(out_change);

                    // freezing
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        CScript freeze_string_script = GetScriptForNullAssetDataDestination(DecodeDestination(addresses[i].get_str()));
                        CNullAssetTxData freezeString(strAssetName, freeze_op);
                        freezeString.ConstructTransaction(freeze_string_script);
                        CTxOut out_freeze(0, freeze_string_script);
                        rawTx.vout.push_back(out_freeze);
                    }
                } else if (assetKey_ == "freeze_asset" || assetKey_ == "unfreeze_asset") {
                    int8_t freeze_op = assetKey_ == "freeze_asset" ? 1 : 0;

                    if (asset_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"[freeze|unfreeze]_asset\": {\"key\": value}, ...}"));
                    auto assetData = asset_.getValues()[0].get_obj();

                    const UniValue& asset_name = find_value(assetData, "asset_name");
                    if (!asset_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing data for key: asset_name");
                    std::string strAssetName = asset_name.get_str();
                    if (!IsAssetNameAnRestricted(strAssetName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, a valid restricted asset name must be provided, e.g. $MY_ASSET");

                    // owner change
                    CScript change_script = GetScriptForDestination(destination);
                    CAssetTransfer transfer_change(RestrictedNameToOwnerName(strAssetName), OWNER_ASSET_AMOUNT);
                    transfer_change.ConstructTransaction(change_script);
                    CTxOut out_change(0, change_script);
                    rawTx.vout.push_back(out_change);

                    // freezing
                    CScript freeze_string_script;
                    CNullAssetTxData freezeString(strAssetName, freeze_op);
                    freezeString.ConstructGlobalRestrictionTransaction(freeze_string_script);
                    CTxOut out_freeze(0, freeze_string_script);
                    rawTx.vout.push_back(out_freeze);

                } else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, unknown output type: " + assetKey_));
                }
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, Output must be of the type object"));
            }
            /** RVN COIN STOP **/
        }
    }

//    if (!request.params[3].isNull() && rbfOptIn != SignalsOptInRBF(rawTx)) {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter combination: Sequence number(s) contradict replaceable option");
//    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"asset\" : {               (json object) optional\n"
            "           \"name\" : \"name\",      (string) the asset name\n"
            "           \"amount\" : n,           (numeric) the amount of asset that was sent\n"
            "           \"message\" : \"message\", (string optional) the message if one was sent\n"
            "           \"expire_time\" : n,      (numeric optional) the message epoch expiration time if one was set\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) raven address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR});

    CMutableTransaction mtx;

    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToUniv(CTransaction(std::move(mtx)), uint256(), result, false);

    return result;
}

UniValue decodescript(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"asset\" : {               (json object) optional\n"
            "     \"name\" : \"name\",      (string) the asset name\n"
            "     \"amount\" : n,           (numeric) the amount of asset that was sent\n"
            "     \"message\" : \"message\", (string optional) the message if one was sent\n"
            "     \"expire_time\" : n,      (numeric optional ) the message epoch expiration time if one was set\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) raven address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\":\"address\",       (string) address of P2SH script wrapping this redeem script (not returned if the script is already a P2SH).\n"
            "  \"(The following only appears if the script is an asset script)\n"
            "  \"asset_name\":\"name\",      (string) Name of the asset.\n"
            "  \"amount\":\"x.xx\",          (numeric) The amount of assets interacted with.\n"
            "  \"units\": n,                (numeric) The units of the asset. (Only appears in the type (new_asset))\n"
            "  \"reissuable\": true|false, (boolean) If this asset is reissuable. (Only appears in type (new_asset|reissue_asset))\n"
            "  \"hasIPFS\": true|false,    (boolean) If this asset has an IPFS hash. (Only appears in type (new_asset if hasIPFS is true))\n"
            "  \"ipfs_hash\": \"hash\",      (string) The ipfs hash for the new asset. (Only appears in type (new_asset))\n"
            "  \"new_ipfs_hash\":\"hash\",    (string) If new ipfs hash (Only appears in type. (reissue_asset))\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0){
        std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToUniv(script, r, false);

    UniValue type;
    type = find_value(r, "type");

    if (type.isStr() && type.get_str() != "scripthash") {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.push_back(Pair("p2sh", EncodeDestination(CScriptID(script))));
    }

    /** RVN START */
    if (type.isStr() && type.get_str() == ASSET_TRANSFER_STRING) {
        if (!AreAssetsDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Assets are not active");

        CAssetTransfer transfer;
        std::string address;

        if (!TransferAssetFromScript(script, transfer, address))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the transfer asset script");

        r.push_back(Pair("asset_name", transfer.strName));
        r.push_back(Pair("amount", ValueFromAmount(transfer.nAmount)));
        if (!transfer.message.empty())
            r.push_back(Pair("message", EncodeAssetData(transfer.message)));
        if (transfer.nExpireTime)
            r.push_back(Pair("expire_time", transfer.nExpireTime));

    } else if (type.isStr() && type.get_str() == ASSET_REISSUE_STRING) {
        if (!AreAssetsDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Assets are not active");

        CReissueAsset reissue;
        std::string address;

        if (!ReissueAssetFromScript(script, reissue, address))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the reissue asset script");

        r.push_back(Pair("asset_name", reissue.strName));
        r.push_back(Pair("amount", ValueFromAmount(reissue.nAmount)));

        bool reissuable = reissue.nReissuable ? true : false;
        r.push_back(Pair("reissuable", reissuable));

        if (reissue.strIPFSHash != "")
            r.push_back(Pair("new_ipfs_hash", EncodeAssetData(reissue.strIPFSHash)));

    } else if (type.isStr() && type.get_str() == ASSET_NEW_STRING) {
        if (!AreAssetsDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Assets are not active");

        CNewAsset asset;
        std::string ownerAsset;
        std::string address;

        if(AssetFromScript(script, asset, address)) {
            r.push_back(Pair("asset_name", asset.strName));
            r.push_back(Pair("amount", ValueFromAmount(asset.nAmount)));
            r.push_back(Pair("units", asset.units));

            bool reissuable = asset.nReissuable ? true : false;
            r.push_back(Pair("reissuable", reissuable));

            bool hasIPFS = asset.nHasIPFS ? true : false;
            r.push_back(Pair("hasIPFS", hasIPFS));

            if (hasIPFS)
                r.push_back(Pair("ipfs_hash", EncodeAssetData(asset.strIPFSHash)));
        }
        else if (OwnerAssetFromScript(script, ownerAsset, address))
        {
            r.push_back(Pair("asset_name", ownerAsset));
            r.push_back(Pair("amount", ValueFromAmount(OWNER_ASSET_AMOUNT)));
            r.push_back(Pair("units", OWNER_UNITS));
        }
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the new asset script");
        }
    } else {

    }
    /** RVN END */

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", txin.prevout.hash.ToString()));
    entry.push_back(Pair("vout", (uint64_t)txin.prevout.n));
    UniValue witness(UniValue::VARR);
    for (unsigned int i = 0; i < txin.scriptWitness.stack.size(); i++) {
        witness.push_back(HexStr(txin.scriptWitness.stack[i].begin(), txin.scriptWitness.stack[i].end()));
    }
    entry.push_back(Pair("witness", witness));
    entry.push_back(Pair("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
    entry.push_back(Pair("sequence", (uint64_t)txin.nSequence));
    entry.push_back(Pair("error", strMessage));
    vErrorsRet.push_back(entry);
}

UniValue combinerawtransaction(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinerawtransaction [\"hexstring\",...]\n"
            "\nCombine multiple partially signed transactions into one transaction.\n"
            "The combined transaction may be another partially signed transaction or a \n"
            "fully signed transaction."

            "\nArguments:\n"
            "1. \"txs\"         (string) A json array of hex strings of partially signed transactions\n"
            "    [\n"
            "      \"hexstring\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "\"hex\"            (string) The hex-encoded raw transaction with signature(s)\n"

            "\nExamples:\n"
            + HelpExampleCli("combinerawtransaction", "[\"myhex1\", \"myhex2\", \"myhex3\"]")
        );


    UniValue txs = request.params[0].get_array();
    std::vector<CMutableTransaction> txVariants(txs.size());

    for (unsigned int idx = 0; idx < txs.size(); idx++) {
        if (!DecodeHexTx(txVariants[idx], txs[idx].get_str(), true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed for tx %d", idx));
        }
    }

    if (txVariants.empty()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transactions");
    }

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mergedTx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Input not found or already spent");
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;

        // ... and merge in other signatures:
        for (const CMutableTransaction& txv : txVariants) {
            if (txv.vin.size() > i) {
                sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(txv, i));
            }
        }

        UpdateTransaction(mergedTx, i, sigdata);
    }

    return EncodeHexTx(mergedTx);
}

UniValue signrawtransaction(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
#endif

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase(pwallet) + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privkeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

    ObserveSafeMode();
#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (!request.params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = request.params[2].get_array();
        for (unsigned int idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CRavenSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwallet) {
        EnsureWalletIsUnlocked(pwallet);
    }
#endif

    // Add previous txouts given in the RPC call:
    if (!request.params[1].isNull()) {
        UniValue prevTxs = request.params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin& coin = view.AccessCoin(out);
                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = 0;
                if (prevOut.exists("amount")) {
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash() || scriptPubKey.IsP2SHAssetScript())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"txid", UniValueType(UniValue::VSTR)},
                        {"vout", UniValueType(UniValue::VNUM)},
                        {"scriptPubKey", UniValueType(UniValue::VSTR)},
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwallet) ? tempKeystore : *pwallet);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (!request.params[3].isNull()) {
        static std::map<std::string, int> mapSigHashValues = {
            {std::string("ALL"), int(SIGHASH_ALL)},
            {std::string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY)},
            {std::string("NONE"), int(SIGHASH_NONE)},
            {std::string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY)},
            {std::string("SINGLE"), int(SIGHASH_SINGLE)},
            {std::string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY)},
        };
        std::string strHashType = request.params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mtx, i, amount, nHashType), prevPubKey, sigdata);
        sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(mtx, i));

        UpdateTransaction(mtx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
            } else {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(mtx)));
    result.push_back(Pair("complete", fComplete));
    if (!vErrors.empty()) {
        result.push_back(Pair("errors", vErrors));
    }

    return result;
}

UniValue sendrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    ObserveSafeMode();
    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& hashTx = tx->GetHash();

    CAmount nMaxRawTxFee = maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool())
        nMaxRawTxFee = 0;

    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    for (size_t o = 0; !fHaveChain && o < tx->vout.size(); o++) {
        const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
        fHaveChain = !existingCoin.IsSpent();
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, std::move(tx), &fMissingInputs,
                                nullptr /* plTxnReplaced */, false /* bypass_limits */, nMaxRawTxFee)) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CInv inv(MSG_TX, hashTx);
    g_connman->ForEachNode([&inv](CNode* pnode)
    {
        pnode->PushInventory(inv);
    });
    return hashTx.GetHex();
}

UniValue testmempoolaccept(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
                // clang-format off
                "testmempoolaccept [\"rawtxs\"] ( allowhighfees )\n"
                "\nReturns if raw transaction (serialized, hex-encoded) would be accepted by mempool.\n"
                "\nThis checks if the transaction violates the consensus or policy rules.\n"
                "\nSee sendrawtransaction call.\n"
                "\nArguments:\n"
                "1. [\"rawtxs\"]       (array, required) An array of hex strings of raw transactions.\n"
                "                                        Length must be one for now.\n"
                "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
                "\nResult:\n"
                "[                   (array) The result of the mempool acceptance test for each raw transaction in the input array.\n"
                "                            Length is exactly one for now.\n"
                " {\n"
                "  \"txid\"           (string) The transaction hash in hex\n"
                "  \"allowed\"        (boolean) If the mempool allows this tx to be inserted\n"
                "  \"reject-reason\"  (string) Rejection string (only present when 'allowed' is false)\n"
                " }\n"
                "]\n"
                "\nExamples:\n"
                "\nCreate a transaction\n"
                + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
                "Sign the transaction, and get back the hex\n"
                + HelpExampleCli("signrawtransaction", "\"myhex\"") +
                "\nTest acceptance of the transaction (signed hex)\n"
                + HelpExampleCli("testmempoolaccept", "\"signedhex\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("testmempoolaccept", "[\"signedhex\"]")
                // clang-format on
        );
    }

    ObserveSafeMode();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VBOOL});
    if (request.params[0].get_array().size() != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Array must contain exactly one raw transaction for now");
    }

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_array()[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& tx_hash = tx->GetHash();

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    UniValue result(UniValue::VARR);
    UniValue result_0(UniValue::VOBJ);
    result_0.pushKV("txid", tx_hash.GetHex());

    CValidationState state;
    bool missing_inputs;
    bool test_accept_res;
    {
        LOCK(cs_main);
        test_accept_res = AcceptToMemoryPool(mempool, state, std::move(tx), &missing_inputs,
                                             nullptr /* plTxnReplaced */, false /* bypass_limits */, max_raw_tx_fee, /* test_accpet */ true);
    }
    result_0.pushKV("allowed", test_accept_res);
    if (!test_accept_res) {
        if (state.IsInvalid()) {
            result_0.pushKV("reject-reason", strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else if (missing_inputs) {
            result_0.pushKV("reject-reason", "missing-inputs");
        } else {
            result_0.pushKV("reject-reason", state.GetRejectReason());
        }
    }

    result.push_back(std::move(result_0));
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      &getrawtransaction,      {"txid","verbose"} },
    { "rawtransactions",    "createrawtransaction",   &createrawtransaction,   {"inputs","outputs","locktime"} },
    { "rawtransactions",    "decoderawtransaction",   &decoderawtransaction,   {"hexstring"} },
    { "rawtransactions",    "decodescript",           &decodescript,           {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",     &sendrawtransaction,     {"hexstring","allowhighfees"} },
    { "rawtransactions",    "combinerawtransaction",  &combinerawtransaction,  {"txs"} },
    { "rawtransactions",    "signrawtransaction",     &signrawtransaction,     {"hexstring","prevtxs","privkeys","sighashtype"} }, /* uses wallet if enabled */
    { "rawtransactions",    "testmempoolaccept",      &testmempoolaccept,      {"rawtxs","allowhighfees"} },
    { "blockchain",         "gettxoutproof",          &gettxoutproof,          {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",       &verifytxoutproof,       {"proof"} },
};

void RegisterRawTransactionRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
