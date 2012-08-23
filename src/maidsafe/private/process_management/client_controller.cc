/***************************************************************************************************
 *  Copyright 2012 maidsafe.net limited                                                            *
 *                                                                                                 *
 *  The following source code is property of MaidSafe.net limited and is not meant for external    *
 *  use. The use of this code is governed by the licence file licence.txt found in the root of     *
 *  this directory and also on www.maidsafe.net.                                                   *
 *                                                                                                 *
 *  You are not free to copy, amend or otherwise use this source code without the explicit written *
 *  permission of the board of directors of MaidSafe.net.                                          *
 **************************************************************************************************/

#include "maidsafe/private/process_management/client_controller.h"

#include <chrono>
#include <limits>

#include "maidsafe/common/log.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/private/return_codes.h"
#include "maidsafe/private/process_management/controller_messages_pb.h"
#include "maidsafe/private/process_management/local_tcp_transport.h"
#include "maidsafe/private/process_management/utils.h"
#include "maidsafe/private/process_management/invigilator.h"


namespace bptime = boost::posix_time;
namespace bs2 = boost::signals2;

namespace maidsafe {

namespace priv {

namespace process_management {

ClientController::ClientController()
    : invigilator_port_(Invigilator::kMinPort() - 1),
      asio_service_(3),
      receiving_transport_(new LocalTcpTransport(asio_service_.service())),
      on_new_version_available_(),
      mutex_(),
      cond_var_(),
      state_(kInitialising),
      bootstrap_nodes_() {
  asio_service_.Start();
  ConnectToInvigilator();
}

ClientController::~ClientController() {
  receiving_transport_.reset();
  asio_service_.Stop();
}

void ClientController::ConnectToInvigilator() {
  invigilator_port_ = Invigilator::kMinPort() - 1;
  std::string random_data(RandomAlphaNumericString(64));
  std::shared_ptr<bs2::connection> on_message_received_connection(new bs2::connection);
  std::shared_ptr<bs2::connection> on_error_connection(new bs2::connection);
  *on_message_received_connection = receiving_transport_->on_message_received().connect(
          [=](const std::string& message, Port invigilator_port) {
            HandlePingResponse(random_data, message, invigilator_port,
                               on_message_received_connection, on_error_connection);
          });
  *on_error_connection = receiving_transport_->on_error().connect(
          [=](const int& error) {
            LOG(kError) << "Transport reported error code " << error;
            PingInvigilator(random_data, on_message_received_connection, on_error_connection);
          });
  PingInvigilator(random_data, on_message_received_connection, on_error_connection);

  std::unique_lock<std::mutex> lock(mutex_);
  if (!cond_var_.wait_for(lock,
                          std::chrono::seconds(3),
                          [&] { return state_ != kInitialising; })) {
    LOG(kError) << "Timed out waiting for ClientController initialisation.";
  }
  if (state_ != kVerified) {
    LOG(kError) << "ClientController is uninitialised.";
  }
}

void ClientController::PingInvigilator(
    const std::string& random_data,
    std::shared_ptr<bs2::connection> on_message_received_connection,
    std::shared_ptr<bs2::connection> on_error_connection) {
  while (receiving_transport_->Connect(++invigilator_port_) != kSuccess) {
    if (invigilator_port_ == Invigilator::kMaxPort()) {
      LOG(kError) << "ClientController failed to connect to Invigilator on all ports in range "
                  << Invigilator::kMinPort() << " to " << Invigilator::kMaxPort();
      on_message_received_connection->disconnect();
      on_error_connection->disconnect();
      std::lock_guard<std::mutex> lock(mutex_);
      state_ = kFailed;
      return cond_var_.notify_one();
    }
  }
  protobuf::Ping ping;
  ping.set_ping(random_data);
  LOG(kVerbose) << "Sending ping to port " << invigilator_port_;
  receiving_transport_->Send(detail::WrapMessage(MessageType::kPing, ping.SerializeAsString()),
                             invigilator_port_);
}

void ClientController::HandlePingResponse(
    const std::string& data_sent,
    const std::string& message,
    Port /*invigilator_port*/,
    std::shared_ptr<bs2::connection> on_message_received_connection,
    std::shared_ptr<bs2::connection> on_error_connection) {
  MessageType type;
  std::string payload;

  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    return PingInvigilator(data_sent, on_message_received_connection, on_error_connection);
  }

