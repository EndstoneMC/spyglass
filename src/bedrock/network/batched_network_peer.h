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

#include <string>

#include "bedrock/network/network_peer.h"

// Deliberately not derived from NetworkPeer: taking &BatchedNetworkPeer::sendPacket on a virtual
// member yields a vcall thunk, and hooking that dispatches straight back through the vtable.
class BatchedNetworkPeer {
public:
    void sendPacket(const std::string &data, NetworkPeer::Reliability reliability, Compressibility compressible);
};
