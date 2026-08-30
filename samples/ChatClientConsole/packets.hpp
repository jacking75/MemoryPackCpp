#pragma once
// ============================================================================
// samples/ChatClientConsole/packets.hpp
//
// The chat protocol, mirroring samples/ChatServer/Packets.cs. Identical on the
// wire to samples/ChatClient/packets.hpp - the same packet ids, the same
// structs, the same member order - but written with MEMORYPACK_DEFINE instead
// of hand-written memorypack::IMemoryPackable specializations.
//
// That is the point of this file. The hand-written version needs roughly 15
// lines per packet and repeats the member list three times (declaration, write,
// read), so a member added in one place and forgotten in another is a silent
// wire bug. MEMORYPACK_DEFINE derives the object header's member count and both
// directions from a single list, so the two can never disagree.
//
// Version tolerance comes for free: the generated reader only reads the members
// the sender's object header says are present, leaving the rest default-
// constructed. Appending a member to the end of a struct is therefore
// backwards-compatible in both directions.
// ============================================================================

#include "memorypack/memorypack.hpp"
#include "memorypack/packet.hpp"

#include <cstdint>
#include <string>
#include <vector>

// -- Packet ids (must match the C# PacketId enum in ChatServer/Packets.cs) -----
enum class PacketId : uint16_t {
    LoginRequest     = 101,
    LoginResponse    = 102,
    RoomJoinRequest  = 103,
    RoomJoinResponse = 104,
    RoomChat         = 105,
    PrivateChat      = 106,
    UserEntered      = 107,
    UserLeft         = 108,
};

// -- Packet structs -----------------------------------------------------------

struct LoginRequest {
    std::string username;
};

struct LoginResponse {
    bool        success = false;
    std::string message;
};

struct RoomJoinRequest {
    std::string roomName;
};

struct RoomJoinResponse {
    bool                     success = false;
    std::vector<std::string> existingUsers;
};

struct RoomChat {
    std::string senderName;
    std::string message;
};

struct PrivateChat {
    std::string senderName;
    std::string targetName;
    std::string message;
};

struct UserEntered {
    std::string username;
};

struct UserLeft {
    std::string username;
};

// -- Generated serializers ----------------------------------------------------
// One line per packet. Compare with samples/ChatClient/packets.hpp, which spells
// the same thing out by hand in 115 lines.

MEMORYPACK_DEFINE(LoginRequest, username)
MEMORYPACK_DEFINE(LoginResponse, success, message)
MEMORYPACK_DEFINE(RoomJoinRequest, roomName)
MEMORYPACK_DEFINE(RoomJoinResponse, success, existingUsers)
MEMORYPACK_DEFINE(RoomChat, senderName, message)
MEMORYPACK_DEFINE(PrivateChat, senderName, targetName, message)
MEMORYPACK_DEFINE(UserEntered, username)
MEMORYPACK_DEFINE(UserLeft, username)
