// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "transactionview.h"
#include "assets/assets.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "ravenunits.h"
#include "timedata.h"
#include "wallet/wallet.h"
#include "core_io.h"

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}


/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    
    /** RVN START */
    if(isSwapTransaction(wallet, wtx, parts, nCredit, nDebit, nNet))
        return parts;
    /** RVN END */
    if (nNet > 0 || wtx.IsCoinBase())
    {
        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wallet->IsMine(txout);

            /** RVN START */
            if (txout.scriptPubKey.IsAssetScript() || txout.scriptPubKey.IsNullAssetTxDataScript() || txout.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript())
                continue;
            /** RVN END */

            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Raven Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = EncodeDestination(address);
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.IsCoinBase())
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    }
    else
    {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.tx->vin)
        {
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (const CTxOut& txout : wtx.tx->vout)
        {
            /** RVN START */
            if (txout.scriptPubKey.IsAssetScript() || txout.scriptPubKey.IsNullAssetTxDataScript() || txout.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript())
                continue;
            /** RVN END */

            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            CAmount nChange = wtx.GetChange();

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                            -(nDebit - nChange), nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut(AreEnforcedValuesDeployed());

            for (unsigned int nOut = 0; nOut < wtx.tx->vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.tx->vout[nOut];

                /** RVN START */
                if (txout.scriptPubKey.IsAssetScript())
                    continue;
                /** RVN END */

                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wallet->IsMine(txout))
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to Raven Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = EncodeDestination(address);
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //


            /** RVN START */
            // We will only show mixed debit transactions that are nNet < 0 or if they are nNet == 0 and
            // they do not contain assets. This is so the list of transaction doesn't add 0 amount transactions to the
            // list.
            bool fIsMixedDebit = true;
            if (nNet == 0) {
                for (unsigned int nOut = 0; nOut < wtx.tx->vout.size(); nOut++) {
                    const CTxOut &txout = wtx.tx->vout[nOut];

                    if (txout.scriptPubKey.IsAssetScript() || txout.scriptPubKey.IsNullAssetTxDataScript() || txout.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript()) {
                        fIsMixedDebit = false;
                        break;
                    }
                }
            }

            if (fIsMixedDebit) {
                parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
                parts.last().involvesWatchAddress = involvesWatchAddress;
            }
            /** RVN END */
        }
    }


    /** RVN START */
    if (AreAssetsDeployed()) {
        CAmount nFee;
        std::string strSentAccount;
        std::list<COutputEntry> listReceived;
        std::list<COutputEntry> listSent;

        std::list<CAssetOutputEntry> listAssetsReceived;
        std::list<CAssetOutputEntry> listAssetsSent;

        wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, ISMINE_ALL, listAssetsReceived, listAssetsSent);

        //LogPrintf("TXID: %s: Rec: %d Sent: %d\n", wtx.GetHash().ToString(), listAssetsReceived.size(), listAssetsSent.size());

        if (listAssetsReceived.size() > 0)
        {
            for (const CAssetOutputEntry &data : listAssetsReceived)
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = data.vout;

                const CTxOut& txout = wtx.tx->vout[sub.idx];
                isminetype mine = wallet->IsMine(txout);

                sub.address = EncodeDestination(data.destination);
                sub.assetName = data.assetName;
                sub.credit = data.nAmount;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;

                if (data.type == TX_NEW_ASSET)
                    sub.type = TransactionRecord::Issue;
                else if (data.type == TX_REISSUE_ASSET)
                    sub.type = TransactionRecord::Reissue;
                else if (data.type == TX_TRANSFER_ASSET)
                    sub.type = TransactionRecord::TransferFrom;
                else {
                    sub.type = TransactionRecord::Other;
                }

                sub.units = DEFAULT_UNITS;

                if (IsAssetNameAnOwner(sub.assetName))
                    sub.units = OWNER_UNITS;
                else if (CheckIssueDataTx(wtx.tx->vout[sub.idx]))
                {
                    CNewAsset asset;
                    std::string strAddress;
                    if (AssetFromTransaction(wtx, asset, strAddress))
                        sub.units = asset.units;
                }
                else
                {
                    CNewAsset asset;
                    if (passets->GetAssetMetaDataIfExists(sub.assetName, asset))
                        sub.units = asset.units;
                }

                parts.append(sub);
            }
        }

        if (listAssetsSent.size() > 0)
        {
            for (const CAssetOutputEntry &data : listAssetsSent)
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = data.vout;
                sub.address = EncodeDestination(data.destination);
                sub.assetName = data.assetName;
                sub.credit = -data.nAmount;
                sub.involvesWatchAddress = false;

                if (data.type == TX_TRANSFER_ASSET)
                    sub.type = TransactionRecord::TransferTo;
                else
                    sub.type = TransactionRecord::Other;

                if (IsAssetNameAnOwner(sub.assetName))
                    sub.units = OWNER_UNITS;
                else if (CheckIssueDataTx(wtx.tx->vout[sub.idx]))
                {
                    CNewAsset asset;
                    std::string strAddress;
                    if (AssetFromTransaction(wtx, asset, strAddress))
                        sub.units = asset.units;
                }
                else
                {
                    CNewAsset asset;
                    if (passets->GetAssetMetaDataIfExists(sub.assetName, asset))
                        sub.units = asset.units;
                }

                parts.append(sub);
            }
        }
    }
    /** RVN END */

    return parts;
}

