// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include "main.h"
#include "scrypt_kernel.h"
#include "txdb.h"
#include "net.h"
#include "init.h"
#include "script.h"
#include "wallet.h"
//#include "scrypt_mine.h"

using namespace std;
unsigned int modifier = 1000000;
extern int nBestHeight;
extern int nStakeMaxAge;
extern const int64 nStakeTargetSpacing;

// Get selection interval section (in seconds)
static int64 GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert (nSection >= 0 && nSection < 64);
    int64 a = nModifierInterval * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1)));
    return a;
}

// Get stake modifier selection interval (in seconds)
static int64 GetStakeModifierSelectionInterval()
{
    int64 nSelectionInterval = 0;
    for (int nSection=0; nSection<64; nSection++)
    {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64& nStakeModifier, bool& fGeneratedStakeModifier)
{
    nStakeModifier = 0;
    fGeneratedStakeModifier = false;
    if (!pindexPrev)
    {
        fGeneratedStakeModifier = true;
        return true;  // genesis block's modifier is 0
    }
    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    if(modifierLast == NULL)
    {
        if (pindexPrev->GetBlockTime() < 1392281929 + nModifierInterval)
            return true;
    }
    else
    {
        //printf("modifierLast = %i \n", modifierLast->nHeight);
        if (fDebug)
            printf("ComputeNextStakeModifier: prev modifier=0x%016"PRI64x" time=%s\n", nStakeModifier, DateTimeStrFormat(modifierLast->GetBlockTime()).c_str());
        if (pindexPrev->GetBlockTime() < modifierLast->GetBlockTime() + nModifierInterval)
            return true;
    }
    modifierLast = const_cast<CBlockIndex*>(pindexPrev);

    // Sort candidate blocks by timestamp
    vector<pair<int64, uint256> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * nModifierInterval / nStakeTargetSpacing);
    int64 nSelectionInterval = GetStakeModifierSelectionInterval();
    int64 nSelectionIntervalStart = (pindexPrev->GetBlockTime() / nModifierInterval) * nModifierInterval - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;
    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart)
    {
        vSortedByTimestamp.push_back(make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }
    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;
    reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    // Select 64 blocks from candidate blocks to generate stake modifier
    uint64 nStakeModifierNew = 0;
    int64 nSelectionIntervalStop = nSelectionIntervalStart;
    map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound=0; nRound<min(6, (int)vSortedByTimestamp.size()); nRound++)
    {
        // add an interval section to the current selection round
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);
        // write the entropy bit of the selected block
        pair<int64, uint256> ts = vSortedByTimestamp[nRound];
        uint256 hash = ts.second;
        std::map<uint256,CBlockIndex*>::iterator it;
        it = mapBlockIndex.find(hash);
        if(it != mapBlockIndex.end())
        {
            CBlockIndex* cBlock = it->second;
            nStakeModifierNew |= (((uint64) cBlock->GetBlockTime()) >> nRound);

            // add the selected block from candidates to selected list
            mapSelectedBlocks.insert(make_pair(cBlock->GetBlockHash(), cBlock));
            //if (fDebug)
                //printf("ComputeNextStakeModifier: selected round %d stop=%s height=%d bit=%d\n",nRound, DateTimeStrFormat(nSelectionIntervalStop).c_str(), pindex->nHeight, pindex->GetStakeEntropyBit());
        }
    }

    // Print selection map for visualization of the selected blocks
    if (fDebug)
    {
        string strSelectionMap = "";
        // '-' indicates proof-of-work blocks not selected
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->nHeight >= nHeightFirstCandidate)
        {
            // '=' indicates proof-of-stake blocks not selected
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");
            pindex = pindex->pprev;
        }
        BOOST_FOREACH(const PAIRTYPE(uint256, const CBlockIndex*)& item, mapSelectedBlocks)
        {
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake()? "S" : "W");
        }
        printf("ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap.c_str());
    }
    if (fDebug)
    {
        printf("ComputeNextStakeModifier: new modifier=0x%016"PRI64x" time=%s\n", nStakeModifierNew, DateTimeStrFormat(pindexPrev->GetBlockTime()).c_str());
    }

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

