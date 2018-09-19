// Copyright (c) 2018 PrivateDatagram Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/rsa.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <vector>
#include <openssl/rsa.h>
#include <openssl/engine.h>
#include <openssl/pem.h>
#include <iostream>

#define PADDING RSA_PKCS1_PADDING

namespace crypto {
    namespace rsa {

        /**
         * Generates RSA key pair
         * @param keyLen in bits (2048, 4096)
         * @return RSA keypair structure, must be freed
         */
        RSA *GenKeypair(int keyLen) {
            return RSA_generate_key(keyLen, 3, NULL, NULL);
        }

        /**
         * RSA keypair to binary DER public and private
         */
        void KeypairToDER(RSA *keypair, vector<char> &vchoutPubKey, vector<char> &vchoutPrivateKey) {
            unsigned char buffer[2048];
            unsigned char *pbuffer;

            // public key
            pbuffer = &buffer[0];
            int len = i2d_RSAPublicKey(keypair, &pbuffer);
            vchoutPubKey.resize(len);
            memcpy(&vchoutPubKey[0], &buffer[0], len);

            // private
            pbuffer = &buffer[0];
            len = i2d_RSAPrivateKey(keypair, &pbuffer);
            vchoutPrivateKey.resize(len);
            memcpy(&vchoutPrivateKey[0], &buffer[0], len);
        }

        RSA *PublicDERToKey(const vector<char> &derKey) {
            const unsigned char *chDerKey = reinterpret_cast<const unsigned char *>(&derKey[0]);
            return d2i_RSAPublicKey(NULL, &chDerKey, derKey.size());
        }

        RSA *PrivateDERToKey(const vector<char> &derKey) {
            // binary DER to RSA structure
            const unsigned char *chDerKey = reinterpret_cast<const unsigned char *>(&derKey[0]);
            return d2i_RSAPrivateKey(NULL, &chDerKey, derKey.size());
        }

        /**
         * Converts keypair to PEM string format
         * @param keypair keypair
         * @param outPubKey PEM string public key ends with terminator may be null if don't want to fetch it, must be freed
         * @param outPrivateKey PEM string private key ends with terminator may be null if don't want to fetch it, must be freed
         */
        void KeypairToPEM(RSA *keypair, char **outPubKey, char **outPrivateKey) {
            if (outPubKey != NULL) {
                BIO *pubBIO = BIO_new(BIO_s_mem());
                PEM_write_bio_RSAPublicKey(pubBIO, keypair);
                int pub_len = BIO_pending(pubBIO);

                char *pub_key = static_cast<char *>(malloc(pub_len + 1));
                BIO_read(pubBIO, pub_key, pub_len);
                pub_key[pub_len] = '\0';

                *outPubKey = pub_key;

                BIO_free(pubBIO);
            }

            if (outPrivateKey != NULL) {
                BIO *privBIO = BIO_new(BIO_s_mem());
                PEM_write_bio_RSAPrivateKey(privBIO, keypair, NULL, NULL, 0, NULL, NULL);
                int pri_len = BIO_pending(privBIO);

                char *pri_key = static_cast<char *>(malloc(pri_len + 1));
                BIO_read(privBIO, pri_key, pri_len);
                pri_key[pri_len] = '\0';

                *outPrivateKey = pri_key;

                BIO_free(privBIO);
            }
        }

        /**
         * Encrypts data
         * @param pubKey RSA public key
         * @param data source data to be encrypted
         * @param dataSize input data size
         * @param outData pointer to uninitialized char pointer, must be freed
         * @param outLen outData length
         * @return successfulness
         */
        bool RSAEncrypt(RSA *pubKey, const char *data, int dataSize, char **outData, int *outLen) {
            int rsaLen = RSA_size(pubKey);
            unsigned char *encrypted = (unsigned char *) malloc(rsaLen);

            int len = RSA_public_encrypt(dataSize, reinterpret_cast<const unsigned char *>(data), encrypted, pubKey, PADDING);
            if (len == -1) {
                printf("ERROR: RSA_public_encrypt: %s\n", ERR_error_string(ERR_get_error(), NULL)); // TODO: to log
                delete encrypted;
                return false;
            }

            *outLen = len;
            *outData = reinterpret_cast<char *>(encrypted);

            return true;
        }

        /**
         * Decrypts data
         * @param privKey RSA private key
         * @param encryptedData source encrypted data to be decrypted
         * @param encryptedSize input data size
         * @param outData decrypted data, pointer to uninitialized char pointer, must be freed
         * @param outLen outData length
         * @return successfulness
         */
        bool RSADecrypt(RSA *privKey, const char *encryptedData, int encryptedSize, char **outData, int *outLen) {
            int rsaLen = RSA_size(privKey); // That's how many bytes the decrypted data would be

            unsigned char *decrypted = (unsigned char *) malloc(rsaLen);
            int len = RSA_private_decrypt(encryptedSize, reinterpret_cast<const unsigned char *>(encryptedData), decrypted, privKey, PADDING);
            if (len == -1) {
                printf("ERROR: RSA_private_decrypt: %s\n", ERR_error_string(ERR_get_error(), NULL));
                free(decrypted);
                return false;
            }

            *outLen = len;
            *outData = reinterpret_cast<char *>(decrypted);

            return true;
        }

        bool RSAEncrypt(RSA *pubKey, const vector<char> &vchData, vector<char> &vchOutEncrypted) {
            char *data;
            int len;

            if (!RSAEncrypt(pubKey, &vchData[0], (int) vchData.size(), &data, &len))
                return false;

            vchOutEncrypted.resize(len);
            memcpy(&vchOutEncrypted[0], data, len);
            free(data);

            return true;
        }

        bool RSADecrypt(RSA *privKey, const vector<char> &vchEncrypted, vector<char> &vchOutData) {
            char *data;
            int len;

            if (!RSADecrypt(privKey, &vchEncrypted[0], (int) vchEncrypted.size(), &data, &len)) {
                return false;
            }

            vchOutData.resize(len);
            memcpy(&vchOutData[0], data, len);
            free(data);

            return true;
        }

    }
}