// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.p

#include "files.h"

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB* pblocktree;

/** Global variable that points to the active block file tree (protected by cs_main) */
extern CBlockFileTreeDB* pblockfiletree;


bool SaveFileDB(CDBFile& file) {
    unsigned int nFileSize = ::GetSerializeSize(file, SER_DISK, CLIENT_VERSION);
    CDiskFileBlockPos filePos;
    CValidationState state;
    if (!FindFileBlockPos(state, filePos, nFileSize + 8, 0))
        return error("%s : Failed to find file block pos with fileHash - %s", __func__, file.fileHash.ToString());

    if (!WriteFileBlockToDisk(file, filePos))
        return error("%s : Failed to write file block to disk with fileHash - %s", __func__, file.fileHash.ToString());

    if (!pblockfiletree->WriteFileIndex(file.CalcFileHash(), filePos))
        return error("%s : Failed to write file index with fileHash - %s", __func__, file.fileHash.ToString());

    UpdateFileBlockPosData(filePos);

    return true;
}

bool EraseFileDB(CDBFile& file) {
    CDiskFileBlockPos pos;
    if (!pblockfiletree->ReadFileIndex(file.fileHash, pos))
        return error("%s : Failed to read file index with fileHash - %s", __func__, file.fileHash.ToString());

    if (!pblockfiletree->EraseFileIndex(file.fileHash))
        return error("%s : Failed to delete file index with fileHash - %s", __func__, file.fileHash.ToString());

    if (!RemoveFileBlockFromDisk(pos))
        return error("%s : Failed to remove file from blocks with fileHash - %s", __func__, file.fileHash.ToString());

    return true;
}