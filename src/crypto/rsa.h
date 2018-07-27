// Copyright (c) 2018 PrivateDatagram Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RSA_H
#define RSA_H

#include <vector>
#include <openssl/rsa.h>
#include <openssl/engine.h>
#include <openssl/pem.h>

using namespace std;

namespace crypto {
    namespace rsa {

        /**
         * Generates RSA key pair
         * @param keyLen in bits (2048, 4096)
         * @return RSA keypair structure, must be freed
         */
        RSA *GenKeypair(int keyLen);

        /**
         * RSA keypair to binary DER public and private
         */
        void KeypairToDER(RSA *keypair, vector<unsigned char> &vchoutPubKey, vector<unsigned char> &vchoutPrivateKey);

        RSA *PublicDERToKey(vector<unsigned char> &derKey);

        RSA *PrivateDERToKey(vector<unsigned char> &derKey);

        /**
         * Converts keypair to PEM string format
         * @param keypair keypair
         * @param outPubKey PEM string public key ends with terminator may be null if don't want to fetch it, must be freed
         * @param outPrivateKey PEM string private key ends with terminator may be null if don't want to fetch it, must be freed
         */
        void KeypairToPEM(RSA *keypair, char **outPubKey, char **outPrivateKey);

        /**
         * Encrypts data
         * @param pubKey RSA public key
         * @param data source data to be encrypted
         * @param dataSize input data size
         * @param outData pointer to uninitialized char pointer, must be freed
         * @param outLen outData length
         * @return successfulness
         */
        bool RSAEncrypt(RSA *pubKey, const unsigned char *data, int dataSize, unsigned char **outData, int *outLen);

        /**
         * Decrypts data
         * @param privKey RSA private key
         * @param encryptedData source encrypted data to be decrypted
         * @param encryptedSize input data size
         * @param outData decrypted data, pointer to uninitialized char pointer, must be freed
         * @param outLen outData length
         * @return successfulness
         */
        bool RSADecrypt(RSA *privKey, const unsigned char *encryptedData, int encryptedSize, unsigned char **outData,
                        int *outLen);

        bool RSAEncrypt(RSA *pubKey, const vector<unsigned char> &vchData, vector<unsigned char> &vchOutEncrypted);

        bool RSADecrypt(RSA *pubKey, const vector<unsigned char> &vchEncrypted, vector<unsigned char> &vchOutData);

    }
}

#endif //RSA_H