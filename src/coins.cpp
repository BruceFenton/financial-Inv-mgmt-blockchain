// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "consensus/consensus.h"
#include "memusage.h"
#include "random.h"
#include "util.h"
#include "validation.h"
#include "tinyformat.h"
#include "base58.h"

#include <assert.h>
#include <assets/assets.h>
#include <wallet/wallet.h>

bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
std::vector<uint256> CCoinsView::GetHeadBlocks() const { return std::vector<uint256>(); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) { return false; }
CCoinsViewCursor *CCoinsView::Cursor() const { return nullptr; }

bool CCoinsView::HaveCoin(const COutPoint &outpoint) const
{
    Coin coin;
    return GetCoin(outpoint, coin);
}

CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const { return base->GetCoin(outpoint, coin); }
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const { return base->HaveCoin(outpoint); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
std::vector<uint256> CCoinsViewBacked::GetHeadBlocks() const { return base->GetHeadBlocks(); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) { return base->BatchWrite(mapCoins, hashBlock); }
CCoinsViewCursor *CCoinsViewBacked::Cursor() const { return base->Cursor(); }
size_t CCoinsViewBacked::EstimateSize() const { return base->EstimateSize(); }

SaltedOutpointHasher::SaltedOutpointHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), cachedCoinsUsage(0) {}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end())
        return it;
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::forward_as_tuple(std::move(tmp))).first;
    if (ret->second.coin.IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it != cacheCoins.end()) {
        coin = it->second.coin;
        return !coin.IsSpent();
    }
    return false;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin&& coin, bool possible_overwrite) {
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable()) return;
    CCoinsMap::iterator it;
    bool inserted;
    std::tie(it, inserted) = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }
    if (!possible_overwrite) {
        if (!it->second.coin.IsSpent()) {
            throw std::logic_error("Adding new coin that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);
    }
    it->second.coin = std::move(coin);
    it->second.flags |= CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0);
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
}

