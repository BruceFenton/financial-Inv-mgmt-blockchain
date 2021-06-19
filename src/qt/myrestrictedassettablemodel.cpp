// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "myrestrictedassettablemodel.h"

#include "addresstablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactiondesc.h"
#include "myrestrictedassetrecord.h"
#include "walletmodel.h"

#include "core_io.h"
#include "validation.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"
#include "wallet/wallet.h"

#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QIcon>
#include <QList>

// Fixing Boost 1.73 compile errors
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

// Amount column is right-aligned it contains numbers
static int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* type */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignLeft|Qt::AlignVCenter /* assetName */
};

// Comparison operator for sort/binary search of model tx list
struct TxLessThan
{
    bool operator()(const MyRestrictedAssetRecord &a, const MyRestrictedAssetRecord &b) const
    {
        return a.hash < b.hash;
    }
    bool operator()(const MyRestrictedAssetRecord &a, const uint256 &b) const
    {
        return a.hash < b;
    }
    bool operator()(const uint256 &a, const MyRestrictedAssetRecord &b) const
    {
        return a < b.hash;
    }
};

// Private implementation
class MyRestrictedAssetsTablePriv
{
public:
    MyRestrictedAssetsTablePriv(CWallet *_wallet, MyRestrictedAssetsTableModel *_parent) :
            wallet(_wallet),
            parent(_parent)
    {
    }

    CWallet *wallet;
    MyRestrictedAssetsTableModel *parent;

    /* Local cache of wallet.
     * As it is in the same order as the CWallet, by definition
     * this is sorted by sha256.
     */
    QMap<QPair<QString,QString>,MyRestrictedAssetRecord> cacheMyAssetData;
    QList<QPair<QString, QString> > vectAssetData;

    /* Query entire wallet anew from core.
     */
    void refreshWallet()
    {
        qDebug() << "MyRestrictedAssetsTablePriv::refreshWallet";
        cacheMyAssetData.clear();
        vectAssetData.clear();
        {
            std::vector<std::tuple<std::string, std::string, bool, uint32_t> > myTaggedAddresses;
            std::vector<std::tuple<std::string, std::string, bool, uint32_t> > myRestrictedAddresses;
            pmyrestricteddb->LoadMyTaggedAddresses(myTaggedAddresses);
            pmyrestricteddb->LoadMyRestrictedAddresses(myRestrictedAddresses);
            myTaggedAddresses.insert(myTaggedAddresses.end(), myRestrictedAddresses.begin(), myRestrictedAddresses.end());
            if(myTaggedAddresses.size()) {
                for (auto item : myTaggedAddresses) {
                    MyRestrictedAssetRecord sub;
                    sub.address = std::get<0>(item);
                    sub.assetName = std::get<1>(item);
                    sub.time = std::get<3>(item);
                    if (IsAssetNameAQualifier(sub.assetName))
                        sub.type = std::get<2>(item) ? MyRestrictedAssetRecord::Type::Tagged : MyRestrictedAssetRecord::Type::UnTagged;
                    else if (IsAssetNameAnRestricted(sub.assetName))
                        sub.type = std::get<2>(item) ? MyRestrictedAssetRecord::Type::Frozen : MyRestrictedAssetRecord::Type::UnFrozen;
                    sub.involvesWatchAddress = IsMine(*this->wallet, DecodeDestination(sub.address)) & ISMINE_WATCH_ONLY;
                    vectAssetData.push_back(qMakePair(QString::fromStdString(std::get<0>(item)), QString::fromStdString(std::get<1>(item))));
                    cacheMyAssetData[qMakePair(QString::fromStdString(std::get<0>(item)), QString::fromStdString(std::get<1>(item)))] = sub;
                }
            }
        }
    }

