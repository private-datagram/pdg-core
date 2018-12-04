// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "main.h"
#include "masternode-sync.h"
#include "net.h"
#include "netbase.h"
#include "rpcserver.h"
#include "spork.h"
#include "timedata.h"
#include "util.h"
#include "univalue/include/univalue.h"

#include <map>

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

using namespace boost;
using namespace boost::assign;
using namespace std;

struct FilePending;
extern map<uint256, FilePending> filesPendingMap;
extern CCriticalSection cs_FilesPendingMap;

struct QueuedFile;
extern map<uint256, QueuedFile> filesInFlightMap;
extern CCriticalSection cs_FilesInFlightMap;

extern map<uint256, RequiredFile> requiredFilesMap;
extern CCriticalSection cs_RequiredFilesMap;

struct FileRequest;
extern map<uint256, std::vector<FileRequest>> hasFileRequestedNodesMap;
extern CCriticalSection cs_HasFileRequestedNodesMap;

typedef map<uint256, map<NodeId, FileRequest>> FileRequestMapType;
extern FileRequestMapType fileRequestedNodesMap;
extern CCriticalSection cs_FileRequestedNodesMap;


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

                "\nResult:\n"
                "{\n"
                "  \"version\": xxxxx,           (numeric) the server version\n"
                "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
                "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
                "  \"balance\": xxxxxxx,         (numeric) the total pdg balance of the wallet (excluding zerocoins)\n"
                "  \"zerocoinbalance\": xxxxxxx, (numeric) the total zerocoin balance of the wallet\n"
                "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
                "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
                "  \"connections\": xxxxx,       (numeric) the number of connections\n"
                "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
                "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
                "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
                "  \"moneysupply\" : \"supply\"       (numeric) The money supply when this block was added to the blockchain\n"
                "  \"zPDGsupply\" :\n"
                "  {\n"
                "     \"1\" : n,            (numeric) supply of 1 zPDG denomination\n"
                "     \"5\" : n,            (numeric) supply of 5 zPDG denomination\n"
                "     \"10\" : n,           (numeric) supply of 10 zPDG denomination\n"
                "     \"50\" : n,           (numeric) supply of 50 zPDG denomination\n"
                "     \"100\" : n,          (numeric) supply of 100 zPDG denomination\n"
                "     \"500\" : n,          (numeric) supply of 500 zPDG denomination\n"
                "     \"1000\" : n,         (numeric) supply of 1000 zPDG denomination\n"
                "     \"5000\" : n,         (numeric) supply of 5000 zPDG denomination\n"
                "     \"total\" : n,        (numeric) The total supply of all zPDG denominations\n"
                "  }\n"
                "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
                "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
                "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
                "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in pdg/kb\n"
                "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for non-free transactions in pdg/kb\n"
                "  \"staking status\": true|false,  (boolean) if the wallet is staking or not\n"
                "  \"errors\": \"...\"           (string) any error messages\n"
                "}\n"

                "\nExamples:\n" +
                HelpExampleCli("getfilesyncstate", "") + HelpExampleRpc("getfilesyncstate", ""));
