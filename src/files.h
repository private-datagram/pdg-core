// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.p

#ifndef PDG_FILES_H
#define PDG_FILES_H

#include <inttypes.h>
#include <string>
#include <algorithm>
#include <vector>
#include <fstream>
#include <climits>
#include "util.h"
#include "hash.h"
#include "crypto/aes.h"
#include "crypto/rsa.h"
#include "primitives/transaction.h"
#include "streams.h"

bool PrepareMeta(Stream& inputFile, std::string filename, AESKey& key, CFileMeta& outFileMeta);

#endif //PDG_FILES_H
