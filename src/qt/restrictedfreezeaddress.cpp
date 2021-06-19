// Copyright (c) 2019-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "restrictedfreezeaddress.h"
#include "ui_restrictedfreezeaddress.h"

#include "ravenunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "assetfilterproxy.h"
#include "assettablemodel.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QCompleter>
#include <validation.h>
#include <utiltime.h>

FreezeAddress::FreezeAddress(const PlatformStyle *_platformStyle, QWidget *parent) :
        QWidget(parent),
        ui(new Ui::FreezeAddress),
        clientModel(0),
        walletModel(0),
        platformStyle(_platformStyle)
{
    ui->setupUi(this);

    ui->buttonSubmit->setDisabled(true);
    ui->lineEditAddress->installEventFilter(this);
    ui->lineEditChangeAddress->installEventFilter(this);
    ui->lineEditAssetData->installEventFilter(this);
    connect(ui->buttonClear, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->buttonCheck, SIGNAL(clicked()), this, SLOT(check()));
    connect(ui->lineEditAddress, SIGNAL(textChanged(QString)), this, SLOT(dataChanged()));
    connect(ui->lineEditChangeAddress, SIGNAL(textChanged(QString)), this, SLOT(dataChanged()));
    connect(ui->lineEditAssetData, SIGNAL(textChanged(QString)), this, SLOT(dataChanged()));
    connect(ui->radioButtonFreezeAddress, SIGNAL(clicked()), this, SLOT(dataChanged()));
    connect(ui->radioButtonUnfreezeAddress, SIGNAL(clicked()), this, SLOT(dataChanged()));
    connect(ui->radioButtonGlobalFreeze, SIGNAL(clicked()), this, SLOT(dataChanged()));
    connect(ui->radioButtonGlobalUnfreeze, SIGNAL(clicked()), this, SLOT(dataChanged()));
    connect(ui->assetComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(dataChanged()));
    connect(ui->radioButtonGlobalFreeze, SIGNAL(clicked()), this, SLOT(globalOptionSelected()));
    connect(ui->radioButtonGlobalUnfreeze, SIGNAL(clicked()), this, SLOT(globalOptionSelected()));
    connect(ui->checkBoxChangeAddress, SIGNAL(stateChanged(int)), this, SLOT(dataChanged()));
    connect(ui->checkBoxChangeAddress, SIGNAL(stateChanged(int)), this, SLOT(changeAddressChanged(int)));

    ui->labelRestricted->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelRestricted->setFont(GUIUtil::getTopLabelFont());

    ui->labelAddress->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelAddress->setFont(GUIUtil::getTopLabelFont());

    ui->labelAssetData->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelAssetData->setFont(GUIUtil::getTopLabelFont());

    ui->checkBoxChangeAddress->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->lineEditChangeAddress->hide();
}


FreezeAddress::~FreezeAddress()
{
    delete ui;
}

void FreezeAddress::setClientModel(ClientModel *model)
{
    this->clientModel = model;
}

void FreezeAddress::setWalletModel(WalletModel *model)
{
    this->walletModel = model;

    assetFilterProxy = new AssetFilterProxy(this);
    assetFilterProxy->setSourceModel(model->getAssetTableModel());
    assetFilterProxy->setDynamicSortFilter(true);
    assetFilterProxy->setAssetNamePrefix("$");
    assetFilterProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    assetFilterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->assetComboBox->setModel(assetFilterProxy);
}

bool FreezeAddress::eventFilter(QObject* object, QEvent* event)
{
    if((object == ui->lineEditAddress || object == ui->lineEditChangeAddress || object == ui->lineEditAssetData) && event->type() == QEvent::FocusIn) {
        static_cast<QLineEdit*>(object)->setStyleSheet(STYLE_VALID);
        // bring up your custom edit
        return false; // lets the event continue to the edit
    }

    return false;
}

Ui::FreezeAddress* FreezeAddress::getUI()
{
    return this->ui;
}

void FreezeAddress::enableSubmitButton()
{
    showWarning(tr("Data has been validated, You can now submit the restriction transaction"), false);
    ui->buttonSubmit->setEnabled(true);
}

void FreezeAddress::showWarning(QString string, bool failure)
{
    if (failure) {
        ui->labelWarning->setStyleSheet(STRING_LABEL_COLOR_WARNING);
    } else {
        ui->labelWarning->setStyleSheet("");
    }
    ui->labelWarning->setText(string);
    ui->labelWarning->show();
}

