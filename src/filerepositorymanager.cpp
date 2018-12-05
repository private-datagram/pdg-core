// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "filerepositorymanager.h"

#include "chainparams.h"
#include "net.h"
#include "txdb.h"
#include "db.h"
#include "util.h"

#include "primitives/zerocoin.h"
#include "libzerocoin/Denominations.h"
#include "invalid.h"

#include "files.h"

#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>

using namespace boost;
using namespace std;
using namespace libzerocoin;


CFileRepositoryManager::CFileRepositoryManager(int removedFilesSizeShrinkPercent): vFileRepositoryBlockInfo(), nLastFileRepositoryBlock(0), dbFileRepositoryState(removedFilesSizeShrinkPercent), lastUpdateTime(GetTimeMillis() / 1000), cs_RepositoryReadWriteLock()  {
}

bool CFileRepositoryManager::SaveFile(CDBFile& file) {
    WRITE_LOCK(cs_RepositoryReadWriteLock);

    LogPrint("file", "%s - FILES. Saving file to db. filehash: %s, filesize: %d\n", __func__, file.fileHash.ToString(), file.vBytes.size());

    unsigned int nRepositoryFileSize = GetRepositoryFileSize(file);
    CFileRepositoryBlockDiskPos filePos;
    CValidationState state;
    if (!FindAndAllocateBlockFile(state, filePos, nRepositoryFileSize))
        return error("%s : Failed to find file block pos with fileHash - %s", __func__, file.fileHash.ToString());


    if (filePos.nBlockFileIndex != 0) {
        LogPrint("%s : Failed to read DB file header", __func__);
    }

    if (!WriteFileRepositoryBlockToDisk(file, filePos, false))
        return error("%s : Failed to write file block to disk with fileHash - %s", __func__, file.fileHash.ToString());

    if (!pblockfiletree->WriteFileIndex(file.fileHash, filePos))
        return error("%s : Failed to write file index with fileHash - %s", __func__, file.fileHash.ToString());

    dbFileRepositoryState.AddFile(nRepositoryFileSize);

    LogPrint("file", "%s - FILES. File saved at position %d/%d\n", __func__, filePos.nBlockFileIndex, filePos.nOffset);

    return true;
}

bool CFileRepositoryManager::EraseFile(CDBFile& file) {
    WRITE_LOCK(cs_RepositoryReadWriteLock);

    LogPrint("file", "%s : Erase file from DB %s\n", __func__, file.fileHash.ToString());

    CFileRepositoryBlockDiskPos pos;
    if (!pblockfiletree->ReadFileIndex(file.fileHash, pos))
        return error("%s : Failed to read file index with fileHash - %s", __func__, file.fileHash.ToString());

    LogPrint("file", "%s : Erased file position: %d/%d\n", __func__, pos.nBlockFileIndex, pos.nOffset);

    if (!pblockfiletree->EraseFileIndex(file.fileHash))
        return error("%s : Failed to delete file index with fileHash - %s", __func__, file.fileHash.ToString());

    if (!RemoveFileRepositoryBlockFromDisk(pos))
        return error("%s : Failed to remove file from blocks with fileHash - %s", __func__, file.fileHash.ToString());

    dbFileRepositoryState.SubtractFile(GetRepositoryFileSize(file));

    return true;
}

bool CFileRepositoryManager::SaveFileRepositoryState() {
    LogPrint("file", "%s - FILES. Saving file repository state.%s\n", __func__, dbFileRepositoryState.ToString()); // TODO: PDG 2 remove after debug

    WRITE_LOCK(cs_RepositoryReadWriteLock);
    return SaveManagerState(vFileRepositoryBlockInfo, nLastFileRepositoryBlock);
}

bool CFileRepositoryManager::LoadFileDBState() {
    LogPrint("file", "%s - FILES. Loading file repository state.%s\n", __func__, dbFileRepositoryState.ToString()); // TODO: PDG 2 remove after debug

    READ_LOCK(cs_RepositoryReadWriteLock);
    return pblockfiletree->ReadCDBFileRepositoryState(dbFileRepositoryState);
}

