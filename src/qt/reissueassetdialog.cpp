// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "reissueassetdialog.h"
#include "ui_reissueassetdialog.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "assettablemodel.h"
#include "addresstablemodel.h"
#include "core_io.h"
#include "univalue.h"
#include "assets/assettypes.h"
#include "ravenunits.h"
#include "optionsmodel.h"
#include "sendcoinsdialog.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "clientmodel.h"
#include "guiconstants.h"

#include <validation.h>
#include <script/standard.h>
#include <wallet/wallet.h>
#include <policy/policy.h>
#include <base58.h>

#include "wallet/coincontrol.h"
#include "policy/fees.h"
#include "wallet/fees.h"

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


ReissueAssetDialog::ReissueAssetDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::ReissueAssetDialog),
        platformStyle(_platformStyle)
{
    ui->setupUi(this);
    setWindowTitle("Reissue Assets");
    connect(ui->comboBox, SIGNAL(activated(int)), this, SLOT(onAssetSelected(int)));
    connect(ui->quantitySpinBox, SIGNAL(valueChanged(double)), this, SLOT(onQuantityChanged(double)));
    connect(ui->ipfsBox, SIGNAL(clicked()), this, SLOT(onIPFSStateChanged()));
    connect(ui->ipfsText, SIGNAL(textChanged(QString)), this, SLOT(onIPFSHashChanged(QString)));
    connect(ui->addressText, SIGNAL(textChanged(QString)), this, SLOT(onAddressNameChanged(QString)));
    connect(ui->reissueAssetButton, SIGNAL(clicked()), this, SLOT(onReissueAssetClicked()));
    connect(ui->reissuableBox, SIGNAL(clicked()), this, SLOT(onReissueBoxChanged()));
    connect(ui->unitSpinBox, SIGNAL(valueChanged(int)), this, SLOT(onUnitChanged(int)));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(onClearButtonClicked()));
    connect(ui->lineEditVerifierString, SIGNAL(textChanged(QString)), this, SLOT(onVerifierStringChanged(QString)));
    this->asset = new CNewAsset();
    asset->SetNull();

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(coinControlChangeEdited(const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_TRANSACTION_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);
    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

    formatGreen = "%1%2 <font color=green><b>%3</b></font>";
    formatBlack = "%1%2 <font color=black><b>%3</b></font>";
    if (darkModeEnabled)
        formatBlack = "%1%2 <font color=white><b>%3</b></font>";

    setupCoinControlFrame(platformStyle);
    setupAssetDataView(platformStyle);
    setupFeeControl(platformStyle);

    /** Setup the asset list combobox */
    stringModel = new QStringListModel;

    proxy = new QSortFilterProxyModel;
    proxy->setSourceModel(stringModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->comboBox->setModel(proxy);
    ui->comboBox->setEditable(true);
    ui->comboBox->lineEdit()->setPlaceholderText(tr("Select an asset to reissue.."));
    ui->comboBox->lineEdit()->setToolTip(tr("Select the asset you want to reissue."));

    completer = new QCompleter(proxy,this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->comboBox->setCompleter(completer);
    adjustSize();


    ui->addressText->installEventFilter(this);
    ui->comboBox->installEventFilter(this);
    ui->lineEditVerifierString->installEventFilter(this);
}

void ReissueAssetDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void ReissueAssetDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        bool fCoinControlEnabled = _model->getOptionsModel()->getCoinControlFeatures();
        ui->frameCoinControl->setVisible(fCoinControlEnabled);
        ui->addressText->setVisible(fCoinControlEnabled);
        ui->addressLabel->setVisible(fCoinControlEnabled);
        coinControlUpdateLabels();

        // Custom Fee Control
        ui->frameFee->setVisible(_model->getOptionsModel()->getCustomFeeFeatures());
        connect(_model->getOptionsModel(), SIGNAL(customFeeFeaturesChanged(bool)), this, SLOT(feeControlFeatureChanged(bool)));

        // fee section
        for (const int &n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n * GetParams().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(coinControlUpdateLabels()));

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        connect(ui->groupFee, &QButtonGroup::idClicked, this, &ReissueAssetDialog::updateFeeSectionControls);
        connect(ui->groupFee, &QButtonGroup::idClicked, this, &ReissueAssetDialog::coinControlUpdateLabels);
#else
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(coinControlUpdateLabels()));
#endif
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
//        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(updateSmartFeeLabel()));
//        connect(ui->optInRBF, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
        ui->customFee->setSingleStep(GetRequiredFee(1000));
        updateFeeSectionControls();
        updateMinFeeLabel();
        updateSmartFeeLabel();

        // set default rbf checkbox state
//        ui->optInRBF->setCheckState(model->getDefaultWalletRbf() ? Qt::Checked : Qt::Unchecked);
        ui->optInRBF->hide();

        // set the smartfee-sliders default value (wallets default conf.target or last stored value)
        QSettings settings;
        if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
            // migrate nSmartFeeSliderPosition to nConfTarget
            // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
            int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
            settings.setValue("nConfTarget", nConfirmTarget);
            settings.remove("nSmartFeeSliderPosition");
        }
        if (settings.value("nConfTarget").toInt() == 0)
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->getDefaultConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));

        ui->reissueCostLabel->setText(tr("Cost") + ": " + RavenUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), GetBurnAmount(AssetType::REISSUE)));
        ui->reissueCostLabel->setStyleSheet("font-weight: bold");

        // Setup the default values
        setUpValues();

        restrictedAssetUnselected();

        adjustSize();
    }
}

