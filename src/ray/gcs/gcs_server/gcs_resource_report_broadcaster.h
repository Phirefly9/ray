#include "ray/common/asio/instrumented_io_context.h"
#include "ray/gcs/gcs_server/gcs_resource_manager.h"
#include "ray/rpc/node_manager/node_manager_client_pool.h"

namespace ray {
namespace gcs {

/// Broadcasts resource report batches to raylets from a separate thread.
class GcsResourceReportBroadcaster {
 public:
  GcsResourceReportBroadcaster(
      std::shared_ptr<rpc::NodeManagerClientPool> raylet_client_pool,
      std::function<void(rpc::ResourceUsageBatchData &)>
          get_resource_usage_batch_for_broadcast,
      /* Default values should only be changed for testing. */
      std::function<void(const rpc::Address &,
                         std::shared_ptr<rpc::NodeManagerClientPool> &, std::string &)>
          send_batch = [](const rpc::Address &address,
                          std::shared_ptr<rpc::NodeManagerClientPool> &raylet_client_pool,
                          std::string &serialized_resource_usage_batch) {
            auto raylet_client = raylet_client_pool->GetOrConnectByAddress(address);
            raylet_client->UpdateResourceUsage(
                serialized_resource_usage_batch,
                [](const ray::Status &status,
                   const ray::rpc::UpdateResourceUsageReply &reply) {});
          });
  ~GcsResourceReportBroadcaster();

  void Initialize(const GcsInitData &gcs_init_data);

  /// Start a thread to broadcast resource reports..
  void Start();

  /// Stop broadcasting resource reports.
  void Stop();

  /// Event handler when a new node joins the cluster.
  void HandleNodeAdded(const rpc::GcsNodeInfo &node_info) LOCKS_EXCLUDED(mutex_);

  /// Event handler when a node leaves the cluster.
  void HandleNodeRemoved(const rpc::GcsNodeInfo &node_info) LOCKS_EXCLUDED(mutex_);

 private:
  // An asio service which does the broadcasting work.
  instrumented_io_context broadcast_service_;
  // The associated thread it runs on.
  std::unique_ptr<std::thread> broadcast_thread_;
  // Timer tick to send the next broadcast round.
  PeriodicalRunner ticker_;

  // The shared, thread safe pool of raylet clients, which we use to minimize connections.
  std::shared_ptr<rpc::NodeManagerClientPool> raylet_client_pool_;
  /// See GcsResourcManager::GetResourceUsageBatchForBroadcast. This is passed as an
  /// argument for unit testing purposes only.
  std::function<void(rpc::ResourceUsageBatchData &)>
      get_resource_usage_batch_for_broadcast_;
  std::function<void(const rpc::Address &, std::shared_ptr<rpc::NodeManagerClientPool> &,
                     std::string &)>
      send_batch_;

  // A lock to protect nodes_
  absl::Mutex mutex_;
  std::unordered_map<NodeID, rpc::Address> nodes_ GUARDED_BY(mutex_);

  uint64_t broadcast_period_ms_;

  void SendBroadcast();
};
}  // namespace gcs
}  // namespace ray