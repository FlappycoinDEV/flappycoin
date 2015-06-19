// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef PPCOIN_KERNEL_H
#define PPCOIN_KERNEL_H

#include <boost/assign/list_of.hpp>

#include "main.h"
#include "util.h"

static std::map<int, unsigned int> mapStakeModifierCheckpoints =
    boost::assign::map_list_of
        (     0, 0xcce10e5au )
        ;

// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static const unsigned int MODIFIER_INTERVAL = 6 * 60 * 60;
extern unsigned int nModifierInterval;

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64 & nStakeModifier, bool& fGeneratedStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
bool CheckStakeKernelHashScrypt(unsigned int nBits, unsigned int blockFromTime, uint256 blockFromHash, const CWalletTx txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, bool fPrintProofOfStake=false);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(const CTransaction& tx, unsigned int nBits, uint256& hashProofOfStake);

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64 nTimeBlock, int64 nTimeTx);

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, CBlockIndex *pindexNew);

#endif // PPCOIN_KERNEL_H