  protobuf::Ping ping;
  if (!ping.ParseFromString(payload) || !ping.IsInitialized()) {
    LOG(kError) << "Failed to parse Ping.";
    return PingInvigilator(data_sent, on_message_received_connection, on_error_connection);
  }

  if (ping.ping() != data_sent) {
    LOG(kError) << "Ping response didn't contain original data sent.";
    return PingInvigilator(data_sent, on_message_received_connection, on_error_connection);
  }

  bootstrap_nodes_ = ping.bootstrap_nodes();
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = kVerified;
  LOG(kSuccess) << "Successfully connected to Invigilator on port " << invigilator_port_;

  // Success - disconnect current slots and connect service functions
  on_message_received_connection->disconnect();
  on_error_connection->disconnect();
  receiving_transport_->on_message_received().connect(
      [this](const std::string& message, Port invigilator_port) {
        HandleReceivedRequest(message, invigilator_port);
      });
  receiving_transport_->on_error().connect([](const int& error) {
    LOG(kError) << "Transport reported error code " << error;
  });

  cond_var_.notify_one();
}

bool ClientController::StartVault(const asymm::Keys& keys,
                                  const std::string& account_name,
                                  const boost::asio::ip::udp::endpoint& bootstrap_endpoint) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != kVerified) {
      LOG(kError) << "Not connected to Invigilator.";
      return false;
    }
  }

  std::mutex local_mutex;
  std::condition_variable local_cond_var;
  bool done(false), local_result(false);
  protobuf::StartVaultRequest start_vault_request;
  start_vault_request.set_account_name(account_name);
  std::string serialised_keys;
  if (!asymm::SerialiseKeys(keys, serialised_keys)) {
    LOG(kError) << "Failed to serialise keys.";
    return false;
  }
  start_vault_request.set_keys(serialised_keys);
  if (!bootstrap_endpoint.address().is_unspecified()) {
    start_vault_request.set_bootstrap_endpoint(
        bootstrap_endpoint.address().to_string() + ":" +
        boost::lexical_cast<std::string>(bootstrap_endpoint.port()));
    LOG(kVerbose) << "Setting bootstrap endpoint to " << start_vault_request.bootstrap_endpoint();
  }

  std::function<void(bool)> callback =
    [&](bool result) {
      std::lock_guard<std::mutex> lock(local_mutex);
      local_result = result;
      done = true;
      local_cond_var.notify_one();
    };

  TransportPtr request_transport(new LocalTcpTransport(asio_service_.service()));
  if (request_transport->Connect(invigilator_port_) != kSuccess) {
    LOG(kError) << "Failed to connect request transport to Invigilator.";
    return false;
  }
  request_transport->on_message_received().connect(
      [this, callback](const std::string& message, Port /*invigilator_port*/) {
        HandleStartStopVaultResponse<protobuf::StartVaultResponse>(message, callback);
      });
  request_transport->on_error().connect([this, callback](const int& error) {
    LOG(kError) << "Transport reported error code " << error;
    callback(false);
  });

  LOG(kVerbose) << "Sending request to start vault to port " << invigilator_port_;
  request_transport->Send(detail::WrapMessage(MessageType::kStartVaultRequest,
                                              start_vault_request.SerializeAsString()),
                          invigilator_port_);

  std::unique_lock<std::mutex> lock(local_mutex);
  if (!local_cond_var.wait_for(lock, std::chrono::seconds(10), [&] { return done; })) {
    LOG(kError) << "Timed out waiting for reply.";
    return false;
  }
  if (!local_result)
    LOG(kError) << "Failed starting vault.";
  return local_result;
}

