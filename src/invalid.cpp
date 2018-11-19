// Copyright (c) 2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "invalid.h"

namespace invalid_out
{
    std::set<CBigNum> setInvalidSerials;
    std::set<COutPoint> setInvalidOutPoints;

    UniValue read_json(const std::string& jsondata)
    {
        UniValue v;

        if (!v.read(jsondata) || !v.isArray())
        {
            return UniValue(UniValue::VARR);
        }
        return v.get_array();
    }

    bool LoadOutpoints()
    {
        // implement loading invalid points if necessary. Removed in commit on 19/11/2018
        return false;
    }

    bool LoadSerials()
    {
        // implement loading invalid points if necessary. Removed in commit on 19/11/2018
        return false;
    }

    bool ContainsOutPoint(const COutPoint& out)
    {
        return static_cast<bool>(setInvalidOutPoints.count(out));
    }

    bool ContainsSerial(const CBigNum& bnSerial)
    {
        return static_cast<bool>(setInvalidSerials.count(bnSerial));
    }
}

