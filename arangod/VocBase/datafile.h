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
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_VOC_BASE_DATAFILE_H
#define ARANGOD_VOC_BASE_DATAFILE_H 1

#include "Basics/Common.h"
#include "VocBase/shaped-json.h"
#include "VocBase/vocbase.h"

struct TRI_datafile_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief Datafiles
///
/// All data is stored in datafiles. A set of datafiles forms a collection.
/// In the following sections the internal structure of a datafile is
/// described.
///
/// A datafile itself is a collection of blobs. These blobs can be shaped
/// JSON documents or any other information. All blobs have a header field,
/// call marker followed by the data of the blob itself.
///
/// @section DatafileMarker Datafile Marker
///
/// @copydetails TRI_df_marker_t
///
/// @copydetails TRI_df_header_marker_t
///
/// @copydetails TRI_df_footer_marker_t
///
/// A datafile is therefore structured as follows:
///
/// <table border>
///   <tr>
///     <td>TRI_df_header_marker_t</td>
///     <td>header entry</td>
///   </tr>
///   <tr>
///     <td>...</td>
///     <td>data entry</td>
///   </tr>
///   <tr>
///     <td>...</td>
///     <td>data entry</td>
///   </tr>
///   <tr>
///     <td>...</td>
///     <td>data entry</td>
///   </tr>
///   <tr>
///     <td>...</td>
///     <td>data entry</td>
///   </tr>
///   <tr>
///     <td>TRI_df_footer_marker_t</td>
///     <td>footer entry</td>
///   </tr>
/// </table>
///
/// @section WorkingWithDatafile Working With Datafiles
///
/// A datafile is created using the function @ref TRI_CreateDatafile.
///
/// @copydetails TRI_CreateDatafile
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile version
////////////////////////////////////////////////////////////////////////////////

#define TRI_DF_VERSION (2)

////////////////////////////////////////////////////////////////////////////////
/// @brief maximum size of a single marker (in bytes)
////////////////////////////////////////////////////////////////////////////////

#define TRI_MARKER_MAXIMAL_SIZE (256 * 1024 * 1024)

////////////////////////////////////////////////////////////////////////////////
/// @brief state of the datafile
////////////////////////////////////////////////////////////////////////////////

enum TRI_df_state_e {
  TRI_DF_STATE_CLOSED = 1,       // datafile is closed
  TRI_DF_STATE_READ = 2,         // datafile is opened read only
  TRI_DF_STATE_WRITE = 3,        // datafile is opened read/append
  TRI_DF_STATE_OPEN_ERROR = 4,   // an error has occurred while opening
  TRI_DF_STATE_WRITE_ERROR = 5,  // an error has occurred while writing
  TRI_DF_STATE_RENAME_ERROR = 6  // an error has occurred while renaming
};

////////////////////////////////////////////////////////////////////////////////
/// @brief type of the marker
////////////////////////////////////////////////////////////////////////////////

enum TRI_df_marker_type_e {
  TRI_MARKER_MIN = 999,  // not a real marker type,
                         // but used for bounds checking

  TRI_DF_MARKER_HEADER = 1000,
  TRI_DF_MARKER_FOOTER = 1001,
  TRI_DF_MARKER_PROLOGUE = 1002,

  TRI_DF_MARKER_BLANK = 1100,

  TRI_COL_MARKER_HEADER = 2000,
  
  TRI_DOC_MARKER_KEY_DOCUMENT = 3007,  // new marker with key values
  TRI_DOC_MARKER_KEY_EDGE = 3008,      // new marker with key values

  TRI_WAL_MARKER_BEGIN_REMOTE_TRANSACTION = 4023,
  TRI_WAL_MARKER_COMMIT_REMOTE_TRANSACTION = 4024,
  TRI_WAL_MARKER_ABORT_REMOTE_TRANSACTION = 4025,

