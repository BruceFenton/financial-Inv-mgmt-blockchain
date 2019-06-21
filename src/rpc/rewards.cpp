// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "assets/assetdb.h"
#include "assets/messages.h"
#include "assets/messagedb.h"
#include <map>
#include "tinyformat.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "httpserver.h"
#include "validation.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "assets/rewardrequestdb.h"
#include "assets/assetsnapshotdb.h"
#include "assets/payoutdb.h"

CAmount AmountFromValue(bool p_isRVN, const UniValue& p_value);

//  Transfer the specified amount of the asset from the source to the target
bool InitiateTransfer(
    CWallet * const p_walletPtr, const std::string & p_src,
    std::vector<CPayment> & p_payments,
    UniValue & p_batchResult);

UniValue schedulereward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 3)
        throw std::runtime_error(
                "schedulereward total_payout_amount \"funding_asset\" \"target_asset\" ( \"exception_addresses\" )\n"
                "\nSchedules a reward for the specified amount, using either RVN or the specified source asset name,\n"
                "\tto all owners of the specified asset, excluding the exception addresses.\n"

                "\nArguments:\n"
                "total_payout_amount: (number, required) The amount of the source asset to distribute amongst owners of the target asset\n"
                "funding_asset: (string, required) Either RVN or the asset name to distribute as the reward\n"
                "target_asset: (string, required) The asset name to whose owners the reward will be paid\n"
                "exception_addresses: (comma-delimited string, optional) A list of exception addresses that should not receive rewards\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  snapshot_height: (number),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("schedulereward", "100 \"RVN\" \"TRONCO\"")
                + HelpExampleRpc("schedulereward", "1000 \"BLACKCO\" \"TRONCO\" \"RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H,RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Figure out which wallet to use
    CWallet * const walletPtr = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(walletPtr, request.fHelp)) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system requires a wallet.");
        return ret;
    }

    ObserveSafeMode();
    LOCK2(cs_main, walletPtr->cs_wallet);

    EnsureWalletIsUnlocked(walletPtr);

    //  Extract parameters
    std::string funding_asset = request.params[1].get_str();

    CAmount total_payout_amount = AmountFromValue(
        (funding_asset == "RVN"), request.params[0]);
    if (total_payout_amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount to reward");

    std::string target_asset_name = request.params[2].get_str();
    std::string exception_addresses;
    if (request.params.size() > 3) {
        exception_addresses = request.params[3].get_str();
    }

    AssetType fndAssetType;
    AssetType tgtAssetType;

    if (funding_asset != "RVN") {
        if (!IsAssetNameValid(funding_asset, fndAssetType))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid funding_asset: Please use a valid funding_asset"));

        if (fndAssetType == AssetType::UNIQUE || fndAssetType == AssetType::OWNER || fndAssetType == AssetType::MSGCHANNEL)
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));
    }

    if (!IsAssetNameValid(target_asset_name, tgtAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid target_asset_name: Please use a valid target_asset_name"));

    if (tgtAssetType == AssetType::UNIQUE || tgtAssetType == AssetType::OWNER || tgtAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));

    const int64_t FUTURE_BLOCK_HEIGHT_OFFSET = 61; //  Select to hopefully be far enough forward to be safe from forks

    //  Build our reward record for scheduling
    boost::uuids::random_generator uuidGenerator;
    boost::uuids::uuid rewardUUID = uuidGenerator();
    CRewardRequest entryToAdd;
    entryToAdd.rewardID = to_string(rewardUUID);
    entryToAdd.walletName = walletPtr->GetName();
    entryToAdd.heightForPayout = chainActive.Height() + FUTURE_BLOCK_HEIGHT_OFFSET;
    entryToAdd.totalPayoutAmt = total_payout_amount;
    entryToAdd.tgtAssetName = target_asset_name;
    entryToAdd.payoutSrc = funding_asset;
    entryToAdd.exceptionAddresses = exception_addresses;

    if (pRewardRequestDb->SchedulePendingReward(entryToAdd)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("reward_id", entryToAdd.rewardID));
        obj.push_back(Pair("snapshot_height", entryToAdd.heightForPayout));

        return obj;
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to add scheduled reward to database"));
}

