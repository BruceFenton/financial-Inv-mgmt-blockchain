// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "atomicswapsdialog.h"
#include "ui_atomicswapsdialog.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "sendcoinsdialog.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "ravenunits.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "guiconstants.h"
#include "txmempool.h"

#include "wallet/coincontrol.h"
#include "consensus/validation.h"
#include "policy/fees.h"
#include "wallet/fees.h"

#include <script/standard.h>
#include <base58.h>
#include <validation.h> // mempool and minRelayTxFee
#include <wallet/wallet.h>
#include <core_io.h>
#include <policy/policy.h>
#include "assets/assettypes.h"
#include "assettablemodel.h"

#include <QGraphicsDropShadowEffect>
#include <QModelIndex>
#include <QDebug>
#include <QMessageBox>
#include <QClipboard>
#include <QSettings>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
#define QTversionPreFiveEleven
#endif

AtomicSwapsDialog::AtomicSwapsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
        QDialog(parent, Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint),
        ui(new Ui::AtomicSwapsDialog),
        platformStyle(_platformStyle)
{
    ui->setupUi(this);
    setWindowTitle("Create Assets");
    connect(ui->signedPartialText, SIGNAL(textChanged()), this, SLOT(onSignedPartialChanged()));
    connect(ui->executeSwapButton, SIGNAL(clicked()), this, SLOT(onExecuteSwap()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear(true)));
}

void AtomicSwapsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void AtomicSwapsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());

        // Setup the default values
        setUpValues();

        adjustSize();
    }
}

AtomicSwapsDialog::~AtomicSwapsDialog()
{
    delete ui;
}

/** Helper Methods */
void AtomicSwapsDialog::setUpValues()
{
    hideMessage();
    CheckFormState();
}

void AtomicSwapsDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                                 const CAmount& watchBalance, const CAmount& watchUnconfirmedBalance, const CAmount& watchImmatureBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchBalance);
    Q_UNUSED(watchUnconfirmedBalance);
    Q_UNUSED(watchImmatureBalance);

    ui->labelBalance->setFont(GUIUtil::getSubLabelFont());
    ui->label->setFont(GUIUtil::getSubLabelFont());

    if(model && model->getOptionsModel())
    {
        ui->labelBalance->setText(RavenUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
    }
}

void AtomicSwapsDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0);
}

void AtomicSwapsDialog::showMessage(QString string)
{
    ui->messageLabel->setStyleSheet("color: red; font-size: 15pt;font-weight: bold;");
    ui->messageLabel->setText(string);
    ui->messageLabel->show();
}

void AtomicSwapsDialog::showValidMessage(QString string)
{
    ui->messageLabel->setStyleSheet("color: green; font-size: 15pt;font-weight: bold;");
    ui->messageLabel->setText(string);
    ui->messageLabel->show();
}

void AtomicSwapsDialog::hideMessage()
{
    ui->messageLabel->hide();
}

void AtomicSwapsDialog::disableExecuteButton()
{
    ui->executeSwapButton->setDisabled(true);
}

void AtomicSwapsDialog::enableExecuteButton()
{
    ui->executeSwapButton->setDisabled(false);
}

void AtomicSwapsDialog::onExecuteSwap()
{
    if(this->validSwap)
    {
        CTransactionRef sentTx;
        QString errorMessage;
        if(!AttemptTransmit(*loadedSwap, sentTx, errorMessage))
        {
            //error :(
            Q_EMIT message(tr("Execute Failed"), tr("Swap failed to execute.\n%1").arg(errorMessage), 
                CClientUIInterface::MSG_ERROR);
        }
        else
        {
            //woot woot
            const uint256& tx_hash = loadedSwap->SignedFinal.GetHash();
            Q_EMIT message(tr("Execute Success"), tr("Swap executed succesfully.\ntxid: %1").arg(tx_hash.GetHex().c_str()), 
                CClientUIInterface::MSG_WARNING);
            clear(true);
        }
    }
}

