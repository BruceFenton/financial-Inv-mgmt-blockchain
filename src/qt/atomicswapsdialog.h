// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_ATOMICSWAPSDIALOG_H
#define RAVEN_QT_ATOMICSWAPSDIALOG_H

#include "walletmodel.h"

#include <QDialog>

class PlatformStyle;
class WalletModel;
class ClientModel;

namespace Ui {
    class AtomicSwapsDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
class QStringListModel;
class QSortFilterProxyModel;
class QCompleter;
QT_END_NAMESPACE

enum AtomicSwapType
{
    Buy,
    Sell,
    Trade
};

struct AtomicSwapDetails
{
    std::string Raw;
    AtomicSwapType Type;
    QString ProvidedType;
    QString ExpectedType;
    CAmount ProvidedQuantity;
    CAmount ExpectedQuantity;
    CAmount UnitPrice;
    CTxDestination SwapDestination;

    CAmount FeeTotal;
    CMutableTransaction Transaction;
    CMutableTransaction SignedFinal;

    AtomicSwapDetails(std::string RawTranscation)
        :Raw(RawTranscation)
    {
    }

    QString GetSummary();

    bool FindAssetUTXOs(CWallet* wallet, CCoinControl ctrl, std::map<std::string, std::vector<COutput> > mapAssetCoins, 
                        CMutableTransaction& finalTx, CAmount& totalSupplied, CAmount& change);
    bool FindRavenUTXOs(CWallet* wallet, CCoinControl ctrl, std::vector<COutput> vAvailableCoins, 
                        CMutableTransaction& finalTx, CAmount& totalSupplied, CAmount& change);
    bool SignTransaction(CWallet* wallet, CMutableTransaction& finalTx, CMutableTransaction& signedTx);

};

/** Dialog for execution and creation of atomic swaps. */
class AtomicSwapsDialog : public QDialog
{
Q_OBJECT

public:
    explicit AtomicSwapsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~AtomicSwapsDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

    QStringListModel* stringModel;
    QSortFilterProxyModel* proxy;
    QCompleter* completer;

private:
    Ui::AtomicSwapsDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;

    std::shared_ptr<AtomicSwapDetails> loadedSwap;
    bool validSwap;

    const PlatformStyle *platformStyle;

    void setUpValues();
    void showMessage(QString string);
    void showValidMessage(QString string);
    void hideMessage();
    void disableExecuteButton();
    void enableExecuteButton();
    void CheckFormState();
    bool AttemptParseTransaction(AtomicSwapDetails& result, QString& errorMessage);
    bool AttemptCompleteTransaction(CMutableTransaction& signedTransaction, AtomicSwapDetails& result, QString& errorMessage);
    bool AttemptTransmit(AtomicSwapDetails& result, CTransactionRef sentTx, QString& errorMessage);

private Q_SLOTS:
    void clear(bool clearAll);
    void onSignedPartialChanged();
    void onExecuteSwap();

    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);
    void updateDisplayUnit();


protected:
//    bool eventFilter( QObject* sender, QEvent* event);


Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};

#endif // RAVEN_QT_ATOMICSWAPSDIALOG_H