UniValue getreward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "getreward \"reward_id\"\n"
                "\nRetrieves the specified reward request details.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward that will be returned\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  wallet_name: (string),\n"
                "  payout_height: (number),\n"
                "  total_amount: (number),\n"
                "  target_asset: (string),\n"
                "  funding_asset: (string),\n"
                "  exception_addresses: (string),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("getreward", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Extract parameters
    std::string rewardID = request.params[0].get_str();

    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified reward
    CRewardRequest rewardEntry;

    if (pRewardRequestDb->RetrieveRewardWithID(rewardID, rewardEntry)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("reward_id", rewardEntry.rewardID));
        obj.push_back(Pair("wallet_name", rewardEntry.walletName));
        obj.push_back(Pair("payout_height", rewardEntry.heightForPayout));
        obj.push_back(Pair("total_amount", rewardEntry.totalPayoutAmt));
        obj.push_back(Pair("target_asset", rewardEntry.tgtAssetName));
        obj.push_back(Pair("funding_asset", rewardEntry.payoutSrc));
        obj.push_back(Pair("exception_addresses", rewardEntry.exceptionAddresses));

        return obj;
    }
    else {
        LogPrintf("Failed to retrieve specified reward '%s'!\n", rewardID.c_str());
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to retrieve specified reward"));
}

UniValue cancelreward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "cancelreward \"reward_id\"\n"
                "\nCancels the specified reward request.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward that will be cancelled\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  reward_status: (string),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("cancelreward", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Extract parameters
    std::string rewardID = request.params[0].get_str();

    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified reward
    if (pRewardRequestDb->RemoveReward(rewardID)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("reward_id", rewardID));
        obj.push_back(Pair("reward_status", "Removed"));

        return obj;
    }
    else {
        LogPrintf("Failed to cancel specified reward '%s'!\n", rewardID.c_str());
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to remove specified reward"));
}

UniValue calculatepayments(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "calculatepayments \"reward_id\"\n"
                "\nGenerates payment records for the specified reward ID.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward that will be calculated\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  target_asset: (string),\n"
                "  funding_asset: (string),\n"
                "  payout_height: (number),\n"
                "  payouts: [\n"
                "    {\n"
                "      address: (string),\n"
                "      payout_amount: (number),\n"
                "    }\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("calculatepayments", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Figure out which wallet to use
    CWallet * const walletPtr = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(walletPtr, request.fHelp)) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system requires a wallet.");
        return ret;
    }

    ObserveSafeMode();
    LOCK2(cs_main, walletPtr->cs_wallet);

    EnsureWalletIsUnlocked(walletPtr);

    //  Extract parameters
    std::string rewardID = request.params[0].get_str();

    if (!passetsdb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Assets database is not setup. Please restart wallet to try again"));
    if (!pAssetSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Asset Snapshot database is not setup. Please restart wallet to try again"));
    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));
    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified reward
    CRewardRequest rewardEntry;

    if (pRewardRequestDb->RetrieveRewardWithID(rewardID, rewardEntry)) {
        //  Retrieve the asset snapshot entry for the target asset at the specified height
        CAssetSnapshotDBEntry snapshotEntry;

        if (pAssetSnapshotDb->RetrieveOwnershipSnapshot(rewardEntry.tgtAssetName, rewardEntry.heightForPayout, snapshotEntry)) {
            //  Generate payment transactions and store in the payments DB
            CPayoutDBEntry payoutEntry;
            if (!pPayoutDb->GeneratePayouts(rewardEntry, snapshotEntry, payoutEntry)) {
                LogPrintf("Failed to generate payouts for reward '%s'!\n", rewardEntry.rewardID.c_str());                
            }
            else {
                UniValue obj(UniValue::VOBJ);

                obj.push_back(Pair("reward_id", rewardEntry.rewardID));
                obj.push_back(Pair("target_asset", rewardEntry.tgtAssetName));
                obj.push_back(Pair("funding_asset", rewardEntry.payoutSrc));
                obj.push_back(Pair("payout_height", rewardEntry.heightForPayout));

                UniValue entries(UniValue::VARR);
                for (auto const & payment : payoutEntry.payments) {
                    UniValue entry(UniValue::VOBJ);

                    entry.push_back(Pair("address", payment.address));
                    entry.push_back(Pair("payout_amount", payment.payoutAmt));

                    entries.push_back(entry);
                }

                obj.push_back(Pair("payouts", entries));

                return obj;
            }
        }
        else {
            LogPrintf("Failed to retrieve ownership snapshot for '%s' at height %d!\n",
                rewardEntry.tgtAssetName.c_str(), rewardEntry.heightForPayout);                
        }
    }
    else {
        LogPrintf("Failed to retrieve specified reward '%s'!\n", rewardID.c_str());
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to calculate payments specified reward"));
}