ReissueAssetDialog::~ReissueAssetDialog()
{
    delete ui;
    delete asset;
}

bool ReissueAssetDialog::eventFilter(QObject *sender, QEvent *event)
{
    if (sender == ui->addressText)
    {
        if(event->type()== QEvent::FocusIn)
        {
            ui->addressText->setStyleSheet("");
        }
    } else if (sender == ui->lineEditVerifierString)
    {
        if(event->type()== QEvent::FocusIn)
        {
            hideInvalidVerifierStringMessage();
        }
    } else if (sender == ui->comboBox)
    {
        if(event->type()== QEvent::Show)
        {
            updateAssetsList();
        }
    }
    return QWidget::eventFilter(sender,event);
}

/** Helper Methods */
void ReissueAssetDialog::setUpValues()
{
    if (!model)
        return;

    ui->reissuableBox->setCheckState(Qt::CheckState::Checked);
    ui->ipfsText->setDisabled(true);
    hideMessage();

    ui->unitExampleLabel->setStyleSheet("font-weight: bold");

    updateAssetsList();

    // Set style for current asset data
    ui->currentAssetData->viewport()->setAutoFillBackground(false);
    ui->currentAssetData->setFrameStyle(QFrame::NoFrame);

    // Set style for update asset data
    ui->updatedAssetData->viewport()->setAutoFillBackground(false);
    ui->updatedAssetData->setFrameStyle(QFrame::NoFrame);

    setDisplayedDataToNone();

    // Hide the reissue Warning Label
    ui->reissueWarningLabel->hide();

    disableAll();
}

void ReissueAssetDialog::setupCoinControlFrame(const PlatformStyle *platformStyle)
{
    /** Update the assetcontrol frame */
    ui->frameCoinControl->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));
    ui->widgetCoinControl->setStyleSheet(".QWidget {background-color: transparent;}");
    /** Create the shadow effects on the frames */

    ui->frameCoinControl->setGraphicsEffect(GUIUtil::getShadowEffect());

    ui->labelCoinControlFeatures->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlFeatures->setFont(GUIUtil::getTopLabelFont());

    ui->labelCoinControlQuantityText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlQuantityText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlAmountText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlAmountText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlFeeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlFeeText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlAfterFeeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlAfterFeeText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlBytesText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlBytesText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlLowOutputText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlLowOutputText->setFont(GUIUtil::getSubLabelFont());

    ui->labelCoinControlChangeText->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCoinControlChangeText->setFont(GUIUtil::getSubLabelFont());

    // Align the other labels next to the input buttons to the text in the same height
    ui->labelCoinControlAutomaticallySelected->setStyleSheet(STRING_LABEL_COLOR);

    // Align the Custom change address checkbox
    ui->checkBoxCoinControlChange->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->labelCoinControlQuantity->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlAmount->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlAfterFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlBytes->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlLowOutput->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->checkBoxCoinControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->lineEditCoinControlChange->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlInsuffFunds->setFont(GUIUtil::getSubLabelFont());
    ui->labelCoinControlAutomaticallySelected->setFont(GUIUtil::getSubLabelFont());

}

void ReissueAssetDialog::setupAssetDataView(const PlatformStyle *platformStyle)
{
    /** Update the scrollview*/
    ui->frame->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));
    ui->frame->setGraphicsEffect(GUIUtil::getShadowEffect());

    ui->assetNameLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->assetNameLabel->setFont(GUIUtil::getSubLabelFont());

    ui->addressLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->addressLabel->setFont(GUIUtil::getSubLabelFont());

    ui->quantityLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->quantityLabel->setFont(GUIUtil::getSubLabelFont());

    ui->unitLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->unitLabel->setFont(GUIUtil::getSubLabelFont());

    ui->reissuableBox->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->ipfsBox->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->frame_3->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));
    ui->frame_3->setGraphicsEffect(GUIUtil::getShadowEffect());

    ui->frame_2->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));
    ui->frame_2->setGraphicsEffect(GUIUtil::getShadowEffect());

    ui->currentDataLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->currentDataLabel->setFont(GUIUtil::getTopLabelFont());

    ui->labelReissueAsset->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelReissueAsset->setFont(GUIUtil::getTopLabelFont());

    ui->reissueAssetDataLabel->setStyleSheet(STRING_LABEL_COLOR);
    ui->reissueAssetDataLabel->setFont(GUIUtil::getTopLabelFont());

    ui->labelVerifierString->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelVerifierString->setFont(GUIUtil::getSubLabelFont());

}