bool CFileRepositoryManager::SaveManagerState(vector<CFileRepositoryBlockInfo> &vblockFileInfo, int &lastBlockFileIndex) {
    LogPrint("file", "%s - FILES. Save file repository state. vBlockFileSize=%d, lastBlockFileIndex=%d \n", __func__, vblockFileInfo.size(), lastBlockFileIndex);
    WRITE_LOCK(cs_RepositoryReadWriteLock);
    bool blockChanged = false;
    for (std::vector<CFileRepositoryBlockInfo>::const_iterator it = vblockFileInfo.begin(); it != vblockFileInfo.end(); it++) {
        if (it->lastWriteTime <= lastUpdateTime)
            continue;

        unsigned long nFile = it - vblockFileInfo.begin();
        if (!pblockfiletree->WriteFileRepositoryBlockInfo(nFile, vblockFileInfo[nFile])) {
            LogPrint("file", "%s - FILES. Failed to write to fileBlock index \n", __func__);
            return false;
        }
        blockChanged = true;
    }

    LogPrint("file", "%s - FILES. Write to file block index . %s\n", __func__, dbFileRepositoryState.ToString());
    if (blockChanged && (!pblockfiletree->WriteLastFileRepositoryBlock(lastBlockFileIndex) || !pblockfiletree->Sync())) {
        LogPrint("file", "%s - FILES. Failed to write to file block index \n", __func__);
        return false;
    }

    dbFileRepositoryState.nBlocksCount = lastBlockFileIndex + 1;

    LogPrint("file", "%s - FILES. Write file blockfiles state. %s\n", __func__, dbFileRepositoryState.ToString());
    if (!pblockfiletree->WriteCDBFileRepositoryState(dbFileRepositoryState) || !pblockfiletree->Sync()) {
        LogPrint("file", "%s - FILES. Error write file blockfiles state in db\n", __func__);
        return false;
    }

    LogPrint("file", "%s - FILES. Save file blockfiles state FINISH.\n", __func__);
    return true;
}

bool CFileRepositoryManager::LoadManagerState() {
    WRITE_LOCK(cs_RepositoryReadWriteLock);

    //Load sync file state
    FileRepositoryBlockSyncState syncState;
    if (!pblockfiletree->ReadFileRepositoryBlockSyncState(syncState)) {
        LogPrintf("%s: Filed to read repository block synchronization state. Creating new\n", __func__);

        if (!pblockfiletree->WriteFileRepositoryBlockSyncState(syncState))
            return error("%s: Filed to init repository block synchronization state.\n", __func__);
    }

    //Load fileBlock file info
    if (!pblockfiletree->ReadLastFileRepositoryBlock(nLastFileRepositoryBlock)) {
        LogPrintf("%s: Filed to read last file block. Creating new\n", __func__);

        nLastFileRepositoryBlock = 0;
        if (!pblockfiletree->WriteLastFileRepositoryBlock(nLastFileRepositoryBlock))
            return error("%s: Filed to init files repository state\n", __func__);
    }

    vFileRepositoryBlockInfo.clear();
    vFileRepositoryBlockInfo.resize(nLastFileRepositoryBlock + 1);

    LogPrintf("%s: last fileBlock file = %i\n", __func__, nLastFileRepositoryBlock);
    for (int nFile = 0; nFile <= nLastFileRepositoryBlock; nFile++) {
        if (!pblockfiletree->ReadFileRepositoryBlockInfo(nFile, vFileRepositoryBlockInfo[nFile])) {
            if (nFile == 0) {
                LogPrintf("%s: Filed to read first file repository block info. Creating new\n", __func__);
                vFileRepositoryBlockInfo[nFile].SetNull();
                if (!pblockfiletree->WriteFileRepositoryBlockInfo(nFile, vFileRepositoryBlockInfo[nFile]))
                    return error("%s: Filed to init files repository state on block info\n", __func__);
                break;
            } else {
                //this can only be when previous failed sync
                if (!syncState.IsNull()) {
                    LogPrint("file", "%s - FILES. Repository block info not Found at disk: %d .\n", __func__, nFile);
                    nFile++;
                    continue;
                }

                return error("%s: Filed to load file info from db on file: %d\n", __func__, nFile);
            }
        }
    }

    LogPrintf("%s: last fileBlock file info: %s\n", __func__, vFileRepositoryBlockInfo[nLastFileRepositoryBlock].ToString());

    // Load all saved files to db if nLastFileRepositoryBlock saved invalid
    for (int nFile = nLastFileRepositoryBlock + 1; true; nFile++) {
        CFileRepositoryBlockInfo info;
        if (!pblockfiletree->ReadFileRepositoryBlockInfo(nFile, info))
            break;

        vFileRepositoryBlockInfo.push_back(info);
    }

    nLastFileRepositoryBlock = vFileRepositoryBlockInfo.size() - 1;

    bool loadedDbFileRepositoryState = pblockfiletree->ReadCDBFileRepositoryState(dbFileRepositoryState);

    if (fReindex || !loadedDbFileRepositoryState) {
        dbFileRepositoryState.SetNull();

        LogPrintf("%s: Start reindex FileRepositoryState\n", __func__);

        uint64_t totalFilesSize = 0;
        uint32_t filesCount = 0;

        for (unsigned int nFile = 0; nFile < vFileRepositoryBlockInfo.size(); nFile++) {
            filesCount += vFileRepositoryBlockInfo[nFile].nFilesCount;
            totalFilesSize += vFileRepositoryBlockInfo[nFile].nBlockSize;
        }

        // update dbFileRepositoryState
        dbFileRepositoryState.nBlocksCount = (unsigned int) vFileRepositoryBlockInfo.size();
        dbFileRepositoryState.filesCount = filesCount;
        dbFileRepositoryState.nTotalFileStorageSize = totalFilesSize;

        if (!SaveFileRepositoryState()) {
            return error("%s: Filed to save file FileRepositoryState on reindex\n", __func__);
        }
    }

    //is sync not null - failed previous repository block sync. Reindex block files.
    if (!syncState.IsNull() && !FinishFileRepositorySync(syncState)) {
        return error("%s: Filed to finish the synchronization.\n", __func__);
    }

    return true;
}