UniValue getpayments(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "getpayments \"reward_id\"\n"
                "\nRetrieves payment records for the specified reward ID.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward that will be retrieved\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  target_asset: (string),\n"
                "  funding_asset: (string),\n"
                "  payout_height: (number),\n"
                "  payouts: [\n"
                "    {\n"
                "      address: (string),\n"
                "      payout_amount: (number),\n"
                "    }\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("getpayments", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Extract parameters
    std::string rewardID = request.params[0].get_str();

    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified payout entry
    CPayoutDBEntry payoutEntry;

    if (pPayoutDb->RetrievePayoutEntry(rewardID, payoutEntry)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("reward_id", payoutEntry.rewardID));
        obj.push_back(Pair("target_asset", payoutEntry.assetName));
        obj.push_back(Pair("funding_asset", payoutEntry.srcAssetName));

        UniValue entries(UniValue::VARR);
        for (auto const & payment : payoutEntry.payments) {
            UniValue entry(UniValue::VOBJ);

            entry.push_back(Pair("address", payment.address));
            entry.push_back(Pair("payout_amount", payment.payoutAmt));

            entries.push_back(entry);
        }

        obj.push_back(Pair("payouts", entries));

        return obj;
    }
    else {
        LogPrintf("Failed to retrieve payment set for reward '%s'!\n", rewardID.c_str());
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to calculate payments specified reward"));
}

UniValue cancelpayments(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "cancelpayments \"reward_id\"\n"
                "\nRemoves payment records for the specified reward ID.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward whose payments will be removed\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  payment_status: (string),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("cancelpayments", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Extract parameters
    std::string rewardID = request.params[0].get_str();

    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified payout entry
    CPayoutDBEntry payoutEntry;

    if (pPayoutDb->RemovePayoutEntry(rewardID)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("reward_id", rewardID));
        obj.push_back(Pair("payment_status", "Removed"));

        return obj;
    }
    else {
        LogPrintf("Failed to retrieve payment set for reward '%s'!\n", rewardID.c_str());
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to calculate payments specified reward"));
}

