// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PDG_CORE_FILEREPOSITORYMANAGER_H
#define PDG_CORE_FILEREPOSITORYMANAGER_H

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "net.h"
#include "primitives/block.h"
#include "sync.h"
#include "tinyformat.h"
#include "uint256.h"
#include "validationstate.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include <mutex>
//#include <shared_mutex>
#include <boost/thread/shared_mutex.hpp>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/unordered_map.hpp>


struct FileRepositoryBlockSyncState {
    bool isSync;                    //! Synchronization status.
    int nProcessedSourceBlocks;     //! Number complete handle of file
    int nProcessedTempBlocks;       //! Number complete handle temp of file

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(isSync);
        READWRITE(nProcessedSourceBlocks);
        READWRITE(nProcessedTempBlocks);
    }

    FileRepositoryBlockSyncState() : isSync(false), nProcessedSourceBlocks(-1), nProcessedTempBlocks(-1) {}

    FileRepositoryBlockSyncState(const bool isSync, const int nProcessedSourceBlocks, int nProcessedTempBlocks) : isSync(isSync), nProcessedSourceBlocks(nProcessedSourceBlocks), nProcessedTempBlocks(nProcessedTempBlocks) {}
    void SetNull()
    {
        isSync = false;
        nProcessedSourceBlocks = -1;
        nProcessedTempBlocks = -1;
    }

    bool IsNull() {
        return !isSync && nProcessedSourceBlocks == -1 && nProcessedTempBlocks == -1;
    }
};

class CDBFileRepositoryState {
public:
    const int removedFilesSizeShrinkPercent;

    uint64_t nTotalFileStorageSize;            //!number of bytes of all files stored
    unsigned int nBlocksCount;                 //! number of block files
    unsigned int filesCount;                  //! number of all files in all blocks

    uint64_t removeCandidatesTotalSize;       //! number of bytes of all mark remove files in db
    unsigned int removeCandidatesFilesCount;  //! number of mark removed files in db

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(VARINT(nTotalFileStorageSize));
        READWRITE(VARINT(nBlocksCount));
        READWRITE(VARINT(filesCount));
        READWRITE(VARINT(removeCandidatesTotalSize));
        READWRITE(VARINT(removeCandidatesFilesCount));
    }

    void SetNull()
    {
        nTotalFileStorageSize = 0;
        nBlocksCount = 0;
        filesCount = 0;
        removeCandidatesTotalSize = 0;
        removeCandidatesFilesCount = 0;
    }

    CDBFileRepositoryState(int removedFilesSizeShrinkPercent): removedFilesSizeShrinkPercent(removedFilesSizeShrinkPercent)
    {
        SetNull();
    }

    std::string ToString() const;

    /** update statistics (does not update blocksCount) */
    void AddFile(uint64_t fileSize)
    {
        nTotalFileStorageSize += fileSize;
        filesCount++;
    }

    /** update statistics (does not update numberBytesSize) */
    void SubtractFile(uint64_t fileSize)
    {
        removeCandidatesTotalSize += fileSize;
        removeCandidatesFilesCount++;
    }

    bool IsClearDiskSpaceNeeded() {
        //marked remove byte size more than limit relatively total size.
        return removeCandidatesTotalSize > (removedFilesSizeShrinkPercent * nTotalFileStorageSize / 100);
    }
};

/*struct RepositoryBlockDiskInfo
{
    boost::filesystem::path path;
    const int32_t type;

    RepositoryBlockDiskInfo(const boost::filesystem::path path, const int32_t type) : path(path), type(type) {}
};

enum {
    ORIGINAL_DISK_FILE = 0,
    TEMP_DISK_FILE = 1
};*/

class CFileRepositoryBlockInfo
{
public:
    uint32_t nBlockSize;                //! number of used bytes of block file.
    uint32_t nFilesCount;               //! number of files stored in file.
    int64_t firstWriteTime;            //! earliest time of block in file.
    int64_t lastWriteTime;             //! latest time of block in file.
    bool isFull;                        //! file is full.

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(VARINT(nBlockSize));
        READWRITE(VARINT(nFilesCount));
        READWRITE(firstWriteTime);
        READWRITE(lastWriteTime);
        READWRITE(isFull);
    }

    void SetNull()
    {
        nFilesCount = 0;
        nBlockSize = 0;
        firstWriteTime = 0;
        isFull = false;
        UpdateLastWrite();
    }

    CFileRepositoryBlockInfo()
    {
        SetNull();
    }

    bool IsBlockFileEmpty() {
        LogPrint("file", "%s - FILES. Check diskFile empty. Meta data: numberFiles: %d, byteSize: %d\n", __func__, nFilesCount, nBlockSize); //TODO: PDG 2 remove after debug
        if ((nBlockSize == 0 && nFilesCount != 0) || (nBlockSize != 0 && nFilesCount == 0)) {
            LogPrint("file", "%s - FILES. ERROR. Meta data blockfile info not valid.\n", __func__);
        }

        return nBlockSize == 0 && nFilesCount == 0;
    }

    std::string ToString() const;

    /** update statistics (does not update numberBytesSize) */
    void AddFile(unsigned int nAddSize)
    {
        if (nFilesCount == 0)
            firstWriteTime = GetTimeMillis() / 1000;

        nFilesCount++;
        nBlockSize += nAddSize;

        UpdateLastWrite();
    }

    void SubtractFile(unsigned int nBytesSize)
    {
        if (nFilesCount == 0 || nBytesSize > nBlockSize) {
            LogPrint("file", "%s - FILES. ERROR. Meta data blockfile info not valid: %d, bytes size total: %d, remove bytes: %d\n",
                     __func__,
                     nFilesCount,
                     nBlockSize,
                     nBytesSize); //TODO: PDG 2 remove after debug
            return;
        }

        //TODO: PDG 3 check match with file.vBytes.size() including additional sizes (e.g. bytes header)
        nBlockSize -= nBytesSize;
        nFilesCount--;

        UpdateLastWrite();
    }

    void UpdateLastWrite() {
        lastWriteTime = GetTimeMillis() / 1000;
    }
};


