// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ---
// Author: Sainbayar Sukhbaatar
//         Dai Mikurube
//
// This file contains a class DeepHeapProfile and its public function
// DeepHeapProfile::FillOrderedProfile() which works as an alternative of
// HeapProfileTable::FillOrderedProfile().
//
// DeepHeapProfile::FillOrderedProfile() dumps more detailed information about
// heap usage, which includes OS-level information such as whether the memory
// block is actually in memory, or not.  DeepHeapProfile::FillOrderedProfile()
// uses logged data in HeapProfileTable as one of its data sources.
// DeepHeapProfile works only when its FillOrderedProfile() is called.  It has
// overhead when dumping, but no overhead when logging.
//
// It currently works only on Linux.  It just delegates to HeapProfileTable in
// non-Linux environments.


#ifndef BASE_DEEP_HEAP_PROFILE_H_
#define BASE_DEEP_HEAP_PROFILE_H_

#include "config.h"

#if defined(__linux__)
#define DEEP_HEAP_PROFILE 1
#endif

#include "addressmap-inl.h"
#include "heap-profile-table.h"

class DeepHeapProfile {
 public:
  typedef HeapProfileTable::Bucket Bucket;
  typedef HeapProfileTable::AllocationMap AllocationMap;
  typedef HeapProfileTable::AllocValue AllocValue;
  typedef HeapProfileTable::Stats Stats;

  // Construct a DeepHeapProfile instance.  It works as a wrapper of
  // HeapProfileTable.
  //
  // |heap_profile| is a pointer to HeapProfileTable.  DeepHeapProfile reads
  // data in |heap_profile| and forwards operations to |heap_profile| if
  // DeepHeapProfile is not available (non-Linux).
  // |prefix| is a prefix of dumped file names.
  DeepHeapProfile(HeapProfileTable* heap_profile, const char* prefix);
  ~DeepHeapProfile();

  // Fill deep profile data into |buffer| of |size|, and return the actual
  // size occupied by the dump in |buffer|.  It works as an alternative
  // of HeapProfileTable::FillOrderedProfile.
  //
  // The profile buckets are dumped in the decreasing order of currently
  // allocated bytes.  We do not provision for 0-terminating |buffer|.
  int FillOrderedProfile(char buffer[], int buffer_size);

 private:
#ifdef DEEP_HEAP_PROFILE
  struct DeepBucket {
    Bucket* bucket;
    size_t committed_size;
    int   id;         // Unique ID of the bucket.
    bool  is_logged;  // True if the stracktrace is logged to a file.
  };

  typedef AddressMap<DeepBucket> DeepBucketMap;

  struct PageState {
    bool is_committed;  // Currently, we use only this
    bool is_present;
    bool is_swapped;
    bool is_shared;
    bool is_mmap;
  };

  class RegionStats {
   public:
    // Initialize virtual_bytes and committed_bytes.
    void Initialize();

    // Update the RegionStats to include the tallies of virtual_bytes and
    // committed_bytes in the region from |first_adress| to |last_address|
    // inclusive.
    void Record(int pagemap_fd, uint64 first_address, uint64 last_address);

    size_t virtual_bytes() const { return virtual_bytes_; }
    size_t committed_bytes() const { return committed_bytes_; }
    void set_virtual_bytes(size_t virtual_bytes) {
      virtual_bytes_ = virtual_bytes;
    }
    void set_committed_bytes(size_t committed_bytes) {
      committed_bytes_ = committed_bytes;
    }
    void AddToVirtualBytes(size_t additional_virtual_bytes) {
      virtual_bytes_ += additional_virtual_bytes;
    }
    void AddToCommittedBytes(size_t additional_committed_bytes) {
      committed_bytes_ += additional_committed_bytes;
    }

   private:
    size_t virtual_bytes_;
    size_t committed_bytes_;
  };

  struct GlobalStats {
    // All RegionStats members in this class contain the bytes of virtual
    // memory and committed memory.
    // TODO(dmikurube): These regions should be classified more precisely later
    // for more detailed analysis.

    // Total bytes of the process memory.
    RegionStats total;

    // Total bytes of memory which is mapped to a file.
    // Regions which contain file paths in the last column of /proc/<pid>/maps.
    RegionStats file_mapped;

    // Total bytes of memory which is mapped anonymously.
    // Regions which contain nothing in the last column of /proc/<pid>/maps.
    RegionStats anonymous;

    // Total bytes of memory which is labeled, but not mapped to any file.
    // Regions which contain non-path strings in the last column of
    // /proc/<pid>/maps.
    RegionStats other;

    // Total bytes of mmap'ed regions.
    RegionStats record_mmap;

