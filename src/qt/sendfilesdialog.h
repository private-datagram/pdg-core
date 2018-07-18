#ifndef SENDFILESDIALOG_H
#define SENDFILESDIALOG_H

#include "walletmodel.h"

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
    explicit SendFilesDialog(QWidget *parent = 0);
    ~SendFilesDialog();

    SendCoinsRecipient getValue();
    bool validate();

    void setModel(WalletModel* model);
    void setAddress(const QString &address);
    void initFileHistory();

public slots:
    void clear();
    void reject();
    void accept();

private:
    Ui::SendFilesDialog* ui;
    void send(QList<SendCoinsRecipient> recipients, QString strFee, QStringList formatted);
    WalletModel* model;
    bool fNewRecipientAllowed;
    SendCoinsRecipient recipient;
    TransactionFilterProxy* filter;
    //todo:
    // Process WalletModel::SendCoinsReturn and generate a pair consisting
    // of a message and message flags for use in emit message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendFilesReturn(const WalletModel::SendCoinsReturn& sendCoinsReturn, const QString& msgArg = QString(), bool fPrepare = false);
    bool readFile(const string &filename, vector<char> &vchFile) const;

signals:
    // Fired when a message should be reported to the user
    void message(const QString& title, const QString& message, unsigned int style);

private slots:
    void on_uploadFile_clicked();
    void on_sendFileToAddressButton_clicked();
    void coinControlUpdateLabels();
    void on_listTransactions_doubleClicked(const QModelIndex &index);
};

#endif // SENDFILESDIALOG_H
