// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.p

#include "files.h"


FILE* OpenDiskFile(unsigned int nPos, boost::filesystem::path& path, bool fReadOnly)
{
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (nPos) {
        if (fseek(file, nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}