*/
/*
#ifdef ENABLE_WALLET
        LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
 *//*


    UniValue obj(UniValue::VOBJ);

    //
    UniValue requiredFilesList(UniValue::VARR);
    for (auto it = requiredFilesMap.begin(); it != requiredFilesMap.end(); it++) {
        UniValue item(UniValue::VOBJ);
        item.push_back(Pair("key", it->first.ToString()));
        item.push_back(Pair("requestExpirationTime", (uint64_t) it->second.requestExpirationTime));
        item.push_back(Pair("fileExpirationTime", (uint64_t) it->second.fileExpirationTime));
    }
    obj.push_back(Pair("requiredFilesMap", requiredFilesList));

    //
    // TODO: fix incomplete type error
    UniValue filePendingList(UniValue::VARR);
    for (auto it = filesPendingMap.begin(); it != filesPendingMap.end(); it++) {
        const uint256 &fileTxHash = it->first;

        UniValue item(UniValue::VOBJ);

        item.push_back(Pair("key", fileTxHash.ToString()));
        item.push_back(Pair("fileTxHash", it->second->fileTxHash.ToString()));
        std::string nodesList;
        join(it->second->nodes, ',', nodesList);
        item.push_back(Pair("nodes", nodesList));

        filePendingList.push_back(item);
    }
    obj.push_back(Pair("filesPendingMap", filePendingList));

    //
    UniValue fileInFlightList(UniValue::VARR);
    for (auto it = filesInFlightMap.begin(); it != filesInFlightMap.end(); it++) {
        const uint256 &fileTxHash = it->first;

        UniValue item(UniValue::VOBJ);

        item.push_back(Pair("key", fileTxHash.ToString()));
        item.push_back(Pair("nodeId", nodeId));
        item.push_back(Pair("fileTxHash", fileTxHash.ToString()));
        item.push_back(Pair("nTime", nTime));

        fileInFlightList.push_back(item);
    }
    obj.push_back(Pair("fileInFlightList", fileInFlightList));

    //
    UniValue hasFileRequestedNodesList(UniValue::VARR);
    for (auto it = hasFileRequestedNodesMap.begin(); it != hasFileRequestedNodesMap.end(); it++) {
        const uint256 &fileTxHash = it->first;

        UniValue item(UniValue::VOBJ);
        item.push_back(Pair("key", fileTxHash.ToString()));

        UniValue fileRequestNodesList(UniValue::VARR);
        BOOST_FOREACH (const FileRequest& fileRequest, it->second) {
            fileRequestNodesList.push_back(Pair("node", fileRequest.node));
            fileRequestNodesList.push_back(Pair("date", fileRequest.date));
            fileRequestNodesList.push_back(Pair("events", fileRequest.events));

            if (txin.fileHash.is_initialized()) {
                fileRequestNodesList.push_back(Pair("fileHash", fileRequest.fileHash));
            }

            if (txin.fileTxHash.is_initialized()) {
                fileRequestNodesList.push_back(Pair("fileTxHash", fileRequest.fileTxHash));
            }
        }

        item.push_back(Pair("fileRequestNodesList", fileRequestNodesList));
        hasFileRequestedNodesList.push_back(item);
    }
    obj.push_back(Pair("hasFileRequestedNodesList", hasFileRequestedNodesList));

    //
    UniValue hasFileRequestedNodesList(UniValue::VARR);
    for (auto it = FileRequestMapType.begin(); it != FileRequestMapType.end(); it++) {
        const uint256 &fileTxHash = it->first;

        UniValue item(UniValue::VOBJ);
        item.push_back(Pair("key", fileTxHash.ToString()));

        UniValue fileRequestNodesList(UniValue::VARR);
        BOOST_FOREACH (const FileRequest& fileRequest, it->second) {
            fileRequestNodesList.push_back(Pair("node", fileRequest.node));
            fileRequestNodesList.push_back(Pair("date", fileRequest.date));
            fileRequestNodesList.push_back(Pair("events", fileRequest.events));

            if (txin.fileHash.is_initialized()) {
                fileRequestNodesList.push_back(Pair("fileHash", fileRequest.fileHash));
            }

            if (txin.fileTxHash.is_initialized()) {
                fileRequestNodesList.push_back(Pair("fileTxHash", fileRequest.fileTxHash));
            }
        }

        item.push_back(Pair("fileRequestNodesList", fileRequestNodesList));
        hasFileRequestedNodesList.push_back(item);
    }

    // ValueFromAmount()
    return obj;
}

void join(const set<int>& v, char c, std::string& s) {

    s.clear();

    BOOST_FOREACH(const int item, v) {
        s += item;
        s += c;
    }

    if (s.length() > 0) {
        s.resize(s.length() - 1);
    }

}