bool CFileRepositoryManager::FinishFileRepositorySync(FileRepositoryBlockSyncState &syncState) {
    LogPrintf("%s Finish sync.\n", __func__);

    filesystem::path filesDir = GetDataDir() / "files";
    if (!filesystem::exists(filesDir)) {
        return error("%s: Filed to read temp repository block files.\n", __func__);
    }

    for (int i = 0; i <= syncState.nProcessedTempBlocks; i++) {
        filesystem::path source = GetDataDir() / "files" / strprintf("blk%05d.dat", i);
        if (!filesystem::exists(source)) {
            return error("%s: Filed to rename temp repository block file. File not found.  file number: %d\n", __func__, i);
        } else {
            LogPrintf("%s Rename temp repository block file to original. File number: %d\n", __func__, i);
            if (!RenameTmpOriginalFileBlockDisk(i)) {
                return error("%s: Filed to rename temp repository block file. File number: %d\n", __func__, i);
            }
        }
    }

    //sync end
    syncState.SetNull();
    if (!pblockfiletree->WriteFileRepositoryBlockSyncState(syncState))
        return error("%s: Filed to finish sync repository block state.\n", __func__);

    return true;
}

// TODO: figure out with locks
bool CFileRepositoryManager::GetFile(const uint256& fileHash, CDBFile& fileOut) {
    READ_LOCK(cs_RepositoryReadWriteLock);

    CFileRepositoryBlockDiskPos posFile;
    if (!pblockfiletree->ReadFileIndex(fileHash, posFile))
        return error("%s : File not found in DB. fileHash %s", __func__, fileHash.ToString());

    CDBFile file;
    if (!ReadFileBlockFromDisk(fileOut, posFile, false))
        return error("%s : File not found on disk. fileHash %s", __func__, fileHash.ToString());

    return true;
}

bool CFileRepositoryManager::ReadFileBlockFromDisk(CDBFile& file, const CFileRepositoryBlockDiskPos& pos, bool isTmp)
{
    return ReadFileBlockFromDiskTo(file, pos, SER_DISK, CLIENT_VERSION, isTmp);
}

bool CFileRepositoryManager::ReadFileBlockHeaderFromDisk(CDBFileHeaderOnly& file, const CFileRepositoryBlockDiskPos& pos, bool isTmp)
{
    return ReadFileBlockFromDiskTo(file, pos, SER_DISK, CLIENT_VERSION, isTmp);
}

bool CFileRepositoryManager::WriteFileRepositoryBlockToDisk(CDBFile &file, CFileRepositoryBlockDiskPos &pos, bool isTmp)
{
    return WriteFileRepositoryBlockToDiskFrom(file, pos, SER_DISK, CLIENT_VERSION, isTmp);
}

bool CFileRepositoryManager::WriteFileRepositoryBlockToDisk(CDBFileHeaderOnly &file, CFileRepositoryBlockDiskPos &pos, bool isTmp)
{
    return WriteFileRepositoryBlockToDiskFrom(file, pos, SER_DISK, CLIENT_VERSION, isTmp);
}

