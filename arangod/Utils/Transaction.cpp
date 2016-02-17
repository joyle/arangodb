////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Max Neunhoeffer
////////////////////////////////////////////////////////////////////////////////

#include "Utils/transactions.h"
#include "Indexes/PrimaryIndex.h"
#include "Storage/Marker.h"
#include "VocBase/KeyGenerator.h"

#include <velocypack/Builder.h>
#include <velocypack/Collection.h>
#include <velocypack/Slice.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;

////////////////////////////////////////////////////////////////////////////////
/// @brief if this pointer is set to an actual set, then for each request
/// sent to a shardId using the ClusterComm library, an X-Arango-Nolock
/// header is generated.
////////////////////////////////////////////////////////////////////////////////

thread_local std::unordered_set<std::string>* Transaction::_makeNolockHeaders =
    nullptr;
  
////////////////////////////////////////////////////////////////////////////////
/// @brief opens the declared collections of the transaction
////////////////////////////////////////////////////////////////////////////////

int Transaction::openCollections() {
  if (_trx == nullptr) {
    return TRI_ERROR_TRANSACTION_INTERNAL;
  }

  if (_setupState != TRI_ERROR_NO_ERROR) {
    return _setupState;
  }

  if (!_isReal) {
    return TRI_ERROR_NO_ERROR;
  }

  int res = TRI_EnsureCollectionsTransaction(_trx, _nestingLevel);

  return res;
}
  
////////////////////////////////////////////////////////////////////////////////
/// @brief begin the transaction
////////////////////////////////////////////////////////////////////////////////

int Transaction::begin() {
  if (_trx == nullptr) {
    return TRI_ERROR_TRANSACTION_INTERNAL;
  }

  if (_setupState != TRI_ERROR_NO_ERROR) {
    return _setupState;
  }

  if (!_isReal) {
    if (_nestingLevel == 0) {
      _trx->_status = TRI_TRANSACTION_RUNNING;
    }
    return TRI_ERROR_NO_ERROR;
  }

  int res = TRI_BeginTransaction(_trx, _hints, _nestingLevel);

  return res;
}
  
////////////////////////////////////////////////////////////////////////////////
/// @brief commit / finish the transaction
////////////////////////////////////////////////////////////////////////////////

int Transaction::commit() {
  if (_trx == nullptr || getStatus() != TRI_TRANSACTION_RUNNING) {
    // transaction not created or not running
    return TRI_ERROR_TRANSACTION_INTERNAL;
  }

  if (!_isReal) {
    if (_nestingLevel == 0) {
      _trx->_status = TRI_TRANSACTION_COMMITTED;
    }
    return TRI_ERROR_NO_ERROR;
  }

  int res = TRI_CommitTransaction(_trx, _nestingLevel);

  return res;
}
  
////////////////////////////////////////////////////////////////////////////////
/// @brief abort the transaction
////////////////////////////////////////////////////////////////////////////////

int Transaction::abort() {
  if (_trx == nullptr || getStatus() != TRI_TRANSACTION_RUNNING) {
    // transaction not created or not running
    return TRI_ERROR_TRANSACTION_INTERNAL;
  }

  if (!_isReal) {
    if (_nestingLevel == 0) {
      _trx->_status = TRI_TRANSACTION_ABORTED;
    }

    return TRI_ERROR_NO_ERROR;
  }

  int res = TRI_AbortTransaction(_trx, _nestingLevel);

  return res;
}
  
////////////////////////////////////////////////////////////////////////////////
/// @brief finish a transaction (commit or abort), based on the previous state
////////////////////////////////////////////////////////////////////////////////

