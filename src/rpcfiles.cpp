// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "rpcserver.h"

#include <map>
#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <boost/assign/list_of.hpp>
#include <univalue.h>

using namespace boost;
using namespace boost::assign;
using namespace std;

void join(const set<int>& v, char c, std::string& s);

/**
 * File sync state info
 **/
UniValue getfilesyncstate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getfilesyncstate\n"
                "\nReturns an object containing file sync state info.\n"
                // TODO: print result struct
                "\nExamples:\n" +
                HelpExampleCli("getfilesyncstate", "") + HelpExampleRpc("getfilesyncstate", ""));


    UniValue obj(UniValue::VOBJ);

    {
        //LOCK(cs_RequiredFilesMap);

        UniValue requiredFilesList(UniValue::VARR);
        for (auto it = requiredFilesMap.begin(); it != requiredFilesMap.end(); it++) {
            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("key", it->first.ToString()));
            item.push_back(Pair("requestExpirationTime", (uint64_t) it->second.requestExpirationTime));
            item.push_back(Pair("fileExpirationTime", (uint64_t) it->second.fileExpirationTime));

            requiredFilesList.push_back(item);
        }
        obj.push_back(Pair("requiredFilesMap", requiredFilesList));
    }

    {
        //LOCK(cs_FilesPendingMap);

        UniValue filePendingList(UniValue::VARR);
        for (auto it = filesPendingMap.begin(); it != filesPendingMap.end(); it++) {
            const uint256 &fileTxHash = it->first;

            UniValue item(UniValue::VOBJ);

            item.push_back(Pair("key", fileTxHash.ToString()));
            item.push_back(Pair("fileTxHash", it->second.fileTxHash.ToString()));
            std::string nodesList;
            join(it->second.nodes, ',', nodesList);
            item.push_back(Pair("nodes", nodesList));

            filePendingList.push_back(item);
        }
        obj.push_back(Pair("filesPendingMap", filePendingList));
    }


    {
        //LOCK(cs_FilesInFlightMap);

        UniValue fileInFlightList(UniValue::VARR);
        for (auto it = filesInFlightMap.begin(); it != filesInFlightMap.end(); it++) {
            const uint256 &fileTxHash = it->first;

            UniValue item(UniValue::VOBJ);

            item.push_back(Pair("key", fileTxHash.ToString()));
            item.push_back(Pair("nodeId", it->second.nodeId));
            item.push_back(Pair("fileTxHash", fileTxHash.ToString()));
            item.push_back(Pair("nTime", it->second.nTime / 1000000));

            fileInFlightList.push_back(item);
        }
        obj.push_back(Pair("fileInFlightList", fileInFlightList));
    }

    {
        //LOCK(cs_HasFileRequestedNodesMap);

        UniValue hasFileRequestedNodesList(UniValue::VARR);
        for (auto it = hasFileRequestedNodesMap.begin(); it != hasFileRequestedNodesMap.end(); it++) {
            const uint256 &fileTxHash = it->first;

            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("key", fileTxHash.ToString()));

            UniValue fileRequestNodesList(UniValue::VARR);
            BOOST_FOREACH (FileRequest &fileRequest, it->second) {
                UniValue frItem(UniValue::VOBJ);
                frItem.push_back(Pair("nodeId", fileRequest.node));
                frItem.push_back(Pair("date", fileRequest.date / 1000000));
                frItem.push_back(Pair("events", fileRequest.events));

                if (fileRequest.fileHash.is_initialized()) {
                    frItem.push_back(Pair("fileHash", fileRequest.fileHash.get().ToString()));
                }

                if (fileRequest.fileTxHash.is_initialized()) {
                    frItem.push_back(Pair("fileTxHash", fileRequest.fileTxHash.get().ToString()));
                }

                fileRequestNodesList.push_back(frItem);
            }

            item.push_back(Pair("fileRequestNodesList", fileRequestNodesList));

            hasFileRequestedNodesList.push_back(item);
        }
        obj.push_back(Pair("hasFileRequestedNodesList", hasFileRequestedNodesList));
    }

    {
        //LOCK(cs_FileRequestedNodesMap);

        UniValue fileRequestMapList(UniValue::VARR);
        for (auto it = fileRequestedNodesMap.begin(); it != fileRequestedNodesMap.end(); it++) {
            const uint256 &fileTxHash = it->first;

            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("key", fileTxHash.ToString()));

            UniValue fileRequestNodesMapList(UniValue::VARR);
            for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++) {
                UniValue frItem(UniValue::VOBJ);
                frItem.push_back(Pair("nodeId", it2->first));
                frItem.push_back(Pair("node", it2->second.node));
                frItem.push_back(Pair("date", it2->second.date / 1000000));
                frItem.push_back(Pair("events", it2->second.events));

                if (it2->second.fileHash.is_initialized()) {
                    frItem.push_back(Pair("fileHash", it2->second.fileHash.get().ToString()));
                }

                if (it2->second.fileTxHash.is_initialized()) {
                    frItem.push_back(Pair("fileTxHash", it2->second.fileTxHash.get().ToString()));
                }

                fileRequestNodesMapList.push_back(frItem);
            }

            item.push_back(Pair("fileRequestNodesMapList", fileRequestNodesMapList));

            fileRequestMapList.push_back(item);
        }
        obj.push_back(Pair("fileRequestMapList", fileRequestMapList));
    }

    {
        UniValue lockList(UniValue::VOBJ);

        bool isHeldRequiredFilesMap;
        bool isHeldFilesPendingMap;
        bool isHeldFilesInFlightMap;
        bool isHeldHasFileRequestedNodesMap;
        bool isHeldFileRequestedNodesMap;
        bool isHeldMain;
        bool isHeldNodes;

        {TRY_LOCK(cs_RequiredFilesMap, isRequiredFilesMap); isHeldRequiredFilesMap = isRequiredFilesMap;}
        {TRY_LOCK(cs_FilesPendingMap, isFilesPendingMap); isHeldFilesPendingMap = isFilesPendingMap;}
        {TRY_LOCK(cs_FilesInFlightMap, isFilesInFlightMap); isHeldFilesInFlightMap = isFilesInFlightMap;}
        {TRY_LOCK(cs_HasFileRequestedNodesMap, isHasFileRequestedNodesMap); isHeldHasFileRequestedNodesMap = isHasFileRequestedNodesMap;}
        {TRY_LOCK(cs_FileRequestedNodesMap, isFileRequestedNodesMap); isHeldFileRequestedNodesMap = isFileRequestedNodesMap;}
        {TRY_LOCK(cs_main, isMain); isHeldMain = isMain;}
        {TRY_LOCK(cs_vNodes, isNodes); isHeldNodes = isNodes;}

        lockList.push_back(Pair("RequiredFilesMap held", isHeldRequiredFilesMap));
        lockList.push_back(Pair("FilesPendingMap held", isHeldFilesPendingMap));
        lockList.push_back(Pair("FilesInFlightMap held", isHeldFilesInFlightMap));
        lockList.push_back(Pair("HasFileRequestedNodesMap held", isHeldHasFileRequestedNodesMap));
        lockList.push_back(Pair("FileRequestedNodesMap held", isHeldFileRequestedNodesMap));
        lockList.push_back(Pair("Main held", isHeldMain));
        lockList.push_back(Pair("vNode held", isHeldNodes));

        obj.push_back(Pair("locks", lockList));
    }

    return obj;
}

void join(const set<int>& v, char c, std::string& s) {

    s.clear();

    BOOST_FOREACH(const int item, v) {
        s += std::to_string(item);
        s += c;
    }

    if (s.length() > 0) {
        s.resize(s.length() - 1);
    }

}
