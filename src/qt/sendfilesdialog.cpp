
#include "sendfilesdialog.h"
#include "ui_sendfilesdialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "sendcoinsentry.h"

#include "transactionfilterproxy.h"

#include "transactionrecord.h"
#include "transactiontablemodel.h"

//send file
#include <fstream>

#include "coincontrol.h"
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
    delete paymentColumnResizingFixer;
    delete fileColumnResizingFixer;
}

void SendFilesDialog::clear()
{
    ui->fileNameField->clear();
    ui->descriptionField->clear();
    ui->priceField->clear();
    ui->addressField->clear();
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

    CKeyID destKeyId;
    if (!CBitcoinAddress(recipient.address.toStdString()).GetKeyID(destKeyId)) {
        QMessageBox::critical(this, tr("Send File"), tr("Destination address error: %1").arg(recipient.address), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    // TODO: refactor
    if (!readFile(ui->fileNameField->text().toStdString(), recipient.vchFile)) {
        QMessageBox::critical(this, tr("Send File"), tr("Error opening file"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    //region Prepare meta
    CPaymentRequest meta;

    meta.sComment = ui->descriptionField->text().toStdString();
    meta.nPrice = ui->priceField->value();
    meta.nFileSize = CalcEncodedFileSize(recipient.vchFile.size());

    CPubKey pubKey;
    if (!pwalletMain->GetKeyFromPool(pubKey)) {
        QMessageBox::critical(this, tr("Send File"), tr("Error: Keypool ran out, please call keypoolrefill first"),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    meta.paymentAddress = pubKey.GetID();

    QFileInfo fileInfo(ui->fileNameField->text());
    recipient.filename = fileInfo.fileName();

    if (validate()) {
        recipients.append(recipient);
    } else {
        valid = false;
    }

    if (!valid)
        return;

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

        formatted.append(tr("Filename") + ": " + fileInfo.fileName() + "<br />");
        formatted.append(tr("File price") + ": " + BitcoinUnits::formatHtmlWithUnit(BitcoinUnits::PIV, ui->priceField->value()) + "<br />");
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
    recipient.amount = 0; // TODO: PDG1 ?
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
        QMessageBox::critical(this, tr("Send File"), tr("Error to prepare transaction. Status code: %1").arg((int)prepareStatus.status),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    QString questionString = tr("Are you sure you want to send payment request for file?");
    questionString.append("<br /><br />%1");

    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are file transaction extra fee"));
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
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send file"),
                                                               questionString.arg(formatted.join("<br />")),
                                                               QMessageBox::Yes | QMessageBox::Cancel,
                                                               QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) {
        fNewRecipientAllowed = true;
        return;
    }

    if (!saveFileMeta(recipients[0], currentTransaction)) {
        QMessageBox::critical(this, tr("Send File"), tr("Error: Keypool ran out, please call keypoolrefill first"),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction);

    if (sendStatus.status == WalletModel::OK) {
        accept();
    } else {
        // erase saved file on send payment request failed
        CWalletDB walletdb(pwalletMain->strWalletFile);
        const uint256 &paymentRequestTx = SerializeHash(*(CTransaction *) currentTransaction.getTransaction());
        walletdb.EraseWalletFileTx(paymentRequestTx);
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
    FileTransactionTableModel *fileTransactionTableModel = new FileTransactionTableModel(pwalletMain, this->model);

    // Set up files list
    filter = new TransactionFilterProxy();
    filter->setSourceModel(fileTransactionTableModel);
    filter->setLimit(50);
    filter->setDynamicSortFilter(true);
    filter->setSortRole(Qt::EditRole);
    filter->setShowInactive(false);
    filter->setTypeFilter(TransactionFilterProxy::TYPE(TransactionRecord::RecvFileTransfer));
    filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

    ui->tableFileTransactions->verticalHeader()->hide();
    ui->tableFileTransactions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->tableFileTransactions->setModel(filter);
    ui->tableFileTransactions->setAlternatingRowColors(true);
    ui->tableFileTransactions->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableFileTransactions->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableFileTransactions->setSortingEnabled(true);
    ui->tableFileTransactions->sortByColumn(FileTransactionTableModel::Date, Qt::DescendingOrder);

    ui->tableFileTransactions->setColumnWidth(FileTransactionTableModel::Status, STATUS_COLUMN_WIDTH);
    ui->tableFileTransactions->setColumnWidth(FileTransactionTableModel::Date, DATE_COLUMN_WIDTH);
    ui->tableFileTransactions->setColumnWidth(FileTransactionTableModel::Description, DESCRIPTION_COLUMN_WIDTH);
    ui->tableFileTransactions->setColumnWidth(FileTransactionTableModel::ToAddress, ADDRESS_COLUMN_WIDTH);

    fileColumnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(ui->tableFileTransactions, PRICE_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH);


    // Set up invoices table
    paymentTransactionTableModel = new PaymentTransactionTableModel(pwalletMain, this->model);

    paymentRequestsFilter = new TransactionFilterProxy();
    paymentRequestsFilter->setSourceModel(paymentTransactionTableModel);
    paymentRequestsFilter->setLimit(50);
    paymentRequestsFilter->setDynamicSortFilter(true);
    paymentRequestsFilter->setSortRole(Qt::EditRole);
    paymentRequestsFilter->setShowInactive(false);
    paymentRequestsFilter->setTypeFilter(TransactionFilterProxy::TYPE(TransactionRecord::RecvFilePaymentRequest));

    ui->tablePaymentRequests->verticalHeader()->hide();
    ui->tablePaymentRequests->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->tablePaymentRequests->setModel(paymentRequestsFilter);
    ui->tablePaymentRequests->setAlternatingRowColors(true);
    ui->tablePaymentRequests->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tablePaymentRequests->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tablePaymentRequests->setSortingEnabled(true);
    ui->tablePaymentRequests->sortByColumn(PaymentTransactionTableModel::Date, Qt::DescendingOrder);

    ui->tablePaymentRequests->setColumnWidth(PaymentTransactionTableModel::Status, STATUS_COLUMN_WIDTH);
    ui->tablePaymentRequests->setColumnWidth(PaymentTransactionTableModel::Date, DATE_COLUMN_WIDTH);
    ui->tablePaymentRequests->setColumnWidth(PaymentTransactionTableModel::Description, DESCRIPTION_COLUMN_WIDTH);
    ui->tablePaymentRequests->setColumnWidth(PaymentTransactionTableModel::ToAddress, ADDRESS_COLUMN_WIDTH);
    ui->tablePaymentRequests->setColumnWidth(PaymentTransactionTableModel::Price, PRICE_MINIMUM_COLUMN_WIDTH);

    paymentColumnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(ui->tablePaymentRequests, PRICE_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH);
}

// TODO: PDG2 fix problem with resizing
void SendFilesDialog::resize() {
    fileColumnResizingFixer->stretchColumnWidth(PaymentTransactionTableModel::Description);
    paymentColumnResizingFixer->stretchColumnWidth(PaymentTransactionTableModel::Description);
}

void SendFilesDialog::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    resize();
}

void SendFilesDialog::on_tablePaymentRequests_doubleClicked(const QModelIndex &index) {
    if (!paymentRequestsFilter)
        return;

    // extract value
    QVariant blockQVariantFile = index.data(TransactionTableModel::TxHashRole);
    uint256 txHash = uint256(blockQVariantFile.value<QString>().toStdString());

    // prepare tx data

    if (!pwalletMain->mapWallet.count(txHash)) {
        QMessageBox::critical(this, tr("Send File"), tr("Invalid or non-wallet transaction id"),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    const CWalletTx& paymentRequestWtx = pwalletMain->mapWallet[txHash];

    if (paymentRequestWtx.type != TX_FILE_PAYMENT_REQUEST) {
        QMessageBox::critical(this, tr("Send File"), tr("Invalid transaction type. Required Payment Request"),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    // TODO: check if already confirmed

    CPaymentRequest* paymentRequest = &paymentRequestWtx.meta.get<CPaymentRequest>();

    CBitcoinAddress paymentAddress(CKeyID(paymentRequest->paymentAddress));

    if (!paymentAddress.IsValid()) {
        QMessageBox::critical(this, tr("Send File"), tr("Invalid PIVX address"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    // Amount
    CAmount nAmount = paymentRequest->nPrice;

    CWalletTx wtx;

    // crypto
    RSA* keypair = crypto::rsa::GenKeypair(2048);
    vector<char> publicKey;
    vector<char> privateKey;
    crypto::rsa::KeypairToDER(keypair, publicKey, privateKey);

    wtx.type = TX_FILE_PAYMENT_CONFIRM;
    const int nStorageDays = 30;
    wtx.meta = CPaymentConfirm(paymentRequestWtx.GetHash(), nStorageDays * 24 * 60 * 60, publicKey); // TODO: PDG 2 change lifetime

    // prepare tx

    QList<SendCoinsRecipient> recipients;
    SendCoinsRecipient recipient;
    recipient.address = QString::fromStdString(paymentAddress.ToString());
    recipient.amount = nAmount; // TODO: PDG1 ?
    recipient.useSwiftTX = false;
    recipient.inputType = ALL_COINS;
    recipients.append(recipient);
    QString strFunds = "";
    QString strFee = "";

    // ui data

    // Format confirmation message
    QStringList formatted;
    QString infoElement;

    // generate bold amount string
    QString amount =
            "<b>" + BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), recipient.amount);
    amount.append("</b> ").append(strFunds);

    infoElement.append(tr("File description") + ": " + QString::fromStdString(paymentRequest->sComment) + "<br />");
    infoElement.append(tr("File price") + ": " + BitcoinUnits::formatHtmlWithUnit(BitcoinUnits::PIV, paymentRequest->nPrice) + "<br />");
    infoElement.append(tr("File size") + ": " + BitcoinUnits::formatBytes(paymentRequest->nFileSize) + "<br />");
    infoElement.append(tr("Storage period") + ": " + QString::number(nStorageDays) + " day(s)" + "<br />");
    formatted.append(infoElement);

    WalletModelTransaction currentTransaction(recipients, wtx.meta);

    WalletModel::SendCoinsReturn prepareStatus;
    if (model->getOptionsModel()->getCoinControlFeatures()) // coin control enabled
        prepareStatus = model->prepareTransaction(currentTransaction, CoinControlDialog::coinControl);
    else
        prepareStatus = model->prepareTransaction(currentTransaction);

    if (prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        QMessageBox::critical(this, tr("Send file payment"), tr("Error to prepare transaction. Status code: %1").arg((int)prepareStatus.status),
                              QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    QString questionString = tr("Are you sure you want to pay for file?");
    questionString.append("<br /><br />%1");

    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are file transaction and storage fee"));
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
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send payment"),
                                                               questionString.arg(formatted.join("<br />")),
                                                               QMessageBox::Yes | QMessageBox::Cancel,
                                                               QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) {
        return;
    }

    // save keys
    CWalletDB wdb(pwalletMain->strWalletFile, "r+");
    wdb.WriteFileEncryptKeys(((CTransaction *) &paymentRequestWtx)->GetHash(), publicKey, privateKey);

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction);

    if (sendStatus.status != WalletModel::OK) {
        wdb.EraseFileEncryptKeys(((CTransaction *) &paymentRequestWtx)->GetHash());
    }
}

void SendFilesDialog::on_tableFileTransactions_doubleClicked(const QModelIndex &index)
{
    if (!filter)
        return;

    // extract value
    QVariant blockQVariantFile = index.data(TransactionTableModel::TxHashRole);
    uint256 txHash = uint256(blockQVariantFile.value<QString>().toStdString());

    saveFileFromTx(txHash);
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

bool SendFilesDialog::saveFileMeta(const SendCoinsRecipient &recipient,
                                   WalletModelTransaction &currentTransaction) const {
    // save data to wallet, needed when file will send
    CKeyID destinationKeyId;
    if (!CBitcoinAddress(recipient.address.toStdString()).GetKeyID(destinationKeyId))
        return error("%s: destination address invalid: %s", __func__, recipient.address.toStdString().data());

    CWalletFileTx wftx;
    wftx.filename = recipient.filename.toStdString();
    wftx.vchBytes = recipient.vchFile;
    CTransaction *tx = (CTransaction *) currentTransaction.getTransaction();
    wftx.paymentRequestTxid = SerializeHash(*tx); // TODO: check twice
    wftx.destinationAddress = destinationKeyId;

    CWalletDB walletdb(pwalletMain->strWalletFile);
    return walletdb.WriteWalletFileTx(wftx);
}

void SendFilesDialog::saveFileFromTx(const uint256 &txHash) {
    CTransaction fileTx;
    uint256 hashBlock;

    // find transaction in blockchain
    if (!GetTransaction(txHash, fileTx, hashBlock, true)) {
        QMessageBox::critical(this, tr("Save file"), tr("Unable to find file transaction"));
        return;
    }

    CDBFile dbFile;
    if (!GetFile(fileTx.vfiles[0].fileHash, dbFile)) {
        QMessageBox::critical(this, tr("Save file"), tr("Unable to find file in local storage. May be you need to resync file storage"));
    }

    // check hash of encrypted file
    if (fileTx.vfiles[0].fileHash != Hash(dbFile.vBytes.begin(), dbFile.vBytes.end())) {
        QMessageBox::critical(this, tr("Save file"), tr("File hash doesn't match. File corrupted"));
        return;
    }

    CFileMeta* fileMeta = &fileTx.meta.get<CFileMeta>();

    CTransaction paymentTx;
    if (!GetTransaction(fileMeta->confirmTxId, paymentTx, hashBlock, true)) {
        QMessageBox::critical(this, tr("Save file"), tr("Unable to find file payment transaction"));
        return;
    }

    const uint256 &requestTxid = paymentTx.meta.get<CPaymentConfirm>().requestTxid;

    // red key from wallet db
    CWalletDB wdb(pwalletMain->strWalletFile, "r+");
    vector<char> publicKey;
    vector<char> privateKey;
    if (!wdb.ReadFileEncryptKeys(requestTxid, publicKey, privateKey) || privateKey.empty()) {
        QMessageBox::critical(this, tr("Save file"), tr("Decrypt file error. Failed to load file keys. Possible wallet is damaged"));
        return;
    }
    RSA* privKey = PrivateDERToKey(privateKey);
    unique_ptr<RSA> privKeyPtr(privKey);

    // decrypt meta
    vector<char> outMeta;
    RSADecrypt(privKey, fileMeta->vfEncodedMeta, outMeta);

    CDataStream metaStream(SER_NETWORK, PROTOCOL_VERSION);
    metaStream.write(&outMeta[0], outMeta.size());
    CEncodedMeta encodedMeta;
    metaStream >> encodedMeta;
    string filename(encodedMeta.vfFilename.begin(),encodedMeta.vfFilename.end());
    string description(encodedMeta.vfFilename.begin(),encodedMeta.vfFilename.end());

    // choose destination filename
    QString destFilename = QFileDialog::getSaveFileName(this,
                                                        tr("Save File: %1") // TODO: remove information after UI will be implemented
                                                                .arg(QString::fromStdString(filename)),
                                                        filename.data(), tr("All Files (*)")
    );

    if (destFilename.isEmpty())
        return;

    // extract key
    AESKey key;
    memcpy(&key.key[0], &encodedMeta.vfFileKey[0], encodedMeta.vfFileKey.size());

    // decrypt file
    CDataStream encodedStream(SER_NETWORK, PROTOCOL_VERSION);
    encodedStream.write(&dbFile.vBytes[0], dbFile.vBytes.size());
    CDataStream destStream(SER_NETWORK, PROTOCOL_VERSION);
    DecryptAES(key, destStream, encodedStream, encodedStream.size());

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