int Transaction::finish(int errorNum) {
  if (errorNum == TRI_ERROR_NO_ERROR) {
    // there was no previous error, so we'll commit
    return this->commit();
  }
  
  // there was a previous error, so we'll abort
  this->abort();

  // return original error number
  return errorNum;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief read all master pointers, using skip and limit and an internal
/// offset into the primary index. this can be used for incremental access to
/// the documents without restarting the index scan at the begin
////////////////////////////////////////////////////////////////////////////////

int Transaction::readIncremental(TRI_transaction_collection_t* trxCollection,
                                 std::vector<TRI_doc_mptr_copy_t>& docs,
                                 arangodb::basics::BucketPosition& internalSkip,
                                 uint64_t batchSize, uint64_t& skip,
                                 uint64_t limit, uint64_t& total) {
  TRI_document_collection_t* document = documentCollection(trxCollection);

  // READ-LOCK START
  int res = this->lock(trxCollection, TRI_TRANSACTION_READ);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  if (orderDitch(trxCollection) == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  try {
    if (batchSize > 2048) {
      docs.reserve(2048);
    } else if (batchSize > 0) {
      docs.reserve(batchSize);
    }

    auto primaryIndex = document->primaryIndex();
    uint64_t count = 0;

    while (count < batchSize || skip > 0) {
      TRI_doc_mptr_t const* mptr =
          primaryIndex->lookupSequential(this, internalSkip, total);

      if (mptr == nullptr) {
        break;
      }
      if (skip > 0) {
        --skip;
      } else {
        docs.emplace_back(*mptr);

        if (++count >= limit) {
          break;
        }
      }
    }
  } catch (...) {
    this->unlock(trxCollection, TRI_TRANSACTION_READ);
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  this->unlock(trxCollection, TRI_TRANSACTION_READ);
  // READ-LOCK END

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief read all master pointers, using skip and limit and an internal
/// offset into the primary index. this can be used for incremental access to
/// the documents without restarting the index scan at the begin
////////////////////////////////////////////////////////////////////////////////

int Transaction::any(TRI_transaction_collection_t* trxCollection,
                     std::vector<TRI_doc_mptr_copy_t>& docs,
                     arangodb::basics::BucketPosition& initialPosition,
                     arangodb::basics::BucketPosition& position,
                     uint64_t batchSize, uint64_t& step,
                     uint64_t& total) {
  TRI_document_collection_t* document = documentCollection(trxCollection);
  // READ-LOCK START
  int res = this->lock(trxCollection, TRI_TRANSACTION_READ);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }
  if (orderDitch(trxCollection) == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  uint64_t numRead = 0;
  TRI_ASSERT(batchSize > 0);

  while (numRead < batchSize) {
    auto mptr = document->primaryIndex()->lookupRandom(this, initialPosition,
                                                       position, step, total);
    if (mptr == nullptr) {
      // Read all documents randomly
      break;
    }
    docs.emplace_back(*mptr);
    ++numRead;
  }
  this->unlock(trxCollection, TRI_TRANSACTION_READ);
  // READ-LOCK END
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief read any (random) document
////////////////////////////////////////////////////////////////////////////////

int Transaction::any(TRI_transaction_collection_t* trxCollection,
                     TRI_doc_mptr_copy_t* mptr) {
  TRI_ASSERT(mptr != nullptr);
  TRI_document_collection_t* document = documentCollection(trxCollection);

  // READ-LOCK START
  int res = this->lock(trxCollection, TRI_TRANSACTION_READ);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }
  if (orderDitch(trxCollection) == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  auto idx = document->primaryIndex();
  arangodb::basics::BucketPosition intPos;
  arangodb::basics::BucketPosition pos;
  uint64_t step = 0;
  uint64_t total = 0;

  TRI_doc_mptr_t* found = idx->lookupRandom(this, intPos, pos, step, total);
  if (found != nullptr) {
    *mptr = *found;
  }
  this->unlock(trxCollection, TRI_TRANSACTION_READ);
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief read all documents
////////////////////////////////////////////////////////////////////////////////

int Transaction::all(TRI_transaction_collection_t* trxCollection,
                     std::vector<std::string>& ids, bool lock) {
  TRI_document_collection_t* document = documentCollection(trxCollection);

  if (lock) {
    // READ-LOCK START
    int res = this->lock(trxCollection, TRI_TRANSACTION_READ);

    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }
  }

  if (orderDitch(trxCollection) == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  auto idx = document->primaryIndex();
  size_t used = idx->size();

  if (used > 0) {
    arangodb::basics::BucketPosition step;
    uint64_t total = 0;

    while (true) {
      TRI_doc_mptr_t const* mptr = idx->lookupSequential(this, step, total);

      if (mptr == nullptr) {
        break;
      }
      ids.emplace_back(TRI_EXTRACT_MARKER_KEY(mptr));
    }
  }

  if (lock) {
    this->unlock(trxCollection, TRI_TRANSACTION_READ);
    // READ-LOCK END
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief read all master pointers, using skip and limit
////////////////////////////////////////////////////////////////////////////////

int Transaction::readSlice(TRI_transaction_collection_t* trxCollection,
                           std::vector<TRI_doc_mptr_copy_t>& docs, int64_t skip,
                           uint64_t limit, uint64_t& total) {
  TRI_document_collection_t* document = documentCollection(trxCollection);

  if (limit == 0) {
    // nothing to do
    return TRI_ERROR_NO_ERROR;
  }

  // READ-LOCK START
  int res = this->lock(trxCollection, TRI_TRANSACTION_READ);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  if (orderDitch(trxCollection) == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  uint64_t count = 0;
  auto idx = document->primaryIndex();
  TRI_doc_mptr_t const* mptr = nullptr;

  if (skip < 0) {
    arangodb::basics::BucketPosition position;
    do {
      mptr = idx->lookupSequentialReverse(this, position);
      ++skip;
    } while (skip < 0 && mptr != nullptr);

    if (mptr == nullptr) {
      this->unlock(trxCollection, TRI_TRANSACTION_READ);
      // To few elements, skipped all
      return TRI_ERROR_NO_ERROR;
    }

    do {
      mptr = idx->lookupSequentialReverse(this, position);

      if (mptr == nullptr) {
        break;
      }
      ++count;
      docs.emplace_back(*mptr);
    } while (count < limit);

    this->unlock(trxCollection, TRI_TRANSACTION_READ);
    return TRI_ERROR_NO_ERROR;
  }
  arangodb::basics::BucketPosition position;

  while (skip > 0) {
    mptr = idx->lookupSequential(this, position, total);
    --skip;
    if (mptr == nullptr) {
      // To few elements, skipped all
      this->unlock(trxCollection, TRI_TRANSACTION_READ);
      return TRI_ERROR_NO_ERROR;
    }
  }

  do {
    mptr = idx->lookupSequential(this, position, total);
    if (mptr == nullptr) {
      break;
    }
    ++count;
    docs.emplace_back(*mptr);
  } while (count < limit);

  this->unlock(trxCollection, TRI_TRANSACTION_READ);

  return TRI_ERROR_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////////
/// @brief return one or multiple documents from a collection
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::document(std::string const& collectionName,
                                      VPackSlice const& value,
                                      OperationOptions const& options) {
  TRI_ASSERT(getStatus() == TRI_TRANSACTION_RUNNING);

  if (!value.isObject() && !value.isArray()) {
    // must provide a document object or an array of documents
    THROW_ARANGO_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  if (value.isArray()) {
    // multi-document variant is not yet implemented
    THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
  }

  if (ServerState::instance()->isCoordinator()) {
    return documentCoordinator(collectionName, value, options);
  }

  return documentLocal(collectionName, value, options);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief read one or multiple documents in a collection, coordinator
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::documentCoordinator(std::string const& collectionName,
                                                 VPackSlice const& value,
                                                 OperationOptions const& options) {
  // TODO
  THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief read one or multiple documents in a collection, local
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::documentLocal(std::string const& collectionName,
                                           VPackSlice const& value,
                                           OperationOptions const& options) {
  TRI_voc_cid_t cid = resolver()->getCollectionId(collectionName);

  if (cid == 0) {
    return OperationResult(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }
  
  std::string key;
  TRI_voc_rid_t expectedRevision = 0;

  // extract _key
  if (value.isObject()) {
    VPackSlice k = value.get(TRI_VOC_ATTRIBUTE_KEY);
    if (!k.isString()) {
      return OperationResult(TRI_ERROR_ARANGO_DOCUMENT_KEY_BAD);
    }
    key = k.copyString();
  
    // extract _rev  
    VPackSlice r = value.get(TRI_VOC_ATTRIBUTE_REV);
    if (!r.isNone()) {
      if (r.isString()) {
        expectedRevision = arangodb::basics::StringUtils::uint64(r.copyString());
      }
      else if (r.isInteger()) {
        expectedRevision = r.getNumber<TRI_voc_rid_t>();
      }
    }
  } else if (value.isString()) {
    key = value.copyString();
  } else {
    return OperationResult(TRI_ERROR_ARANGO_DOCUMENT_KEY_BAD);
  }

  // TODO: clean this up
  TRI_document_collection_t* document = documentCollection(trxCollection(cid));

  if (orderDitch(trxCollection(cid)) == nullptr) {
    return OperationResult(TRI_ERROR_OUT_OF_MEMORY);
  }
 
  TRI_doc_mptr_copy_t mptr;
  int res = document->read(this, key, &mptr, !isLocked(document, TRI_TRANSACTION_READ));

  if (res != TRI_ERROR_NO_ERROR) {
    return OperationResult(res);
  }
  
  TRI_ASSERT(mptr.getDataPtr() != nullptr);
  if (expectedRevision != 0 && expectedRevision != mptr._rid) {
    return OperationResult(TRI_ERROR_ARANGO_CONFLICT);
  }
  
  VPackBuilder resultBuilder;
  resultBuilder.add(VPackValue(mptr.vpack()));

  return OperationResult(TRI_ERROR_NO_ERROR, resultBuilder.steal()); 
}

//////////////////////////////////////////////////////////////////////////////
/// @brief create one or multiple documents in a collection
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::insert(std::string const& collectionName,
                                    VPackSlice const& value,
                                    OperationOptions const& options) {
  TRI_ASSERT(getStatus() == TRI_TRANSACTION_RUNNING);

  if (!value.isObject() && !value.isArray()) {
    // must provide a document object or an array of documents
    THROW_ARANGO_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  if (value.isArray()) {
    // multi-document variant is not yet implemented
    THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
  }

  if (ServerState::instance()->isCoordinator()) {
    return insertCoordinator(collectionName, value, options);
  }

  return insertLocal(collectionName, value, options);
}
   
//////////////////////////////////////////////////////////////////////////////
/// @brief create one or multiple documents in a collection, coordinator
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::insertCoordinator(std::string const& collectionName,
                                               VPackSlice const& value,
                                               OperationOptions const& options) {
  // TODO
  THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief create one or multiple documents in a collection, local
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::insertLocal(std::string const& collectionName,
                                         VPackSlice const& value,
                                         OperationOptions const& options) {
  
  TRI_voc_cid_t cid = resolver()->getCollectionId(collectionName);

  if (cid == 0) {
    return OperationResult(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  // TODO: clean this up
  TRI_document_collection_t* document = documentCollection(trxCollection(cid));
 

  // add missing attributes for document (_id, _rev, _key)
  VPackBuilder merge;
  merge.openObject();
   
  // generate a new tick value
  TRI_voc_tick_t const tick = TRI_NewTickServer();

  auto key = value.get(TRI_VOC_ATTRIBUTE_KEY);

  if (key.isNone()) {
    // "_key" attribute not present in object
    merge.add(TRI_VOC_ATTRIBUTE_KEY, VPackValue(document->_keyGenerator->generate(tick)));
  } else if (!key.isString()) {
    // "_key" present but wrong type
    return OperationResult(TRI_ERROR_ARANGO_DOCUMENT_KEY_BAD);
  } else {
    int res = document->_keyGenerator->validate(key.copyString(), false);

    if (res != TRI_ERROR_NO_ERROR) {
      // invalid key value
      return OperationResult(res);
    }
  }
  
  // add _rev attribute
  merge.add(TRI_VOC_ATTRIBUTE_REV, VPackValue(std::to_string(tick)));

  // add _id attribute
  uint8_t* p = merge.add(TRI_VOC_ATTRIBUTE_ID, VPackValuePair(9ULL, VPackValueType::Custom));
  *p++ = 0xf3; // custom type for _id
  MarkerHelper::storeNumber<uint64_t>(p, cid, sizeof(uint64_t));

  merge.close();

  VPackBuilder toInsert = VPackCollection::merge(value, merge.slice(), false, false); 
  VPackSlice insertSlice = toInsert.slice();
  
  if (orderDitch(trxCollection(cid)) == nullptr) {
    return OperationResult(TRI_ERROR_OUT_OF_MEMORY);
  }

  TRI_doc_mptr_copy_t mptr;
  int res = document->insert(this, &insertSlice, &mptr, !isLocked(document, TRI_TRANSACTION_WRITE), options.waitForSync);
  
  if (res != TRI_ERROR_NO_ERROR) {
    return OperationResult(res);
  }

  TRI_ASSERT(mptr.getDataPtr() != nullptr);
  
  VPackSlice vpack(mptr.vpack());
  std::string resultKey = VPackSlice(mptr.vpack()).get(TRI_VOC_ATTRIBUTE_KEY).copyString(); 

  VPackBuilder resultBuilder;
  resultBuilder.openObject();
  resultBuilder.add(TRI_VOC_ATTRIBUTE_ID, VPackValue(std::string(collectionName + "/" + resultKey)));
  resultBuilder.add(TRI_VOC_ATTRIBUTE_REV, VPackValue(vpack.get(TRI_VOC_ATTRIBUTE_REV).copyString()));
  resultBuilder.add(TRI_VOC_ATTRIBUTE_KEY, VPackValue(resultKey));
  resultBuilder.close();

  return OperationResult(TRI_ERROR_NO_ERROR, resultBuilder.steal()); 
}
  
OperationResult Transaction::replace(std::string const& collectionName,
                            VPackSlice const& oldValue,
                            VPackSlice const& updateValue,
                            OperationOptions const& options) {
  THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief update/patch one or multiple documents in a collection
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::update(std::string const& collectionName,
                                    VPackSlice const& oldValue,
                                    VPackSlice const& newValue,
                                    OperationOptions const& options) {
  TRI_ASSERT(getStatus() == TRI_TRANSACTION_RUNNING);

  if (!oldValue.isObject() && !oldValue.isArray()) {
    // must provide a document object or an array of documents
    THROW_ARANGO_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }
  
  if (!newValue.isObject() && !newValue.isArray()) {
    // must provide a document object or an array of documents
    THROW_ARANGO_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  if (oldValue.isArray() || newValue.isArray()) {
    // multi-document variant is not yet implemented
    THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
  }

  if (ServerState::instance()->isCoordinator()) {
    return updateCoordinator(collectionName, oldValue, newValue, options);
  }

  return updateLocal(collectionName, oldValue, newValue, options);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief update one or multiple documents in a collection, coordinator
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::updateCoordinator(std::string const& collectionName,
                                               VPackSlice const& oldValue,
                                               VPackSlice const& newValue,
                                               OperationOptions const& options) {
  // TODO
  THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief update one or multiple documents in a collection, local
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::updateLocal(std::string const& collectionName,
                                         VPackSlice const& oldValue,
                                         VPackSlice const& newValue,
                                         OperationOptions const& options) {
  // TODO
  THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief remove one or multiple documents in a collection
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::remove(std::string const& collectionName,
                                    VPackSlice const& value,
                                    OperationOptions const& options) {
  TRI_ASSERT(getStatus() == TRI_TRANSACTION_RUNNING);

  if (!value.isObject() && !value.isArray() && !value.isString()) {
    // must provide a document object or an array of documents
    THROW_ARANGO_EXCEPTION(TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  if (value.isArray()) {
    // multi-document variant is not yet implemented
    THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
  }

  if (ServerState::instance()->isCoordinator()) {
    return removeCoordinator(collectionName, value, options);
  }

  return removeLocal(collectionName, value, options);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief remove one or multiple documents in a collection, coordinator
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::removeCoordinator(std::string const& collectionName,
                                               VPackSlice const& value,
                                               OperationOptions const& options) {
  // TODO
  THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
}

//////////////////////////////////////////////////////////////////////////////
/// @brief remove one or multiple documents in a collection, local
/// the single-document variant of this operation will either succeed or,
/// if it fails, clean up after itself
//////////////////////////////////////////////////////////////////////////////

OperationResult Transaction::removeLocal(std::string const& collectionName,
                                         VPackSlice const& value,
                                         OperationOptions const& options) {
  TRI_voc_cid_t cid = resolver()->getCollectionId(collectionName);

  if (cid == 0) {
    return OperationResult(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  // TODO: clean this up
  TRI_document_collection_t* document = documentCollection(trxCollection(cid));
 
  std::string key; 
  TRI_voc_rid_t expectedRevision = 0;
 
  VPackBuilder builder;
  builder.openObject();

  // extract _key
  if (value.isObject()) {
    VPackSlice k = value.get(TRI_VOC_ATTRIBUTE_KEY);
    if (!k.isString()) {
      return OperationResult(TRI_ERROR_ARANGO_DOCUMENT_KEY_BAD);
    }
    builder.add(TRI_VOC_ATTRIBUTE_KEY, k);
    
    VPackSlice r = value.get(TRI_VOC_ATTRIBUTE_REV);
    if (!r.isNone()) {
      if (r.isString()) {
        expectedRevision = arangodb::basics::StringUtils::uint64(r.copyString());
      }
      else if (r.isInteger()) {
        expectedRevision = r.getNumber<TRI_voc_rid_t>();
      }
    }
  } else if (value.isString()) {
    builder.add(TRI_VOC_ATTRIBUTE_KEY, value);
  }
  
  // add _rev  
  builder.add(TRI_VOC_ATTRIBUTE_REV, VPackValue(std::to_string(expectedRevision)));
  builder.close();

  VPackSlice removeSlice = builder.slice();

  TRI_voc_rid_t actualRevision = 0;
  TRI_doc_update_policy_t updatePolicy(expectedRevision == 0 ? TRI_DOC_UPDATE_LAST_WRITE : TRI_DOC_UPDATE_ERROR, expectedRevision, &actualRevision);
  int res = document->remove(this, &removeSlice, &updatePolicy, !isLocked(document, TRI_TRANSACTION_WRITE), options.waitForSync);
  
  if (res != TRI_ERROR_NO_ERROR) {
    return OperationResult(res);
  }

  VPackBuilder resultBuilder;
  resultBuilder.openObject();
  resultBuilder.add(TRI_VOC_ATTRIBUTE_ID, VPackValue(std::string(collectionName + "/" + key)));
  resultBuilder.add(TRI_VOC_ATTRIBUTE_REV, VPackValue(std::to_string(actualRevision)));
  resultBuilder.add(TRI_VOC_ATTRIBUTE_KEY, VPackValue(key));
  resultBuilder.close();

  return OperationResult(TRI_ERROR_NO_ERROR, resultBuilder.steal()); 
}