bool CFileRepositoryManager::RemoveFileRepositoryBlockFromDisk(const CFileRepositoryBlockDiskPos& pos)
{
    CFileRepositoryBlockDiskPos newPos = pos;
    CDBFile file;
    if (!ReadFileBlockFromDisk(file, newPos, false))
        return error("RemoveFileRepositoryBlockFromDisk : read file block failed");

    file.removed = true;
    if (!WriteFileRepositoryBlockToDisk(file, newPos, false))
        return error("RemoveFileRepositoryBlockFromDisk : write updated(removed) file failed");

    return true;
}

bool CFileRepositoryManager::RemoveFileRepositoryBlockFromDisk(int fileNumber, bool isTmp)
{
    boost::filesystem::path path;
    if (isTmp) {
        path = GetTmpFilePosFilename(fileNumber, "blk");
    } else {
        path = GetFilePosFilename(fileNumber, "blk");
    }

    LogPrint("file", "%s - FILES. Remove file from disk. path: %s\n", __func__, path);

    if (!remove(path)) {
        LogPrint("file", "%s - FILES. Remove file from disk failed.\n", __func__);
        return false;
    }

    return true;
}

bool CFileRepositoryManager::RenameTmpOriginalFileBlockDisk(int tmpFileNumber)
{
    boost::filesystem::path tmpPath = GetTmpFilePosFilename(tmpFileNumber, "blk");
    boost::filesystem::path path = GetFilePosFilename(tmpFileNumber, "blk");
    LogPrint("file", "%s - FILES. Rename tmp fileblock file from disk. path: %s to %s\n", __func__, tmpPath, path);

    //remove original file
    rename(tmpPath, path);
    return true;
}

void CFileRepositoryManager::FlushBlockFiles() {
    if (vFileRepositoryBlockInfo.empty())
        return;

    FlushFileRepositoryBlock(nLastFileRepositoryBlock, vFileRepositoryBlockInfo[nLastFileRepositoryBlock].nBlockSize);
}

void CFileRepositoryManager::FlushFileRepositoryBlock(int nLastBlockIndex, unsigned int nLastBlockSize, bool fFinalize, bool isTmp) {
    //in order to open last disk file
    CFileRepositoryBlockDiskPos lastFilePos(nLastBlockIndex, 0, 0);
    FILE* fileLast = OpenFileRepositoryBlock(lastFilePos, false, isTmp);
    if (fileLast) {
        if (fFinalize)
            TruncateFile(fileLast, nLastBlockSize);
        FileCommit(fileLast);
        fclose(fileLast);
    }
}

bool CFileRepositoryManager::FindAndAllocateBlockFile(CValidationState& state, CFileRepositoryBlockDiskPos &pos, const uint32_t nDBFileSize) {
    LogPrint("file", "%s - FILES. Find file block pos. \n", __func__);
    return FindAndAllocateBlockFile(state, pos, nDBFileSize, nLastFileRepositoryBlock, vFileRepositoryBlockInfo, false);
}

bool CFileRepositoryManager::FindAndAllocateBlockFile(CValidationState &state, CFileRepositoryBlockDiskPos &pos,
                              const uint32_t nAddSize, int &lastBlockFileIndex, vector<CFileRepositoryBlockInfo> &vblockFileInfo, bool isTmp)
{
    pos.nBlockFileIndex = lastBlockFileIndex;
    LogPrint("file", "%s - FILES. Number last fileblock disk file: %d\n", __func__, lastBlockFileIndex);

    if (nAddSize > MAX_FILEBLOCKFILE_SIZE) {
        LogPrint("file", "%s - FILES. Files size too large to save in one block.\n", __func__);
        return false;
    }

    if (vblockFileInfo[pos.nBlockFileIndex].nBlockSize + nAddSize >= MAX_FILEBLOCKFILE_SIZE) {
        LogPrint("file", "%s - FILES. MAX_FILEBLOCKFILE_SIZE. Flush fileblock disk file.\n", __func__);

        // finalize block (truncate file)
        FlushFileRepositoryBlock(lastBlockFileIndex, vblockFileInfo[lastBlockFileIndex].nBlockSize, true, isTmp);

        //update meta previous diskfile
        vblockFileInfo[pos.nBlockFileIndex].isFull = true;

        // allocate new block and update state
        vblockFileInfo.emplace_back(CFileRepositoryBlockInfo());
        pos.nBlockFileIndex = lastBlockFileIndex = vblockFileInfo.size() - 1;
        dbFileRepositoryState.nBlocksCount = vblockFileInfo.size();
    }

    //update meta data
    LogPrint("file", "%s - FILES. Updated meta data. Number last file disk file: %d\n", __func__, lastBlockFileIndex);

    pos.nOffset = vblockFileInfo[lastBlockFileIndex].nBlockSize;
    pos.nFileSize = nAddSize;

    vblockFileInfo[lastBlockFileIndex].AddFile(nAddSize);

    unsigned int nOldChunks = (pos.nOffset + FILEBLOCKFILE_CHUNK_SIZE - 1) / FILEBLOCKFILE_CHUNK_SIZE;
    LogPrint("file", "%s - FILES. Old chunks: %d\n", __func__, nOldChunks);

    unsigned int nNewChunks = (vblockFileInfo[lastBlockFileIndex].nBlockSize + FILEBLOCKFILE_CHUNK_SIZE - 1) / FILEBLOCKFILE_CHUNK_SIZE;
    LogPrint("file", "%s - FILES. New chunks: %d\n", __func__, nNewChunks);

    if (nNewChunks > nOldChunks) {
        if (!CheckDiskSpace(nNewChunks * FILEBLOCKFILE_CHUNK_SIZE - pos.nOffset)) {
            return state.Error("out of disk space");
        }

        FILE* file = OpenFileRepositoryBlock(pos, false, isTmp);
        LogPrint("file", "%s - FILES. Open fileblock disk file. number position: %d\n", __func__, pos.nOffset);
        if (file) {
            LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * FILEBLOCKFILE_CHUNK_SIZE, pos.nOffset);
            AllocateFileRange(file, pos.nOffset, nNewChunks * FILEBLOCKFILE_CHUNK_SIZE - pos.nOffset);
            fclose(file);
        }
    }

    return true;
}

