// Copyright (c) 2024, The Endstone Project. (https://endstone.dev) All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "bedrock/common_types.h"
#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/core/utility/enable_non_owner_references.h"
#include "bedrock/network/network_connection.h"
#include "bedrock/network/network_identifier.h"
#include "bedrock/network/network_peer.h"

class AppPlatform;
class NetEventCallback;
class NetworkStatistics;
struct NetworkIdentifierWithSubId;
class Packet;
class Scheduler;
class TaskGroup;

namespace cereal {
struct ReflectionCtx;
}

namespace Connector {
class ConnectionCallbacks {
public:
    virtual ~ConnectionCallbacks() = default;
};
}  // namespace Connector

namespace RakNetConnector {
class ConnectionCallbacks : public Connector::ConnectionCallbacks {};
}  // namespace RakNetConnector

namespace RakPeerHelper {
class IPSupportInterface {
public:
    virtual ~IPSupportInterface() = default;
};
}  // namespace RakPeerHelper

class NetworkEnableDisableListener {
public:
    virtual ~NetworkEnableDisableListener() = default;

private:
    int state_{0};
    Bedrock::NonOwnerPointer<AppPlatform> app_platform_;
};

static_assert(sizeof(NetworkEnableDisableListener) == 40);

class NetworkSystem : public RakNetConnector::ConnectionCallbacks,
                      public RakPeerHelper::IPSupportInterface,
                      public NetworkEnableDisableListener {
public:
    void send(const NetworkIdentifier &id, const Packet &packet, SubClientId recipient);
    void sendToMultiple(const std::vector<NetworkIdentifierWithSubId> &ids, const Packet &packet);
    bool onNewIncomingConnection(const NetworkIdentifier &id, std::shared_ptr<NetworkPeer> &&peer);
    bool onNewOutgoingConnection(const NetworkIdentifier &id, std::shared_ptr<NetworkPeer> &&peer);
    [[nodiscard]] NetworkConnection *_getConnectionFromId(const NetworkIdentifier &id) const;

    [[nodiscard]] const BinaryStream &sendStream() const
    {
#ifdef _WIN32
        static_assert(offsetof(NetworkSystem, send_stream_) == 0x168);
        static_assert(offsetof(NetworkSystem, reflection_ctx_) == 0x208);
#endif
        return send_stream_;
    }

private:
    struct IncomingPacketQueue {
        NetEventCallback *callbacks_obj;
    };

    Bedrock::NonOwnerPointer<class NetworkSessionOwner> network_session_owner_;
    std::recursive_mutex connections_mutex_;
    std::vector<std::unique_ptr<NetworkConnection>> connections_;
    std::shared_ptr<class LocalConnector> local_connector_;
    std::shared_ptr<class PacketGroupBuilder> packet_group_builder_;
    std::unique_ptr<class RemoteConnector> remote_connector_;
    std::unique_ptr<class ServerLocator> server_locator_;
    std::size_t current_connection_;
    std::shared_ptr<void> receive_task_;
    std::unique_ptr<TaskGroup> receive_task_group_;
    Bedrock::NonOwnerPointer<class IPacketObserver> packet_observer_;
    Scheduler *main_thread_;
    std::string receive_buffer_;
    std::string send_buffer_;
    BinaryStream send_stream_;
    std::unique_ptr<IncomingPacketQueue> incoming_packets_[4];
    bool use_ipv6_only_;
    std::uint16_t default_game_port_;
    std::uint16_t default_game_port_v6_;
    bool is_lan_discovery_enabled_;
    std::unique_ptr<NetworkStatistics> network_statistics_;
    bool websockets_enabled_;
    NetworkSettingOptions network_setting_options_;
    bool raw_recording_enabled_;
    std::unique_ptr<cereal::ReflectionCtx> reflection_ctx_;
    std::unique_ptr<class IPacketSerializationController> packet_overrides_;
    std::unique_ptr<class SessionSummaryPublisher> session_summary_publisher_;
};

#ifdef _WIN32
static_assert(sizeof(NetworkSystem) == 0x220);
#endif
