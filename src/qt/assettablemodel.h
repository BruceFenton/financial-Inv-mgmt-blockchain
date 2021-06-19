// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_ASSETTABLEMODEL_H
#define RAVEN_QT_ASSETTABLEMODEL_H

#include "amount.h"

#include <QAbstractTableModel>
#include <QStringList>

class AssetTablePriv;
class WalletModel;
class AssetRecord;

class CAssets;

/** Models assets portion of wallet as table of owned assets.
 */
class AssetTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AssetTableModel(WalletModel *parent = 0);
    ~AssetTableModel();

    enum ColumnIndex {
        Name = 0,
        Quantity = 1
    };

    /** Roles to get specific information from a transaction row.
        These are independent of column.
    */
    enum RoleIndex {
        /** Net amount of transaction */
            AmountRole = 100,
        /** RVN or name of an asset */
            AssetNameRole = 101,
        /** Formatted amount, without brackets when unconfirmed */
            FormattedAmountRole = 102,
        /** AdministratorRole */
            AdministratorRole = 103,
        /** RVN or name of an asset */
            AssetIPFSHashRole = 104,
        /** IPFS Decoration Role */
            AssetIPFSHashDecorationRole = 105
    };

    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    QString formatTooltip(const AssetRecord *rec) const;
    QString formatAssetData(const AssetRecord *wtx) const;
    QString formatAssetName(const AssetRecord *wtx) const;
    QString formatAssetQuantity(const AssetRecord *wtx) const;

    void checkBalanceChanged();

private:
    WalletModel *walletModel;
    QStringList columns;
    AssetTablePriv *priv;

    friend class AssetTablePriv;
};

#endif // RAVEN_QT_ASSETTABLEMODEL_H