    void updateMyRestrictedAssets(const QString &address, const QString& asset_name, const int type, const qint64& date) {
        MyRestrictedAssetRecord rec;

        if (IsAssetNameAQualifier(asset_name.toStdString())) {
            rec.type = type ? MyRestrictedAssetRecord::Tagged : MyRestrictedAssetRecord::UnTagged;
        } else {
            rec.type = type ? MyRestrictedAssetRecord::Frozen : MyRestrictedAssetRecord::UnFrozen;
        }

        rec.time = date;
        rec.assetName = asset_name.toStdString();
        rec.address = address.toStdString();

        QPair<QString, QString> pair(address, asset_name);
        if (cacheMyAssetData.contains(pair)) {
            rec.involvesWatchAddress = cacheMyAssetData[pair].involvesWatchAddress;
            cacheMyAssetData[pair] = rec;
        } else {
            rec.involvesWatchAddress =
                    IsMine(*this->wallet, DecodeDestination(address.toStdString())) & ISMINE_WATCH_ONLY ? true : false;
            parent->beginInsertRows(QModelIndex(), 0, 0);
            cacheMyAssetData[pair] = rec;
            vectAssetData.push_front(pair);
            parent->endInsertRows();
        }
    }

    int size()
    {
        return cacheMyAssetData.size();
    }

    MyRestrictedAssetRecord *index(int idx)
    {
        if(idx >= 0 && idx < vectAssetData.size())
        {
            auto pair = vectAssetData[idx];
            if (cacheMyAssetData.contains(pair)) {
                MyRestrictedAssetRecord *rec = &cacheMyAssetData[pair];
                return rec;
            }
        }
        return 0;
    }

//    QString describe(TransactionRecord *rec, int unit)
//    {
//        {
//            LOCK2(cs_main, wallet->cs_wallet);
//            std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(rec->hash);
//            if(mi != wallet->mapWallet.end())
//            {
//                return TransactionDesc::toHTML(wallet, mi->second, rec, unit);
//            }
//        }
//        return QString();
//    }

//    QString getTxHex(TransactionRecord *rec)
//    {
//        LOCK2(cs_main, wallet->cs_wallet);
//        std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(rec->hash);
//        if(mi != wallet->mapWallet.end())
//        {
//            std::string strHex = EncodeHexTx(static_cast<CTransaction>(mi->second));
//            return QString::fromStdString(strHex);
//        }
//        return QString();
//    }
};

MyRestrictedAssetsTableModel::MyRestrictedAssetsTableModel(const PlatformStyle *_platformStyle, CWallet* _wallet, WalletModel *parent):
        QAbstractTableModel(parent),
        wallet(_wallet),
        walletModel(parent),
        priv(new MyRestrictedAssetsTablePriv(_wallet, this)),
        fProcessingQueuedTransactions(false),
        platformStyle(_platformStyle)
{
    columns << tr("Date") << tr("Type") << tr("Address") << tr("Asset Name");

    priv->refreshWallet();

    connect(walletModel->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    subscribeToCoreSignals();
}

MyRestrictedAssetsTableModel::~MyRestrictedAssetsTableModel()
{
    unsubscribeFromCoreSignals();
    delete priv;
}

void MyRestrictedAssetsTableModel::updateMyRestrictedAssets(const QString &address, const QString& asset_name, const int type, const qint64 date)
{
    priv->updateMyRestrictedAssets(address, asset_name, type, date);
}

int MyRestrictedAssetsTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int MyRestrictedAssetsTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QString MyRestrictedAssetsTableModel::formatTxDate(const MyRestrictedAssetRecord *wtx) const
{
    if(wtx->time)
    {
        return GUIUtil::dateTimeStr(wtx->time);
    }
    return QString();
}

/* Look up address in address book, if found return label (address)
   otherwise just return (address)
 */
QString MyRestrictedAssetsTableModel::lookupAddress(const std::string &address, bool tooltip) const
{
    QString label = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(address));
    QString description;
    if(!label.isEmpty())
    {
        description += label;
    }
    if(label.isEmpty() || tooltip)
    {
        description += QString(" (") + QString::fromStdString(address) + QString(")");
    }
    return description;
}

QString MyRestrictedAssetsTableModel::formatTxType(const MyRestrictedAssetRecord *wtx) const
{
    switch(wtx->type)
    {
        case MyRestrictedAssetRecord::Tagged:
            return tr("Tagged");
        case MyRestrictedAssetRecord::UnTagged:
            return tr("Untagged");
        case MyRestrictedAssetRecord::Frozen:
            return tr("Frozen");
        case MyRestrictedAssetRecord::UnFrozen:
            return tr("Unfrozen");
        case MyRestrictedAssetRecord::Other:
            return tr("Other");
        default:
            return QString();
    }
}