void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, uint256 blockHash, bool check, CAssetsCache* assetsCache, std::pair<std::string, CBlockAssetUndo>* undoAssetData) {
    bool fCoinbase = tx.IsCoinBase();
    const uint256& txid = tx.GetHash();

    /** RVN START */
    if (AreAssetsDeployed()) {
        if (assetsCache) {
            if (tx.IsNewAsset()) { // This works are all new root assets, sub asset, and restricted assets
                CNewAsset asset;
                std::string strAddress;
                AssetFromTransaction(tx, asset, strAddress);

                std::string ownerName;
                std::string ownerAddress;
                OwnerFromTransaction(tx, ownerName, ownerAddress);

                // Add the new asset to cache
                if (!assetsCache->AddNewAsset(asset, strAddress, nHeight, blockHash))
                    error("%s : Failed at adding a new asset to our cache. asset: %s", __func__,
                          asset.strName);

                // Add the owner asset to cache
                if (!assetsCache->AddOwnerAsset(ownerName, ownerAddress))
                    error("%s : Failed at adding a new asset to our cache. asset: %s", __func__,
                          asset.strName);

            } else if (tx.IsReissueAsset()) {
                CReissueAsset reissue;
                std::string strAddress;
                ReissueAssetFromTransaction(tx, reissue, strAddress);

                int reissueIndex = tx.vout.size() - 1;

                // Get the asset before we change it
                CNewAsset asset;
                if (!assetsCache->GetAssetMetaDataIfExists(reissue.strName, asset))
                    error("%s: Failed to get the original asset that is getting reissued. Asset Name : %s",
                          __func__, reissue.strName);

                if (!assetsCache->AddReissueAsset(reissue, strAddress, COutPoint(txid, reissueIndex)))
                    error("%s: Failed to reissue an asset. Asset Name : %s", __func__, reissue.strName);

                // Check to see if we are reissuing a restricted asset
                bool fFoundRestrictedAsset = false;
                AssetType type;
                IsAssetNameValid(asset.strName, type);
                if (type == AssetType::RESTRICTED) {
                    fFoundRestrictedAsset = true;
                }

                // Set the old IPFSHash for the blockundo
                bool fIPFSChanged = !reissue.strIPFSHash.empty();
                bool fUnitsChanged = reissue.nUnits != -1;
                bool fVerifierChanged = false;
                std::string strOldVerifier = "";

                // If we are reissuing a restricted asset, we need to check to see if the verifier string is being reissued
                if (fFoundRestrictedAsset) {
                    CNullAssetTxVerifierString verifier;
                    // Search through all outputs until you find a restricted verifier change.
                    for (auto index: tx.vout) {
                        if (index.scriptPubKey.IsNullAssetVerifierTxDataScript()) {
                            if (!AssetNullVerifierDataFromScript(index.scriptPubKey, verifier)) {
                                error("%s: Failed to get asset null verifier data and add it to the coins CTxOut: %s", __func__,
                                      index.ToString());
                                break;
                            }

                            fVerifierChanged = true;
                            break;
                        }
                    }

                    CNullAssetTxVerifierString oldVerifer{strOldVerifier};
                    if (fVerifierChanged && !assetsCache->GetAssetVerifierStringIfExists(asset.strName, oldVerifer))
                        error("%s : Failed to get asset original verifier string that is getting reissued, Asset Name: %s", __func__, asset.strName);

                    if (fVerifierChanged) {
                        strOldVerifier = oldVerifer.verifier_string;
                    }

                    // Add the verifier to the cache if there was one found
                    if (fVerifierChanged && !assetsCache->AddRestrictedVerifier(asset.strName, verifier.verifier_string))
                        error("%s : Failed at adding a restricted verifier to our cache: asset: %s, verifier : %s",
                              asset.strName, verifier.verifier_string);
                }

                // If any of the following items were changed by reissuing, we need to database the old values so it can be undone correctly
                if (fIPFSChanged || fUnitsChanged || fVerifierChanged) {
                    undoAssetData->first = reissue.strName; // Asset Name
                    undoAssetData->second = CBlockAssetUndo {fIPFSChanged, fUnitsChanged, asset.strIPFSHash, asset.units, ASSET_UNDO_INCLUDES_VERIFIER_STRING, fVerifierChanged, strOldVerifier}; // ipfschanged, unitchanged, Old Assets IPFSHash, old units
                }
            } else if (tx.IsNewUniqueAsset()) {
                for (int n = 0; n < (int)tx.vout.size(); n++) {
                    auto out = tx.vout[n];

                    CNewAsset asset;
                    std::string strAddress;

                    if (IsScriptNewUniqueAsset(out.scriptPubKey)) {
                        AssetFromScript(out.scriptPubKey, asset, strAddress);

                        // Add the new asset to cache
                        if (!assetsCache->AddNewAsset(asset, strAddress, nHeight, blockHash))
                            error("%s : Failed at adding a new asset to our cache. asset: %s", __func__,
                                  asset.strName);
                    }
                }
            } else if (tx.IsNewMsgChannelAsset()) {
                CNewAsset asset;
                std::string strAddress;
                MsgChannelAssetFromTransaction(tx, asset, strAddress);

                // Add the new asset to cache
                if (!assetsCache->AddNewAsset(asset, strAddress, nHeight, blockHash))
                    error("%s : Failed at adding a new asset to our cache. asset: %s", __func__,
                          asset.strName);
            } else if (tx.IsNewQualifierAsset()) {
                CNewAsset asset;
                std::string strAddress;
                QualifierAssetFromTransaction(tx, asset, strAddress);

                // Add the new asset to cache
                if (!assetsCache->AddNewAsset(asset, strAddress, nHeight, blockHash))
                    error("%s : Failed at adding a new qualifier asset to our cache. asset: %s", __func__,
                          asset.strName);
            }  else if (tx.IsNewRestrictedAsset()) {
                CNewAsset asset;
                std::string strAddress;
                RestrictedAssetFromTransaction(tx, asset, strAddress);

                // Add the new asset to cache
                if (!assetsCache->AddNewAsset(asset, strAddress, nHeight, blockHash))
                    error("%s : Failed at adding a new restricted asset to our cache. asset: %s", __func__,
                          asset.strName);

                // Find the restricted verifier string and cache it
                CNullAssetTxVerifierString verifier;
                // Search through all outputs until you find a restricted verifier change.
                for (auto index: tx.vout) {
                    if (index.scriptPubKey.IsNullAssetVerifierTxDataScript()) {
                        CNullAssetTxVerifierString verifier;
                        if (!AssetNullVerifierDataFromScript(index.scriptPubKey, verifier))
                            error("%s: Failed to get asset null data and add it to the coins CTxOut: %s", __func__,
                                  index.ToString());

                        // Add the verifier to the cache
                        if (!assetsCache->AddRestrictedVerifier(asset.strName, verifier.verifier_string))
                            error("%s : Failed at adding a restricted verifier to our cache: asset: %s, verifier : %s",
                                  asset.strName, verifier.verifier_string);

                        break;
                    }
                }
            }
        }
    }
    /** RVN END */

    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        // Always set the possible_overwrite flag to AddCoin for coinbase txn, in order to correctly
        // deal with the pre-BIP30 occurrences of duplicate coinbase transactions.
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), overwrite);

        /** RVN START */
        if (AreAssetsDeployed()) {
            if (assetsCache) {
                CAssetOutputEntry assetData;
                if (GetAssetData(tx.vout[i].scriptPubKey, assetData)) {

                    // If this is a transfer asset, and the amount is greater than zero
                    // We want to make sure it is added to the asset addresses database if (fAssetIndex == true)
                    if (assetData.type == TX_TRANSFER_ASSET && assetData.nAmount > 0) {
                        // Create the objects needed from the assetData
                        CAssetTransfer assetTransfer(assetData.assetName, assetData.nAmount, assetData.message, assetData.expireTime);
                        std::string address = EncodeDestination(assetData.destination);

                        // Add the transfer asset data to the asset cache
                        if (!assetsCache->AddTransferAsset(assetTransfer, address, COutPoint(txid, i), tx.vout[i]))
                            LogPrintf("%s : ERROR - Failed to add transfer asset CTxOut: %s\n", __func__,
                                      tx.vout[i].ToString());

                        /** Subscribe to new message channels if they are sent to a new address, or they are the owner token or message channel */
#ifdef ENABLE_WALLET
                        if (fMessaging && pMessageSubscribedChannelsCache) {
                            LOCK(cs_messaging);
                            if (vpwallets.size() && vpwallets[0]->IsMine(tx.vout[i]) == ISMINE_SPENDABLE) {
                                AssetType aType;
                                IsAssetNameValid(assetTransfer.strName, aType);

                                if (aType == AssetType::ROOT || aType == AssetType::SUB) {
                                    if (!IsChannelSubscribed(GetParentName(assetTransfer.strName) + OWNER_TAG)) {
                                        if (!IsAddressSeen(address)) {
                                            AddChannel(GetParentName(assetTransfer.strName) + OWNER_TAG);
                                            AddAddressSeen(address);
                                        }
                                    }
                                } else if (aType == AssetType::OWNER || aType == AssetType::MSGCHANNEL) {
                                    AddChannel(assetTransfer.strName);
                                    AddAddressSeen(address);
                                }
                            }
                        }
#endif
                    } else if (assetData.type == TX_NEW_ASSET) {
                        /** Subscribe to new message channels if they are assets you created, or are new msgchannels of channels already being watched */
#ifdef ENABLE_WALLET
                        if (fMessaging && pMessageSubscribedChannelsCache) {
                            LOCK(cs_messaging);
                            if (vpwallets.size()) {
                                AssetType aType;
                                IsAssetNameValid(assetData.assetName, aType);
                                if (vpwallets[0]->IsMine(tx.vout[i]) == ISMINE_SPENDABLE) {
                                    if (aType == AssetType::ROOT || aType == AssetType::SUB) {
                                        AddChannel(assetData.assetName + OWNER_TAG);
                                        AddAddressSeen(EncodeDestination(assetData.destination));
                                    } else if (aType == AssetType::OWNER || aType == AssetType::MSGCHANNEL) {
                                        AddChannel(assetData.assetName);
                                        AddAddressSeen(EncodeDestination(assetData.destination));
                                    }
                                } else {
                                    if (aType == AssetType::MSGCHANNEL) {
                                        if (IsChannelSubscribed(GetParentName(assetData.assetName) + OWNER_TAG)) {
                                            AddChannel(assetData.assetName);
                                        }
                                    }
                                }
                            }
                        }
#endif
                    }
                }

                CScript script = tx.vout[i].scriptPubKey;
                if (script.IsNullAsset()) {
                    if (script.IsNullAssetTxDataScript()) {
                        CNullAssetTxData data;
                        std::string address;
                        AssetNullDataFromScript(script, data, address);

                        AssetType type;
                        IsAssetNameValid(data.asset_name, type);

                        if (type == AssetType::RESTRICTED) {
                            assetsCache->AddRestrictedAddress(data.asset_name, address, data.flag ? RestrictedType::FREEZE_ADDRESS : RestrictedType::UNFREEZE_ADDRESS);
                        } else if (type == AssetType::QUALIFIER || type == AssetType::SUB_QUALIFIER) {
                            assetsCache->AddQualifierAddress(data.asset_name, address, data.flag ? QualifierType::ADD_QUALIFIER : QualifierType::REMOVE_QUALIFIER);
                        }
                    } else if (script.IsNullGlobalRestrictionAssetTxDataScript()) {
                        CNullAssetTxData data;
                        GlobalAssetNullDataFromScript(script, data);

                        assetsCache->AddGlobalRestricted(data.asset_name, data.flag ? RestrictedType::GLOBAL_FREEZE : RestrictedType::GLOBAL_UNFREEZE);
                    }
                }
            }
        }
        /** RVN END */
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin* moveout, CAssetsCache* assetsCache) {

    CCoinsMap::iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end())
        return false;
    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();

    /** RVN START */
    Coin tempCoin = it->second.coin;
    /** RVN END */

    if (moveout) {
        *moveout = std::move(it->second.coin);
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH) {
        cacheCoins.erase(it);
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.coin.Clear();
    }

    /** RVN START */
    if (AreAssetsDeployed()) {
        if (assetsCache) {
            if (!assetsCache->TrySpendCoin(outpoint, tempCoin.out)) {
                return error("%s : Failed to try and spend the asset. COutPoint : %s", __func__, outpoint.ToString());
            }
        }
    }
    /** RVN END */

    return true;
}