UniValue executepayments(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "executepayments \"reward_id\"\n"
                "\nGenerates transactions for all payment records tied to the specified reward.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward for which transactions will be generated\n"

                "\nResult:\n"
                "{\n"
                "  reward_id: (string),\n"
                "  batch_results: [\n"
                "    {\n"
                "      transaction_id: (string),\n"
                "      result: (string),\n"
                "      expected_count: (number),\n"
                "      actual_count: (number),\n"
                "    }\n"
                "  payout_db_update: (string),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("executepayments", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Figure out which wallet to use
    CWallet * const walletPtr = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(walletPtr, request.fHelp)) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system requires a wallet.");
        return ret;
    }

    ObserveSafeMode();
    LOCK2(cs_main, walletPtr->cs_wallet);

    EnsureWalletIsUnlocked(walletPtr);

    //  Extract parameters
    std::string rewardID = request.params[0].get_str();

    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve all scheduled payouts for the target asset
    CPayoutDBEntry payoutEntry;
    if (!pPayoutDb->RetrievePayoutEntry(rewardID, payoutEntry)) {
        LogPrintf("Failed to retrieve payout entry for reward '%s'!\n",
            rewardID.c_str());                
    }
    else {
        UniValue responseObj(UniValue::VOBJ);

        responseObj.push_back(Pair("reward_id", payoutEntry.rewardID));

        //
        //  Loop through the payout addresses and process them in batches
        //
        const int MAX_PAYMENTS_PER_TRANSACTION = 50;
        UniValue batchResults(UniValue::VARR);
        bool atLeastOneTxnSucceeded = false;
        std::vector<CPayment> paymentVector;
        std::set<CPayment> updatedPayments;

        for (auto & payment : payoutEntry.payments) {
            paymentVector.push_back(payment);

            //  Issue a transaction if we've hit the max payment count
            if (paymentVector.size() >= MAX_PAYMENTS_PER_TRANSACTION) {
                UniValue batchResult(UniValue::VOBJ);

                //  Build a transaction for the current batch
                if (InitiateTransfer(walletPtr, payoutEntry.srcAssetName, paymentVector, batchResult)) {
                    atLeastOneTxnSucceeded = true;
                }
                else {
                    LogPrintf("Transaction generation failed for '%s' using source '%s'!\n",
                        payoutEntry.assetName.c_str(), payoutEntry.srcAssetName.c_str());
                }

                batchResults.push_back(batchResult);

                //  Move the updated payments into a new set
                for (auto const & payment : paymentVector) {
                    updatedPayments.insert(payment);
                }

                //  Clear the vector after a batch is processed
                paymentVector.clear();
            }
        }

        //
        //  If any payments are left in the last batch, send them
        //
        if (paymentVector.size() > 0) {
            UniValue batchResult(UniValue::VOBJ);

            //  Build a transaction for the current batch
            if (InitiateTransfer(walletPtr, payoutEntry.srcAssetName, paymentVector, batchResult)) {
                atLeastOneTxnSucceeded = true;
            }
            else {
                LogPrintf("Transaction generation failed for '%s' using source '%s'!\n",
                    payoutEntry.assetName.c_str(), payoutEntry.srcAssetName.c_str());
            }

            batchResults.push_back(batchResult);

            //  Move the updated payments into a new set
            for (auto const & payment : paymentVector) {
                updatedPayments.insert(payment);
            }

            //  Clear the vector after a batch is processed
            paymentVector.clear();
        }

        //  Replace the existing set of payments with the updated one
        payoutEntry.payments.clear();
        for (auto const & payment : updatedPayments) {
            payoutEntry.payments.insert(payment);
        }
        updatedPayments.clear();

        responseObj.push_back(Pair("batch_results", batchResults));

        //  Write the payment back to the database if anything succeded
        if (atLeastOneTxnSucceeded) {
            if (pPayoutDb->UpdatePayoutEntry(payoutEntry)) {
                responseObj.push_back(Pair("payout_db_update", "succeeded"));
            }
            else {
                LogPrintf("Failed to update payout DB payment status for reward '%s'!\n",
                    payoutEntry.rewardID.c_str());
                responseObj.push_back(Pair("payout_db_update", "failed"));
            }
        }

        return responseObj;
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to execute payments specified reward"));
}

static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            {   "rewards",      "schedulereward",             &schedulereward,             {"total_payout_amount", "payout_source", "target_asset_name", "exception_addresses"}},
            {   "rewards",      "getreward",                  &getreward,                  {"reward_id"}},
            {   "rewards",      "cancelreward",               &cancelreward,               {"reward_id"}},
            {   "rewards",      "calculatepayments",          &calculatepayments,          {"reward_id"}},
            {   "rewards",      "getpayments",                &getpayments,                {"reward_id"}},
            {   "rewards",      "cancelpayments",             &cancelpayments,             {"reward_id"}},
            {   "rewards",      "executepayments",            &executepayments,            {"reward_id"}},
    };

void RegisterRewardsRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