QVariant MyRestrictedAssetsTableModel::txAddressDecoration(const MyRestrictedAssetRecord *wtx) const
{
    if (wtx->involvesWatchAddress)
        return QIcon(":/icons/eye");

    return QVariant();

    // TODO get icons, and update when added
//    switch(wtx->type)
//    {
//        case MyRestrictedAssetRecord::Tagged:
//            return QIcon(":/icons/tx_mined");
//        case MyRestrictedAssetRecord::UnTagged:
//            return QIcon(":/icons/tx_input");
//        case MyRestrictedAssetRecord::Frozen:
//            return QIcon(":/icons/tx_output");
//        case MyRestrictedAssetRecord::UnFrozen:
//            return QIcon(":/icons/tx_asset_input");
//        case MyRestrictedAssetRecord::Other:
//            return QIcon(":/icons/tx_inout");
//        default:
//            return QIcon(":/icons/tx_inout");
//    }
}

QString MyRestrictedAssetsTableModel::formatTxToAddress(const MyRestrictedAssetRecord *wtx, bool tooltip) const
{
    QString watchAddress;
    if (tooltip) {
        // Mark transactions involving watch-only addresses by adding " (watch-only)"
        watchAddress = wtx->involvesWatchAddress ? QString(" (") + tr("watch-only") + QString(")") : "";
    }

    return QString::fromStdString(wtx->address) + watchAddress;
}

QVariant MyRestrictedAssetsTableModel::addressColor(const MyRestrictedAssetRecord *wtx) const
{
    return QVariant();
}


QVariant MyRestrictedAssetsTableModel::txWatchonlyDecoration(const MyRestrictedAssetRecord *wtx) const
{
    if (wtx->involvesWatchAddress)
        return QIcon(":/icons/eye");
    else
        return QVariant();
}

QString MyRestrictedAssetsTableModel::formatTooltip(const MyRestrictedAssetRecord *rec) const
{
    QString tooltip = formatTxType(rec);
    return tooltip;
}

QVariant MyRestrictedAssetsTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    MyRestrictedAssetRecord *rec = static_cast<MyRestrictedAssetRecord*>(index.internalPointer());

    switch(role)
    {
        case RawDecorationRole:
            switch(index.column())
            {
                case ToAddress:
                    return txAddressDecoration(rec);
                case AssetName:
                    return QString::fromStdString(rec->assetName);
            }
            break;
        case Qt::DecorationRole:
        {
            QIcon icon = qvariant_cast<QIcon>(index.data(RawDecorationRole));
            return platformStyle->TextColorIcon(icon);
        }
        case Qt::DisplayRole:
            switch(index.column())
            {
                case Date:
                    return formatTxDate(rec);
                case Type:
                    return formatTxType(rec);
                case ToAddress:
                    return formatTxToAddress(rec, false);
                case AssetName:
                    return QString::fromStdString(rec->assetName);
            }
            break;
        case Qt::EditRole:
            // Edit role is used for sorting, so return the unformatted values
            switch(index.column())
            {
                case Date:
                    return rec->time;
                case Type:
                    return formatTxType(rec);
                case ToAddress:
                    return formatTxToAddress(rec, true);
                case AssetName:
                    return QString::fromStdString(rec->assetName);
            }
            break;
        case Qt::ToolTipRole:
            return formatTooltip(rec);
        case Qt::TextAlignmentRole:
            return column_alignments[index.column()];
        case Qt::ForegroundRole:
            if(index.column() == ToAddress)
            {
                return addressColor(rec);
            }
            break;
        case TypeRole:
            return rec->type;
        case DateRole:
            return QDateTime::fromTime_t(static_cast<uint>(rec->time));
        case WatchonlyRole:
            return rec->involvesWatchAddress;
        case WatchonlyDecorationRole:
            return txWatchonlyDecoration(rec);
        case AddressRole:
            return QString::fromStdString(rec->address);
        case LabelRole:
            return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
        case TxIDRole:
            return rec->getTxID();
        case TxHashRole:
            return QString::fromStdString(rec->hash.ToString());
        case TxHexRole:
            return ""; //priv->getTxHex(rec);
        case TxPlainTextRole:
        {
            QString details;
            QDateTime date = QDateTime::fromTime_t(static_cast<uint>(rec->time));
            QString txLabel = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));

            details.append(date.toString("M/d/yy HH:mm"));
            details.append(" ");
            details.append(". ");
            if(!formatTxType(rec).isEmpty()) {
                details.append(formatTxType(rec));
                details.append(" ");
            }
            if(!rec->address.empty()) {
                if(txLabel.isEmpty())
                    details.append(tr("(no label)") + " ");
                else {
                    details.append("(");
                    details.append(txLabel);
                    details.append(") ");
                }
                details.append(QString::fromStdString(rec->address));
                details.append(" ");
            }
            return details;
        }
        case AssetNameRole:
        {
            QString assetName;
            assetName.append(QString::fromStdString(rec->assetName));
            return assetName;
        }
    }
    return QVariant();
}