static const Coin coinEmpty;

const Coin& CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    } else {
        return it->second.coin;
    }
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn) {
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
                    // Otherwise we will need to create it in the parent
                    // and move the data up and mark it as dirty
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coin = std::move(it->second.coin);
                    cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY;
                    // We can mark it FRESH in the parent if it was FRESH in the child
                    // Otherwise it might have just been flushed from the parent's cache
                    // and already exist in the grandparent
                    if (it->second.flags & CCoinsCacheEntry::FRESH)
                        entry.flags |= CCoinsCacheEntry::FRESH;
                }
            } else {
                // Assert that the child cache entry was not marked FRESH if the
                // parent cache entry has unspent outputs. If this ever happens,
                // it means the FRESH flag was misapplied and there is a logic
                // error in the calling code.
                if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent())
                    throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");

                // Found the entry in the parent cache
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                    itUs->second.coin = std::move(it->second.coin);
                    cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                    // NOTE: It is possible the child has a FRESH flag here in
                    // the event the entry we found in the parent is pruned. But
                    // we must not copy that FRESH flag to the parent as that
                    // pruned state likely still needs to be communicated to the
                    // grandparent.
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock);
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    return fOk;
}

void CCoinsViewCache::Uncache(const COutPoint& hash)
{
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && it->second.flags == 0) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
        cacheCoins.erase(it);
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += AccessCoin(tx.vin[i].prevout).out.nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!HaveCoin(tx.vin[i].prevout)) {
                return false;
            }
        }
    }
    return true;
}

static const size_t MIN_TRANSACTION_OUTPUT_WEIGHT = WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxOut(), SER_NETWORK, PROTOCOL_VERSION);
//static const size_t MAX_OUTPUTS_PER_BLOCK = MAX_BLOCK_WEIGHT / MIN_TRANSACTION_OUTPUT_WEIGHT;

const Coin& AccessByTxid(const CCoinsViewCache& view, const uint256& txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < GetMaxBlockWeight() / MIN_TRANSACTION_OUTPUT_WEIGHT) {
        const Coin& alternate = view.AccessCoin(iter);
        if (!alternate.IsSpent()) return alternate;
        ++iter.n;
    }
    return coinEmpty;
}