bool AtomicSwapsDialog::AttemptTransmit(AtomicSwapDetails& result, CTransactionRef sentTx, QString& errorMessage)
{
    std::string address = EncodeDestination(result.SwapDestination);
    CAmount nFeeRequired = result.FeeTotal;

    // Format confirmation message
    QStringList formatted;

    // generate bold amount string
    formatted.append(result.GetSummary());

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if(nFeeRequired > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(RavenUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), nFeeRequired));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));

        // append transaction size
        questionString.append(" (" + QString::number((double)GetVirtualTransactionSize(result.SignedFinal) / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    if(result.Type != AtomicSwapType::Trade)
    {
        CAmount totalAmount = (result.Type == AtomicSwapType::Sell) ? result.ExpectedQuantity : result.ProvidedQuantity;
        QStringList alternativeUnits;
        for (RavenUnits::Unit u : RavenUnits::availableUnits())
        {
            if(u != model->getOptionsModel()->getDisplayUnit())
                alternativeUnits.append(RavenUnits::formatHtmlWithUnit(u, totalAmount));
        }
        questionString.append(tr("Total Amount %1")
                                      .arg(RavenUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));
        questionString.append(QString("<span style='font-size:10pt;font-weight:normal;'><br />(=%2)</span>")
                                      .arg(alternativeUnits.join(" " + tr("or") + "<br />")));
    }

    QString titleQuestion;
    switch(result.Type)
    {
        case AtomicSwapType::Buy: titleQuestion = tr("Confirm Sell Assets?"); break;
        case AtomicSwapType::Sell: titleQuestion = tr("Confirm Buy Assets?"); break;
        case AtomicSwapType::Trade: titleQuestion = tr("Confirm Trade Assets?"); break;
    }

    SendConfirmationDialog confirmationDialog(titleQuestion, 
                questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if(retval != QMessageBox::Yes)
    {
        return false;
    }

    sentTx = MakeTransactionRef(std::move(result.SignedFinal));
    const uint256& hashTx = sentTx->GetHash();
    CCoinsViewCache &view = *pcoinsTip;

    // now send the prepared transaction
    bool fHaveChain = false;
    for (size_t o = 0; !fHaveChain && o < sentTx->vout.size(); o++) {
        const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
        fHaveChain = !existingCoin.IsSpent();
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!::AcceptToMemoryPool(mempool, state, std::move(sentTx), &fMissingInputs,
                                nullptr /* plTxnReplaced */, false /* bypass_limits */, maxTxFee)) {
            if (state.IsInvalid()) {
                LogPrintf("%i: %s\n", state.GetRejectCode(), state.GetRejectReason().c_str());
                errorMessage = tr("%1: %2").arg(state.GetRejectCode()).arg(state.GetRejectReason().c_str());
                return false;
            } else {
                if (fMissingInputs) {
                    LogPrintf("Missing Inputs\n");
                    errorMessage = "Missing Inputs";
                    return false;
                }
                LogPrintf("Fail: %s\n", state.GetRejectReason().c_str());
                errorMessage = tr("Failed: %1").arg(state.GetRejectReason().c_str());
                return false;
            }
        } else {
            return true;
        }
    } else if (fHaveChain) {
        LogPrintf("Already on chain\n");
        errorMessage = tr("Transaction already sent.");
        return false;
    } else {
        LogPrintf("Already in mempool\n");
        errorMessage = tr("Transaction already in mempool.");
        return false;
    }
}

void AtomicSwapsDialog::CheckFormState()
{
    clear(false);
    
    if(ui->signedPartialText->toPlainText().isEmpty())
        return;

    QString errorMessage;
    this->loadedSwap = std::make_shared<AtomicSwapDetails>(ui->signedPartialText->toPlainText().toUtf8().constData());
     
    if (AttemptParseTransaction(*loadedSwap, errorMessage))
    {
        auto formattedProvided = RavenUnits::formatWithCustomName(loadedSwap->ProvidedType, loadedSwap->ProvidedQuantity, 2);
        auto formattedExpected = RavenUnits::formatWithCustomName(loadedSwap->ExpectedType, loadedSwap->ExpectedQuantity, 2);

        //This is from the perspective of the trade
        switch(loadedSwap->Type)
        {
            case AtomicSwapType::Buy:
            {
                ui->tradeTypeLabel->setText(tr("Buy Order (You are selling assets)"));
                ui->assetNameLabel->setText(formattedExpected);
                ui->totalPriceLabel->setText(QString("+") + formattedProvided); //Indicate we are getting money
                //Shows up as X.XXXXXX RVN/YYYYY
                auto formattedUnit = RavenUnits::formatWithCustomName(loadedSwap->ProvidedType, loadedSwap->UnitPrice);
                ui->unitPriceLabel->setText(QString("%1/[%2]").arg(formattedUnit).arg(loadedSwap->ExpectedType));
                ui->summaryLabel->setText(loadedSwap->GetSummary());
                break;
            }
            case AtomicSwapType::Sell:
            {
                ui->tradeTypeLabel->setText(tr("Sell Order (You are purchasing assets)"));
                ui->assetNameLabel->setText(formattedProvided);
                ui->totalPriceLabel->setText(formattedExpected);
                auto formattedUnit = RavenUnits::formatWithCustomName(loadedSwap->ExpectedType, loadedSwap->UnitPrice);
                ui->unitPriceLabel->setText(QString("%1/[%2]").arg(formattedUnit).arg(loadedSwap->ProvidedType));
                ui->summaryLabel->setText(loadedSwap->GetSummary());
                break;
            }
            case AtomicSwapType::Trade:
            {
                ui->tradeTypeLabel->setText(tr("Trade Order (You are trading assets for assets)"));
                ui->assetNameLabel->setText(formattedProvided);
                ui->totalPriceLabel->setText(formattedExpected);
                auto formattedUnit = RavenUnits::formatWithCustomName(loadedSwap->ProvidedType, loadedSwap->UnitPrice);
                ui->unitPriceLabel->setText(QString("%1/[%2]").arg(formattedUnit).arg(loadedSwap->ExpectedType));
                ui->summaryLabel->setText(loadedSwap->GetSummary());
                break;
            }
        }

        CMutableTransaction finalTX;
        if(AttemptCompleteTransaction(finalTX, *loadedSwap, errorMessage))
        {
            this->validSwap = true;

            loadedSwap.get()->SignedFinal = finalTX;

            showValidMessage("Valid!");
            enableExecuteButton();
        }
        else
        {
            this->validSwap = false;
            
            showMessage(tr("Unable to complete signed partial.\n%1").arg(errorMessage));
            disableExecuteButton();
        }
    } else {
        this->validSwap = false;
        if(!errorMessage.isEmpty())
            showMessage(tr("Invalid Swap.\n%1").arg(errorMessage));
        disableExecuteButton();
    }
}

bool AtomicSwapsDialog::AttemptParseTransaction(AtomicSwapDetails& result, QString& errorMessage)
{
    if (!DecodeHexTx(result.Transaction, result.Raw, true))
    {
        errorMessage = tr("Unable to decode partial transaction");
        return false;
    }
        
    if(result.Transaction.vin.size() != 1 || result.Transaction.vout.size() != 1)
    {
        errorMessage = tr("Input contains an invalid number of inputs/outputs.");
        return false;
    }
    
    CTxIn swap_vin = result.Transaction.vin[0];
    CTxOut swap_vout = result.Transaction.vout[0];

    auto vin_script = ScriptToAsmStr(swap_vin.scriptSig, true);
    auto fIsSingleSign = vin_script.find("[SINGLE|ANYONECANPAY]") != std::string::npos;

    if(!fIsSingleSign)
    {
        errorMessage = tr("Must be signed with %1").arg("SINGLE|ANYONECANPAY");
        return false;
    }

    Coin vinCoin;
    if (!pcoinsTip->GetCoin(swap_vin.prevout, vinCoin))
    {
        errorMessage = tr("Trade already executed, or this transaction is for another ravencoin network.");
        return false;
    }

    if (!ExtractDestination(swap_vout.scriptPubKey, result.SwapDestination))
    {
        errorMessage = tr("Unable to extract destination from swap transaction.");
        return false;
    }

    //These are from our perspective. Do we need to send or do we receive assets.
    bool fSendAsset = swap_vout.scriptPubKey.IsAssetScript();
    bool fRecvAsset = vinCoin.IsAsset();
    
    AtomicSwapType tradeType;
    CAmount totalPrice, unitPrice;
    CAssetOutputEntry sendAsset, recvAsset;
    if(fSendAsset) GetAssetData(swap_vout.scriptPubKey, sendAsset);
    if(fRecvAsset) GetAssetData(vinCoin.out.scriptPubKey, recvAsset);
    
    if(fSendAsset && fRecvAsset)
    {
        tradeType = AtomicSwapType::Trade;
        totalPrice = sendAsset.nAmount;
        unitPrice = (CAmount)((double)totalPrice / recvAsset.nAmount * COIN); //Consider our input the "currency"
    }
    else if(fSendAsset)
    {
        tradeType = AtomicSwapType::Buy;
        totalPrice = vinCoin.out.nValue;
        unitPrice = (CAmount)((double)totalPrice / sendAsset.nAmount * COIN);

        recvAsset.assetName = "RVN";
        recvAsset.nAmount = totalPrice;
    }
    else if(fRecvAsset)
    {
        tradeType = AtomicSwapType::Sell;
        totalPrice = swap_vout.nValue;
        unitPrice = (CAmount)((double)totalPrice / recvAsset.nAmount * COIN);

        sendAsset.assetName = "RVN";
        sendAsset.nAmount = totalPrice;
    }
    else
    {
        errorMessage = tr("Unknown swap type");
        return false;
    }

    //Need to flip the perspective for the trade itself
    result.Type = tradeType;
    result.ProvidedType = QString::fromStdString(recvAsset.assetName);
    result.ExpectedType = QString::fromStdString(sendAsset.assetName);
    result.ProvidedQuantity = recvAsset.nAmount;
    result.ExpectedQuantity = sendAsset.nAmount;
    result.UnitPrice = unitPrice;

    return true;
}

bool AtomicSwapsDialog::AttemptCompleteTransaction(CMutableTransaction& finalTransaction, AtomicSwapDetails& result, QString& errorMessage)
{
    CCoinControl ctrl;
    CWallet* wallet = model->getWallet();
    CReserveKey rkey(wallet);

    ctrl.fAllowOtherInputs = true;

    std::string strFailReason;

    //Ensure we have 
    if (boost::get<CNoDestination>(&ctrl.destChange)){
        CKeyID newDest;
        if (!wallet->CreateNewChangeAddress(rkey, newDest, strFailReason))
            return false;
        ctrl.destChange = newDest;
    }

    if (boost::get<CNoDestination>(&ctrl.assetDestChange)){
        CKeyID newDest;
        if (!wallet->CreateNewChangeAddress(rkey, newDest, strFailReason))
            return false;
        ctrl.assetDestChange = newDest;
    }

    CAmount rvnSend=0, rvnChange=0, assetSend=0, assetChange=0;
    CMutableTransaction finalTx;

    //First, add the original swap TX vin/vout pair
    finalTx.vin.emplace_back(result.Transaction.vin[0]);
    finalTx.vout.emplace_back(result.Transaction.vout[0]);

    //Lists of all available UTXO's coming from out coin ctrl
    std::vector<COutput> vAvailableCoins;
    std::map<std::string, std::vector<COutput> > mapAssetCoins;
    wallet->AvailableCoinsWithAssets(vAvailableCoins, mapAssetCoins, true, &ctrl);
    LogPrintf("%i RVN and %i Asset UTXO's found for given CoinCtrl\n", vAvailableCoins.size(), mapAssetCoins.size());

    if(!result.FindAssetUTXOs(wallet, ctrl, mapAssetCoins, finalTx, assetSend, assetChange))
    {
        LogPrintf("Unable to find Asset UTXOs\n");
        errorMessage = tr("Unable to find Asset UTXOs");
        return false;
    }
    
    LogPrintf("Post-Assets TX: %i:%i (vin:vout) \n", finalTx.vin.size(), finalTx.vout.size());

    if(!result.FindRavenUTXOs(wallet, ctrl, vAvailableCoins, finalTx, rvnSend, rvnChange))
    {
        LogPrintf("Unable to find RVN UTXOs\n");
        errorMessage = tr("Unable to find RVN UTXOs");
        return false;
    }

    std::string hexout = EncodeHexTx(CTransaction(finalTx));

    LogPrintf("Final TX: %i:%i (vin:vout) for transaction. RVN: %i mRVN (Change: %i mRVN) \t Asset: %i mA (Change: %i mA).\n", 
        finalTx.vin.size(), finalTx.vout.size(), rvnSend / CENT, rvnChange / CENT, assetSend / CENT, assetChange / CENT);
    LogPrintf("Raw: %s\n", hexout.c_str());

    if(!result.SignTransaction(wallet, finalTx, finalTransaction))
    {
        errorMessage = tr("Error Signing");
        return false;
    }

    std::string signhexout = EncodeHexTx(CTransaction(finalTransaction));
    LogPrintf("Raw: %s\n", signhexout.c_str());

    return true;
}

/** SLOTS */

void AtomicSwapsDialog::onSignedPartialChanged()
{
    CheckFormState();
}

void AtomicSwapsDialog::clear(bool clearAll)
{
    hideMessage();
    disableExecuteButton();
    ui->tradeTypeLabel->clear();
    ui->assetNameLabel->clear();
    ui->totalPriceLabel->clear();
    ui->unitPriceLabel->clear();
    ui->summaryLabel->clear();
    if(clearAll)
        ui->signedPartialText->clear();
}

/** Helper functions **/

QString AtomicSwapDetails::GetSummary()
{
    auto formattedProvided = RavenUnits::formatWithCustomName(ProvidedType, ProvidedQuantity, 2);
    auto formattedExpected = RavenUnits::formatWithCustomName(ExpectedType, ExpectedQuantity, 2);
    switch(Type)
    {
        case AtomicSwapType::Buy:
        {
            return AtomicSwapsDialog::tr("You are selling your %1 for %2").arg(formattedExpected).arg(formattedProvided);
            break;
        }
        case AtomicSwapType::Sell:
        {
            return AtomicSwapsDialog::tr("You are buying %1 for %2").arg(formattedProvided).arg(formattedExpected);
            break;
        }
        case AtomicSwapType::Trade:
        {
            return AtomicSwapsDialog::tr("You are trading your %1 for their %2").arg(formattedExpected).arg(formattedProvided);
            break;
        }
    }
    return AtomicSwapsDialog::tr("Unknown swap type");
}

bool AtomicSwapDetails::FindAssetUTXOs(CWallet* wallet, CCoinControl ctrl, std::map<std::string, std::vector<COutput> > mapAssetCoins, CMutableTransaction& finalTx, CAmount& totalSupplied, CAmount& change)
{
    totalSupplied = 0;
    change = 0;

    std::string expectedType = ExpectedType.toUtf8().constData();

    //Determine which asset type we need to send. Order types are from trade-perspective
    if(Type == AtomicSwapType::Sell || Type == AtomicSwapType::Trade) {
        LogPrintf("Adding output from counterparty of %i mAssets\n", ExpectedQuantity / CENT);
        CScript assetOutDest = GetScriptForDestination(ctrl.assetDestChange);
        CAssetTransfer assetTransfer(ProvidedType.toUtf8().constData(), ProvidedQuantity);
        assetTransfer.ConstructTransaction(assetOutDest);
        CTxOut newAssetTxOut(0, assetOutDest);
        finalTx.vout.emplace_back(newAssetTxOut);
    }

    if(Type == AtomicSwapType::Sell)
        return true; //Nothing to do here, if they are selling assets, we don't supply any assets
    
    std::map<std::string, CAmount> sendAsset;
    std::map<std::string, CAmount> mapAssetsIn; //Total we actually send?
    std::set<CInputCoin> setAssets;

    sendAsset[expectedType] = ExpectedQuantity;

    LogPrintf("Looking for a total of %i mAssets\n", ExpectedQuantity / CENT);

    if (!wallet->SelectAssets(mapAssetCoins, sendAsset, setAssets, mapAssetsIn)) {
        //strFailReason = _("Insufficient asset funds");
        return false;
    }

    totalSupplied = mapAssetsIn[expectedType];

    const uint32_t nSequence = CTxIn::SEQUENCE_FINAL - 1;
    //Add the assets to the vin's on the tx
    LogPrintf("Adding %i asset UTXOs totalling %i mA\n", setAssets.size(), totalSupplied / CENT);
    for (const auto &asset : setAssets)
        finalTx.vin.push_back(CTxIn(asset.outpoint, CScript(), nSequence));


    change = totalSupplied - ExpectedQuantity;

    if(change > 0) {
        LogPrintf("Sending self asset change.\n");

        CScript scriptAssetChange = GetScriptForDestination(ctrl.assetDestChange);
        CAssetTransfer assetTransfer(expectedType, change);

        assetTransfer.ConstructTransaction(scriptAssetChange);
        CTxOut newAssetTxOut(0, scriptAssetChange);

        finalTx.vout.emplace_back(newAssetTxOut);
    }

    return true;
}

bool AtomicSwapDetails::FindRavenUTXOs(CWallet* wallet, CCoinControl ctrl, std::vector<COutput> vAvailableCoins, CMutableTransaction& finalTx, CAmount& totalSupplied, CAmount& change)
{
    FeeCalculation feeCalc;
    //CAmount curBalance = wallet->GetBalance();
    CAmount recvAmount = 0, sendAmount = 0; //Net debit/credit for the transaction
    const uint32_t nSequence = CTxIn::SEQUENCE_FINAL - 1;

    switch(Type)
    {
        case AtomicSwapType::Buy:
        {
            //They are buying, we get RVN, we just take fees out of that.
            recvAmount = ProvidedQuantity;
            break;
        }
        case AtomicSwapType::Sell:
        {
            //They are selling, we provide RVN + fees
            sendAmount = ExpectedQuantity;
            break;
        }
        case AtomicSwapType::Trade:
        {
            //In a trade, we don't get or send RVN, but still need to pay fees
            break;
        }
    }

    LogPrintf("Send: %i mRVN. Recv: %i mRVN\n", sendAmount / CENT, recvAmount / CENT);

    totalSupplied = recvAmount;

    // Parse Raven address
    CScript opposingDestination = GetScriptForDestination(SwapDestination);
    CScript changeDestination = GetScriptForDestination(ctrl.destChange);

    //Add a placeholder output for now, this will be reduced by fees later
    CTxOut selfOutput(recvAmount, changeDestination);
    int selfIdx = finalTx.vout.size();
    finalTx.vout.emplace_back(selfOutput);

    std::set<CInputCoin> allInputs;

    //Add inputs to satisfy the counterparty
    if(sendAmount > 0) {
        CAmount totalSend = 0;
        std::set<CInputCoin> setCoins;
        if (!wallet->SelectCoins(vAvailableCoins, sendAmount, setCoins, totalSend, &ctrl))
        {
            //strFailReason = _("Insufficient funds");
            return false;
        }

        //Add the coin UTXOs to the vin's on the tx
        LogPrintf("Adding %i coin UTXOs totalling %i mRVN\n", setCoins.size(), totalSend / CENT);
        for (const auto &coin : setCoins) {
            finalTx.vin.push_back(CTxIn(coin.outpoint, CScript(), nSequence));
            ctrl.Select(coin.outpoint); //Remove this UTXO from future checks
            allInputs.insert(coin);
        }
        totalSupplied += totalSend;
    }

    CAmount feeEstimate = 0;
    bool fTxValid = false;

    //LogPrintf("Transaction net send: %i mRVN\n", netSend / CENT);

    while(!fTxValid) {

        wallet->DummySignTx(finalTx, allInputs);

        auto nBytes = GetVirtualTransactionSize(finalTx) + 110; //Fixed size to account for not including vin[0] in allInputs

        // Remove scriptSigs to eliminate the fee calculation dummy signatures
        for (auto& vin : finalTx.vin) {
            vin.scriptSig = CScript();
            vin.scriptWitness.SetNull();
        }

        //feeEstimate = GetMinimumFee(nBytes, ctrl, ::mempool, ::feeEstimator, &feeCalc);
        feeEstimate = GetRequiredFee(nBytes);

        LogPrintf("Transaction size: %i bytes. Estimate: %i sat\n", nBytes, feeEstimate);

        CAmount totalNegative = sendAmount + feeEstimate;
        change = totalSupplied - totalNegative;
        
        LogPrintf("Transaction breakdown: %i mRVN in. %i mRVN out\n", totalSupplied / CENT, totalNegative / CENT);

        if(totalSupplied > totalNegative) {
            LogPrintf("Transaction requested: %i mRVN, received: %i mRVN. Estimate: %i sat\n", sendAmount / CENT, recvAmount / CENT, feeEstimate);
            fTxValid = true;
        } else if(totalSupplied < totalNegative) {
            LogPrintf("Insufficient fees, trying again. Missing: %i sat\n", change);
            CAmount extraSend = 0;
            std::set<CInputCoin> extraSet;
            if (!wallet->SelectCoins(vAvailableCoins, totalNegative, extraSet, extraSend, &ctrl))
            {
                return false;
            }
            //Only add the inputs we haven't already added to the coin ctrl list.
            for (const auto &coin : extraSet) {
                if (!ctrl.IsSelected(coin.outpoint)) {
                    finalTx.vin.push_back(CTxIn(coin.outpoint, CScript(), nSequence));
                    ctrl.Select(coin.outpoint); //Remove this UTXO from future checks
                    allInputs.insert(coin);
                    totalSupplied += coin.txout.nValue;
                }
            }
            
        } else {
            fTxValid = true; //What are the odds....?
        }
    }

    finalTx.vout[selfIdx].nValue = change;
    FeeTotal = feeEstimate;

    return true;
}

bool AtomicSwapDetails::SignTransaction(CWallet* wallet, CMutableTransaction& finalTx, CMutableTransaction& signedTx)
{
    signedTx = CMutableTransaction(finalTx);
    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(signedTx);


    //Copied from rawtransaction.cpp:1871
    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : signedTx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }


    //First copy the signature from the originating partial.
    CTxIn& swapCounterpartyVin = signedTx.vin[0];
    const Coin& coin = view.AccessCoin(swapCounterpartyVin.prevout);
    if (coin.IsSpent()) {
        //TODO: Error message
        return false;
    }
    const CScript& prevPubKey = coin.out.scriptPubKey;
    const CAmount& amount = coin.out.nValue;

    SignatureData singlesign;

    singlesign = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, 0, amount), singlesign, DataFromTransaction(Transaction, 0));
    UpdateTransaction(signedTx, 0, singlesign);


    //Then sign the rest of the transaction, skip the first
    int nHashType = SIGHASH_ALL;
    for (unsigned int i = 1; i < signedTx.vin.size(); i++)
    {
        CTxIn& txin = signedTx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            //TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            LogPrintf("Input not found or already spent");
            return false;
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;
        ProduceSignature(MutableTransactionSignatureCreator(wallet, &signedTx, i, amount, nHashType), prevPubKey, sigdata);
        sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(signedTx, i));

        UpdateTransaction(signedTx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                //TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
                LogPrintf("Unable to sign input and verification failed (possible attempt to partially sign)");
                return false;
            } else {
                //TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
                LogPrintf("Transaction size: %s\n", ScriptErrorString(serror));
                return false;
            }
        }
    }

    return true;
}