class CFileRepositoryManager {
private:
    std::vector<CFileRepositoryBlockInfo> vFileRepositoryBlockInfo;
    int nLastFileRepositoryBlock;
    CDBFileRepositoryState dbFileRepositoryState;
    int64_t lastUpdateTime;
    mutable boost::shared_mutex cs_RepositoryReadWriteLock;

    bool RemoveFileRepositoryBlockFromDisk(int fileNumber, bool isTmp);

    bool RemoveFileRepositoryBlockFromDisk(const CFileRepositoryBlockDiskPos& pos);

    bool WriteFileRepositoryBlockToDisk(CDBFile &file, CFileRepositoryBlockDiskPos &pos, bool isTmp);

    bool WriteFileRepositoryBlockToDisk(CDBFileHeaderOnly &file, CFileRepositoryBlockDiskPos &pos, bool isTmp);

    bool ReadFileBlockFromDisk(CDBFile &file, const CFileRepositoryBlockDiskPos& pos, bool isTmp);

    bool ReadFileBlockHeaderFromDisk(CDBFileHeaderOnly &file, const CFileRepositoryBlockDiskPos& pos, bool isTmp);

    bool RenameTmpOriginalFileBlockDisk(int tmpFileNumber);

    bool FinishFileRepositorySync(FileRepositoryBlockSyncState &syncState);

    void FlushFileRepositoryBlock(int nLastBlockIndex, unsigned int nLastBlockSize, bool fFinalize = false, bool isTmp = false);

    bool FindAndAllocateBlockFile(CValidationState& state, CFileRepositoryBlockDiskPos &pos, const uint32_t nAddSize);

    bool FindAndAllocateBlockFile(CValidationState &state, CFileRepositoryBlockDiskPos &pos,
                                  const uint32_t nAddSize, int &lastBlockFileIndex, vector<CFileRepositoryBlockInfo> &vblockFileInfo, bool isTmp);

    bool SaveManagerState(vector<CFileRepositoryBlockInfo> &vblockFileInfo, int &lastBlockFileIndex);

    FILE* OpenFileRepositoryBlock(const CFileRepositoryBlockDiskPos& pos, bool fReadOnly, bool isTmp);

    boost::filesystem::path GetFilePosFilename(const int numberDiskFile, const char* prefix);

    boost::filesystem::path GetTmpFilePosFilename(const int numberDiskFile, const char* prefix);

    uint32_t GetRepositoryFileSize(const CDBFile &file);

    template <typename FileType>
    bool ReadFileBlockFromDiskTo(FileType &file, const CFileRepositoryBlockDiskPos& pos, int nTypeIn, int nVersionIn, bool isTmp)
    {
        // Open history file to read
        CAutoFile filein(OpenFileRepositoryBlock(pos, true, isTmp), nTypeIn, nVersionIn);
        if (filein.IsNull())
            return error("%s : OpenFileRepositoryBlock failed", __func__);

        // Read file
        try {
            MessageStartChars messageStart;
            unsigned int nSize;
            filein >> FLATDATA(messageStart) >> nSize;

            if (memcmp(messageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0)
                return error("%s : Read file error. Message start missmatch. Position: %d/%u", __func__, pos.nBlockFileIndex, pos.nOffset);

            filein >> file;
        } catch (std::exception& e) {
            return error("%s : Deserialize or I/O error - %s", __func__, e.what());
        }

        return true;
    }

    template <typename FileType>
    bool WriteFileRepositoryBlockToDiskFrom(const FileType &file, CFileRepositoryBlockDiskPos &pos, int nTypeIn, int nVersionIn, bool isTmp)
    {
        // Open history file to append
        CAutoFile fileout(OpenFileRepositoryBlock(pos, false, isTmp), nTypeIn, nVersionIn);
        if (fileout.IsNull())
            return error("WriteFileRepositoryBlockToDiskFrom: OpenFileRepositoryBlock failed");

        // Write index header
        unsigned int nSize = fileout.GetSerializeSize(file);
        fileout << FLATDATA(Params().MessageStart()) << nSize;

        // Write block
        long fileOutPos = ftell(fileout.Get());
        if (fileOutPos < 0)
            return error("WriteFileRepositoryBlockToDiskFrom: ftell failed");
        //pos.nOffset = (unsigned int) fileOutPos;
        fileout << file;

        return true;
    }

public:

    CFileRepositoryManager(int removedFilesSizeShrinkPercent);

    bool SaveFile(CDBFile& file);

    bool EraseFile(CDBFile& file);

    bool SaveFileRepositoryState();

    bool LoadFileDBState();

    //todo: PDG5 remove
    void FillTestData();

    bool LoadManagerState();

    bool GetFile(const uint256& fileHash, CDBFile& fileOut);

    bool handleEmptySrcFile(FileRepositoryBlockSyncState &syncState, const CFileRepositoryBlockDiskPos &srcFilePos);

    void FlushBlockFiles();

    void FindAndRecycleExpiredFiles();

    void ShrinkRecycledFiles();

};

#endif //PDG_CORE_FILEREPOSITORYMANAGER_H
