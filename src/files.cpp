// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.p

#include "files.h"


template <typename Stream>
bool PrepareMeta(Stream& inputFile, std::string filename, AESKey &key, CFileMeta& outFileMeta) {
    // prepare meta
    CDataStream metaStream(SER_NETWORK, PROTOCOL_VERSION);

    {
        CEncodedMeta metaToEncode;
        metaToEncode.nFileSize = inputFile.size();
        metaToEncode.vfFileKey.insert(metaToEncode.vfFileKey.end(), &key.key[0], &key.key[0] + sizeof(key.key));
        metaToEncode.fileHash = Hash(inputFile.begin(), inputFile.end());
        metaToEncode.vfFilename.insert(metaToEncode.vfFilename.end(), filename.data(), filename.data() + filename.size());

        metaStream.reserve(1000);
        metaStream << metaToEncode;
    }

    // encode meta
    RSA* rsaPubKey = crypto::rsa::PublicDERToKey(paymentConfirm->vfPublicKey);

    char* encryptedMeta;
    int encryptedLen;
    if (!crypto::rsa::RSAEncrypt(rsaPubKey, &metaStream[0], metaStream.size(), &encryptedMeta, &encryptedLen)) {
        RSA_free(rsaPubKey);
        return error("%s : Failed to encrypt meta", __func__);
    }
    RSA_free(rsaPubKey);

    metaStream.clear();

    // fill filemeta
    outFileMeta.vfEncodedMeta.insert(fileMeta.vfEncodedMeta.end(), encryptedMeta, encryptedMeta + encryptedLen);
    outFileMeta.confirmTxid = tx.GetHash();

    delete encryptedMeta;
    
    return true;
}