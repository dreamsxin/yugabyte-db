// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_server.h"

#include "yb/util/flag_tags.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/cqlserver/cql_service.h"

using yb::rpc::ServiceIf;
using yb::rpc::ServicePoolOptions;

DEFINE_int32(cql_service_num_threads, 10,
             "Number of RPC worker threads for the CQL service");
TAG_FLAG(cql_service_num_threads, advanced);

DEFINE_int32(cql_service_queue_length, 50,
             "RPC queue length for CQL service");
TAG_FLAG(cql_service_queue_length, advanced);

namespace yb {
namespace cqlserver {

CQLServer::CQLServer(const CQLServerOptions& opts)
    : RpcAndWebServerBase("CQLServer", opts, "yb.cqlserver"), opts_(opts) {
}

Status CQLServer::Start() {
  RETURN_NOT_OK(server::RpcAndWebServerBase::Init());

  gscoped_ptr<ServiceIf> cql_service(new CQLServiceImpl(this, opts_.master_addresses_flag));
  RETURN_NOT_OK(RegisterService(SERVICE_POOL_OPTIONS(cql_service, cqlsvc), cql_service.Pass()));

  RETURN_NOT_OK(server::RpcAndWebServerBase::Start());

  return Status::OK();
}

}  // namespace cqlserver
}  // namespace yb