// Copyright (c) 2017-2017 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
#include <script/standard.h>
#include <util.h>
#include <validation.h>
#include "tx_verify.h"
#include "chainparams.h"

#include "consensus.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "validation.h"
#include <cmath>
#include <wallet/wallet.h>
#include <base58.h>
#include <tinyformat.h>

// TODO remove the following dependencies
#include "chain.h"
#include "coins.h"
#include "utilmoneystr.h"

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto& txout : tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state, bool fCheckDuplicateInputs, bool fMempoolCheck, bool fBlockCheck)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > GetMaxBlockWeight())
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    std::set<std::string> setAssetTransferNames;
    std::map<std::pair<std::string, std::string>, int> mapNullDataTxCount; // (asset_name, address) -> int
    std::set<std::string> setNullGlobalAssetChanges;
    bool fContainsNewRestrictedAsset = false;
    bool fContainsRestrictedAssetReissue = false;
    bool fContainsNullAssetVerifierTx = false;
    int nCountAddTagOuts = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");

        /** RVN START */
        // Find and handle all new OP_RVN_ASSET null data transactions
        if (txout.scriptPubKey.IsNullAsset()) {
            CNullAssetTxData data;
            std::string address;
            std::string strError = "";

            if (txout.scriptPubKey.IsNullAssetTxDataScript()) {
                if (!AssetNullDataFromScript(txout.scriptPubKey, data, address))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-asset-data-serialization");

                if (!VerifyNullAssetDataFlag(data.flag, strError))
                    return state.DoS(100, false, REJECT_INVALID, strError);

                auto pair = std::make_pair(data.asset_name, address);
                if(!mapNullDataTxCount.count(pair)){
                    mapNullDataTxCount.insert(std::make_pair(pair, 0));
                }

                mapNullDataTxCount.at(pair)++;

                if (mapNullDataTxCount.at(pair) > 1)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-data-only-one-change-per-asset-address");

                // For each qualifier that is added, there is a burn fee
                if (IsAssetNameAQualifier(data.asset_name)) {
                    if (data.flag == (int)QualifierType::ADD_QUALIFIER) {
                        nCountAddTagOuts++;
                    }
                }

            } else if (txout.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript()) {
                if (!GlobalAssetNullDataFromScript(txout.scriptPubKey, data))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-global-asset-data-serialization");

                if (!VerifyNullAssetDataFlag(data.flag, strError))
                    return state.DoS(100, false, REJECT_INVALID, strError);

                if (setNullGlobalAssetChanges.count(data.asset_name)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-data-only-one-global-change-per-asset-name");
                }

                setNullGlobalAssetChanges.insert(data.asset_name);

            } else if (txout.scriptPubKey.IsNullAssetVerifierTxDataScript()) {

                if (!CheckVerifierAssetTxOut(txout, strError))
                    return state.DoS(100, false, REJECT_INVALID, strError);

                if (fContainsNullAssetVerifierTx)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-data-only-one-verifier-per-tx");

                fContainsNullAssetVerifierTx = true;
            }
        }
        /** RVN END */

        /** RVN START */
        bool isAsset = false;
        int nType;
        bool fIsOwner;
        if (txout.scriptPubKey.IsAssetScript(nType, fIsOwner))
            isAsset = true;
        
        // Check for transfers that don't meet the assets units only if the assetCache is not null
        if (isAsset) {
            // Get the transfer transaction data from the scriptPubKey
            if (nType == TX_TRANSFER_ASSET) {
                CAssetTransfer transfer;
                std::string address;
                if (!TransferAssetFromScript(txout.scriptPubKey, transfer, address))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-asset-bad-deserialize");

                // insert into set, so that later on we can check asset null data transactions
                setAssetTransferNames.insert(transfer.strName);

                // Check asset name validity and get type
                AssetType assetType;
                if (!IsAssetNameValid(transfer.strName, assetType)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-asset-name-invalid");
                }

                // If the transfer is an ownership asset. Check to make sure that it is OWNER_ASSET_AMOUNT
                if (IsAssetNameAnOwner(transfer.strName)) {
                    if (transfer.nAmount != OWNER_ASSET_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-owner-amount-was-not-1");
                }

                // If the transfer is a unique asset. Check to make sure that it is UNIQUE_ASSET_AMOUNT
                if (assetType == AssetType::UNIQUE) {
                    if (transfer.nAmount != UNIQUE_ASSET_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-unique-amount-was-not-1");
                }

                // If the transfer is a restricted channel asset.
                if (assetType == AssetType::RESTRICTED) {
                    // TODO add checks here if any
                }

                // If the transfer is a qualifier channel asset.
                if (assetType == AssetType::QUALIFIER || assetType == AssetType::SUB_QUALIFIER) {
                    if (transfer.nAmount < QUALIFIER_ASSET_MIN_AMOUNT || transfer.nAmount > QUALIFIER_ASSET_MAX_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-qualifier-amount-must be between 1 - 100");
                }
                
                // Specific check and error message to go with to make sure the amount is 0
                if (txout.nValue != 0)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-asset-transfer-amount-isn't-zero");
            } else if (nType == TX_NEW_ASSET) {
                // Specific check and error message to go with to make sure the amount is 0
                if (txout.nValue != 0)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-asset-issued-amount-isn't-zero");
            } else if (nType == TX_REISSUE_ASSET) {
                // Specific check and error message to go with to make sure the amount is 0
                if (AreEnforcedValuesDeployed()) {
                    // We only want to not accept these txes when checking them from CheckBlock.
                    // We don't want to change the behavior when reading transactions from the database
                    // when AreEnforcedValuesDeployed return true
                    if (fBlockCheck) {
                        if (txout.nValue != 0) {
                            return state.DoS(0, false, REJECT_INVALID, "bad-txns-asset-reissued-amount-isn't-zero");
                        }
                    }
                }

                if (fMempoolCheck) {
                    // Don't accept to the mempool no matter what on these types of transactions
                    if (txout.nValue != 0) {
                        return state.DoS(0, false, REJECT_INVALID, "bad-mempool-txns-asset-reissued-amount-isn't-zero");
                    }
                }
            } else {
                return state.DoS(0, false, REJECT_INVALID, "bad-asset-type-not-any-of-the-main-three");
            }
        }
    }

    // Check for Add Tag Burn Fee
    if (nCountAddTagOuts) {
        if (!tx.CheckAddingTagBurnFee(nCountAddTagOuts))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-doesn't-contain-required-burn-fee-for-adding-tags");
    }

    for (auto entry: mapNullDataTxCount) {
        if (entry.first.first.front() == RESTRICTED_CHAR) {
            std::string ownerToken = entry.first.first.substr(1,  entry.first.first.size()); // $TOKEN into TOKEN
            if (!setAssetTransferNames.count(ownerToken + OWNER_TAG)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-contains-restricted-asset-null-tx-without-asset-transfer");
            }
        } else { // must be a qualifier asset QUALIFIER_CHAR
            if (!setAssetTransferNames.count(entry.first.first)) {
                return state.DoS(100, false, REJECT_INVALID,
                                 "bad-txns-tx-contains-qualifier-asset-null-tx-without-asset-transfer");
            }
        }
    }

    for (auto name: setNullGlobalAssetChanges) {
        if (name.size() == 0)
            return state.DoS(100, false, REJECT_INVALID,"bad-txns-tx-contains-global-asset-null-tx-with-null-asset-name");

        std::string rootName = name.substr(1,  name.size()); // $TOKEN into TOKEN
        if (!setAssetTransferNames.count(rootName + OWNER_TAG)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-contains-global-asset-null-tx-without-asset-transfer");
        }
    }

    /** RVN END */

    if (fCheckDuplicateInputs) {
        std::set<COutPoint> vInOutPoints;
        for (const auto& txin : tx.vin)
        {
            if (!vInOutPoints.insert(txin.prevout).second)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        }
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");

        if (AreCoinbaseCheckAssetsDeployed()) {
            for (auto vout : tx.vout) {
                if (vout.scriptPubKey.IsAssetScript() || vout.scriptPubKey.IsNullAsset()) {
                    return state.DoS(0, error("%s: coinbase contains asset transaction", __func__),
                                     REJECT_INVALID, "bad-txns-coinbase-contains-asset-txes");
                }
            }
        }
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    /** RVN START */
    if (tx.IsNewAsset()) {
        /** Verify the reissue assets data */
        std::string strError = "";
        if(!tx.VerifyNewAsset(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CNewAsset asset;
        std::string strAddress;
        if (!AssetFromTransaction(tx, asset, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-asset-from-transaction");

        // Validate the new assets information
        if (!IsNewOwnerTxValid(tx, asset.strName, strAddress, strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        if(!CheckNewAsset(asset, strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

    } else if (tx.IsReissueAsset()) {

        /** Verify the reissue assets data */
        std::string strError;
        if (!tx.VerifyReissueAsset(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CReissueAsset reissue;
        std::string strAddress;
        if (!ReissueAssetFromTransaction(tx, reissue, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-asset");

        if (!CheckReissueAsset(reissue, strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        // Get the assetType
        AssetType type;
        IsAssetNameValid(reissue.strName, type);

        // If this is a reissuance of a restricted asset, mark it as such, so we can check to make sure only valid verifier string tx are added to the chain
        if (type == AssetType::RESTRICTED) {
            CNullAssetTxVerifierString new_verifier;
            bool fNotFound = false;

            // Try and get the verifier string if it was changed
            if (!tx.GetVerifierStringFromTx(new_verifier, strError, fNotFound)) {
                // If it return false for any other reason besides not being found, fail the transaction check
                if (!fNotFound) {
                    return state.DoS(100, false, REJECT_INVALID,
                                     "bad-txns-reissue-restricted-verifier-" + strError);
                }
            }

            fContainsRestrictedAssetReissue = true;
        }

    } else if (tx.IsNewUniqueAsset()) {

        /** Verify the unique assets data */
        std::string strError = "";
        if (!tx.VerifyNewUniqueAsset(strError)) {
            return state.DoS(100, false, REJECT_INVALID, strError);
        }


        for (auto out : tx.vout)
        {
            if (IsScriptNewUniqueAsset(out.scriptPubKey))
            {
                CNewAsset asset;
                std::string strAddress;
                if (!AssetFromScript(out.scriptPubKey, asset, strAddress))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-check-transaction-issue-unique-asset-serialization");

                if (!CheckNewAsset(asset, strError))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-unique" + strError);
            }
        }
    } else if (tx.IsNewMsgChannelAsset()) {
        /** Verify the msg channel assets data */
        std::string strError = "";
        if(!tx.VerifyNewMsgChannelAsset(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CNewAsset asset;
        std::string strAddress;
        if (!MsgChannelAssetFromTransaction(tx, asset, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-from-transaction");

        if (!CheckNewAsset(asset, strError))
            return state.DoS(100, error("%s: %s", __func__, strError), REJECT_INVALID, "bad-txns-issue-msgchannel" + strError);

    } else if (tx.IsNewQualifierAsset()) {
        /** Verify the qualifier channel assets data */
        std::string strError = "";
        if(!tx.VerifyNewQualfierAsset(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CNewAsset asset;
        std::string strAddress;
        if (!QualifierAssetFromTransaction(tx, asset, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-from-transaction");

        if (!CheckNewAsset(asset, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualfier" + strError);

    } else if (tx.IsNewRestrictedAsset()) {
        /** Verify the restricted assets data. */
        std::string strError = "";
        if(!tx.VerifyNewRestrictedAsset(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        // Get asset data
        CNewAsset asset;
        std::string strAddress;
        if (!RestrictedAssetFromTransaction(tx, asset, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-from-transaction");

        if (!CheckNewAsset(asset, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted" + strError);

        // Get verifier string
        CNullAssetTxVerifierString verifier;
        if (!tx.GetVerifierStringFromTx(verifier, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-verifier-search-" + strError);

        // Mark that this transaction has a restricted asset issuance, for checks later with the verifier string tx
        fContainsNewRestrictedAsset = true;
    }
    else {
        // Fail if transaction contains any non-transfer asset scripts and hasn't conformed to one of the
        // above transaction types.  Also fail if it contains OP_RVN_ASSET opcode but wasn't a valid script.
        for (auto out : tx.vout) {
            int nType;
            bool _isOwner;
            if (out.scriptPubKey.IsAssetScript(nType, _isOwner)) {
                if (nType != TX_TRANSFER_ASSET) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-asset-transaction");
                }
            } else {
                if (out.scriptPubKey.Find(OP_RVN_ASSET)) {
                    if (out.scriptPubKey[0] != OP_RVN_ASSET) {
                        return state.DoS(100, false, REJECT_INVALID,
                                         "bad-txns-op-rvn-asset-not-in-right-script-location");
                    }
                }
            }
        }
    }

    // Check to make sure that if there is a verifier string, that there is also a issue or reissuance of a restricted asset
    if (fContainsNullAssetVerifierTx && !fContainsRestrictedAssetReissue && !fContainsNewRestrictedAsset)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-cointains-verifier-string-without-restricted-asset-issuance-or-reissuance");

    // If there is a restricted asset issuance, verify that there is a verifier tx associated with it.
    if (fContainsNewRestrictedAsset && !fContainsNullAssetVerifierTx) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-cointains-restricted-asset-issuance-without-verifier");
    }

    // we allow restricted asset reissuance without having a verifier string transaction, we don't force it to be update
    /** RVN END */

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__), tx.GetHash());
    }

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(false,
                REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange", false, "", tx.GetHash());
        }
    }

    const CAmount value_out = tx.GetValueOut(AreEnforcedValuesDeployed());
    if (nValueIn < value_out) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)), tx.GetHash());
    }

    // Tally transaction fees
    const CAmount txfee_aux = nValueIn - value_out;
    if (!MoneyRange(txfee_aux)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-out-of-range", false, "", tx.GetHash());
    }

    txfee = txfee_aux;
    return true;
}

//! Check to make sure that the inputs and outputs CAmount match exactly.
bool Consensus::CheckTxAssets(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, CAssetsCache* assetCache, bool fCheckMempool, std::vector<std::pair<std::string, uint256> >& vPairReissueAssets, const bool fRunningUnitTests, std::set<CMessage>* setMessages, int64_t nBlocktime,   std::vector<std::pair<std::string, CNullAssetTxData>>* myNullAssetData)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missing-or-spent", false,
                         strprintf("%s: inputs missing/spent", __func__), tx.GetHash());
    }

    // Create map that stores the amount of an asset transaction input. Used to verify no assets are burned
    std::map<std::string, CAmount> totalInputs;

    std::map<std::string, std::string> mapAddresses;

    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        if (coin.IsAsset()) {
            CAssetOutputEntry data;
            if (!GetAssetData(coin.out.scriptPubKey, data))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-failed-to-get-asset-from-script", false, "", tx.GetHash());

            // Add to the total value of assets in the inputs
            if (totalInputs.count(data.assetName))
                totalInputs.at(data.assetName) += data.nAmount;
            else
                totalInputs.insert(make_pair(data.assetName, data.nAmount));

            if (AreMessagesDeployed()) {
                mapAddresses.insert(make_pair(data.assetName,EncodeDestination(data.destination)));
            }

            if (IsAssetNameAnRestricted(data.assetName)) {
                if (assetCache->CheckForAddressRestriction(data.assetName, EncodeDestination(data.destination), true)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-restricted-asset-transfer-from-frozen-address", false, "", tx.GetHash());
                }
            }
        }
    }

    // Create map that stores the amount of an asset transaction output. Used to verify no assets are burned
    std::map<std::string, CAmount> totalOutputs;
    int index = 0;
    int64_t currentTime = GetTime();
    std::string strError = "";
    int i = 0;
    for (const auto& txout : tx.vout) {
        i++;

        // Values are subject to change, by isAssetScript a few lines down.
        int nType = 0;
        int nScriptType = 0;
        int nStart = 0;
        bool fIsOwner = false;

        // False until BIP9 consensus activates P2SH for Assets.
        bool fP2Active = AreP2SHAssetsAllowed();

        // Returns true if operations on assets are found in the script.
        // It will also possibly change the values of the arguments, as they are passed by reference.
        bool fIsAsset = txout.scriptPubKey.IsAssetScript(nType, nScriptType, fIsOwner, nStart, fP2Active);

        if (assetCache) {
            if (fIsAsset && !AreAssetsDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-is-asset-and-asset-not-active");

            if (txout.scriptPubKey.IsNullAsset()) {
                if (!AreRestrictedAssetsDeployed())
                    return state.DoS(100, false, REJECT_INVALID,
                                     "bad-tx-null-asset-data-before-restricted-assets-activated");

                if (txout.scriptPubKey.IsNullAssetTxDataScript()) {
                    if (!ContextualCheckNullAssetTxOut(txout, assetCache, strError, myNullAssetData))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else if (txout.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript()) {
                    if (!ContextualCheckGlobalAssetTxOut(txout, assetCache, strError))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else if (txout.scriptPubKey.IsNullAssetVerifierTxDataScript()) {
                    if (!ContextualCheckVerifierAssetTxOut(txout, assetCache, strError))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else {
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-null-asset-data-unknown-type", false, "", tx.GetHash());
                }
            }
        }

        if (nType == TX_TRANSFER_ASSET) {
            CAssetTransfer transfer;
            std::string address = "";
            if (!TransferAssetFromScript(txout.scriptPubKey, transfer, address))
                return state.DoS(100, false, REJECT_INVALID, "bad-tx-asset-transfer-bad-deserialize", false, "",
                                 tx.GetHash());

            if (!ContextualCheckTransferAsset(assetCache, transfer, address, strError))
                return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());

            // Add to the total value of assets in the outputs
            if (totalOutputs.count(transfer.strName))
                totalOutputs.at(transfer.strName) += transfer.nAmount;
            else
                totalOutputs.insert(make_pair(transfer.strName, transfer.nAmount));

            if (!fRunningUnitTests) {
                if (IsAssetNameAnOwner(transfer.strName)) {
                    if (transfer.nAmount != OWNER_ASSET_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-owner-amount-was-not-1", false, "", tx.GetHash());
                } else {
                    // For all other types of assets, make sure they are sending the right type of units
                    CNewAsset asset;
                    if (!assetCache->GetAssetMetaDataIfExists(transfer.strName, asset))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-asset-not-exist", false, "", tx.GetHash());

                    if (asset.strName != transfer.strName)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-asset-database-corrupted", false, "", tx.GetHash());

                    if (!CheckAmountWithUnits(transfer.nAmount, asset.units))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-asset-amount-not-match-units", false, "", tx.GetHash());
                }
            }

            /** Get messages from the transaction, only used when getting called from ConnectBlock **/
            // Get the messages from the Tx unless they are expired
            if (AreMessagesDeployed() && fMessaging && setMessages) {
                if (IsAssetNameAnOwner(transfer.strName) || IsAssetNameAnMsgChannel(transfer.strName)) {
                    if (!transfer.message.empty()) {
                        if (transfer.nExpireTime == 0 || transfer.nExpireTime > currentTime) {
                            if (mapAddresses.count(transfer.strName)) {
                                if (mapAddresses.at(transfer.strName) == address) {
                                    COutPoint out(tx.GetHash(), index);
                                    CMessage message(out, transfer.strName, transfer.message,
                                                     transfer.nExpireTime, nBlocktime);
                                    setMessages->insert(message);
                                    LogPrintf("Got message: %s\n", message.ToString()); // TODO remove after testing
                                }
                            }
                        }
                    }
                }
            }
        } else if (nType == TX_REISSUE_ASSET) {
            CReissueAsset reissue;
            std::string address;
            if (!ReissueAssetFromScript(txout.scriptPubKey, reissue, address))
                return state.DoS(100, false, REJECT_INVALID, "bad-tx-asset-reissue-bad-deserialize", false, "", tx.GetHash());

            if (mapReissuedAssets.count(reissue.strName)) {
                if (mapReissuedAssets.at(reissue.strName) != tx.GetHash())
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-reissue-chaining-not-allowed", false, "", tx.GetHash());
            } else {
                vPairReissueAssets.emplace_back(std::make_pair(reissue.strName, tx.GetHash()));
            }
        }
        index++;
    }

    if (assetCache) {
        if (tx.IsNewAsset()) {
            // Get the asset type
            CNewAsset asset;
            std::string address;
            if (!AssetFromScript(tx.vout[tx.vout.size() - 1].scriptPubKey, asset, address)) {
                error("%s : Failed to get new asset from transaction: %s", __func__, tx.GetHash().GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-serialzation-failed", false, "", tx.GetHash());
            }

            AssetType assetType;
            IsAssetNameValid(asset.strName, assetType);

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, strError);

        } else if (tx.IsReissueAsset()) {
            CReissueAsset reissue_asset;
            std::string address;
            if (!ReissueAssetFromScript(tx.vout[tx.vout.size() - 1].scriptPubKey, reissue_asset, address)) {
                error("%s : Failed to get new asset from transaction: %s", __func__, tx.GetHash().GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-serialzation-failed", false, "", tx.GetHash());
            }
            if (!ContextualCheckReissueAsset(assetCache, reissue_asset, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewUniqueAsset()) {
            if (!ContextualCheckUniqueAssetTx(assetCache, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-unique-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewMsgChannelAsset()) {
            if (!AreMessagesDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-before-messaging-is-active", false, "", tx.GetHash());

            CNewAsset asset;
            std::string strAddress;
            if (!MsgChannelAssetFromTransaction(tx, asset, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, error("%s: %s", __func__, strError), REJECT_INVALID,
                                 "bad-txns-issue-msgchannel-contextual-" + strError);
        } else if (tx.IsNewQualifierAsset()) {
            if (!AreRestrictedAssetsDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-before-it-is-active", false, "", tx.GetHash());

            CNewAsset asset;
            std::string strAddress;
            if (!QualifierAssetFromTransaction(tx, asset, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualfier-contextual" + strError, false, "", tx.GetHash());

        } else if (tx.IsNewRestrictedAsset()) {
            if (!AreRestrictedAssetsDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-before-it-is-active", false, "", tx.GetHash());

            // Get asset data
            CNewAsset asset;
            std::string strAddress;
            if (!RestrictedAssetFromTransaction(tx, asset, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewAsset(assetCache, asset, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-contextual" + strError, false, "", tx.GetHash());

            // Get verifier string
            CNullAssetTxVerifierString verifier;
            if (!tx.GetVerifierStringFromTx(verifier, strError))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-verifier-search-" + strError, false, "", tx.GetHash());

            // Check the verifier string against the destination address
            if (!ContextualCheckVerifierString(assetCache, verifier.verifier_string, strAddress, strError))
                return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());

        } else {
            for (auto out : tx.vout) {
                int nType;
                int nScriptType;
                bool _isOwner;
                if (out.scriptPubKey.IsAssetScript(nType, nScriptType, _isOwner)) {
                    if (nType != TX_TRANSFER_ASSET) {
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-asset-transaction", false, "", tx.GetHash());
                    }
                } else {
                    if (out.scriptPubKey.Find(OP_RVN_ASSET)) {
                        if (AreRestrictedAssetsDeployed()) {
                            if (out.scriptPubKey[0] != OP_RVN_ASSET) {
                                return state.DoS(100, false, REJECT_INVALID,
                                                 "bad-txns-op-rvn-asset-not-in-right-script-location", false, "", tx.GetHash());
                            }
                        } else {
                            return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-asset-script", false, "", tx.GetHash());
                        }
                    }
                }
            }
        }
    }

    for (const auto& outValue : totalOutputs) {
        if (!totalInputs.count(outValue.first)) {
            std::string errorMsg;
            errorMsg = strprintf("Bad Transaction - Trying to create outpoint for asset that you don't have: %s", outValue.first);
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-inputs-outputs-mismatch " + errorMsg, false, "", tx.GetHash());
        }

        if (totalInputs.at(outValue.first) != outValue.second) {
            std::string errorMsg;
            errorMsg = strprintf("Bad Transaction - Assets would be burnt %s", outValue.first);
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-inputs-outputs-mismatch " + errorMsg, false, "", tx.GetHash());
        }
    }

    // Check the input size and the output size
    if (totalOutputs.size() != totalInputs.size()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-asset-inputs-size-does-not-match-outputs-size", false, "", tx.GetHash());
    }
    return true;
}
