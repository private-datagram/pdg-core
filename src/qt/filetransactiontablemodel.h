// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_FILETRANSACTIONTABLEMODEL_H
#define BITCOIN_QT_FILETRANSACTIONTABLEMODEL_H

#include "bitcoinunits.h"

#include <QAbstractTableModel>
#include <QStringList>

class TransactionRecord;
class FileTransactionTablePriv;
class WalletModel;

class CWallet;

/** UI model for the transaction table of a wallet.
 */
class FileTransactionTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FileTransactionTableModel(CWallet* wallet, WalletModel* parent = 0);
    ~FileTransactionTableModel();

    enum ColumnIndex {
        Status = 0,
        Date = 1,
        Description = 2,
        ToAddress = 3,
        FileSize = 4,
        Price = 5
    };

    /** Roles to get specific information from a transaction row.
        These are independent of column.
    */
    enum RoleIndex {
        /** Type of transaction */
        TypeRole = Qt::UserRole,
        /** Date and time this transaction was created */
        DateRole,
        /** Watch-only boolean */
        WatchonlyRole,
        /** Watch-only icon */
        WatchonlyDecorationRole,
        /** Long description (HTML format) */
        LongDescriptionRole,
        /** Address of transaction */
        AddressRole,
        /** Label of address related to transaction */
        LabelRole,
        /** Net amount of transaction */
        AmountRole,
        /** Unique identifier */
        TxIDRole,
        /** Transaction hash */
        TxHashRole,
        /** Is transaction confirmed? */
        ConfirmedRole,
        /** Formatted amount, without brackets when unconfirmed */
        FormattedAmountRole,
        /** Transaction status (TransactionRecord::Status) */
        StatusRole
    };

    int rowCount(const QModelIndex& parent) const;
    int columnCount(const QModelIndex& parent) const;
    QVariant data(const QModelIndex& index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;
    bool processingQueuedTransactions() { return fProcessingQueuedTransactions; }

private:
    CWallet* wallet;
    WalletModel* walletModel;
    QStringList columns;
    FileTransactionTablePriv* priv;
    bool fProcessingQueuedTransactions;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

    QString lookupAddress(const std::string& address, bool tooltip) const;
    QVariant addressColor(const TransactionRecord* wtx) const;
    QString formatTxStatus(const TransactionRecord* wtx) const;
    QString formatTxDate(const TransactionRecord* wtx) const;
    QString formatTxDescription(const TransactionRecord* wtx) const;
    QString formatTxToAddress(const TransactionRecord* wtx, bool tooltip) const;
    QString formatTxFileSize(const TransactionRecord* wtx, bool tooltip) const;
    QString formatTxPrice(const TransactionRecord* wtx, bool showUnconfirmed = true, BitcoinUnits::SeparatorStyle separators = BitcoinUnits::separatorStandard) const;
    QString formatTooltip(const TransactionRecord* rec) const;
    QVariant txStatusDecoration(const TransactionRecord* wtx) const;
    QVariant txWatchonlyDecoration(const TransactionRecord* wtx) const; // TODO: remove
    QVariant txAddressDecoration(const TransactionRecord* wtx) const;

public slots:
    /* New transaction, or transaction changed status */
    void updateTransaction(const QString& hash, int status, bool showTransaction);
    void updateConfirmations();
    void updateDisplayUnit();
    /** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
    void updateAmountColumnTitle();
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value) { fProcessingQueuedTransactions = value; }

    friend class FileTransactionTablePriv;
};

#endif // BITCOIN_QT_FILETRANSACTIONTABLEMODEL_H