void FreezeAddress::hideWarning()
{
    ui->labelWarning->hide();
    ui->labelWarning->clear();
}

void FreezeAddress::clear()
{
    ui->lineEditAddress->clear();
    ui->lineEditChangeAddress->clear();
    ui->lineEditAssetData->clear();
    ui->buttonSubmit->setDisabled(true);
    ui->lineEditAddress->setStyleSheet(STYLE_VALID);
    ui->lineEditChangeAddress->setStyleSheet(STYLE_VALID);
    ui->lineEditAssetData->setStyleSheet(STYLE_VALID);
    ui->radioButtonFreezeAddress->setChecked(true);
    hideWarning();
}

void FreezeAddress::dataChanged()
{
    ui->buttonSubmit->setDisabled(true);
    hideWarning();
}

void FreezeAddress::globalOptionSelected()
{
    ui->lineEditAddress->setStyleSheet(STYLE_VALID);
}

void FreezeAddress::changeAddressChanged(int state)
{
    if (state == Qt::CheckState::Checked) {
        ui->lineEditChangeAddress->setEnabled(true);
        ui->lineEditChangeAddress->show();
    }
    if (state == Qt::CheckState::Unchecked) {
        ui->lineEditChangeAddress->setEnabled(false);
        ui->lineEditChangeAddress->hide();
    }
}

void FreezeAddress::check()
{
    QString restricted_asset = ui->assetComboBox->currentData(AssetTableModel::RoleIndex::AssetNameRole).toString();
    QString address = ui->lineEditAddress->text();
    bool freeze_address = ui->radioButtonFreezeAddress->isChecked();
    bool unfreeze_address = ui->radioButtonUnfreezeAddress->isChecked();
    bool freeze_global = ui->radioButtonGlobalFreeze->isChecked();
    bool unfreeze_global = ui->radioButtonGlobalUnfreeze->isChecked();

    bool isSingleAddress = freeze_address || unfreeze_address;
    bool isGlobal = freeze_global || unfreeze_global;

    bool failed = false;
    if (!IsAssetNameAnRestricted(restricted_asset.toStdString())){
        showWarning(tr("Must have a restricted asset selected"));
        failed = true;
    }

    std::string strAddress = address.toStdString();
    if (isSingleAddress) {
        CTxDestination dest = DecodeDestination(strAddress);
        if (!IsValidDestination(dest)) {
            ui->lineEditAddress->setStyleSheet(STYLE_INVALID);
            failed = true;
        }
    }

    if (ui->checkBoxChangeAddress->isChecked()) {
        std::string strChangeAddress = ui->lineEditChangeAddress->text().toStdString();
        if (!strChangeAddress.empty()) {
            CTxDestination changeDest = DecodeDestination(strChangeAddress);
            if (!IsValidDestination(changeDest)) {
                ui->lineEditChangeAddress->setStyleSheet(STYLE_INVALID);
                failed = true;
            }
        }
    }

    if (ui->lineEditAssetData->text().size()) {
        std::string strAssetData = ui->lineEditAssetData->text().toStdString();

        if (DecodeAssetData(strAssetData).empty()) {
            ui->lineEditAssetData->setStyleSheet(STYLE_INVALID);
            failed = true;
        }
    }

    if (failed) return;

    if (passets) {
        if (isSingleAddress) {
            bool fCurrentlyAddressRestricted = passets->CheckForAddressRestriction(restricted_asset.toStdString(), address.toStdString(), true);

            if (freeze_address && fCurrentlyAddressRestricted) {
                showWarning(tr("Address is already frozen"));
            } else if (unfreeze_address && !fCurrentlyAddressRestricted) {
                showWarning(tr("Address is not frozen"));
            } else {
                enableSubmitButton();
            }
        } else if (isGlobal) {
           bool fCurrentlyGloballyRestricted = passets->CheckForGlobalRestriction(restricted_asset.toStdString(), true);

           if (freeze_global && fCurrentlyGloballyRestricted) {
               showWarning(tr("Restricted asset is already frozen globally"));
           } else if (unfreeze_global && !fCurrentlyGloballyRestricted) {
               showWarning(tr("Restricted asset is not frozen globally"));
           } else {
               enableSubmitButton();
           }
        }
    } else {
        showWarning(tr("Unable to preform action at this time"));
    }
};