    // Total bytes of malloc'ed regions.
    RegionStats record_malloc;
  };

  // Clear the is_logged flag in a DeepBucket object as a callback function
  // for DeepBucketMap::Iterate().
  static void ClearIsLogged(const void* pointer,
                            DeepBucket* db,
                            DeepHeapProfile* deep_profile);

  // Open /proc/pid/pagemap and return its file descriptor.
  // File descriptors need to be refreshed after each fork.
  static int OpenProcPagemap();

  // Seek to the offset of the open pagemap file pagemap_fd.
  // It returns true if succeeded.  Otherwise, it returns false.
  static bool SeekProcPagemap(int pagemap_fd, uint64 address);

  // Read a pagemap state from the current pagemap_fd offset.
  // It returns true if succeeded.  Otherwise, it returns false.
  static bool ReadProcPagemap(int pagemap_fd, PageState* state);

  // Returns the number of resident (including swapped) bytes of the memory
  // region starting at |first_address| and ending at |last_address| inclusive.
  static size_t GetCommittedSize(int pagemap_fd,
                                 uint64 first_address,
                                 uint64 last_address);

  // Write re-formatted /proc/self/maps into a file which has |filename_prefix|
  // with using |buffer| of size |buffer_size|.
  static void WriteMapsToFile(const char* filename_prefix,
                              int buffer_size,
                              char buffer[]);

  // Compute the global statistics from /proc/self/maps and |pagemap_fd|, and
  // store the statistics in |stats|.
  static void SnapshotGlobalStatsWithoutMalloc(int pagemap_fd,
                                               GlobalStats* stats);

  // Get the DeepBucket object corresponding to the given |bucket|.
  // DeepBucket is an extension to Bucket which is declared above.
  DeepBucket* GetDeepBucket(Bucket* bucket);

  // Reset committed_size member variables in DeepBucket objects to 0.
  void ResetCommittedSize(Bucket** bucket_table);

  // Fill bucket data in |bucket_table| into buffer |buffer| of size
  // |buffer_size|, and return the size occupied by the bucket data in
  // |buffer|.  |bucket_length| is the offset for |buffer| to start filling.
  int SnapshotBucketTableWithoutMalloc(Bucket** bucket_table,
                                       int used_in_buffer,
                                       int buffer_size,
                                       char buffer[]);

  // Record both virtual and committed byte counts of malloc and mmap regions
  // as callback functions for AllocationMap::Iterate().
  static void RecordAlloc(const void* pointer,
                          AllocValue* alloc_value,
                          DeepHeapProfile* deep_profile);
  static void RecordMMap(const void* pointer,
                         AllocValue* alloc_value,
                         DeepHeapProfile* deep_profile);
  void SnapshotAllAllocsWithoutMalloc();

  // Fill a bucket (a bucket id and its corresponding calling stack) into
  // |buffer| of size |buffer_size|.
  int FillBucketForBucketFile(const DeepBucket* deep_bucket,
                              int buffer_size,
                              char buffer[]);

  // Write a |bucket_table| into a file of |bucket_fd|.
  void WriteBucketsTableToBucketFile(Bucket** bucket_table, RawFD bucket_fd);

  // Write both malloc and mmap bucket tables into a "bucket file".
  void WriteBucketsToBucketFile();

  // Fill a |deep_bucket| and its corresponding bucket into |buffer| from the
  // offset |used_in_buffer|.  Add the sizes to |profile_stats| if it's not
  // NULL.
  static int UnparseBucket(const DeepBucket& deep_bucket,
                           const char* extra,
                           int used_in_buffer,
                           int buffer_size,
                           char* buffer,
                           Stats* profile_stats);

  // Fill statistics of a region into |buffer|.
  static int UnparseRegionStats(const RegionStats* stats,
                                const char* name,
                                int used_in_buffer,
                                int buffer_size,
                                char* buffer);

  // Fill global statistics into |buffer|.
  int UnparseGlobalStats(int used_in_buffer, int buffer_size, char* buffer);

  int pagemap_fd_;         // File descriptor of /proc/self/pagemap.

  // Process ID of the last dump.  This could change by fork.
  pid_t most_recent_pid_;
  GlobalStats stats_;      // Stats about total memory.
  int dump_count_;         // The number of dumps.
  char* filename_prefix_;  // Output file prefix.
  char* profiler_buffer_;  // Buffer we use many times.

  int bucket_id_;
  DeepBucketMap* deep_bucket_map_;
#endif  // DEEP_HEAP_PROFILE

  HeapProfileTable* heap_profile_;

  DISALLOW_COPY_AND_ASSIGN(DeepHeapProfile);
};

#endif  // BASE_DEEP_HEAP_PROFILE_H_
