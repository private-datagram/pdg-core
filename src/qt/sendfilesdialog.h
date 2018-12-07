#ifndef SENDFILESDIALOG_H
#define SENDFILESDIALOG_H

#include "walletmodel.h"

#include "guiutil.h"
#include "filetransactiontablemodel.h"
#include "paymenttransactiontablemodel.h"

#include <QDialog>
#include <QString>

class TransactionFilterProxy;

//static const int MAX_SEND_POPUP_ENTRIES = 10;

namespace Ui
{
class SendFilesDialog;
}

QT_BEGIN_NAMESPACE
QT_END_NAMESPACE

using namespace std;

class SendFilesDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        STATUS_COLUMN_WIDTH = 23,
        DATE_COLUMN_WIDTH = 120,
        DESCRIPTION_COLUMN_WIDTH = 310,
        ADDRESS_COLUMN_WIDTH = 360,
        FILE_SIZE_COLUMN_WIDTH = 120,
        PRICE_MINIMUM_COLUMN_WIDTH = 125,
        MINIMUM_COLUMN_WIDTH = 23
    };

    explicit SendFilesDialog(QWidget *parent = 0);
    ~SendFilesDialog();

    SendCoinsRecipient getValue();
    bool validate();

    void setModel(WalletModel* model);
    void setAddress(const QString &address);
    void initFileHistory();
    void resize();

public slots:
    void clear();
    void reject();
    void accept();

private:
    Ui::SendFilesDialog* ui;
    void send(const QList<SendCoinsRecipient> &recipients, const PtrContainer<CTransactionMeta>& meta, QString strFee, QStringList formatted);
    WalletModel* model;
    bool fNewRecipientAllowed;
    SendCoinsRecipient recipient;
    TransactionFilterProxy* filter;
    TransactionFilterProxy* paymentRequestsFilter;
    GUIUtil::TableViewLastColumnResizingFixer* fileColumnResizingFixer;
    GUIUtil::TableViewLastColumnResizingFixer* paymentColumnResizingFixer;
    //todo:
    // Process WalletModel::SendCoinsReturn and generate a pair consisting
    // of a message and message flags for use in emit message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendFilesReturn(const WalletModel::SendCoinsReturn& sendCoinsReturn, const QString& msgArg = QString());
    bool readFile(const string &filename, vector<char> &vchFile) const;
    long getFileSize(const std::string &filename) const;
    bool saveFileMeta(const SendCoinsRecipient &recipient, WalletModelTransaction &currentTransaction) const;
    void saveFileFromTx(const uint256 &txHash);
    virtual void resizeEvent(QResizeEvent* event);

signals:
    // Fired when a message should be reported to the user
    void message(const QString& title, const QString& message, unsigned int style);

private slots:
    void on_uploadFile_clicked();
    void on_clearButton_clicked();
    void on_sendFileToAddressButton_clicked();
    void coinControlUpdateLabels();
    void on_tableFileTransactions_doubleClicked(const QModelIndex &index);
    void on_tablePaymentRequests_doubleClicked(const QModelIndex &index);
};

#endif // SENDFILESDIALOG_H
