////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2017 ArangoDB GmbH, Cologne, Germany
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
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_ROCKSDB_SPHERICAL_INDEX_H
#define ARANGOD_ROCKSDB_SPHERICAL_INDEX_H 1

#include "Basics/Result.h"
#include "Geo/GeoUtils.h"
#include "Geo/GeoParams.h"
#include "Geo/Near.h"
#include "Indexes/IndexIterator.h"
#include "RocksDBEngine/RocksDBIndex.h"
#include "VocBase/voc-types.h"

#include <geometry/s2cellid.h>
#include <velocypack/Builder.h>

class S2Region;

namespace arangodb {
class RocksDBGeoS2Index;

/// Common spherical Iterator supertype
class RocksDBGeoS2IndexIterator : public IndexIterator {
 public:
  RocksDBGeoS2IndexIterator(LogicalCollection* collection,
                            transaction::Methods* trx,
                            ManagedDocumentResult* mmdr,
                            RocksDBGeoS2Index const* index);

  char const* typeName() const override { return "s2index-index-iterator"; }

  //virtual geo::FilterType filterType() const = 0;

 protected:
  RocksDBGeoS2Index const* _index;
  std::unique_ptr<rocksdb::Iterator> _iter;
};

class RocksDBGeoS2Index final : public arangodb::RocksDBIndex {
  friend class RocksDBSphericalIndexIterator;

 public:
  RocksDBGeoS2Index() = delete;

  RocksDBGeoS2Index(TRI_idx_iid_t, arangodb::LogicalCollection*,
                    velocypack::Slice const&);

  ~RocksDBGeoS2Index() override {}

 public:
  /// @brief geo index variants
  enum class IndexVariant : uint8_t {
    NONE = 0,
    /// two distinct fields representing GeoJSON Point
    INDIVIDUAL_LAT_LON,
    /// pair [<latitude>, <longitude>] eqvivalent to GeoJSON Point
    COMBINED_LAT_LON,
    // geojson object or legacy coordinate
    // pair [<longitude>, <latitude>]. Should also support
    // other geojson object types.
    COMBINED_GEOJSON
  };

 public:
  IndexType type() const override { return TRI_IDX_TYPE_S2_INDEX; }

  char const* typeName() const override { return "s2index"; }

  IndexIterator* iteratorForCondition(transaction::Methods*,
                                      ManagedDocumentResult*,
                                      arangodb::aql::AstNode const*,
                                      arangodb::aql::Variable const*,
                                      bool) override;

  bool allowExpansion() const override { return false; }

  bool canBeDropped() const override { return true; }

  bool isSorted() const override { return true; }

  bool hasSelectivityEstimate() const override { return false; }

  void toVelocyPack(velocypack::Builder&, bool, bool) const override;
  // Uses default toVelocyPackFigures

  bool matchesDefinition(velocypack::Slice const& info) const override;

  void unload() override {}

  void truncate(transaction::Methods*) override;

  /// insert index elements into the specified write batch.
  Result insertInternal(transaction::Methods* trx, RocksDBMethods*,
                        LocalDocumentId const& documentId,
                        arangodb::velocypack::Slice const&,
                        OperationMode mode) override;

  /// remove index elements and put it in the specified write batch.
  Result removeInternal(transaction::Methods*, RocksDBMethods*,
                        LocalDocumentId const& documentId,
                        arangodb::velocypack::Slice const&,
                        OperationMode mode) override;

  IndexVariant variant() const { return _variant; }

 private:
  Result parse(velocypack::Slice const& doc, std::vector<S2CellId>& cells,
               geo::Coordinate& co) const;

 private:
  /// @brief immutable region coverer parameters
  geo::RegionCoverParams _coverParams;
  /// @brief the type of geo we support
  IndexVariant _variant;
  
  /// @brief attribute paths
  std::vector<std::string> _location;
  std::vector<std::string> _latitude;
  std::vector<std::string> _longitude;
  ;
};
}  // namespace arangodb

#endif