//file region
FILE* CFileRepositoryManager::OpenFileRepositoryBlock(const CFileRepositoryBlockDiskPos& pos, bool fReadOnly, bool isTmp)
{
    boost::filesystem::path path;
    if (isTmp) {
        path = GetTmpFilePosFilename(pos.nBlockFileIndex, "blk");
    } else {
        path = GetFilePosFilename(pos.nBlockFileIndex, "blk");
    }

    LogPrintf("Open fileblock file: %s\n", path.string());

    if (pos.IsNull())
        return NULL;

    return OpenDiskFile(pos.nOffset, path, fReadOnly);
}

boost::filesystem::path CFileRepositoryManager::GetFilePosFilename(const int numberDiskFile, const char* prefix)
{
    return GetDataDir() / "files" / strprintf("%s%05u.dat", prefix, numberDiskFile);
}

boost::filesystem::path CFileRepositoryManager::GetTmpFilePosFilename(const int numberDiskFile, const char* prefix)
{
    return GetDataDir() / "files" / strprintf("tmp_%s%05u.dat", prefix, numberDiskFile);
}
//end region

//todo: PDG5 remove
void CFileRepositoryManager::FillTestData() {
    WRITE_LOCK(cs_RepositoryReadWriteLock);
    LogPrint("file", "%s - -----------------------\n", __func__);
    LogPrint("file", "%s - FILES. Fill test data.\n", __func__);

    bool isFiling = true;
    int nFiles = 0;
    while (isFiling) {
            CDBFile dbFile;
            vector<char> vTestFile;
            int nChars = rand() % 2000 + 10000;
            //int nChars = 5000000;
            char myword[] = { 'H', 'e', 'l', 'l', 'o', '\0', 'd', '2', '@'};

            for (int i = 0; i < nChars; i++) {
                int charId = rand() %  (sizeof(myword)/sizeof(*myword));
                vTestFile.emplace_back(myword[charId]);
            }

            dbFile.vBytes.reserve(vTestFile.size());
            dbFile.vBytes.assign(vTestFile.begin(), vTestFile.end());
            dbFile.UpdateFileHash();

            //1.5 minutes
            //30 days 30 * 24 * 60 * 60 * 1000 * 1000
            dbFile.fileExpiredDate = GetAdjustedTime() +  rand() % 180 + 60;
            LogPrint("file", "%s - FILES. Saving file, file hash: %s, calc file hash: %s\n", __func__, dbFile.fileHash.ToString(), dbFile.CalcFileHash().ToString());

            if (!SaveFileDB(dbFile))
                LogPrint("%s : Failed to save file to db for fileHash - %s", __func__, dbFile.fileHash.ToString());

            LogPrint("%s :TEST FILE - test file be saved. nFile:%d , byteSize: %d", __func__, nFiles, dbFile.vBytes.size());
            nFiles++;

            if (nFiles == 420) {
                break;
            }
      }

    LogPrint("file", "%s - -----------------------\n", __func__);

    //fill data
}