void ReissueAssetDialog::setupFeeControl(const PlatformStyle *platformStyle)
{
    /** Update the coincontrol frame */
    ui->frameFee->setStyleSheet(QString(".QFrame {background-color: %1; padding-top: 10px; padding-right: 5px; border: none;}").arg(platformStyle->WidgetBackGroundColor().name()));
    /** Create the shadow effects on the frames */

    ui->frameFee->setGraphicsEffect(GUIUtil::getShadowEffect());

    ui->labelFeeHeadline->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelFeeHeadline->setFont(GUIUtil::getSubLabelFont());

    ui->labelSmartFee3->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelCustomPerKilobyte->setStyleSheet(QString(".QLabel{ %1; }").arg(STRING_LABEL_COLOR));
    ui->radioSmartFee->setStyleSheet(STRING_LABEL_COLOR);
    ui->radioCustomFee->setStyleSheet(STRING_LABEL_COLOR);
    ui->checkBoxMinimumFee->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->buttonChooseFee->setFont(GUIUtil::getSubLabelFont());
    ui->fallbackFeeWarningLabel->setFont(GUIUtil::getSubLabelFont());
    ui->buttonMinimizeFee->setFont(GUIUtil::getSubLabelFont());
    ui->radioSmartFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelSmartFee2->setFont(GUIUtil::getSubLabelFont());
    ui->labelSmartFee3->setFont(GUIUtil::getSubLabelFont());
    ui->confTargetSelector->setFont(GUIUtil::getSubLabelFont());
    ui->radioCustomFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelCustomPerKilobyte->setFont(GUIUtil::getSubLabelFont());
    ui->customFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelMinFeeWarning->setFont(GUIUtil::getSubLabelFont());
    ui->optInRBF->setFont(GUIUtil::getSubLabelFont());
    ui->reissueAssetButton->setFont(GUIUtil::getSubLabelFont());
    ui->clearButton->setFont(GUIUtil::getSubLabelFont());
    ui->labelSmartFee->setFont(GUIUtil::getSubLabelFont());
    ui->labelFeeEstimation->setFont(GUIUtil::getSubLabelFont());
    ui->labelFeeMinimized->setFont(GUIUtil::getSubLabelFont());

}

void ReissueAssetDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
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

void ReissueAssetDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0);
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void ReissueAssetDialog::toggleIPFSText()
{
    if (ui->ipfsBox->isChecked()) {
        ui->ipfsText->setDisabled(false);
    } else {
        ui->ipfsText->setDisabled(true);
    }

    buildUpdatedData();
    CheckFormState();
}

void ReissueAssetDialog::showMessage(QString string)
{
    ui->messageLabel->setStyleSheet("color: red");
    ui->messageLabel->setText(string);
    ui->messageLabel->show();
}

void ReissueAssetDialog::showValidMessage(QString string)
{
    ui->messageLabel->setStyleSheet("color: green");
    ui->messageLabel->setText(string);
    ui->messageLabel->show();
}

void ReissueAssetDialog::hideMessage()
{
    ui->messageLabel->hide();
}

void ReissueAssetDialog::disableReissueButton()
{
    ui->reissueAssetButton->setDisabled(true);
}

void ReissueAssetDialog::enableReissueButton()
{
    ui->reissueAssetButton->setDisabled(false);
}

void ReissueAssetDialog::CheckFormState()
{
    disableReissueButton(); // Disable the button by default


    bool fReissuingRestricted = IsAssetNameAnRestricted(ui->comboBox->currentText().toStdString());

    // If asset is null
    if (asset->strName == "") {
        showMessage(tr("Asset data couldn't be found"));
        return;
    }

    // If the quantity is to large
    if (asset->nAmount + ui->quantitySpinBox->value() * COIN > MAX_MONEY) {
        showMessage(tr("Quantity is to large. Max is 21,000,000,000"));
        return;
    }

    // Check the destination address
    const CTxDestination dest = DecodeDestination(ui->addressText->text().toStdString());
    if (!ui->addressText->text().isEmpty()) {
        if (!IsValidDestination(dest)) {
            showMessage(tr("Invalid Raven Destination Address"));
            return;
        }
    }

    if (ui->ipfsBox->isChecked())
        if (!checkIPFSHash(ui->ipfsText->text()))
            return;

    if (fReissuingRestricted) {

        QString qVerifier = ui->lineEditVerifierString->text();
        std::string strVerifier = qVerifier.toStdString();

        std::string strippedVerifier = GetStrippedVerifierString(strVerifier);

        if (!strVerifier.empty()) {
            // A valid address must be given
            QString qAddress = ui->addressText->text();
            std::string strAddress = qAddress.toStdString();

            bool fHasQuantity = ui->quantitySpinBox->value() > 0;
            if (fHasQuantity && strAddress.empty()) {
                ui->addressText->setStyleSheet(STYLE_INVALID);
                showMessage(tr("Warning: Restricted Assets Issuance requires an address"));
                return;
            }

            if (fHasQuantity && !IsValidDestination(dest)) {
                ui->addressText->setStyleSheet(STYLE_INVALID);
                showMessage(tr("Warning: Invalid Raven address"));
                return;
            }

            // Check the verifier string
            std::string strError;
            ErrorReport errorReport;
            errorReport.type = ErrorReport::ErrorType::NotSetError;
            if (!ContextualCheckVerifierString(passets, strippedVerifier, strAddress, strError, &errorReport)) {
                ui->lineEditVerifierString->setStyleSheet(STYLE_INVALID);
                qDebug() << "Failing here 1";
                showInvalidVerifierStringMessage(QString::fromStdString(GetUserErrorString(errorReport)));
                return;
            } else {
                hideInvalidVerifierStringMessage();
            }
        } else {
            // In some cases, if the user. Fixes the addressText box staying red
            if (ui->quantitySpinBox->value() > 0)
                ui->addressText->setStyleSheet("");
        }
    }

    // Keep the button disabled if no changes have been made
    if ((!ui->ipfsBox->isChecked() || (ui->ipfsBox->isChecked() && ui->ipfsText->text().isEmpty())) && ui->reissuableBox->isChecked() && ui->quantitySpinBox->value() == 0 && ui->unitSpinBox->value() == asset->units && ui->lineEditVerifierString->text().isEmpty()) {
        hideMessage();
        return;
    }


    enableReissueButton();
    hideMessage();
}