bool TransactionRecord::isSwapTransaction(const CWallet *wallet, const CWalletTx &wtx, QList<TransactionRecord> &txRecords, const CAmount& nCredit, const CAmount& nDebit, const CAmount& nNet)
{
    if(!AreAssetsDeployed()) return false;

    bool isSwap = false;
    std::map<std::string, std::string> mapValue = wtx.mapValue;
    for(size_t i=0; i < wtx.tx->vin.size(); i++)
    {
        auto tx_vin = wtx.tx->vin[i];

        std::string vin_script = ScriptToAsmStr(tx_vin.scriptSig, true);
        auto fIsSingleSign = vin_script.find("[SINGLE|ANYONECANPAY]") != std::string::npos;
        isminetype mine = wallet->IsMine(tx_vin);

        if(fIsSingleSign)
        {
            //There will always be a corresponding vout with the same index when SINGLE|ANYONECANPAY is used
            auto swap_vout = wtx.tx->vout[i]; //The vout here represents what was recieved in the trade

            TransactionRecord sub(wtx.GetHash(), wtx.GetTxTime());
            sub.idx = i;
            
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            CTxDestination destination;
            ExtractDestination(swap_vout.scriptPubKey, destination);
            sub.address = EncodeDestination(destination);

            bool fSentAssets = false;
            bool fRecvAssets = false;

            CTxOut myProvidedInput;
            CTxOut myReceievedOutput;

            if(mine)
            {
                //If this is our SINGLE|ANYONECANPAY section, that means this was executed by another party
                //Lookup previous transaction (*OF OURS*) in wallet history for full vout info
                auto it = wallet->mapWallet.find(tx_vin.prevout.hash);
                bool missing = (it == wallet->mapWallet.end());
                CWalletTx my_input_vout_tx;
                CTxOut my_input_vout;
                if (missing) {
                    LogPrintf("\tUnable to cross-refrerence historical transaction\n");
                } else {
                    my_input_vout_tx = it->second;
                    my_input_vout = my_input_vout_tx.tx->vout[tx_vin.prevout.n];
                }

                //We were sent assets in the output
                if(swap_vout.scriptPubKey.IsAssetScript())
                    fRecvAssets = true;

                //The vout we provided was for assets
                if(!missing && my_input_vout.scriptPubKey.IsAssetScript())
                    fSentAssets = true;

                myReceievedOutput = swap_vout;
                myProvidedInput = my_input_vout;
                sub.type = TransactionRecord::Swap;
            }
            else //We were the ones executing the transaction
            {
                //In this case, the counterparty received assets, so we sent them.
                if(swap_vout.scriptPubKey.IsAssetScript())
                    fSentAssets = true;

                //We can't directly check the counterparties vin here, so we have to look at the vouts to determine if we were sent assets or not
                for(const CTxOut &txout : wtx.tx->vout)
                {
                    if(wallet->IsMine(txout))
                    {
                        //If we sent assets, we need to see if we were sent assets or RVN in return
                        if(txout.scriptPubKey.IsAssetScript())
                        {
                            if(fSentAssets) { //Check to skip asset change by name
                                std::string sentType;CAmount sentAmount;
                                GetAssetInfoFromScript(swap_vout.scriptPubKey, sentType, sentAmount);
                                std::string recvType;CAmount recvAmount;
                                GetAssetInfoFromScript(txout.scriptPubKey, recvType, recvAmount);
                                if(sentType == recvType)
                                    continue;
                            }
                            fRecvAssets = true;
                            myReceievedOutput = txout;
                            break;
                        }
                        else //We got RVN
                        {
                            if(fSentAssets) { //If we sent assets, this is ours. but we need to adjust for change.
                                myReceievedOutput = txout;
                            } else {
                                //If we didn't send assets (buying), this is likely just change
                            }
                        }
                    }
                }

                myProvidedInput = swap_vout;
                sub.type = TransactionRecord::SwapExecute;
            }

            std::string sentType;CAmount sentAmount;
            std::string recvType;CAmount recvAmount;
            if(fSentAssets) GetAssetInfoFromScript(myProvidedInput.scriptPubKey, sentType, sentAmount);
            if(fRecvAssets) GetAssetInfoFromScript(myReceievedOutput.scriptPubKey, recvType, recvAmount);
            
            if(fSentAssets && fRecvAssets) {
                //Trade!
                //Amount represents the asset we sent, no matter the perspective
                sub.credit = recvAmount;
                std::string asset_qty_format = RavenUnits::formatWithCustomName(QString::fromStdString(sentType), sentAmount, 2).toUtf8().constData();
                sub.assetName = strprintf("%s (%s %s)", TransactionView::tr("Traded Away").toUtf8().constData(), recvType, asset_qty_format);
            } else if (fSentAssets) {
                //Sell!
                //Total price paid, need to use net calculation when we executed
                sub.credit = mine ? myReceievedOutput.nValue : nNet;
                std::string asset_qty_format = RavenUnits::formatWithCustomName(QString::fromStdString(sentType), sentAmount, 2).toUtf8().constData();
                sub.assetName = strprintf("RVN (%s %s)", TransactionView::tr("Sold").toUtf8().constData(), asset_qty_format);
            } else if (fRecvAssets) {
                //Buy!
                //Total price paid, need to use net calculation when we executed
                sub.credit = (mine ? -myProvidedInput.nValue : nNet);
                std::string asset_qty_format = RavenUnits::formatWithCustomName(QString::fromStdString(recvType), recvAmount, 2).toUtf8().constData();
                sub.assetName = strprintf("RVN (%s %s)", TransactionView::tr("Bought").toUtf8().constData(), asset_qty_format);
            } else {
                LogPrintf("\tFell Through!\n");
                return false; //!
            }

            txRecords.append(sub);
            isSwap = true;
        }
    }

    return isSwap;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();

    if (!CheckFinalTx(wtx))
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.tx->nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.tx->nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded() const
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate;
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
