// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The PrivateDatagram developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"
#include "containers.h"

#include <list>

enum {
    TX_PAYMENT = 1,
    TX_FILE_PAYMENT_REQUEST,
    TX_FILE_PAYMENT_CONFIRM,
    TX_FILE_TRANSFER
};

enum {
    TX_META_EMPTY = 0,
    TX_META_FILE = 1
};

class CTransaction;

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    COutPoint() { SetNull(); }
    COutPoint(uint256 hashIn, uint32_t nIn) { hash = hashIn; n = nIn; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(FLATDATA(*this));
    }

    void SetNull() { hash.SetNull(); n = (uint32_t) -1; }
    bool IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }
    bool IsMasternodeReward(const CTransaction* tx) const;

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
    std::string ToStringShort() const;

    uint256 GetHash();

};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;
    CScript prevPubKey;

    CTxIn()
    {
        nSequence = std::numeric_limits<unsigned int>::max();
    }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=std::numeric_limits<unsigned int>::max());
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=std::numeric_limits<uint32_t>::max());

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);
    }

    bool IsFinal() const
    {
        return (nSequence == std::numeric_limits<uint32_t>::max());
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;
    int nRounds;

    CTxOut()
    {
        SetNull();
    }

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
        nRounds = -10; // an initial value, should be no way to get this by calculations
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    void SetEmpty()
    {
        nValue = 0;
        scriptPubKey.clear();
    }

    bool IsEmpty() const
    {
        return (nValue == 0 && scriptPubKey.empty());
    }

    uint256 GetHash() const;

    bool IsDust(CFeeRate minRelayTxFee) const
    {
        // "Dust" is defined in terms of CTransaction::minRelayTxFee, which has units upiv-per-kilobyte.
        // If you'd pay more than 1/3 in fees to spend something, then we consider it dust.
        // A typical txout is 34 bytes big, and will need a CTxIn of at least 148 bytes to spend
        // i.e. total is 148 + 32 = 182 bytes. Default -minrelaytxfee is 10000 upiv per kB
        // and that means that fee per txout is 182 * 10000 / 1000 = 1820 upiv.
        // So dust is a txout less than 1820 *3 = 5460 upiv
        // with default -minrelaytxfee = minRelayTxFee = 10000 upiv per kB.
        size_t nSize = GetSerializeSize(SER_DISK,0)+148u;
        return (nValue < 3*minRelayTxFee.GetFee(nSize));
    }

    bool IsZerocoinMint() const
    {
        return !scriptPubKey.empty() && scriptPubKey.IsZerocoinMint();
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey &&
                a.nRounds      == b.nRounds);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};


struct CMutableTransaction;
class CFileMeta;
struct CFile;

class CTransactionMeta {
protected:
    uint32_t nFlags; // TODO: why?

public:
    CTransactionMeta();

    virtual ~CTransactionMeta() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nFlags);
    }

    bool IsNull() {
        return nFlags != TX_META_EMPTY;
    }

    virtual CTransactionMeta* clone() const {
        CTransactionMeta* clone = new CTransactionMeta();
        *clone = *this;
        return clone;
    }

};

/**
 * Meta information about payment request
 */
class CPaymentRequest: public CTransactionMeta {

public:
    CPaymentRequest();

    std::vector<char> vfMessage;
    CAmount nPrice;
    // TODO: don't forget about hash, think about security

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        CTransactionMeta::SerializationOp(s, ser_action, nType, nVersion);

        READWRITE(*const_cast<std::vector<char>*>(&vfMessage));
        READWRITE(nPrice);
    }

    CTransactionMeta* clone() const override {
        CPaymentRequest* clone = new CPaymentRequest();
        *clone = *this;
        return clone;
    }

};

/**
 * Meta information about payment confirm
 */
class CPaymentConfirm: public CTransactionMeta {

public:
    CPaymentConfirm();

    // Payment request transaction hash
    uint256 requestTxid;
    // The RSA public key of receiver for file encryption
    std::vector<char> vfPublicKey;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        CTransactionMeta::SerializationOp(s, ser_action, nType, nVersion);

        READWRITE(requestTxid);
        READWRITE(*const_cast<std::vector<char>*>(&vfPublicKey));
    }

    CPaymentConfirm* clone() const override {
        CPaymentConfirm* clone = new CPaymentConfirm();
        *clone = *this;
        return clone;
    }

};

/**
 * Meta information in transaction with file transfering
 */
class CFileMeta: public CTransactionMeta {
public:
    CFileMeta();

    // Payment confirm transaction hash
    uint256 confirmTxid;
    // Encoded bytes of struct CEncodedMeta
    std::vector<char> vfEncodedMeta;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        CTransactionMeta::SerializationOp(s, ser_action, nType, nVersion);

        READWRITE(confirmTxid);
        READWRITE(*const_cast<std::vector<char>*>(&vfEncodedMeta));
    }

    CFileMeta* clone() const override {
        CFileMeta* clone = new CFileMeta();
        *clone = *this;
        return clone;
    }

};


/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
private:
    /** Memory only. */
    const uint256 hash;
    void UpdateHash() const;