void ReissueAssetDialog::disableAll()
{
    ui->quantitySpinBox->setDisabled(true);
    ui->addressText->setDisabled(true);
    ui->reissuableBox->setDisabled(true);
    ui->ipfsBox->setDisabled(true);
    ui->reissueAssetButton->setDisabled(true);
    ui->unitSpinBox->setDisabled(true);
    ui->lineEditVerifierString->setDisabled(true);

    asset->SetNull();
}

void ReissueAssetDialog::enableDataEntry()
{
    ui->quantitySpinBox->setDisabled(false);
    ui->addressText->setDisabled(false);
    ui->reissuableBox->setDisabled(false);
    ui->ipfsBox->setDisabled(false);
    ui->unitSpinBox->setDisabled(false);
    ui->lineEditVerifierString->setDisabled(false);
}

void ReissueAssetDialog::buildUpdatedData()
{
    // Get the display value for the asset quantity
    auto value = ValueFromAmount(asset->nAmount, asset->units);

    double newValue = value.get_real() + ui->quantitySpinBox->value();

    std::stringstream ss;
    ss.precision(asset->units);
    ss << std::fixed << newValue;

    QString reissuable = ui->reissuableBox->isChecked() ? tr("Yes") : tr("No");
    QString name = formatBlack.arg(tr("Name"), ":", QString::fromStdString(asset->strName)) + "\n";

    QString quantity;
    if (ui->quantitySpinBox->value() > 0)
        quantity = formatGreen.arg(tr("Total Quantity"), ":", QString::fromStdString(ss.str())) + "\n";
    else
        quantity = formatBlack.arg(tr("Total Quantity"), ":", QString::fromStdString(ss.str())) + "\n";

    QString units;
    if (ui->unitSpinBox->value() != asset->units)
        units = formatGreen.arg(tr("Units"), ":", QString::number(ui->unitSpinBox->value())) + "\n";
    else
        units = formatBlack.arg(tr("Units"), ":", QString::number(ui->unitSpinBox->value())) + "\n";

    QString reissue;
    if (ui->reissuableBox->isChecked())
        reissue = formatBlack.arg(tr("Can Reisssue"), ":", reissuable) + "\n";
    else
        reissue = formatGreen.arg(tr("Can Reisssue"), ":", reissuable) + "\n";

    QString ipfs;

    if (asset->nHasIPFS && (!ui->ipfsBox->isChecked() || (ui->ipfsBox->isChecked() && ui->ipfsText->text().isEmpty()))) {
        QString qstr = QString::fromStdString(EncodeAssetData(asset->strIPFSHash));
        if (qstr.size() == 46) {
            ipfs = formatBlack.arg(tr("IPFS Hash"), ":", qstr) + "\n";
        } else if (qstr.size() == 64) {
            ipfs = formatBlack.arg(tr("Txid Hash"), ":", qstr) + "\n";
        }
    } else if (ui->ipfsBox->isChecked() && !ui->ipfsText->text().isEmpty()) {
        QString qstr = ui->ipfsText->text();
        if (qstr.size() == 46) {
            ipfs = formatGreen.arg(tr("IPFS Hash"), ":", qstr) + "\n";
        } else if (qstr.size() == 64) {
            ipfs = formatGreen.arg(tr("Txid Hash"), ":", qstr) + "\n";
        }
    }

    QString verifierString;
    if (!ui->lineEditVerifierString->isHidden()) {
        if (!ui->lineEditVerifierString->text().isEmpty()) {
            QString qstr = ui->lineEditVerifierString->text();
            verifierString = formatGreen.arg(tr("Verifier String"), ":", qstr) + "\n";
        } else {
            CNullAssetTxVerifierString verifier;
            if (passets->GetAssetVerifierStringIfExists(ui->comboBox->currentText().toStdString(), verifier)) {
                verifierString = formatBlack.arg(tr("Current Verifier String"), ":", QString::fromStdString(verifier.verifier_string)) + "\n";
            } else {
                verifierString = "No verifier string found";
            }
        }
    }


    ui->updatedAssetData->clear();
    ui->updatedAssetData->append(name);
    ui->updatedAssetData->append(quantity);
    ui->updatedAssetData->append(units);
    ui->updatedAssetData->append(reissue);
    if (!ipfs.isEmpty())
        ui->updatedAssetData->append(ipfs);
    if (!verifierString.isEmpty())
        ui->updatedAssetData->append(verifierString);
    ui->updatedAssetData->show();
    ui->updatedAssetData->setFixedHeight(ui->updatedAssetData->document()->size().height());
}

