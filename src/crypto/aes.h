// Copyright (c) 2018 PrivateDatagram Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AES_H
#define AES_H

#include <inttypes.h>
#include <string>
#include <algorithm>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <cstring>
#include <vector>
#include <fstream>
#include <climits>
#include <util.h>

#define AES_BITS 256
#define BUFFER_SIZE 2048

namespace crypto {
    namespace aes {

        struct AESKey {
            unsigned char key[AES_BITS / 8];
        };

        void GenerateAESKey(AESKey &outKey) {
            RAND_bytes(outKey.key, sizeof(outKey.key));
        }

        template <typename Stream>
        bool EncryptAES(AESKey &key, Stream &dstStream, Stream &srcStream, unsigned long size, unsigned long *outSize = NULL) {
            uint8_t iv[AES_BLOCK_SIZE];
            RAND_bytes(&iv[0], sizeof(iv));

            // Setup the AES Key structure required for use in the OpenSSL APIs
            AES_KEY *AesKey = new AES_KEY();
            AES_set_encrypt_key(key.key, AES_BITS, AesKey);

            unsigned char srcBuf[BUFFER_SIZE + AES_BLOCK_SIZE];
            unsigned char encBuf[BUFFER_SIZE + AES_BLOCK_SIZE];
            unsigned long totalRead = 0;
            unsigned long bufSize;
            unsigned long read;

            // write iv as is
            dstStream.write((char *) &iv[0], sizeof(iv));

            // first block random data to more security
            RAND_bytes(&srcBuf[0], AES_BLOCK_SIZE);
            AES_cbc_encrypt(&srcBuf[0], &encBuf[0], AES_BLOCK_SIZE, (const AES_KEY *) AesKey, &iv[0], AES_ENCRYPT);
            dstStream.write((char *) &encBuf[0], AES_BLOCK_SIZE);

            // read, encrypt and write
            while (true) {
                read = (totalRead + BUFFER_SIZE) > size ? (size - totalRead) : BUFFER_SIZE;
                srcStream.read((char *) &srcBuf[0], read);
                totalRead += read;
                bufSize = read;

                // fill the rest space of last block with random data for padding
                if (totalRead == size) {
                    bufSize += static_cast<int>(AES_BLOCK_SIZE - (read % AES_BLOCK_SIZE));

                    // if read exactly multiple AES_BLOCK_SIZE, add new block
                    if (bufSize == read)
                        bufSize += AES_BLOCK_SIZE;

                    for (unsigned long i = read; i < bufSize - 1; ++i) {
                        srcBuf[i] = static_cast<unsigned char>(rand() & 0xFF);
                    }

                    // put padding length
                    srcBuf[bufSize - 1] = static_cast<unsigned char>((bufSize - read) & 0xff);
                }

                AES_cbc_encrypt(&srcBuf[0], &encBuf[0], bufSize, (const AES_KEY *) AesKey, &iv[0], AES_ENCRYPT);

                dstStream.write((char *) &encBuf[0], bufSize);

                if (totalRead == size)
                    break;
            }

            if (outSize != NULL)
                *outSize = totalRead + sizeof(iv) + AES_BLOCK_SIZE;

            return true;
        }

        template <typename Stream>
        bool DecryptAES(AESKey &key, Stream &dstStream, Stream &srcStream, unsigned long size, unsigned long *outSize = NULL) {
            // Setup the AES Key structure required for use in the OpenSSL APIs
            AES_KEY *aesDecryptKey = new AES_KEY();
            AES_set_decrypt_key(key.key, AES_BITS, aesDecryptKey);

            unsigned char srcBuf[BUFFER_SIZE + AES_BLOCK_SIZE];
            unsigned char decBuf[BUFFER_SIZE + AES_BLOCK_SIZE];
            unsigned long totalRead = 0;
            unsigned long totalWrite = 0;
            unsigned long bufSize;
            unsigned long read;

            if (size < AES_BLOCK_SIZE * 2) {
                LogPrintStr("DecryptAES error. size < AES_BLOCK_SIZE * 2. size: " + size);
                return false;
            }

            // read iv
            uint8_t iv[AES_BLOCK_SIZE];
            srcStream.read((char *) &iv[0], sizeof(iv));
            totalRead += AES_BLOCK_SIZE;

            // read and skip random block
            srcStream.read((char *) &srcBuf[0], AES_BLOCK_SIZE);
            totalRead += AES_BLOCK_SIZE;

            AES_cbc_encrypt(&srcBuf[0], &decBuf[0], AES_BLOCK_SIZE, (const AES_KEY *) aesDecryptKey, &iv[0], AES_DECRYPT);

            // read, decrypt and write
            while (true) {
                read = (totalRead + BUFFER_SIZE) > size ? (size - totalRead) : BUFFER_SIZE;
                srcStream.read((char *) &srcBuf[0], read);
                totalRead += read;
                bufSize = read;

                AES_cbc_encrypt(&srcBuf[0], &decBuf[0], bufSize, (const AES_KEY *) aesDecryptKey, &iv[0], AES_DECRYPT);

                // subtract last random padding bytes
                if (totalRead == size) {
                    unsigned long paddingBytes = decBuf[read - 1];
                    if (paddingBytes > bufSize) {
                        LogPrintStr("Invalid padding bytes number");
                        return false;
                    }
                    bufSize -= paddingBytes;
                }

                dstStream.write((char *) &decBuf[0], bufSize);
                totalWrite += bufSize;

                if (totalRead == size)
                    break;
            }

            if (outSize != NULL)
                *outSize = totalWrite;

            return true;
        }

    }
}

#endif // AES_H