  TRI_WAL_MARKER_VPACK_DOCUMENT = 5000,
  TRI_WAL_MARKER_VPACK_REMOVE = 5001,
  TRI_WAL_MARKER_VPACK_CREATE_COLLECTION = 5010,
  TRI_WAL_MARKER_VPACK_DROP_COLLECTION = 5011,
  TRI_WAL_MARKER_VPACK_RENAME_COLLECTION = 5012,
  TRI_WAL_MARKER_VPACK_CHANGE_COLLECTION = 5013,
  TRI_WAL_MARKER_VPACK_CREATE_INDEX = 5020,
  TRI_WAL_MARKER_VPACK_DROP_INDEX = 5021,
  TRI_WAL_MARKER_VPACK_CREATE_DATABASE = 5030,
  TRI_WAL_MARKER_VPACK_DROP_DATABASE = 5031,
  TRI_WAL_MARKER_VPACK_BEGIN_TRANSACTION = 5040,
  TRI_WAL_MARKER_VPACK_COMMIT_TRANSACTION = 5041,
  TRI_WAL_MARKER_VPACK_ABORT_TRANSACTION = 5042,

  TRI_MARKER_MAX  // again, this is not a real
                  // marker, but we use it for
                  // bounds checking
};

////////////////////////////////////////////////////////////////////////////////
/// @brief storage type of the marker
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_df_marker_type_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile version
////////////////////////////////////////////////////////////////////////////////

typedef uint32_t TRI_df_version_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief scan result
////////////////////////////////////////////////////////////////////////////////

struct TRI_df_scan_t {
  TRI_voc_size_t _currentSize;
  TRI_voc_size_t _maximalSize;
  TRI_voc_size_t _endPosition;
  TRI_voc_size_t _numberMarkers;

  TRI_vector_t _entries;

  uint32_t _status;
  bool _isSealed;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief scan result entry
///
/// status:
///   1 - entry ok
///   2 - empty entry
///   3 - empty size
///   4 - size too small
///   5 - CRC failed
////////////////////////////////////////////////////////////////////////////////

struct TRI_df_scan_entry_t {
  TRI_voc_size_t _position;
  TRI_voc_size_t _size;
  TRI_voc_size_t _realSize;
  TRI_voc_tick_t _tick;

  TRI_df_marker_type_t _type;

  uint32_t _status;
  char* _diagnosis;
  char* _key;
  char const* _typeName;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile
////////////////////////////////////////////////////////////////////////////////

struct TRI_datafile_t {
  TRI_voc_fid_t _fid;  // datafile identifier

  TRI_df_state_e _state;  // state of the datafile (READ or WRITE)
  int _fd;                // underlying file descriptor

  void* _mmHandle;  // underlying memory map object handle (windows only)

  TRI_voc_size_t _initSize;     // initial size of the datafile (constant)
  TRI_voc_size_t _maximalSize;  // maximal size of the datafile (adjusted
                                // (=reduced) at runtime)
  TRI_voc_size_t _currentSize;  // current size of the datafile
  TRI_voc_size_t _footerSize;   // size of the final footer

  char* _data;  // start of the data array
  char* _next;  // end of the current data

  TRI_voc_tick_t _tickMin;  // minimum tick value contained
  TRI_voc_tick_t _tickMax;  // maximum tick value contained
  TRI_voc_tick_t _dataMin;  // minimum tick value of document/edge marker
  TRI_voc_tick_t _dataMax;  // maximum tick value of document/edge marker

  char* _filename;  // underlying filename

  // function pointers
  bool (*isPhysical)(struct TRI_datafile_t const*);  // returns true if
                                                     // the datafile is a
                                                     // physical file
  char const* (*getName)(
      struct TRI_datafile_t const*);        // returns the name of a datafile
  void (*close)(struct TRI_datafile_t*);    // close the datafile
  void (*destroy)(struct TRI_datafile_t*);  // destroys the datafile
  bool (*sync)(struct TRI_datafile_t*, char const*,
               char const*);  // syncs the datafile

  int _lastError;  // last (critical) error
  bool _full;  // at least one request was rejected because there is not enough
               // room
  bool _isSealed;  // true, if footer has been written

  // .............................................................................
  // access to the following attributes must be protected by a _lock
  // .............................................................................