void ReissueAssetDialog::setDisplayedDataToNone()
{
    ui->currentAssetData->clear();
    ui->updatedAssetData->clear();
    ui->currentAssetData->setText(tr("Please select a asset from the menu to display the assets current settings"));
    ui->updatedAssetData->setText(tr("Please select a asset from the menu to display the assets updated settings"));
}

/** SLOTS */
void ReissueAssetDialog::onAssetSelected(int index)
{
    // Only display asset information when as asset is clicked. The first index is a PlaceHolder
    if (index > 0) {
        enableDataEntry();
        ui->currentAssetData->show();
        QString qstr_name = ui->comboBox->currentText();

        LOCK(cs_main);
        auto currentActiveAssetCache = GetCurrentAssetCache();
        // Get the asset data
        if (!currentActiveAssetCache->GetAssetMetaDataIfExists(qstr_name.toStdString(), *asset)) {
            CheckFormState();
            disableAll();
            asset->SetNull();
            ui->currentAssetData->hide();
            ui->currentAssetData->clear();
            return;
        }

        bool fRestrictedAssetSelected = IsAssetNameAnRestricted(qstr_name.toStdString());

        if (fRestrictedAssetSelected) {
            restrictedAssetSelected();
        } else {
            restrictedAssetUnselected();
        }

        // Get the display value for the asset quantity
        auto value = ValueFromAmount(asset->nAmount, asset->units);
        std::stringstream ss;
        ss.precision(asset->units);
        ss << std::fixed << value.get_real();

        ui->unitSpinBox->setMinimum(asset->units);
        ui->unitSpinBox->setValue(asset->units);

        if (asset->units == MAX_ASSET_UNITS) {
            ui->unitSpinBox->setDisabled(true);
        }
        
        ui->quantitySpinBox->setMaximum(21000000000 - value.get_real());

        ui->currentAssetData->clear();
        // Create the QString to display to the user
        QString name = formatBlack.arg(tr("Name"), ":", QString::fromStdString(asset->strName)) + "\n";
        QString quantity = formatBlack.arg(tr("Current Quantity"), ":", QString::fromStdString(ss.str())) + "\n";
        QString units = formatBlack.arg(tr("Current Units"), ":", QString::number(ui->unitSpinBox->value()));
        QString reissue = formatBlack.arg(tr("Can Reissue"), ":", tr("Yes")) + "\n";
        QString assetdatahash = "";
        if (asset->nHasIPFS) {
            QString qstr = QString::fromStdString(EncodeAssetData(asset->strIPFSHash));
            if (qstr.size() == 46) {
                    assetdatahash = formatBlack.arg(tr("IPFS Hash"), ":", qstr) + "\n";
            } else if (qstr.size() == 64) {
                    assetdatahash = formatBlack.arg(tr("Txid Hash"), ":", qstr) + "\n";
            }
            else {
                assetdatahash = formatBlack.arg(tr("Unknown data hash type"), ":", qstr) + "\n";
            }
        }

        QString verifierString = "";

        if (fRestrictedAssetSelected) {
            CNullAssetTxVerifierString verifier;
            if (passets->GetAssetVerifierStringIfExists(qstr_name.toStdString(), verifier)) {
                verifierString = formatBlack.arg(tr("Current Verifier String"), ":", QString::fromStdString(verifier.verifier_string)) + "\n";
            } else {
                verifierString = "No verifier string found";
            }
        }

        ui->currentAssetData->append(name);
        ui->currentAssetData->append(quantity);
        ui->currentAssetData->append(units);
        ui->currentAssetData->append(reissue);
        if (asset->nHasIPFS)
            ui->currentAssetData->append(assetdatahash);
        if (fRestrictedAssetSelected)
            ui->currentAssetData->append(verifierString);
        ui->currentAssetData->setFixedHeight(ui->currentAssetData->document()->size().height());

        buildUpdatedData();

        CheckFormState();
    } else {
        disableAll();
        asset->SetNull();
        setDisplayedDataToNone();
    }
}

void ReissueAssetDialog::onQuantityChanged(double qty)
{
    buildUpdatedData();
    CheckFormState();
}

void ReissueAssetDialog::onIPFSStateChanged()
{
    toggleIPFSText();
}

void ReissueAssetDialog::onVerifierStringChanged(QString verifier)
{
    buildUpdatedData();
    CheckFormState();
}