QVariant MyRestrictedAssetsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        } else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
                case Date:
                    return tr("Date and time that the transaction was received.");
                case Type:
                    return tr("Type of transaction.");
                case ToAddress:
                    return tr("User-defined intent/purpose of the transaction.");
                case AssetName:
                    return tr("The asset (or RVN) removed or added to balance.");
            }
        }
    }
    return QVariant();
}

QModelIndex MyRestrictedAssetsTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    MyRestrictedAssetRecord *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    return QModelIndex();
}

//// queue notifications to show a non freezing progress dialog e.g. for rescan
struct MyRestrictedTransactionNotification
{
public:
    MyRestrictedTransactionNotification() {}
    MyRestrictedTransactionNotification(std::string _address, std::string _asset_name, int _type, uint32_t _date):
            address(_address), asset_name(_asset_name), type(_type), date(_date) {}

    void invoke(QObject *ttm)
    {
        QString strAddress = QString::fromStdString(address);
        QString strName= QString::fromStdString(asset_name);
        qDebug() << "MyRestrictedAssetChanged: " + strAddress + " asset_name= " + strName;
        QMetaObject::invokeMethod(ttm, "updateMyRestrictedAssets", Qt::QueuedConnection,
                                  Q_ARG(QString, strAddress),
                                  Q_ARG(QString, strName),
                                  Q_ARG(int, type),
                                  Q_ARG(qint64, date));
    }
private:
    std::string address;
    std::string asset_name;
    int type;
    uint32_t date;
};

static bool fQueueNotifications = false;
static std::vector< MyRestrictedTransactionNotification > vQueueNotifications;

static void NotifyTransactionChanged(MyRestrictedAssetsTableModel *ttm, CWallet *wallet, const std::string& address, const std::string& asset_name,
                                     int type, uint32_t date)
{
    MyRestrictedTransactionNotification notification(address, asset_name, type, date);

    if (fQueueNotifications)
    {
        vQueueNotifications.push_back(notification);
        return;
    }
    notification.invoke(ttm);
}

static void ShowProgress(MyRestrictedAssetsTableModel *ttm, const std::string &title, int nProgress)
{
    if (nProgress == 0)
        fQueueNotifications = true;

    if (nProgress == 100)
    {
        fQueueNotifications = false;
        if (vQueueNotifications.size() > 10) // prevent balloon spam, show maximum 10 balloons
            QMetaObject::invokeMethod(ttm, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, true));
        for (unsigned int i = 0; i < vQueueNotifications.size(); ++i)
        {
            if (vQueueNotifications.size() - i <= 10)
                QMetaObject::invokeMethod(ttm, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, false));

            vQueueNotifications[i].invoke(ttm);
        }
        std::vector<MyRestrictedTransactionNotification >().swap(vQueueNotifications); // clear
    }
}

void MyRestrictedAssetsTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyMyRestrictedAssetsChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3, _4, _5));
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
}

void MyRestrictedAssetsTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyMyRestrictedAssetsChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3, _4, _5));
    wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
}
