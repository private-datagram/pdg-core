
#include "sendfilesdialog.h"
#include "ui_sendfilesdialog.h"

#include "addresstablemodel.h"
#include "askpassphrasedialog.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "sendcoinsentry.h"
#include "walletmodel.h"

#include "transactionfilterproxy.h"

#include "transactionrecord.h"
#include "transactiontablemodel.h"

//send file
#include <iostream>
#include <fstream>
#include <sstream>

#include "base58.h"
#include "coincontrol.h"
#include "ui_interface.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "crypto/aes.h"
#include "crypto/rsa.h"

#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTableWidget>
#include <QAbstractItemModel>
#include <QFileDialog>
#include <QDataStream>
#include <QBitArray>
#include <QFileInfo>
#include <QtCore/qfile.h>


#define NUM_ITEMS 9

SendFilesDialog::SendFilesDialog(QWidget *parent) : QDialog(parent),
    ui(new Ui::SendFilesDialog)
{
    ui->setupUi(this);
}

SendFilesDialog::~SendFilesDialog()
{
    delete ui;
}

void SendFilesDialog::clear()
{

}


void SendFilesDialog::reject()
{
    clear();
}

void SendFilesDialog::accept()
{
    clear();
}

void SendFilesDialog::setAddress(const QString& address)
{

}

void SendFilesDialog::on_uploadFile_clicked()
{
    QString filename = QFileDialog::getOpenFileName(
                this,
                tr("Open File"),
                "/home/",
                "All files (*.*)"
                );

    //set text at field
    ui->fileNameField->setText(filename);
}

void SendFilesDialog::on_sendFileToAddressButton_clicked()
{
    if (!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    recipient = getValue();
    // TODO: refactor
    if (!readFile(ui->fileNameField->text().toStdString(), recipient.vchFile)) {
        QMessageBox::critical(this, tr("Send File"),
                              tr("Error opening file"),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    CPaymentRequest meta;

    std::string descriptionMessage = ui->descriptionField->text().toStdString();
    meta.vfMessage.reserve(descriptionMessage.length());
    meta.vfMessage.insert(meta.vfMessage.end(), descriptionMessage.begin(), descriptionMessage.end());
    meta.nPrice = ui->priceField->value();

    QFileInfo fileInfo(ui->fileNameField->text());
    recipient.filename = fileInfo.fileName();


    if (validate()) {
        recipients.append(recipient);
    } else {
        valid = false;
    }

    //if (!valid || recipients.isEmpty()) {

    if (!valid) {
        return;
    }

    QString strFunds = "";
    QString strFee = "";
    recipients[0].inputType = ALL_COINS;

    // Format confirmation message
    QStringList formatted;
    foreach (const SendCoinsRecipient &rcp, recipients) {
        // generate bold amount string
        QString amount =
                "<b>" + BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
        amount.append("</b> ").append(strFunds);

        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        if (!rcp.paymentRequest.IsInitialized()) // normal payment
        {
            if (rcp.label.length() > 0) // label with address
            {
                recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
                recipientElement.append(QString(" (%1)").arg(address));
            } else // just address
            {
                recipientElement = tr("%1 to %2").arg(amount, address);
            }
        } else if (!rcp.authenticatedMerchant.isEmpty()) // secure payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.authenticatedMerchant));
        } else // insecure payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }

        if (CoinControlDialog::coinControl->fSplitBlock) {
            recipientElement.append(tr(" split into %1 outputs using the UTXO splitter.").arg(
                    CoinControlDialog::coinControl->nSplitBlock));
        }

        formatted.append(recipientElement);
    }

    fNewRecipientAllowed = false;
    PtrContainer<CTransactionMeta> ptrMeta(meta);

    // request unlock only if was locked or unlocked for mixing:
    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if (encStatus == model->Locked || encStatus == model->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(model->requestUnlock(AskPassphraseDialog::Context::Send_PIV, true));
        if (!ctx.isValid()) {
            // Unlock wallet was cancelled
            fNewRecipientAllowed = true;
            return;
        }
        send(recipients, ptrMeta, strFee, formatted);
        return;
    }

    // already unlocked or not encrypted at all
    send(recipients, ptrMeta, strFee, formatted);
}

bool SendFilesDialog::validate()
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    if (!model->validateAddress(ui->addressField->text())) {
        //ui->addressField->setValid(false);
        retval = false;
    }

   // if (!ui->payAmount->validate()) {
    //    retval = false;
    //}

    // Sending a zero amount is invalid
    //if (ui->payAmount->value(0) <= 0) {
     //   ui->payAmount->setValid(false);
      //  retval = false;
    //}

    // Reject dust outputs:
    //if (retval && GUIUtil::isDust(ui->payTo->text(), ui->payAmount->value())) {
     //   ui->payAmount->setValid(false);
      //  retval = false;
    //}

    return retval;
}

void SendFilesDialog::setModel(WalletModel* model)
{
    this->model = model;

    if (model && model->getOptionsModel())
        initFileHistory();

    clear();
}

SendCoinsRecipient SendFilesDialog::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    recipient.address = ui->addressField->text();
    recipient.amount = 1 * CENT;
    recipient.useSwiftTX = false;

    return recipient;
}