bool ReissueAssetDialog::checkIPFSHash(QString hash)
{
    if (!hash.isEmpty()) {
        if (!AreMessagesDeployed()) {
            if (hash.length() > 46) {
                showMessage(tr("Only IPFS Hashes allowed until RIP5 is activated"));
                disableReissueButton();
                return false;
            }
        }

        std::string error;
        if (!CheckEncoded(DecodeAssetData(hash.toStdString()), error)) {
            ui->ipfsText->setStyleSheet(STYLE_INVALID);
            showMessage(tr("IPFS/Txid Hash must start with 'Qm' and be 46 characters or Txid Hash must have 64 hex characters"));
            disableReissueButton();
            return false;
        } else if (hash.size() != 46 && hash.size() != 64) {
            ui->ipfsText->setStyleSheet(STYLE_INVALID);
            showMessage(tr("IPFS/Txid Hash must have size of 46 characters, or 64 hex characters"));
            disableReissueButton();
            return false;
        } else if (DecodeAssetData(ui->ipfsText->text().toStdString()).empty()) {
            ui->ipfsText->setStyleSheet(STYLE_INVALID);
            showMessage(tr("IPFS/Txid hash is not valid. Please use a valid IPFS/Txid hash"));
            disableReissueButton();
            return false;
        }
    }

    // No problems where found with the hash, reset the border, and hide the messages.
    hideMessage();
    ui->ipfsText->setStyleSheet("");

    return true;
}

void ReissueAssetDialog::onIPFSHashChanged(QString hash)
{

    if (checkIPFSHash(hash))
        CheckFormState();

    buildUpdatedData();
}

void ReissueAssetDialog::onAddressNameChanged(QString address)
{
    const CTxDestination dest = DecodeDestination(address.toStdString());

    if (address.isEmpty()) // Nothing entered
    {
        hideMessage();
        ui->addressText->setStyleSheet("");
        CheckFormState();
    }
    else if (!IsValidDestination(dest)) // Invalid address
    {
        ui->addressText->setStyleSheet("border: 1px solid red");
        CheckFormState();
    }
    else // Valid address
    {
        hideMessage();
        ui->addressText->setStyleSheet("");
        CheckFormState();
    }
}