  char* _synced;   // currently synced upto, not including
  char* _written;  // currently written upto, not including
};

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile marker
///
/// All blobs of a datafile start with a header. The base structure for all
/// such headers is as follows:
///
/// <table border>
///   <tr>
///     <td>TRI_voc_size_t</td>
///     <td>_size</td>
///     <td>The total size of the blob. This includes the size of the the
///         marker and the data. In order to iterate through the datafile
///         you can read the TRI_voc_size_t entry _size and skip the next
///         _size - sizeof(TRI_voc_size_t) bytes.</td>
///   </tr>
///   <tr>
///     <td>TRI_voc_crc_t</td>
///     <td>_crc</td>
///     <td>A crc of the marker and the data. The zero is computed as if
///         the field _crc is equal to 0.</td>
///   </tr>
///   <tr>
///     <td>TRI_df_marker_type_t</td>
///     <td>_type</td>
///     <td>see @ref TRI_df_marker_type_t</td>
///   </tr>
///   <tr>
///     <td>TRI_voc_tick_t</td>
///     <td>_tick</td>
///     <td>A unique identifier of the current blob. The identifier is
///         unique within all datafiles of all collections. See
///         @ref TRI_voc_tick_t for details.</td>
///   </tr>
/// </table>
///
/// Note that the order is important: _size must be the first entry
/// and _crc the second.
////////////////////////////////////////////////////////////////////////////////

struct TRI_df_marker_t {
  TRI_voc_size_t _size;  // 4 bytes, must be supplied
  TRI_voc_crc_t _crc;    // 4 bytes, will be generated

  TRI_df_marker_type_t _type;  // 4 bytes, must be supplied

#ifdef TRI_PADDING_32
  char _padding_df_marker[4];
#endif

  TRI_voc_tick_t _tick;  // 8 bytes, will be generated
};

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile header marker
///
/// The first blob entry in a datafile is always a TRI_df_header_marker_t.
/// The header marker contains the version number of the datafile, its
/// maximal size and the creation time. There is no data payload.
///
/// <table border>
///   <tr>
///     <td>TRI_df_version_t</td>
///     <td>_version</td>
///     <td>The version of a datafile, see @ref TRI_df_version_t.</td>
///   </tr>
///   <tr>
///     <td>TRI_voc_size_t</td>
///     <td>_maximalSize</td>
///     <td>The maximal size to which a datafile can grow. If you
///         attempt to add more datafile to a datafile, then an
///         error TRI_ERROR_ARANGO_DATAFILE_FULL is returned.</td>
///   </tr>
///   <tr>
///     <td>TRI_voc_tick_t</td>
///     <td>_fid</td>
///     <td>The creation time of the datafile. This time is different
///         from the creation time of the blob entry stored in
///         base._tick.</td>
///   </tr>
/// </table>
////////////////////////////////////////////////////////////////////////////////

struct TRI_df_header_marker_t {
  TRI_df_marker_t base;  // 24 bytes

  TRI_df_version_t _version;    //  4 bytes
  TRI_voc_size_t _maximalSize;  //  4 bytes
  TRI_voc_tick_t _fid;          //  8 bytes
};

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile footer marker
////////////////////////////////////////////////////////////////////////////////

struct TRI_df_prologue_marker_t {
  TRI_df_marker_t base;  // 24 bytes

  TRI_voc_tick_t _databaseId; // 8 bytes
  TRI_voc_cid_t _collectionId; // 8 bytes
};

////////////////////////////////////////////////////////////////////////////////
/// @brief datafile footer marker
///
/// The last entry in a full datafile is always a TRI_df_footer_marker_t.
/// The footer contains the maximal size of the datafile and it total
/// size.
///
/// <table border>
///   <tr>
///     <td>TRI_voc_size_t</td>
///     <td>_maximalSize</td>
///     <td>The maximal size to which a datafile can grow. This should match
///         the maximal stored in the @ref TRI_df_header_marker_t.</td>
///   </tr>
///   <tr>
///     <td>TRI_voc_size_t</td>
///     <td>_totalSize</td>
///     <td>The real size of the datafile. Should always be less than or equal
///         to the _maximalSize.</td>
///   </tr>
/// </table>
///
/// It is not possible to append entries after a footer. A datafile which
/// contains a footer is sealed and read-only.
////////////////////////////////////////////////////////////////////////////////

struct TRI_df_footer_marker_t {
  TRI_df_marker_t base;  // 24 bytes