// Coin Control: update labels
//todo: обновление модели окна отправки.
void SendFilesDialog::coinControlUpdateLabels()
{

}

void SendFilesDialog::send(QList<SendCoinsRecipient> recipients, const PtrContainer<CTransactionMeta>& meta, QString strFee, QStringList formatted)
{
    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients, meta);
    WalletModel::SendCoinsReturn prepareStatus;
    if (model->getOptionsModel()->getCoinControlFeatures()) // coin control enabled
        prepareStatus = model->prepareTransaction(currentTransaction, CoinControlDialog::coinControl);
    else
        prepareStatus = model->prepareTransaction(currentTransaction);

    if (prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee"));
        questionString.append(" ");
        questionString.append(strFee);

        // append transaction size
        questionString.append(" (" + QString::number((double)currentTransaction.getTransactionSize() / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    foreach (BitcoinUnits::Unit u, BitcoinUnits::availableUnits()) {
        if (u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatHtmlWithUnit(u, totalAmount));
    }

    // Show total amount + all alternative units
    questionString.append(tr("Total Amount = <b>%1</b><br />= %2")
                                  .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount))
                                  .arg(alternativeUnits.join("<br />= ")));

    // Limit number of displayed entries
    int messageEntries = formatted.size();
    int displayedEntries = 0;
    for (int i = 0; i < formatted.size(); i++) {
        if (i >= 12) {
            formatted.removeLast();
            i--;
        } else {
            displayedEntries = i + 1;
        }
    }
    questionString.append("<hr />");
    questionString.append(tr("<b>(%1 of %2 entries displayed)</b>").arg(displayedEntries).arg(messageEntries));

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
                                                               questionString.arg(formatted.join("<br />")),
                                                               QMessageBox::Yes | QMessageBox::Cancel,
                                                               QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction);

    if (sendStatus.status == WalletModel::OK) {
        acceptTransaction(recipients[0], currentTransaction);

        accept();
    }

    fNewRecipientAllowed = true;
}


//todo: проверка данных при отправке.
void SendFilesDialog::processSendFilesReturn(const WalletModel::SendCoinsReturn& sendCoinsReturn, const QString& msgArg, bool fPrepare)
{
    bool fAskForUnlock = false;

    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendCoinsDialog usage of WalletModel::SendCoinsReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendCoins()
    // all others are used only in WalletModel::prepareTransaction()
    switch (sendCoinsReturn.status) {
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid, please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgParams.first = tr("The amount to pay must be larger than 0.");
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your balance.");
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgParams.first = tr("The total exceeds your balance when the %1 transaction fee is included.").arg(msgArg);
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found, can only send to each address once per send operation.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed!");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::TransactionCommitFailed:
        msgParams.first = tr("The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AnonymizeOnlyUnlocked:
        // Unlock is only need when the coins are send
        if(!fPrepare)
            fAskForUnlock = true;
        else
            msgParams.first = tr("Error: The wallet was unlocked only to anonymize coins.");
        break;

    case WalletModel::InsaneFee:
        msgParams.first = tr("A fee %1 times higher than %2 per kB is considered an insanely high fee.").arg(10000).arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ::minRelayTxFee.GetFeePerK()));
        break;
    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

    // Unlock wallet if it wasn't fully unlocked already
    if(fAskForUnlock) {
        model->requestUnlock(AskPassphraseDialog::Context::Unlock_Full, false);
        if(model->getEncryptionStatus () != WalletModel::Unlocked) {
            msgParams.first = tr("Error: The wallet was unlocked only to anonymize coins. Unlock canceled.");
        }
        else {
            // Wallet unlocked
            return;
        }
    }

    emit message(tr("Send Coins"), msgParams.first, msgParams.second);
}

void SendFilesDialog::initFileHistory()
{
     // Set up transaction list
     filter = new TransactionFilterProxy();
     filter->setSourceModel(model->getTransactionTableModel());
     filter->setLimit(NUM_ITEMS);
     filter->setDynamicSortFilter(true);
     filter->setSortRole(Qt::EditRole);
     filter->setShowInactive(false);
     filter->setTypeFilter(TransactionFilterProxy::TYPE(TransactionRecord::RecvFileTransfer));
     filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

     ui->listTransactions->setModel(filter);
     ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);
}