void ReissueAssetDialog::onReissueAssetClicked()
{
    if (!model || !asset) {
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        return;
    }

    QString address;
    if (ui->addressText->text().isEmpty()) {
        address = model->getAddressTableModel()->addRow(AddressTableModel::Receive, "", "");
    } else {
        address = ui->addressText->text();
    }

    QString name = ui->comboBox->currentText();
    CAmount quantity = ui->quantitySpinBox->value() * COIN;
    bool reissuable = ui->reissuableBox->isChecked();
    bool hasIPFS = ui->ipfsBox->isChecked() && !ui->ipfsText->text().isEmpty();

    int unit = ui->unitSpinBox->value();
    if (unit == asset->units)
        unit = -1;

    // Always use a CCoinControl instance, use the CoinControlDialog instance if CoinControl has been enabled
    CCoinControl ctrl;
    if (model->getOptionsModel()->getCoinControlFeatures())
        ctrl = *CoinControlDialog::coinControl;

    updateCoinControlState(ctrl);

    std::string ipfsDecoded = "";
    if (hasIPFS)
        ipfsDecoded = DecodeAssetData(ui->ipfsText->text().toStdString());

    CReissueAsset reissueAsset(name.toStdString(), quantity, unit, reissuable ? 1 : 0, ipfsDecoded);

    CWalletTx tx;
    CReserveKey reservekey(model->getWallet());
    std::pair<int, std::string> error;
    CAmount nFeeRequired;

    std::string verifier_string = "";
    if (IsAssetNameAnRestricted(name.toStdString())) {
        verifier_string = ui->lineEditVerifierString->text().toStdString();
        std::string stripped = GetStrippedVerifierString(verifier_string);
        verifier_string = stripped;
    }

    if (IsInitialBlockDownload()) {
        GUIUtil::SyncWarningMessage syncWarning(this);
        bool sendTransaction = syncWarning.showTransactionSyncWarningMessage();
        if (!sendTransaction)
            return;
    }

    // Create the transaction
    if (!CreateReissueAssetTransaction(model->getWallet(), ctrl, reissueAsset, address.toStdString(), error, tx, reservekey, nFeeRequired, verifier_string.empty() ? nullptr : &verifier_string)) {
        showMessage("Invalid: " + QString::fromStdString(error.second));
        return;
    }

    std::string strError = "";
    if (!ContextualCheckReissueAsset(passets, reissueAsset, strError, *tx.tx.get())) {
        showMessage("Invalid: " + QString::fromStdString(strError));
        return;
    }

    // Format confirmation message
    QStringList formatted;

    // generate bold amount string
    QString amount = "<b>" + QString::fromStdString(ValueFromAmountString(GetReissueAssetBurnAmount(), 8)) + " RVN";
    amount.append("</b>");
    // generate monospace address string
    QString addressburn = "<span style='font-family: monospace;'>" + QString::fromStdString(GetParams().ReissueAssetBurnAddress());
    addressburn.append("</span>");

    QString recipientElement1;
    recipientElement1 = tr("%1 to %2").arg(amount, addressburn);
    formatted.append(recipientElement1);

    // generate the bold asset amount
    QString assetAmount = "<b>" + QString::fromStdString(ValueFromAmountString(reissueAsset.nAmount, 8)) + " " + QString::fromStdString(reissueAsset.strName);
    assetAmount.append("</b>");

    // generate the monospace address string
    QString assetAddress = "<span style='font-family: monospace;'>" + address;
    assetAddress.append("</span>");

    QString recipientElement2;
    recipientElement2 = tr("%1 to %2").arg(assetAmount, assetAddress);
    formatted.append(recipientElement2);

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
        questionString.append(" (" + QString::number((double)GetVirtualTransactionSize(tx) / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = GetReissueAssetBurnAmount() + nFeeRequired;
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

    SendConfirmationDialog confirmationDialog(tr("Confirm reissue assets"),
                                              questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if(retval != QMessageBox::Yes)
    {
        return;
    }

    // Create the transaction and broadcast it
    std::string txid;
    if (!SendAssetTransaction(model->getWallet(), tx, reservekey, error, txid)) {
        showMessage("Invalid: " + QString::fromStdString(error.second));
    } else {
        QMessageBox msgBox;
        QPushButton *copyButton = msgBox.addButton(tr("Copy"), QMessageBox::ActionRole);
        copyButton->disconnect();
        connect(copyButton, &QPushButton::clicked, this, [=](){
            QClipboard *p_Clipboard = QApplication::clipboard();
            p_Clipboard->setText(QString::fromStdString(txid), QClipboard::Mode::Clipboard);

            QMessageBox copiedBox;
            copiedBox.setText(tr("Transaction ID Copied"));
            copiedBox.exec();
        });

        QPushButton *okayButton = msgBox.addButton(QMessageBox::Ok);
        msgBox.setText(tr("Asset transaction sent to network:"));
        msgBox.setInformativeText(QString::fromStdString(txid));
        msgBox.exec();

        if (msgBox.clickedButton() == okayButton) {
            clear();

            CoinControlDialog::coinControl->UnSelectAll();
            coinControlUpdateLabels();
        }
    }
}

void ReissueAssetDialog::onReissueBoxChanged()
{
    if (!ui->reissuableBox->isChecked()) {
        ui->reissueWarningLabel->setText("Warning: Once this asset is reissued with the reissuable flag set to false. It won't be able to be reissued in the future");
        ui->reissueWarningLabel->setStyleSheet("color: red");
        ui->reissueWarningLabel->show();
    } else {
        ui->reissueWarningLabel->hide();
    }

    buildUpdatedData();

    CheckFormState();
}

void ReissueAssetDialog::updateCoinControlState(CCoinControl& ctrl)
{
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
//    ctrl.signalRbf = ui->optInRBF->isChecked();
}

void ReissueAssetDialog::updateSmartFeeLabel()
{
    if(!model || !model->getOptionsModel())
        return;
    CCoinControl coin_control;
    updateCoinControlState(coin_control);
    coin_control.m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
    FeeCalculation feeCalc;
    CFeeRate feeRate = CFeeRate(GetMinimumFee(1000, coin_control, ::mempool, ::feeEstimator, &feeCalc));

    ui->labelSmartFee->setText(RavenUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK()) + "/kB");

    if (feeCalc.reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
        int lightness = ui->fallbackFeeWarningLabel->palette().color(QPalette::WindowText).lightness();
        QColor warning_colour(255 - (lightness / 5), 176 - (lightness / 3), 48 - (lightness / 14));
        ui->fallbackFeeWarningLabel->setStyleSheet("QLabel { color: " + warning_colour.name() + "; }");
        #ifndef QTversionPreFiveEleven
    		ui->fallbackFeeWarningLabel->setIndent(QFontMetrics(ui->fallbackFeeWarningLabel->font()).horizontalAdvance("x"));
    	#else
    		ui->fallbackFeeWarningLabel->setIndent(QFontMetrics(ui->fallbackFeeWarningLabel->font()).width("x"));
    	#endif
        
    }
    else
    {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(tr("Estimated to begin confirmation within %n block(s).", "", feeCalc.returnedTarget));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

// Coin Control: copy label "Quantity" to clipboard
void ReissueAssetDialog::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void ReissueAssetDialog::coinControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void ReissueAssetDialog::coinControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void ReissueAssetDialog::coinControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void ReissueAssetDialog::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void ReissueAssetDialog::coinControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void ReissueAssetDialog::coinControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void ReissueAssetDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);
    ui->addressText->setVisible(checked);
    ui->addressLabel->setVisible(checked);

    if (!checked && model) // coin control features disabled
        CoinControlDialog::coinControl->SetNull();

    coinControlUpdateLabels();
}

// Coin Control: settings menu - coin control enabled/disabled by user
void ReissueAssetDialog::feeControlFeatureChanged(bool checked)
{
    ui->frameFee->setVisible(checked);
}

// Coin Control: button inputs -> show actual coin control dialog
void ReissueAssetDialog::coinControlButtonClicked()
{
    CoinControlDialog dlg(platformStyle);
    dlg.setModel(model);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void ReissueAssetDialog::coinControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        CoinControlDialog::coinControl->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void ReissueAssetDialog::coinControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        CoinControlDialog::coinControl->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:red;}");

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid Raven address"));
        }
        else // Valid address
        {
            if (!model->IsSpendable(dest)) {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                                                                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    CoinControlDialog::coinControl->destChange = dest;
                else
                {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");
                    ui->labelCoinControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet("QLabel{color:black;}");

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelCoinControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));

                CoinControlDialog::coinControl->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void ReissueAssetDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*CoinControlDialog::coinControl);

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    CoinControlDialog::payAmounts.append(GetBurnAmount(AssetType::REISSUE));

    if (CoinControlDialog::coinControl->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(model, this);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

void ReissueAssetDialog::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void ReissueAssetDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void ReissueAssetDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void ReissueAssetDialog::setMinimumFee()
{
    ui->customFee->setValue(GetRequiredFee(1000));
}

void ReissueAssetDialog::updateFeeSectionControls()
{
    ui->confTargetSelector      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee           ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->checkBoxMinimumFee      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelMinFeeWarning      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelCustomPerKilobyte  ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
    ui->customFee               ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
}

void ReissueAssetDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(RavenUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void ReissueAssetDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
                RavenUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), GetRequiredFee(1000)) + "/kB")
        );
}