bool ClientController::StopVault(const asymm::PlainText& data,
                                 const asymm::Signature& signature,
                                 const asymm::Identity& identity) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != kVerified) {
      LOG(kError) << "Not connected to Invigilator.";
      return false;
    }
  }

  std::mutex local_mutex;
  std::condition_variable local_cond_var;
  bool done(false), local_result(false);
  protobuf::StopVaultRequest stop_vault_request;
  stop_vault_request.set_data(data);
  stop_vault_request.set_signature(signature);
  stop_vault_request.set_identity(identity);

  std::function<void(bool)> callback =
    [&](bool result) {
      std::lock_guard<std::mutex> lock(local_mutex);
      local_result = result;
      done = true;
      local_cond_var.notify_one();
    };

  TransportPtr request_transport(new LocalTcpTransport(asio_service_.service()));
  if (request_transport->Connect(invigilator_port_) != kSuccess) {
    LOG(kError) << "Failed to connect request transport to Invigilator.";
    return false;
  }
  request_transport->on_message_received().connect(
      [this, callback](const std::string& message, Port /*invigilator_port*/) {
        HandleStartStopVaultResponse<protobuf::StopVaultResponse>(message, callback);
      });
  request_transport->on_error().connect([this, callback](const int& error) {
    LOG(kError) << "Transport reported error code " << error;
    callback(false);
  });

  LOG(kVerbose) << "Sending request to stop vault to port " << invigilator_port_;
  request_transport->Send(detail::WrapMessage(MessageType::kStartVaultRequest,
                                              stop_vault_request.SerializeAsString()),
                          invigilator_port_);

  std::unique_lock<std::mutex> lock(local_mutex);
  if (!local_cond_var.wait_for(lock, std::chrono::seconds(10), [&] { return done; })) {
    LOG(kError) << "Timed out waiting for reply.";
    return false;
  }
  if (!local_result)
    LOG(kError) << "Failed stopping vault.";
  return local_result;
}

template<typename ResponseType>
void ClientController::HandleStartStopVaultResponse(const std::string& message,
                                                    const std::function<void(bool)>& callback) {  // NOLINT
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    callback(false);
    return;
  }

  ResponseType vault_response;
  if (!vault_response.ParseFromString(payload) || !vault_response.IsInitialized()) {
    LOG(kError) << "Failed to parse response.";
    callback(false);
    return;
  }

  callback(vault_response.result());
}

bool ClientController::SetUpdateInterval(const bptime::seconds& update_interval) {
  if (update_interval < Invigilator::kMinUpdateInterval() ||
      update_interval > Invigilator::kMaxUpdateInterval()) {
    LOG(kError) << "Cannot set update interval to " << update_interval << "  It must be in range ["
                << Invigilator::kMinUpdateInterval() << ", "
                << Invigilator::kMaxUpdateInterval() << "]";
    return false;
  }
  return SetOrGetUpdateInterval(update_interval) == update_interval;
}

bptime::time_duration ClientController::GetUpdateInterval() {
  return SetOrGetUpdateInterval(bptime::pos_infin);
}

std::string ClientController::GetBootstrapNodes() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != kVerified) {
      LOG(kError) << "Not connected to Invigilator.";
      return "";
    }
  }
  return bootstrap_nodes_;
}