void CFileRepositoryManager::FindAndRecycleExpiredFiles() {
    LogPrint("file", "%s - FILES. Process mark remove files scheduler.\n", __func__);

    LOCK(cs_main);
    WRITE_LOCK(cs_RepositoryReadWriteLock);

    boost::scoped_ptr<leveldb::Iterator> pcursor(pblockfiletree->NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << 'd';
    pcursor->Seek(ssKeySet.str());

    int countFile = 0;

    // Load file position
    while (pcursor->Valid()) {
        //boost::this_thread::interruption_point();
        try {
            leveldb::Slice sliceKey = pcursor->key();
            CDataStream ssKey(sliceKey.data(), sliceKey.data() + sliceKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType != 'd') {
                break; // if shutdown requested or finished inter on file pos.
            }
            countFile++;

            leveldb::Slice sliceValue = pcursor->value();
            CDataStream ssValue(sliceValue.data(), sliceValue.data() + sliceValue.size(), SER_DISK, CLIENT_VERSION);
            CFileRepositoryBlockDiskPos pos;
            ssValue >> pos;

            LogPrint("file", "%s - FILES. Cursor on file index. file position: %d/%d .\n", __func__, pos.nBlockFileIndex, pos.nOffset);

            CDBFileHeaderOnly fileHeader;

            if (pos.nBlockFileIndex != 0) {
                LogPrint("%s : Failed to read DB file header", __func__);
            }
            if (!ReadFileBlockHeaderFromDisk(fileHeader, pos, false)) {
                LogPrint("%s : Failed to read DB file header", __func__);
                return;
            }

            LogPrint("file", "%s - FILES. DB file load. fileHash: %s. File expired time: %d.\n", __func__, fileHeader.fileHash.ToString(), fileHeader.fileExpiredDate);
            if (fileHeader.removed == true) {
                LogPrint("file", "%s - FILES. DB file be mark as remove. Continue.\n", __func__);
                pcursor->Next();
                continue;
            }

            int64_t now = GetAdjustedTime();
            if (now > fileHeader.fileExpiredDate && !fileHeader.isMine) {
                fileHeader.removed = true;
                if (!WriteFileRepositoryBlockToDisk(fileHeader, pos, false)) {
                    LogPrint("file", "%s - FILES. Write updated(removed) file failed.\n", __func__);
                    return;
                }

                dbFileRepositoryState.SubtractFile(pos.nFileSize);
                LogPrint("file", "%s - FILES. File expired. DB file mark as remove and save at db.\n", __func__);
            }

            pcursor->Next();
        } catch (std::exception& e) {
            LogPrint("%s : Deserialize or I/O error - %s", __func__, e.what());
            return;
        }
    }

    LogPrint("file", "%s - FILES. Process mark remove files scheduler finish.\n", __func__);
}

bool CFileRepositoryManager::handleEmptySrcFile(FileRepositoryBlockSyncState &syncState, const CFileRepositoryBlockDiskPos &srcFilePos) {
    LogPrint("file", "%s - FILES. Repository block file is empty. Remove file from disk. Filenumber: %d", __func__, srcFilePos.nBlockFileIndex);

    if (!RemoveFileRepositoryBlockFromDisk(srcFilePos.nBlockFileIndex, false)) {
        LogPrint("file", "%s - FILES. Failed to remove file block at disk. repository block file number: %d", __func__, srcFilePos.nBlockFileIndex);
        return false;
    }

    if (!pblockfiletree->EraseFileRepositoryBlockInfo(srcFilePos.nBlockFileIndex)) {
        LogPrint("file", "%s - FILES. Failed to remove file block info at disk. repository block file number: %d", __func__, srcFilePos.nBlockFileIndex);
        return false;
    }

    //update sync state after remove original file
    if (syncState.nProcessedSourceBlocks != srcFilePos.nBlockFileIndex) {
        syncState.nProcessedSourceBlocks = srcFilePos.nBlockFileIndex;

        if (!pblockfiletree->WriteFileRepositoryBlockSyncState(syncState)) {
            LogPrint("file", "%s - FILES. Filed to write repository block synchronization state after remove original.\n", __func__);
            return false;
        }
    }

    return true;
}

void CFileRepositoryManager::ShrinkRecycledFiles() {
    LogPrint("file", "%s - FILES. Process diskfile erase scheduler. FileRepositoryState: %s\n", __func__, dbFileRepositoryState.ToString());

    // check if shrink needed
    {
        READ_LOCK(cs_RepositoryReadWriteLock);
        if (!dbFileRepositoryState.IsClearDiskSpaceNeeded()) {
            LogPrint("file", "%s - FILES. diskfile don't need cleaning.\n", __func__);
            return;
        }
    }

    LOCK(cs_main);
    WRITE_LOCK(cs_RepositoryReadWriteLock);

    //set to zero
    dbFileRepositoryState.SetNull();

    FileRepositoryBlockSyncState syncState;
    if (!pblockfiletree->ReadFileRepositoryBlockSyncState(syncState)) {
        LogPrint("file", "%s - FILES. ERROR. Filed to read repository block synchronization state.\n", __func__);
        return;
    }

    if (!syncState.IsNull()) {
        LogPrint("file", "%s - FILES. ERROR. Previous sync not completed .\n", __func__);
        return;
    }

    boost::scoped_ptr<leveldb::Iterator> pcursor(pblockfiletree->NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << 'd';
    pcursor->Seek(ssKeySet.str());

    vector<CFileRepositoryBlockInfo> vNewTempFileRepositoryBlockInfo;
    vNewTempFileRepositoryBlockInfo.resize(1);
    int nNewRepositoryBlockIndex = 0;

    //create empty temp_blk file
    CFileRepositoryBlockDiskPos newPosTmp(0, 0, 0);
    LogPrint("file", "%s - FILES. Create new empty tmep file\n", __func__);
    FILE* newFileTmp = OpenFileRepositoryBlock(newPosTmp, false, true);
    if (!newFileTmp)
        LogPrint("file", "%s - FILES. Failed create new empty tmep file \n", __func__);

    map<CFileRepositoryBlockDiskPos, uint8_t> repositoryFileMap;

    int countFile = 0;

    // Load positions from database
    while (pcursor->Valid()) {
        // boost::this_thread::interruption_point();
        try {
            leveldb::Slice sliceKey = pcursor->key();
            CDataStream ssKey(sliceKey.data(), sliceKey.data() + sliceKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType != 'd') {
                break; // if shutdown requested or finished inter on file pos.
            }

            countFile++;

            leveldb::Slice sliceValue = pcursor->value();
            CDataStream ssValue(sliceValue.data(), sliceValue.data() + sliceValue.size(), SER_DISK, CLIENT_VERSION);
            CFileRepositoryBlockDiskPos pos;
            ssValue >> pos;

            LogPrint("file", "%s - FILES. Cursor on file index. file position: %d/%d .\n", __func__, pos.nBlockFileIndex, pos.nOffset);

            repositoryFileMap.erase(pos);

            pcursor->Next();
        } catch (std::exception& e) {
            LogPrint("file", "%s : Deserialize or I/O error - %s.\n", __func__, e.what());
            return;
        }
    }

    LogPrint("file", "%s - FILES. File fetch positions finished.\n", __func__);

    //handle temp files.
    LogPrint("file", "%s - FILES. Fill temp files.\n", __func__);

    //write sync start status
    syncState.SetNull();
    if (!pblockfiletree->WriteFileRepositoryBlockSyncState(syncState)) {
        LogPrint("file", "%s - FILES. Filed to write repository block synchronization state.\n", __func__);
        return;
    }

    //fill temp files.
    for (auto it = repositoryFileMap.begin(); it != repositoryFileMap.end(); it++) {
        const CFileRepositoryBlockDiskPos &srcFilePos = it->first;
        CFileRepositoryBlockInfo &srcBlockInfo = vFileRepositoryBlockInfo[srcFilePos.nBlockFileIndex];

        LogPrint("file", "%s - FILES. Cursor on file index. file position: %d/%d .\n", __func__, srcFilePos.nBlockFileIndex, srcFilePos.nOffset);

        CDBFile file;
        if (!ReadFileBlockFromDisk(file, srcFilePos, false)) {
            LogPrint("file", "%s - FILES. Read file block failed.\n", __func__);
            return;
        }

        unsigned int nRepositoryFileSize = GetRepositoryFileSize(file);
        const uint256 &fileHash = file.fileHash;

        if (file.removed) {
            LogPrint("file", "%s - FILES. File mark as removed, don't rewrite at new diskfile. Removed file hash: s%\n", __func__, fileHash.ToString());
            srcBlockInfo.SubtractFile(nRepositoryFileSize);

            CFileRepositoryBlockDiskPos testDbFilePos;
            if (!pblockfiletree->ReadFileIndex(fileHash, testDbFilePos)) {
                LogPrint("file", "%s : Failed to erase file index with fileHash - %s", __func__, fileHash.ToString());
                return;
            }

            if (!pblockfiletree->EraseFileIndex(fileHash)) {
                LogPrint("file", "%s : Failed to erase file index with fileHash - %s", __func__, fileHash.ToString());
                return;
            }

            if (srcBlockInfo.IsBlockFileEmpty() && !handleEmptySrcFile(syncState, srcFilePos)) {
                LogPrint("file", "%s : Failed to handle empty src file. nFile: %d", __func__, srcFilePos.nBlockFileIndex);
                return;
            }

            continue;
        }

        CFileRepositoryBlockDiskPos newPosAtTempFile;
        CValidationState state;
        if (!FindAndAllocateBlockFile(state, newPosAtTempFile, nRepositoryFileSize, nNewRepositoryBlockIndex, vNewTempFileRepositoryBlockInfo, true)) {
            LogPrint("file", "%s - FILES. Find file position at temp fileblock from disk failed.\n", __func__);
            return;
        }

        // save file at temp blockfile
        if (!WriteFileRepositoryBlockToDisk(file, newPosAtTempFile, true)) {
            LogPrint("file", "%s - FILES. Failed to write file block to disk with fileHash - %s", __func__, fileHash.ToString());
            return;
        }

        // save new file index at DB
        if (!pblockfiletree->WriteFileIndex(fileHash, newPosAtTempFile)) {
            LogPrint("file", "%s - FILES. Failed to write file index fileHash - %s", __func__, fileHash.ToString());
            return;
        }

        dbFileRepositoryState.AddFile(nRepositoryFileSize);

        //TODO: PDG5.
        srcBlockInfo.SubtractFile(nRepositoryFileSize);
        if (srcBlockInfo.IsBlockFileEmpty() && !handleEmptySrcFile(syncState, srcFilePos)) {
            LogPrint("file", "%s : Failed to handle empty src file. nFile: %d", __func__, srcFilePos.nBlockFileIndex);
            return;
        }

        //update sync state after add new temp file
        if (syncState.nProcessedTempBlocks != newPosAtTempFile.nBlockFileIndex) {
            LogPrint("file", "%s: Update sync state after add new temp repository file.\n", __func__);

            // update previous file state info.
            if (!pblockfiletree->WriteFileRepositoryBlockInfo(syncState.nProcessedTempBlocks, vNewTempFileRepositoryBlockInfo[syncState.nProcessedTempBlocks])) {
                LogPrint("%s: Filed to save new repository state on block info. \n", __func__);
                return;
            }

            // next processing block
            syncState.nProcessedTempBlocks = newPosAtTempFile.nBlockFileIndex;
            if (!pblockfiletree->WriteFileRepositoryBlockSyncState(syncState)) {
                LogPrint("file", "%s - FILES. Filed to write repository block synchronization state after create new temp file.\n", __func__);
                return;
            }
        }
    }

    vFileRepositoryBlockInfo = vNewTempFileRepositoryBlockInfo;
    nLastFileRepositoryBlock = nNewRepositoryBlockIndex;
    if (!SaveManagerState(vNewTempFileRepositoryBlockInfo, nNewRepositoryBlockIndex)) {
        LogPrint("file", "%s - FILES. Filed to save new file repository block info. size: %d, nFile: %d", __func__, vNewTempFileRepositoryBlockInfo.size(), nNewRepositoryBlockIndex);
        return;
    }

    //rename all tmp to original
    LogPrint("file", "%s - FILES. Rename all temp files to original name.", __func__);
    for (int i = 0; i <= nNewRepositoryBlockIndex; i++) {
        if (!RenameTmpOriginalFileBlockDisk(i)) {
            LogPrint("file", "%s - FILES. Failed to rename repository file block at disk. Number: %d", __func__, i);
            return;
        }
    }

    //sync is over
    syncState.SetNull();
    if (!pblockfiletree->WriteFileRepositoryBlockSyncState(syncState)) {
        LogPrint("file", "%s - FILES. Filed to write repository block synchronization is over state.\n", __func__);
        return;
    }

    LogPrint("file", "%s - FILES. Process diskfile erase scheduler finish. FileRepositoryState: %s.\n", __func__, dbFileRepositoryState.ToString());
}

uint32_t CFileRepositoryManager::GetRepositoryFileSize(const CDBFile &file) {
    uint32_t dbFileSize = ::GetSerializeSize(file, SER_DISK, CLIENT_VERSION);
    return dbFileSize + MESSAGE_START_SIZE + sizeof(unsigned int);
}