public:
    static const int32_t CURRENT_VERSION=1;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const int32_t nVersion;
    const int32_t type;
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    PtrContainer<CTransactionMeta> meta;
    std::vector<CFile> vfiles;
    const uint32_t nLockTime;
    //const unsigned int nTime;

    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction &tx);

    CTransaction& operator=(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<int32_t*>(&this->nVersion));
        nVersion = this->nVersion;

        READWRITE(*const_cast<int*>(&type));

        READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));
        READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));

        if (type == TX_FILE_PAYMENT_REQUEST) {
            READWRITE(*const_cast<CPaymentRequest*>(&meta.getOrRecreate<CPaymentRequest>()));

        } else if (type == TX_FILE_PAYMENT_CONFIRM) {
            READWRITE(*const_cast<CPaymentConfirm*>(&meta.getOrRecreate<CPaymentConfirm>()));
        } else if (type == TX_FILE_TRANSFER) {
            READWRITE(*const_cast<CFileMeta*>(&meta.getOrRecreate<CFileMeta>()));
            READWRITE(*const_cast<std::vector<CFile>*>(&vfiles));
        }

        READWRITE(*const_cast<uint32_t*>(&nLockTime));
        if (ser_action.ForRead())
            UpdateHash();
    }

    bool IsNull() const {
        return vin.empty() && vout.empty();
    }

    const uint256& GetHash() const {
        return hash;
    }

    // Return sum of txouts.
    CAmount GetValueOut() const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    // Compute priority, given priority of inputs and (optionally) tx size
    double ComputePriority(double dPriorityInputs, unsigned int nTxSize=0) const;

    // Compute modified tx size for priority calculation (optionally given tx size)
    unsigned int CalculateModifiedSize(unsigned int nTxSize=0) const;

    bool IsZerocoinSpend() const
    {
        return (vin.size() > 0 && vin[0].prevout.hash == 0 && vin[0].scriptSig[0] == OP_ZEROCOINSPEND);
    }

    bool IsZerocoinMint() const
    {
        for(const CTxOut& txout : vout) {
            if (txout.scriptPubKey.IsZerocoinMint())
                return true;
        }
        return false;
    }

    bool ContainsZerocoins() const
    {
        return IsZerocoinSpend() || IsZerocoinMint();
    }

    CAmount GetZerocoinMinted() const;
    CAmount GetZerocoinSpent() const;
    int GetZerocoinMintCount() const;

    bool UsesUTXO(const COutPoint out);
    std::list<COutPoint> GetOutPoints() const;

    bool IsCoinBase() const
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull() && !ContainsZerocoins());
    }

    bool IsCoinStake() const;

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;

    bool GetCoinAge(uint64_t& nCoinAge) const;  // ppcoin: get transaction coin age
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    int32_t nVersion;
    int32_t type;
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    PtrContainer<CTransactionMeta> meta;
    std::vector<CFile> vfiles;
    uint32_t nLockTime;

    CMutableTransaction();
    CMutableTransaction(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;

        READWRITE(type);

        READWRITE(vin);
        READWRITE(vout);

        if (type == TX_FILE_PAYMENT_REQUEST) {
            READWRITE(*const_cast<CPaymentRequest*>(&meta.getOrRecreate<CPaymentRequest>()));
        } else if (type == TX_FILE_PAYMENT_CONFIRM) {
            READWRITE(*const_cast<CPaymentConfirm*>(&meta.getOrRecreate<CPaymentConfirm>()));
        } else if (type == TX_FILE_TRANSFER) {
            READWRITE(*const_cast<CFileMeta*>(&meta.getOrRecreate<CFileMeta>()));
            READWRITE(*const_cast<std::vector<CFile>*>(&vfiles));
        }

        READWRITE(nLockTime);
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;

    std::string ToString() const;

    friend bool operator==(const CMutableTransaction& a, const CMutableTransaction& b)
    {
        return a.GetHash() == b.GetHash();
    }

    friend bool operator!=(const CMutableTransaction& a, const CMutableTransaction& b)
    {
        return !(a == b);
    }

};

struct CFile
{
    uint32_t nFlags;
    // Encrypted file bytes
    std::vector<char> vBytes;
    // Encrypted file hash
    uint256 fileHash;

    CFile();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        // TODO: is nVersion write needed?
        // Update file hash before write
        if (!ser_action.ForRead()) {
            UpdateFileHash();
        }

        READWRITE(nFlags);
        READWRITE(*const_cast<std::vector<char>*>(&vBytes));
        READWRITE(fileHash);
    }

    uint256 CalcFileHash() const;

    uint256 UpdateFileHash();

    std::string ToString() const;

};

struct CEncodedMeta {

    // Source (decrypted) file hash
    uint256 fileHash;
    std::vector<char> vfFilename;
    // The AES key with which the file was encrypted
    std::vector<char> vfFileKey;
    uint64_t nFileSize;

    CEncodedMeta();
    CEncodedMeta(const CEncodedMeta& meta);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        // TODO: is nVersion write needed?
        READWRITE(fileHash);
        READWRITE(*const_cast<std::vector<char>*>(&vfFilename));
        READWRITE(*const_cast<std::vector<char>*>(&vfFileKey));
        READWRITE(nFileSize);
    }

};

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
