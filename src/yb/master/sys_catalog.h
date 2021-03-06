// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#ifndef YB_MASTER_SYS_CATALOG_H_
#define YB_MASTER_SYS_CATALOG_H_

#include <string>
#include <vector>

#include "yb/master/catalog_manager.h"
#include "yb/master/master.pb.h"
#include "yb/server/metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

namespace yb {

class Schema;
class FsManager;

namespace tserver {
class WriteRequestPB;
class WriteResponsePB;
}

namespace master {
class Master;
class MasterOptions;

// Forward declaration from internal header file.
class VisitorBase;
class SysCatalogWriter;

static const char* const kSysCatalogTabletId = "00000000000000000000000000000000";
static const char* const kSysCatalogTableId = "sys.catalog.uuid";
static const char* const kSysCatalogTableColType = "entry_type";
static const char* const kSysCatalogTableColId = "entry_id";
static const char* const kSysCatalogTableColMetadata = "metadata";

// SysCatalogTable is a YB table that keeps track of table and
// tablet metadata.
// - SysCatalogTable has only one tablet.
// - SysCatalogTable is managed by the master and not exposed to the user
//   as a "normal table", instead we have Master APIs to query the table.
class SysCatalogTable {
 public:
  typedef Callback<Status()> ElectedLeaderCallback;

  // 'leader_cb_' is invoked whenever this node is elected as a leader
  // of the consensus configuration for this tablet, including for local standalone
  // master consensus configurations. It used to initialize leader state, submit any
  // leader-specific tasks and so forth.
  //
  /// NOTE: Since 'leader_cb_' is invoked synchronously and can block
  // the consensus configuration's progress, any long running tasks (e.g., scanning
  // tablets) should be performed asynchronously (by, e.g., submitting
  // them to a to a separate threadpool).
  SysCatalogTable(Master* master, MetricRegistry* metrics,
                  ElectedLeaderCallback leader_cb);

  ~SysCatalogTable();

  // Allow for orderly shutdown of tablet peer, etc.
  void Shutdown();

  // Load the Metadata from disk, and initialize the TabletPeer for the sys-table
  CHECKED_STATUS Load(FsManager *fs_manager);

  // Create the new Metadata and initialize the TabletPeer for the sys-table.
  CHECKED_STATUS CreateNew(FsManager *fs_manager);

  // ==================================================================
  // Templated CRUD methods for items in sys.catalog.
  // ==================================================================
  template <class Item>
  CHECKED_STATUS AddItem(Item* item);

  template <class Item>
  CHECKED_STATUS AddItems(const vector<Item*>& items);

  template <class Item>
  CHECKED_STATUS UpdateItem(Item* item);
  template <class Item>
  CHECKED_STATUS UpdateItems(const vector<Item*>& items);

  template <class Item>
  CHECKED_STATUS AddAndUpdateItems(
      const vector<Item*>& added_items,
      const vector<Item*>& updated_items);

  template <class Item>
  CHECKED_STATUS DeleteItem(Item* item);
  template <class Item>
  CHECKED_STATUS DeleteItems(const vector<Item*>& items);

  template <class Item>
  CHECKED_STATUS MutateItems(
      const vector<Item*>& items, const QLWriteRequestPB::QLStmtType& op_type);

  // ==================================================================
  // Static schema related methods.
  // ==================================================================
  static std::string schema_column_type();
  static std::string schema_column_id();
  static std::string schema_column_metadata();

  ThreadPool* raft_pool() const { return raft_pool_.get(); }
  ThreadPool* tablet_prepare_pool() const { return tablet_prepare_pool_.get(); }
  ThreadPool* append_pool() const { return append_pool_.get(); }

  const std::shared_ptr<tablet::TabletPeer> tablet_peer() const {
    return std::atomic_load(&tablet_peer_);
  }

  // Create a new tablet peer with information from the metadata
  void SetupTabletPeer(const scoped_refptr<tablet::TabletMetadata>& metadata);

  // Update the in-memory master addresses. Report missing uuid's in the
  // config when check_missing_uuids is set to true.
  CHECKED_STATUS ConvertConfigToMasterAddresses(
      const yb::consensus::RaftConfigPB& config,
      bool check_missing_uuids = false);

  // Create consensus metadata object and flush it to disk.
  CHECKED_STATUS CreateAndFlushConsensusMeta(
      FsManager* fs_manager,
      const yb::consensus::RaftConfigPB& config,
      int64_t current_term);

  CHECKED_STATUS Visit(VisitorBase* visitor);

 private:
  friend class CatalogManager;

  inline std::unique_ptr<SysCatalogWriter> NewWriter();

  const char *table_name() const { return "sys.catalog"; }

  // Return the schema of the table.
  // NOTE: This is the "server-side" schema, so it must have the column IDs.
  Schema BuildTableSchema();

  // Returns 'Status::OK()' if the WriteTranasction completed
  CHECKED_STATUS SyncWrite(SysCatalogWriter* writer);

  void SysCatalogStateChanged(const std::string& tablet_id,
                              std::shared_ptr<consensus::StateChangeContext> context);

  CHECKED_STATUS SetupTablet(const scoped_refptr<tablet::TabletMetadata>& metadata);

  CHECKED_STATUS OpenTablet(const scoped_refptr<tablet::TabletMetadata>& metadata);

  // Use the master options to generate a new consensus configuration.
  // In addition, resolve all UUIDs of this consensus configuration.
  CHECKED_STATUS SetupConfig(const MasterOptions& options,
                             consensus::RaftConfigPB* committed_config);

  std::string tablet_id() const {
    return tablet_peer()->tablet_id();
  }

  // Conventional "T xxx P xxxx..." prefix for logging.
  std::string LogPrefix() const;

  // Waits for the tablet to reach 'RUNNING' state.
  //
  // Contrary to tablet servers, in master we actually wait for the master tablet
  // to become online synchronously, this allows us to fail fast if something fails
  // and shouldn't induce the all-workers-blocked-waiting-for-tablets problem
  // that we've seen in tablet servers since the master only has to boot a few
  // tablets.
  CHECKED_STATUS WaitUntilRunning();

  // Shutdown the tablet peer and apply pool which are not needed in shell mode for this master.
  CHECKED_STATUS GoIntoShellMode();

  // Initializes the RaftPeerPB for the local peer.
  // Crashes due to an invariant check if the rpc server is not running.
  void InitLocalRaftPeerPB();

  // Table schema, without IDs, used to send messages to the TabletPeer
  Schema schema_;

  // Table schema, with IDs, used for the YQL write path.
  Schema schema_with_ids_;

  MetricRegistry* metric_registry_;

  gscoped_ptr<ThreadPool> apply_pool_;

  // Thread pool for Raft-related operations
  gscoped_ptr<ThreadPool> raft_pool_;

  // Thread pool for preparing transactions, shared between all tablets.
  gscoped_ptr<ThreadPool> tablet_prepare_pool_;

  // Thread pool for appender tasks
  gscoped_ptr<ThreadPool> append_pool_;

  std::shared_ptr<tablet::TabletPeer> tablet_peer_;

  Master* master_;

  ElectedLeaderCallback leader_cb_;

  consensus::RaftPeerPB local_peer_pb_;

  scoped_refptr<Histogram> setup_config_dns_histogram_;

  DISALLOW_COPY_AND_ASSIGN(SysCatalogTable);
};

} // namespace master
} // namespace yb

#include "yb/master/sys_catalog-internal.h"

#endif // YB_MASTER_SYS_CATALOG_H_