// The stake modifier used to hash for a stake kernel is chosen as the stake
// modifier about a selection interval later than the coin generating the kernel
bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64& nStakeModifier, int& nStakeModifierHeight, int64& nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;
    if (!mapBlockIndex.count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");
    const CBlockIndex* pindexFrom = mapBlockIndex[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64 nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;

    // loop to find the stake modifier later by a selection interval
    while (true)
    {
        if (!pindex->pprev)
        {   // reached best block; may happen if node is behind on block chain
            if (fPrintProofOfStake || ((pindex->GetBlockTime() + (nStakeMinAge - nStakeModifierSelectionInterval)) > GetAdjustedTime()))
                return error("GetKernelStakeModifier() : reached best block %s at height %d from block %s",
                    pindex->GetBlockHash().ToString().c_str(), pindex->nHeight, hashBlockFrom.ToString().c_str());
            else
            {
                 //printf(">> nStakeModifierTime = %"PRI64d", pindexFrom->GetBlockTime() = %"PRI64d", nStakeModifierSelectionInterval = %"PRI64d"  returning false...\n",
                 //   nStakeModifierTime, pindexFrom->GetBlockTime(), nStakeModifierSelectionInterval);
                return false;
            }
        }
        pindex = pindex->pprev;
        if (pindex->GeneratedStakeModifier())
        {
            nStakeModifierHeight = pindex->nHeight;
            nStakeModifierTime = pindex->GetBlockTime();
            break;
        }
    }
    nStakeModifier = pindex->nStakeModifier;
    return true;
}

// ppcoin kernel protocol
// coinstake must meet hash target according to the protocol:
// kernel (input 0) must meet the formula
//     hash(nStakeModifier + txPrev.block.nTime + txPrev.offset + txPrev.nTime + txPrev.vout.n + nTime) < bnTarget * nCoinDayWeight
// this ensures that the chance of getting a coinstake is proportional to the
// amount of coin age one owns.
// The reason this hash is chosen is the following:
//   nStakeModifier:
//       (v0.3) scrambles computation to make it very difficult to precompute
//              future proof-of-stake at the time of the coin's confirmation
//       (v0.2) nBits (deprecated): encodes all past block timestamps
//   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
//                       generate transaction for future advantage
//   txPrev.offset: offset of txPrev inside block, to reduce the chance of
//                  nodes generating coinstake at the same time
//   txPrev.nTime: reduce the chance of nodes generating coinstake at the same
//                 time
//   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
//                  generating coinstake at the same time
//   block/tx hash should not be used here as they can be generated in vast
//   quantities so as to generate blocks faster, degrading the system back into
//   a proof-of-work situation.
//
bool CheckStakeKernelHashScrypt(unsigned int nBits, unsigned int blockFromTime, uint256 blockFromHash, const CTransaction &txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, bool fPrintProofOfStake)
{
    unsigned int txPrevTime = blockFromTime;
    if (nTimeTx < txPrevTime)  // Transaction timestamp violation
    {
        printf("nTime violation \n");
        return error("CheckStakeKernelHash() : nTime violation");
    }
    unsigned int nTimeBlockFrom = blockFromTime;
    if (nTimeBlockFrom + nStakeMinAge > nTimeTx) // Min age requirement
    {
        printf("min age violation \n");
        return error("CheckStakeKernelHash() : min age violation");
    }
    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    int64 nValueIn = txPrev.vout[prevout.n].nValue;

    // v0.3 protocol kernel hash weight starts from 0 at the min age
    // this change increases active coins participating the hash and helps
    // to secure the network when proof-of-stake difficulty is low
    int64 nTimeWeight = min((int64)nTimeTx - txPrevTime, (int64)nStakeMaxAge) - nStakeMinAge;
    CBigNum bnCoinDayWeight = CBigNum(nValueIn) * nTimeWeight / COIN / (24 * 60 * 60);

    //printf(">>> CheckStakeKernelHash: nTimeWeight = %"PRI64d"\n", nTimeWeight);
    // Calculate hash
    CDataStream ss(SER_GETHASH, 0);
    uint64 nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64 nStakeModifierTime = 0;

    if (!GetKernelStakeModifier(blockFromHash, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake))
    {
        //printf(">>> CheckStakeKernelHash: GetKernelStakeModifier return false\n");
        return false;
    }
    //printf(">>> CheckStakeKernelHash: passed GetKernelStakeModifier\n");
    ss << nStakeModifier;

    ss << nTimeBlockFrom << txPrevTime << prevout.n << nTimeTx;
    hashProofOfStake = Hash(ss.begin(), ss.end());
    if (fPrintProofOfStake)
    {
        printf("CheckStakeKernelHash() : using modifier 0x%016"PRI64x" at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
            nStakeModifier, nStakeModifierHeight,
            DateTimeStrFormat(nStakeModifierTime).c_str(),
            mapBlockIndex[blockFromHash]->nHeight,
            DateTimeStrFormat(blockFromTime).c_str());
        printf("CheckStakeKernelHash() : check protocol=%s modifier=0x%016"PRI64x" nTimeBlockFrom=%u nTxPrevOffset=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
            "0.3",
            nStakeModifier,
            nTimeBlockFrom, txPrevTime, prevout.n, nTimeTx,
            hashProofOfStake.ToString().c_str());
    }

    // Now check if proof-of-stake hash meets target protocol
    if (CBigNum(hashProofOfStake) > bnCoinDayWeight * bnTargetPerCoinDay * modifier)
    {
         //printf(">>> bnCoinDayWeight = %s, bnTargetPerCoinDay=%s\n",
           // bnCoinDayWeight.ToString().c_str(), bnTargetPerCoinDay.ToString().c_str());

         CBigNum weight = bnCoinDayWeight * bnTargetPerCoinDay;
         //printf("\n\n hashProofOfStake %s > weight %s \n\n", CBigNum(hashProofOfStake).ToString().c_str(), weight.ToString().c_str() );

         //printf(">>> CheckStakeKernelHash - hashProofOfStake too much\n");
        return false;
    }


    if (fDebug && !fPrintProofOfStake)
    {
        printf("CheckStakeKernelHash() : using modifier 0x%016"PRI64x" at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
            nStakeModifier, nStakeModifierHeight,
            DateTimeStrFormat(nStakeModifierTime).c_str(),
            mapBlockIndex[blockFromHash]->nHeight,
            DateTimeStrFormat(blockFromTime).c_str());
        printf("CheckStakeKernelHash() : pass protocol=%s modifier=0x%016"PRI64x" nTimeBlockFrom=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
            "0.3",
            nStakeModifier,
            nTimeBlockFrom, txPrevTime, prevout.n, nTimeTx,
            hashProofOfStake.ToString().c_str());
    }
    return true;
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(const CTransaction& tx, unsigned int nBits, uint256& hashProofOfStake, CBlock* blockFrom, unsigned int curTime)
{    
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString().c_str());

    const CTxIn& txin = tx.vin[0];
    bool contains = false;
    CTransaction wtx;
    BOOST_FOREACH(const CTransaction& tx, blockFrom->vtx)
    {
        if(tx.GetHash() == txin.prevout.hash)
        {
            wtx = tx;
            contains = true;
        }
    }
    if(!contains)
    {
        return tx.DoS(1, error("CheckProofOfStake() : INFO: read txPrev failed"));  // previous transaction not in main chain, may occur during initial download
    }

    // Verify signature
    if (!VerifySignature(wtx, tx, 0, true, 0))
        return tx.DoS(100, error("CheckProofOfStake() : VerifySignature failed on coinstake %s", tx.GetHash().ToString().c_str()));

    if (!CheckStakeKernelHashScrypt(nBits, (unsigned int)blockFrom->nTime, blockFrom->GetHash(), wtx, txin.prevout, curTime, hashProofOfStake, fDebug))
        return tx.DoS(1, error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s", tx.GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str())); // may occur during initial download or if behind on block chain sync

    return true;
}

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64 nTimeBlock, int64 nTimeTx)
{
    // v0.3 protocol
    return ( !(nTimeBlock > (nTimeTx + 10)) );
}

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex)
{
    assert (pindex->pprev || pindex->GetBlockHash() == hashGenesisBlock);
    if(pindex->GetBlockHash() == hashGenesisBlock)
    {
        unsigned int base = 3437301338;
        return base;
    }

    // Hash previous checksum with flags, hashProofOfStake and nStakeModifier
    CDataStream ss(SER_GETHASH, 0);
    if (pindex->pprev)
        ss << pindex->pprev->nStakeModifierChecksum;
    ss << pindex->hashProofOfStake << pindex->nStakeModifier;
    uint256 hashChecksum = Hash(ss.begin(), ss.end());
    hashChecksum >>= (256 - 32);
    uint64 hash64u= hashChecksum.Get64();
    return hash64u;
}

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, CBlockIndex* pindexNew)
{
    if (fTestNet) return true; // Testnet has no checkpoints

    if (mapStakeModifierCheckpoints.count(nHeight))
    {
        return pindexNew->nStakeModifierChecksum == mapStakeModifierCheckpoints[nHeight];
    }
    return true;
}