void ReissueAssetDialog::onUnitChanged(int value)
{
    QString text;
    text += "e.g. 1";
    // Add the period
    if (value > 0)
        text += ".";

    // Add the remaining zeros
    for (int i = 0; i < value; i++) {
        text += "0";
    }

    ui->unitExampleLabel->setText(text);

    buildUpdatedData();
    CheckFormState();

}

void ReissueAssetDialog::updateAssetsList()
{
    LOCK(cs_main);
    std::vector<std::string> assets;
    GetAllAdministrativeAssets(model->getWallet(), assets, 0);

    QStringList list;
    list << "";

    // Load the assets that are reissuable
    for (auto item : assets) {
        std::string name = QString::fromStdString(item).split("!").first().toStdString();
        CNewAsset asset;
        if (passets->GetAssetMetaDataIfExists(name, asset)) {
            if (asset.nReissuable)
                list << QString::fromStdString(asset.strName);
        }

        if (passets->CheckIfAssetExists(RESTRICTED_CHAR + name)) {
            list << QString::fromStdString(RESTRICTED_CHAR + name);
        }
    }


    stringModel->setStringList(list);
}

void ReissueAssetDialog::clear()
{
    ui->comboBox->setCurrentIndex(0);
    ui->addressText->clear();
    ui->quantitySpinBox->setValue(0);
    ui->unitSpinBox->setMinimum(0);
    ui->unitSpinBox->setValue(0);
    onUnitChanged(0);
    ui->reissuableBox->setChecked(true);
    ui->ipfsBox->setChecked(false);
    ui->ipfsText->setDisabled(true);
    ui->ipfsText->clear();
    hideMessage();

    disableAll();
    asset->SetNull();
    setDisplayedDataToNone();

    updateAssetsList();
}

void ReissueAssetDialog::onClearButtonClicked()
{
    clear();
}

void ReissueAssetDialog::focusReissueAsset(const QModelIndex &index)
{
    clear();

    QString name = index.data(AssetTableModel::AssetNameRole).toString();
    if (IsAssetNameAnOwner(name.toStdString()))
        name = name.left(name.size() - 1);

    ui->comboBox->setCurrentIndex(ui->comboBox->findText(name));
    onAssetSelected(ui->comboBox->currentIndex());

    ui->quantitySpinBox->setFocus();
}

void ReissueAssetDialog::restrictedAssetSelected()
{
    ui->addressLabel->show();
    ui->addressText->show();

    ui->labelVerifierString->show();
    ui->lineEditVerifierString->show();
}

void ReissueAssetDialog::restrictedAssetUnselected()
{
    ui->labelVerifierString->hide();
    ui->lineEditVerifierString->hide();
    ui->labelReissueVerifierStringErrorMessage->hide();

    bool fCoinControlEnabled = this->model->getOptionsModel()->getCoinControlFeatures();
    ui->addressText->setVisible(fCoinControlEnabled);
    ui->addressLabel->setVisible(fCoinControlEnabled);
}

void ReissueAssetDialog::showInvalidVerifierStringMessage(QString string)
{
    ui->lineEditVerifierString->setStyleSheet(STYLE_INVALID);
    ui->labelReissueVerifierStringErrorMessage->setStyleSheet("color: red; font-size: 15pt;font-weight: bold;");
    ui->labelReissueVerifierStringErrorMessage->setText(string);
    ui->labelReissueVerifierStringErrorMessage->show();
}

void ReissueAssetDialog::hideInvalidVerifierStringMessage()
{
    ui->lineEditVerifierString->setStyleSheet(STYLE_VALID);
    ui->labelReissueVerifierStringErrorMessage->clear();
    ui->labelReissueVerifierStringErrorMessage->hide();
}