CAmount AmountFromValue(bool p_isRVN, const UniValue& p_value)
{
    if (!p_value.isNum() && !p_value.isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");

    CAmount amount;
    if (!ParseFixedPoint(p_value.getValStr(), 8, &amount))
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Invalid amount (3): %s", p_value.getValStr()));

    if (p_isRVN && !MoneyRange(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Amount out of range: %s", amount));

    return amount;
}

bool InitiateTransfer(
    CWallet * const p_walletPtr, const std::string & p_src,
    std::vector<CPayment> & p_payments,
    UniValue & p_batchResult)
{
    bool fcnRetVal = false;
    std::string transactionID;
    size_t expectedCount = 0;
    size_t actualCount = 0;

    LogPrintf("Initiating batch transfer...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Transfer the specified amount of the asset from the source to the target
        CCoinControl ctrl;
        CWalletTx transaction;

        //  Handle payouts using RVN differently from those using an asset
        if (p_src == "RVN") {
            // Check amount
            CAmount curBalance = p_walletPtr->GetBalance();

            if (p_walletPtr->GetBroadcastTransactions() && !g_connman) {
                LogPrintf("Error: Peer-to-peer functionality missing or disabled\n");
                break;
            }

            std::vector<CRecipient> vDestinations;
            CAmount totalPaymentAmt = 0;

            for (auto & payment : p_payments) {
                //  Have we already processed this payment?
                if (payment.completed) {
                    continue;
                }

                expectedCount++;

                // Parse Raven address
                CTxDestination dest = DecodeDestination(payment.address);
                if (!IsValidDestination(dest)) {
                    LogPrintf("Destination address '%s' is invalid.\n", payment.address.c_str());
                    payment.completed = true;
                    continue;
                }

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, payment.payoutAmt, false};
                vDestinations.emplace_back(recipient);

                totalPaymentAmt += payment.payoutAmt;
                actualCount++;
            }

            //  Verify funds
            if (totalPaymentAmt > curBalance) {
                LogPrintf("Insufficient funds: total payment %lld > available balance %lld\n",
                    totalPaymentAmt, curBalance);
                break;
            }

            // Create and send the transaction
            CReserveKey reservekey(p_walletPtr);
            CAmount nFeeRequired;
            std::string strError;
            int nChangePosRet = -1;

            if (!p_walletPtr->CreateTransaction(vDestinations, transaction, reservekey, nFeeRequired, nChangePosRet, strError, ctrl)) {
                if (totalPaymentAmt + nFeeRequired > curBalance)
                    strError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
                LogPrintf("%s\n", strError.c_str());
                break;
            }

            CValidationState state;
            if (!p_walletPtr->CommitTransaction(transaction, reservekey, g_connman.get(), state)) {
                LogPrintf("Error: The transaction was rejected! Reason given: %s\n", state.GetRejectReason());
                break;
            }

            transactionID = transaction.GetHash().GetHex();
        }
        else {
            std::pair<int, std::string> error;
            std::vector< std::pair<CAssetTransfer, std::string> > vDestinations;

            for (auto & payment : p_payments) {
                //  Have we already processed this payment?
                if (payment.completed) {
                    continue;
                }

                expectedCount++;

                vDestinations.emplace_back(std::make_pair(
                    CAssetTransfer(p_src, payment.payoutAmt, DecodeAssetData(""), 0), payment.address));

                actualCount++;
            }

            CReserveKey reservekey(p_walletPtr);
            CAmount nRequiredFee;

            // Create the Transaction
            if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vDestinations, "", error, transaction, reservekey, nRequiredFee)) {
                LogPrintf("Failed to create transfer asset transaction: %s\n", error.second.c_str());
                break;
            }

            // Send the Transaction to the network
            if (!SendAssetTransaction(p_walletPtr, transaction, reservekey, error, transactionID)) {
                LogPrintf("Failed to send asset transaction: %s\n", error.second.c_str());
                break;
            }
        }

        //  Indicate success
        fcnRetVal = true;
        p_batchResult.push_back(Pair("transaction_id", transactionID));

        //  Post-process the payments in the batch to flag them as completed
        for (auto & payment : p_payments) {
            payment.completed = true;
        }
    } while (false);

    LogPrintf("Batch transfer processing %s.\n",
        fcnRetVal ? "succeeded" : "failed");
    p_batchResult.push_back(Pair("result", fcnRetVal ? "Succeeded" : "Failed"));
    p_batchResult.push_back(Pair("expected_count", expectedCount));
    p_batchResult.push_back(Pair("actual_count", actualCount));

    return fcnRetVal;
}