bptime::time_duration ClientController::SetOrGetUpdateInterval(
      const bptime::time_duration& update_interval) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != kVerified) {
      LOG(kError) << "Not connected to Invigilator.";
      return bptime::pos_infin;
    }
  }

  std::mutex local_mutex;
  std::condition_variable local_cond_var;
  bptime::time_duration returned_result(bptime::neg_infin);
  protobuf::UpdateIntervalRequest update_interval_request;
  if (!update_interval.is_pos_infinity())
    update_interval_request.set_new_update_interval(update_interval.total_seconds());

  std::function<void(bptime::time_duration)> callback =
      [&](bptime::time_duration update_interval) {
        std::lock_guard<std::mutex> lock(local_mutex);
        returned_result = update_interval;
        local_cond_var.notify_one();
      };

  TransportPtr request_transport(new LocalTcpTransport(asio_service_.service()));
  if (request_transport->Connect(invigilator_port_) != kSuccess) {
    LOG(kError) << "Failed to connect request transport to Invigilator.";
      return bptime::pos_infin;
  }
  request_transport->on_message_received().connect(
      [this, callback](const std::string& message, Port /*invigilator_port*/) {
        HandleUpdateIntervalResponse(message, callback);
      });
  request_transport->on_error().connect([this, callback](const int& error) {
    LOG(kError) << "Transport reported error code " << error;
    callback(bptime::pos_infin);
  });

  std::unique_lock<std::mutex> lock(local_mutex);
  LOG(kVerbose) << "Sending request to " << (update_interval.is_pos_infinity() ? "get" : "set")
                << " update interval to Invigilator on port " << invigilator_port_;
  request_transport->Send(detail::WrapMessage(MessageType::kUpdateIntervalRequest,
                                              update_interval_request.SerializeAsString()),
                          invigilator_port_);

  if (!local_cond_var.wait_for(lock,
                               std::chrono::seconds(10),
                               [&] {
                                 return !returned_result.is_neg_infinity();
                               })) {
    LOG(kError) << "Timed out waiting for reply.";
    return bptime::pos_infin;
  }

  if (returned_result.is_pos_infinity())
    LOG(kError) << "Failed to " << (update_interval.is_pos_infinity() ? "get" : "set")
                << " update interval.";
  return returned_result;
}

void ClientController::HandleUpdateIntervalResponse(
    const std::string& message,
    const std::function<void(bptime::time_duration)>& callback) {  // NOLINT
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    callback(bptime::pos_infin);
    return;
  }

  protobuf::UpdateIntervalResponse update_interval_response;
  if (!update_interval_response.ParseFromString(payload) ||
      !update_interval_response.IsInitialized()) {
    LOG(kError) << "Failed to parse UpdateIntervalResponse.";
    callback(bptime::pos_infin);
    return;
  }

  if (update_interval_response.update_interval() == 0) {
    LOG(kError) << "UpdateIntervalResponse indicates failure.";
    callback(bptime::pos_infin);
  } else {
    callback(bptime::seconds(update_interval_response.update_interval()));
  }
}

void ClientController::HandleReceivedRequest(const std::string& message, Port peer_port) {
  assert(peer_port == invigilator_port_);
  MessageType type;
  std::string payload;
  if (!detail::UnwrapMessage(message, type, payload)) {
    LOG(kError) << "Failed to handle incoming message.";
    return;
  }
  LOG(kVerbose) << "HandleReceivedRequest: message type " << static_cast<int>(type) << " received.";
  std::string response;
  switch (type) {
    case MessageType::kNewVersionAvailable:
      HandleNewVersionAvailable(payload, response);
      break;
    default:
      return;
  }
  receiving_transport_->Send(response, peer_port);
}

void ClientController::HandleNewVersionAvailable(const std::string& request,
                                                 std::string& response) {
  protobuf::NewVersionAvailable new_version_available;
  protobuf::NewVersionAvailableAck new_version_available_ack;
  if (!new_version_available.ParseFromString(request) || !new_version_available.IsInitialized()) {
    LOG(kError) << "Failed to parse NewVersionAvailable.";
    new_version_available_ack.set_new_version_filepath("");
  } else {
    boost::system::error_code error_code;
    fs::path new_version(new_version_available.new_version_filepath());
    if (!fs::exists(new_version, error_code) || error_code) {
      LOG(kError) << "New version file missing: " << new_version;
      new_version_available_ack.set_new_version_filepath("");
    } else if (!detail::TokeniseFileName(new_version.filename().string())) {
      LOG(kError) << "New version " << new_version_available.new_version_filepath()
                  << " isn't a valid MaidSafe filename.";
      new_version_available_ack.set_new_version_filepath("");
    } else {
      new_version_available_ack.set_new_version_filepath(
          new_version_available.new_version_filepath());
    }
  }
  response = detail::WrapMessage(MessageType::kNewVersionAvailableAck,
                                 new_version_available_ack.SerializeAsString());
  on_new_version_available_(new_version_available.new_version_filepath());
}

}  // namespace process_management

}  // namespace priv

}  // namespace maidsafe
