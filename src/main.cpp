// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"

#include "logic.hpp"
#include "rest.hpp"
#include "xidrpcserver.hpp"

#include <xayagame/defaultmain.hpp>
#include <xayagame/game.hpp>
#include <xayagame/rpc-stubs/xayawalletrpcclient.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <google/protobuf/stubs/common.h>

#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{

DEFINE_string (xaya_rpc_url, "",
               "URL at which Xaya Core's JSON-RPC interface is available");
DEFINE_int32 (game_rpc_port, 0,
              "the port at which xid's JSON-RPC server will be started"
              " (if non-zero)");

DEFINE_int32 (rest_port, 0,
              "if non-zero, the port at which the REST interface should run");

DEFINE_int32 (enable_pruning, -1,
              "if non-negative (including zero), old undo data will be pruned"
              " and only as many blocks as specified will be kept");

DEFINE_string (datadir, "",
               "base data directory for state data"
               " (will be extended by 'id' the chain)");

DEFINE_bool (allow_wallet, false,
             "whether to allow RPC methods that access the Xaya Core wallet");

class XidInstanceFactory : public xaya::CustomisedInstanceFactory
{

private:

  /**
   * Reference to the XidGame instance.  This is needed to construct the
   * RPC server.
   */
  xid::XidGame& rules;

  /** If set to non-null, enable the Xaya wallet on the RPC server.  */
  XayaWalletRpcClient* xayaWallet = nullptr;

public:

  explicit XidInstanceFactory (xid::XidGame& r)
    : rules(r)
  {}

  void
  EnableWallet (XayaWalletRpcClient& xw)
  {
    xayaWallet = &xw;
  }

  std::unique_ptr<xaya::RpcServerInterface>
  BuildRpcServer (xaya::Game& game,
                  jsonrpc::AbstractServerConnector& conn) override
  {
    using WrappedRpc = xaya::WrappedRpcServer<xid::XidRpcServer>;
    auto rpc = std::make_unique<WrappedRpc> (game, rules, conn);

    if (xayaWallet != nullptr)
      rpc->Get ().EnableWallet (*xayaWallet);

    return rpc;
  }

};

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  gflags::SetUsageMessage ("Run Xaya ID daemon");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_xaya_rpc_url.empty ())
    {
      std::cerr << "Error: --xaya_rpc_url must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_datadir.empty ())
    {
      std::cerr << "Error: --datadir must be specified" << std::endl;
      return EXIT_FAILURE;
    }

  xaya::GameDaemonConfiguration config;
  config.XayaRpcUrl = FLAGS_xaya_rpc_url;
  if (FLAGS_game_rpc_port != 0)
    {
      config.GameRpcServer = xaya::RpcServerType::HTTP;
      config.GameRpcPort = FLAGS_game_rpc_port;
    }
  config.EnablePruning = FLAGS_enable_pruning;
  config.DataDirectory = FLAGS_datadir;

  jsonrpc::HttpClient httpXaya(config.XayaRpcUrl);
  std::unique_ptr<XayaWalletRpcClient> xayaWallet;
  if (FLAGS_allow_wallet)
    xayaWallet
        = std::make_unique<XayaWalletRpcClient> (httpXaya,
                                                 jsonrpc::JSONRPC_CLIENT_V1);

  xid::XidGame rules;
  XidInstanceFactory instanceFact(rules);
  if (xayaWallet != nullptr)
    instanceFact.EnableWallet (*xayaWallet);
  config.InstanceFactory = &instanceFact;

  xid::RestApi rest(FLAGS_rest_port);
  if (FLAGS_rest_port != 0)
    rest.Start ();

  const int rc = xaya::SQLiteMain (config, "id", rules);

  google::protobuf::ShutdownProtobufLibrary ();
  return rc;
}