  TRI_voc_size_t _maximalSize;  //  4 bytes
  TRI_voc_size_t _totalSize;    //  4 bytes
};

////////////////////////////////////////////////////////////////////////////////
/// @brief document datafile header marker
////////////////////////////////////////////////////////////////////////////////

struct TRI_col_header_marker_t {
  TRI_df_marker_t base;  // 24 bytes

  TRI_col_type_t _type;  //  4 bytes

#ifdef TRI_PADDING_32
  char _padding_col_header_marker[4];
#endif

  TRI_voc_cid_t _cid;  //  8 bytes
};

////////////////////////////////////////////////////////////////////////////////
/// @brief document datafile marker with key
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_doc_document_key_marker_s {
  TRI_df_marker_t base;

  TRI_voc_rid_t _rid;  // this is the tick for a create and update
  TRI_voc_tid_t _tid;

  TRI_shape_sid_t _shape;

  uint16_t _offsetKey;
  uint16_t _offsetJson;

#ifdef TRI_PADDING_32
  char _padding_df_marker[4];
#endif
} TRI_doc_document_key_marker_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief edge datafile marker with key
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_doc_edge_key_marker_s {
  TRI_doc_document_key_marker_t base;

  TRI_voc_cid_t _toCid;
  TRI_voc_cid_t _fromCid;

  uint16_t _offsetToKey;
  uint16_t _offsetFromKey;

#ifdef TRI_PADDING_32
  char _padding_df_marker[4];
#endif
} TRI_doc_edge_key_marker_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new datafile
///
/// This either creates a datafile using TRI_CreateAnonymousDatafile or
/// ref TRI_CreatePhysicalDatafile, based on the first parameter
////////////////////////////////////////////////////////////////////////////////

TRI_datafile_t* TRI_CreateDatafile(char const*, TRI_voc_fid_t fid,
                                   TRI_voc_size_t, bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new anonymous datafile
///
/// You must specify a maximal size for the datafile. The maximal
/// size must be divisible by the page size. If it is not, then the size is
/// rounded down. The memory for the datafile is mmapped. The create function
/// automatically adds a @ref TRI_df_footer_marker_t to the file.
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_HAVE_ANONYMOUS_MMAP
TRI_datafile_t* TRI_CreateAnonymousDatafile(TRI_voc_fid_t, TRI_voc_size_t);
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new physical datafile
///
/// You must specify a directory. This directory must exist and must be
/// writable. You must also specify a maximal size for the datafile. The maximal
/// size must be divisible by the page size. If it is not, then the size is
/// rounded down.  The datafile is created as sparse file. So there is a chance
/// that writing to the datafile will fill up your filesystem. This file is then
/// mapped into the address of the process using mmap. The create function
/// automatically adds a @ref TRI_df_footer_marker_t to the file.
////////////////////////////////////////////////////////////////////////////////

TRI_datafile_t* TRI_CreatePhysicalDatafile(char const*, TRI_voc_fid_t,
                                           TRI_voc_size_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyDatafile(TRI_datafile_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and but frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeDatafile(TRI_datafile_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if a marker is a data marker in the WAL
////////////////////////////////////////////////////////////////////////////////

static inline bool TRI_IsWalDataMarkerDatafile(void const* marker) {
  TRI_df_marker_t const* m = static_cast<TRI_df_marker_t const*>(marker);

  return (m->_type == TRI_WAL_MARKER_VPACK_DOCUMENT);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the name for a marker
////////////////////////////////////////////////////////////////////////////////

char const* TRI_NameMarkerDatafile(TRI_df_marker_t const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief initializes a marker with the most basic information
////////////////////////////////////////////////////////////////////////////////

void TRI_InitMarkerDatafile(char*, TRI_df_marker_type_e, TRI_voc_size_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the 8-byte aligned size for the value
////////////////////////////////////////////////////////////////////////////////

template <typename T>
static inline T AlignedSize(T value) {
  return (value + 7) - ((value + 7) & 7);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the 8-byte aligned size for the marker
////////////////////////////////////////////////////////////////////////////////

template <typename T>
static inline T AlignedMarkerSize(TRI_df_marker_t const* marker) {
  size_t value = marker->_size;
  return static_cast<T>((value + 7) - ((value + 7) & 7));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the marker-specific offset to the vpack payload
////////////////////////////////////////////////////////////////////////////////

static inline size_t VPackOffset(TRI_df_marker_type_t type) {
  auto t = static_cast<TRI_df_marker_type_e>(type);

  if (t == TRI_WAL_MARKER_VPACK_DOCUMENT ||
      t == TRI_WAL_MARKER_VPACK_REMOVE) {
    return sizeof(TRI_df_marker_t) + sizeof(TRI_voc_tid_t);
  }
  TRI_ASSERT(false);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks whether a marker is valid
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsValidMarkerDatafile(TRI_df_marker_t const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief reserves room for an element, advances the pointer
////////////////////////////////////////////////////////////////////////////////

int TRI_ReserveElementDatafile(
    TRI_datafile_t* datafile, TRI_voc_size_t size, TRI_df_marker_t** position,
    TRI_voc_size_t maximalJournalSize) TRI_WARN_UNUSED_RESULT;

////////////////////////////////////////////////////////////////////////////////
/// @brief writes a marker to the datafile
/// this function will write the marker as-is, without any CRC or tick updates
////////////////////////////////////////////////////////////////////////////////

int TRI_WriteElementDatafile(TRI_datafile_t* datafile, void* position,
                             TRI_df_marker_t const* marker,
                             bool sync) TRI_WARN_UNUSED_RESULT;

////////////////////////////////////////////////////////////////////////////////
/// @brief checksums and writes a marker to the datafile
////////////////////////////////////////////////////////////////////////////////

int TRI_WriteCrcElementDatafile(TRI_datafile_t* datafile, void* position,
                                TRI_df_marker_t* marker,
                                bool sync) TRI_WARN_UNUSED_RESULT;

////////////////////////////////////////////////////////////////////////////////
/// @brief update tick values for a datafile
////////////////////////////////////////////////////////////////////////////////

void TRI_UpdateTicksDatafile(TRI_datafile_t*, TRI_df_marker_t const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief iterates over a datafile
/// also may set datafile's min/max tick values
////////////////////////////////////////////////////////////////////////////////

bool TRI_IterateDatafile(TRI_datafile_t*,
                         bool (*iterator)(TRI_df_marker_t const*, void*,
                                          TRI_datafile_t*),
                         void* data);

////////////////////////////////////////////////////////////////////////////////
/// @brief opens an existing datafile read-only
////////////////////////////////////////////////////////////////////////////////

TRI_datafile_t* TRI_OpenDatafile(char const*, bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief closes a datafile and all memory regions
////////////////////////////////////////////////////////////////////////////////

bool TRI_CloseDatafile(TRI_datafile_t* datafile);

////////////////////////////////////////////////////////////////////////////////
/// @brief seals a database, writes a footer, sets it to read-only
////////////////////////////////////////////////////////////////////////////////

int TRI_SealDatafile(TRI_datafile_t* datafile) TRI_WARN_UNUSED_RESULT;

////////////////////////////////////////////////////////////////////////////////
/// @brief renames a datafile
////////////////////////////////////////////////////////////////////////////////

bool TRI_RenameDatafile(TRI_datafile_t* datafile, char const* filename);

////////////////////////////////////////////////////////////////////////////////
/// @brief truncates a datafile and seals it, only called by arango-dfdd
////////////////////////////////////////////////////////////////////////////////

int TRI_TruncateDatafile(char const* path, TRI_voc_size_t position);

////////////////////////////////////////////////////////////////////////////////
/// @brief try to repair a datafile, only called by arango-dfdd
////////////////////////////////////////////////////////////////////////////////

bool TRI_TryRepairDatafile(char const* path);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns information about the datafile, only called by arango-dfdd
////////////////////////////////////////////////////////////////////////////////

TRI_df_scan_t TRI_ScanDatafile(char const* path);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys information about the datafile
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyDatafileScan(TRI_df_scan_t* scan);

#endif