void SendFilesDialog::on_listTransactions_doubleClicked(const QModelIndex &index)
{
    if (!filter)
        return;

    // find transaction in blockchain
    QVariant blockQVariantFile = index.data(TransactionTableModel::TxHashRole);
    uint256 txHash = uint256(blockQVariantFile.value<QString>().toStdString());

    CTransaction fileTx;
    uint256 hashBlock;

    if (!GetTransaction(txHash, fileTx, hashBlock, true)) {
        QMessageBox::critical(this, tr("Save file"), tr("Unable to find file transaction"));
        return;
    }

    // check hash of encrypted file
    if (fileTx.vfiles[0].fileHash != Hash(fileTx.vfiles[0].vBytes.begin(), fileTx.vfiles[0].vBytes.end())) {
        QMessageBox::critical(this, tr("Save file"), tr("File hash doesn't match. File corrupted"));
        return;
    }

    CFileMeta* fileMeta = &fileTx.meta.get<CFileMeta>();

    CTransaction paymentTx;
    if (!GetTransaction(fileMeta->confirmTxid, paymentTx, hashBlock, true)) {
        QMessageBox::critical(this, tr("Save file"), tr("Unable to find file payment transaction"));
        return;
    }

    CPaymentConfirm paymentConfirm = paymentTx.meta.get<CPaymentConfirm>();

    // red key from wallet db
    CWalletDB wdb(pwalletMain->strWalletFile, "r+");
    vector<char> publicKey;
    vector<char> privateKey;
    wdb.ReadFileEncryptKeys(paymentConfirm.requestTxid, publicKey, privateKey);
    RSA* privKey = crypto::rsa::PrivateDERToKey(privateKey);
    unique_ptr<RSA> privKeyPtr(privKey);

    // decrypt meta
    vector<char> outMeta;
    crypto::rsa::RSADecrypt(privKey, fileMeta->vfEncodedMeta, outMeta);

    CDataStream metaStream(SER_NETWORK, PROTOCOL_VERSION);
    metaStream.write(&outMeta[0], outMeta.size());
    CEncodedMeta encodedMeta;
    metaStream >> encodedMeta;
    std::string filename(encodedMeta.vfFilename.begin(),encodedMeta.vfFilename.end());
    std::string description(encodedMeta.vfFilename.begin(),encodedMeta.vfFilename.end());

    // choose destination filename
    QString destFilename = QFileDialog::getSaveFileName(this,
                                                        tr("Save File: %1. File size: %2KB") // TODO: remove information after UI will be implemented
                                                                .arg(QString::fromStdString(filename))
                                                                .arg((int)round((float)encodedMeta.nFileSize / 1024.0f)),
                                                        filename.data(), tr("All Files (*)")
    );

    if (destFilename.isEmpty())
        return;

    // extract key
    crypto::aes::AESKey key;
    memcpy(&key.key[0], &encodedMeta.vfFileKey[0], encodedMeta.vfFileKey.size());

    // decrypt file
    CDataStream encodedStream(SER_NETWORK, PROTOCOL_VERSION);
    encodedStream.write(&fileTx.vfiles[0].vBytes[0], fileTx.vfiles[0].vBytes.size());
    CDataStream destStream(SER_NETWORK, PROTOCOL_VERSION);
    crypto::aes::DecryptAES(key, destStream, encodedStream, encodedStream.size());

    // check hash of decrypted file
    if (encodedMeta.fileHash != Hash(destStream.begin(), destStream.end())) {
        QMessageBox::critical(this, tr("Save file"), tr("Decrypt file error. Hash mismatch"));
        return;
    }

    QFile file(destFilename);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Unable to open file for writing"), file.errorString());
        return;
    }

    // save file
    QDataStream out(&file);
    out.writeRawData(&destStream[0], static_cast<int>(destStream.size()));

    file.close();
}


/**
 * @param filename filename
 * @param vchoutFile output value
 * @return is file read successfully
 */
bool SendFilesDialog::readFile(const std::string &filename, vector<char> &vchoutFile) const {
    ifstream file (filename);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, ios_base::end);
    size_t len = static_cast<size_t>(file.tellg());
    vchoutFile.resize(len);
    file.seekg(0, ios_base::beg);
    file.read(&vchoutFile[0], len);
    file.close();

    return true;
}

bool SendFilesDialog::acceptTransaction(const SendCoinsRecipient &recipient, WalletModelTransaction &currentTransaction) const {
    CWalletFileTx wftx;
    wftx.filename = recipient.filename.toStdString();
    wftx.vchBytes = recipient.vchFile;
    CTransaction *tx = (CTransaction *) currentTransaction.getTransaction();
    wftx.paymentRequestTxid = SerializeHash(*tx); // TODO: check twice

    CWalletDB walletdb(pwalletMain->strWalletFile);
    return walletdb.WriteWalletFileTx(wftx);
}