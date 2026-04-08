#include "packets.hpp"
#include <iostream>
#include <cstring>
#include <chrono>

// ── Platform Socket Abstraction ────────────────────────────────────────────────
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <WinSock2.h>
  #include <WS2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define CLOSE_SOCKET closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using socket_t = int;
  constexpr socket_t INVALID_SOCKET = -1;
  #define CLOSE_SOCKET close
#endif

static constexpr const char* SERVER_IP   = "127.0.0.1";
static constexpr uint16_t    SERVER_PORT = 9000;

// ── Network Helpers ────────────────────────────────────────────────────────────
bool recv_exact(socket_t sock, uint8_t* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        auto n = recv(sock, reinterpret_cast<char*>(buf + received),
                      static_cast<int>(len - received), 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

bool send_exact(socket_t sock, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        auto n = send(sock, reinterpret_cast<const char*>(buf + sent),
                      static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool send_packet(socket_t sock, PacketId id, const std::vector<uint8_t>& body) {
    uint8_t header[PACKET_HEADER_SIZE];
    auto packetId   = static_cast<uint16_t>(id);
    auto bodyLength = static_cast<int32_t>(body.size());
    std::memcpy(header + 0, &packetId,   sizeof(uint16_t));
    std::memcpy(header + 2, &bodyLength,  sizeof(int32_t));

    if (!send_exact(sock, header, PACKET_HEADER_SIZE)) return false;
    if (bodyLength > 0 && !send_exact(sock, body.data(), body.size())) return false;
    return true;
}

bool recv_packet(socket_t sock, PacketId& id, std::vector<uint8_t>& body) {
    uint8_t header[PACKET_HEADER_SIZE];
    if (!recv_exact(sock, header, PACKET_HEADER_SIZE)) return false;

    uint16_t packetId;
    int32_t  bodyLength;
    std::memcpy(&packetId,   header + 0, sizeof(uint16_t));
    std::memcpy(&bodyLength,  header + 2, sizeof(int32_t));

    id = static_cast<PacketId>(packetId);
    if (bodyLength > 0) {
        body.resize(static_cast<size_t>(bodyLength));
        if (!recv_exact(sock, body.data(), body.size())) return false;
    } else {
        body.clear();
    }
    return true;
}

// ── Test Helpers ───────────────────────────────────────────────────────────────
void print_separator(const char* testName) {
    std::cout << "\n==== " << testName << " ====\n";
}

int64_t current_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ── Test Functions ─────────────────────────────────────────────────────────────
bool test_login(socket_t sock) {
    print_separator("Test 1: LoginRequest -> LoginResponse");

    LoginRequest req;
    req.username = "TestPlayer";
    req.level    = 42;
    std::cout << "[SEND] LoginRequest: username=\"" << req.username
              << "\", level=" << req.level << "\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::LoginRequest, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<LoginResponse>(respBody);
    std::cout << "[RECV] LoginResponse: success=" << std::boolalpha << resp.success
              << ", playerId=" << resp.playerId
              << ", message=\"" << resp.message << "\"\n";
    return true;
}

bool test_player_state(socket_t sock) {
    print_separator("Test 2: PlayerState (float test)");

    PlayerState req;
    req.playerId = 1001;
    req.posX = 100.5f;
    req.posY = 200.25f;
    req.posZ = -50.75f;
    req.name = "TestPlayer";
    std::cout << "[SEND] PlayerState: id=" << req.playerId
              << ", pos=(" << req.posX << ", " << req.posY << ", " << req.posZ
              << "), name=\"" << req.name << "\"\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::PlayerState, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<PlayerState>(respBody);
    std::cout << "[RECV] PlayerState: id=" << resp.playerId
              << ", pos=(" << resp.posX << ", " << resp.posY << ", " << resp.posZ
              << "), name=\"" << resp.name << "\"\n";
    return true;
}

bool test_chat_message(socket_t sock) {
    print_separator("Test 3: ChatMessage (int64 + string test)");

    ChatMessage req;
    req.senderId  = 1001;
    req.message   = "Hello from C++ client!";
    req.timestamp = current_timestamp_ms();
    std::cout << "[SEND] ChatMessage: senderId=" << req.senderId
              << ", message=\"" << req.message
              << "\", timestamp=" << req.timestamp << "\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::ChatMessage, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<ChatMessage>(respBody);
    std::cout << "[RECV] ChatMessage: senderId=" << resp.senderId
              << ", message=\"" << resp.message
              << "\", timestamp=" << resp.timestamp << "\n";
    return true;
}

bool test_score_update(socket_t sock) {
    print_separator("Test 4: ScoreUpdate (vector<int32> + double test)");

    ScoreUpdate req;
    req.playerId   = 1001;
    req.scores     = {100, 250, 380, 420, 550};
    req.totalScore = 1700.5;
    std::cout << "[SEND] ScoreUpdate: playerId=" << req.playerId
              << ", scores=[";
    for (size_t i = 0; i < req.scores.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << req.scores[i];
    }
    std::cout << "], totalScore=" << req.totalScore << "\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::ScoreUpdate, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<ScoreUpdate>(respBody);
    std::cout << "[RECV] ScoreUpdate: playerId=" << resp.playerId
              << ", scores=[";
    for (size_t i = 0; i < resp.scores.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << resp.scores[i];
    }
    std::cout << "], totalScore=" << resp.totalScore << "\n";
    return true;
}

bool test_inventory(socket_t sock) {
    print_separator("Test 5: InventoryData (vector<string> + vector<int32> test)");

    InventoryData req;
    req.playerId   = 1001;
    req.itemNames  = {"Sword", "Shield", "Potion", "Scroll"};
    req.itemCounts = {1, 1, 5, 3};
    std::cout << "[SEND] InventoryData: playerId=" << req.playerId << ", items=[";
    for (size_t i = 0; i < req.itemNames.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << req.itemNames[i] << "x" << req.itemCounts[i];
    }
    std::cout << "]\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::InventoryData, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<InventoryData>(respBody);
    std::cout << "[RECV] InventoryData: playerId=" << resp.playerId << ", items=[";
    for (size_t i = 0; i < resp.itemNames.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << resp.itemNames[i] << "x" << resp.itemCounts[i];
    }
    std::cout << "]\n";
    return true;
}

bool test_buffer_data(socket_t sock) {
    print_separator("Test 6: BufferData (char/byte array test)");

    BufferData req;
    req.tag   = 0xAB;
    req.grade = -42;
    req.rawData   = {0x00, 0x11, 0x22, 0xFF, 0xEE, 0xDD};
    req.charCodes = {'H', 'e', 'l', 'l', 'o', '!', -1, -128, 127};

    std::cout << "[SEND] BufferData: tag=0x" << std::hex << (int)req.tag
              << ", grade=" << std::dec << (int)req.grade
              << ", rawData=[";
    for (size_t i = 0; i < req.rawData.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "0x" << std::hex << (int)req.rawData[i];
    }
    std::cout << std::dec << "], charCodes=[";
    for (size_t i = 0; i < req.charCodes.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << (int)req.charCodes[i];
    }
    std::cout << "]\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::BufferData, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<BufferData>(respBody);
    std::cout << "[RECV] BufferData: tag=0x" << std::hex << (int)resp.tag
              << ", grade=" << std::dec << (int)resp.grade
              << ", rawData=[";
    for (size_t i = 0; i < resp.rawData.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << "0x" << std::hex << (int)resp.rawData[i];
    }
    std::cout << std::dec << "], charCodes=[";
    for (size_t i = 0; i < resp.charCodes.size(); ++i) {
        if (i > 0) std::cout << ",";
        std::cout << (int)resp.charCodes[i];
    }
    std::cout << "]\n";
    return true;
}

bool test_int_arrays(socket_t sock) {
    print_separator("Test 7: IntArrayPacket (short/int/long array test)");

    IntArrayPacket req;
    req.id         = 7777;
    req.shortArray = {-100, 0, 100, 32767, -32768};
    req.intArray   = {-1, 0, 1, 2147483647, -2147483648};
    req.longArray  = {-1, 0, 1, 9223372036854775807LL, static_cast<int64_t>(-9223372036854775807LL - 1)};

    auto print_vec = [](const char* name, auto& vec) {
        std::cout << name << "=[";
        for (size_t i = 0; i < vec.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << vec[i];
        }
        std::cout << "]";
    };

    std::cout << "[SEND] IntArrayPacket: id=" << req.id << ", ";
    print_vec("short", req.shortArray); std::cout << ", ";
    print_vec("int",   req.intArray);   std::cout << ", ";
    print_vec("long",  req.longArray);  std::cout << "\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::IntArrayPacket, body)) return false;

    PacketId respId;
    std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<IntArrayPacket>(respBody);
    std::cout << "[RECV] IntArrayPacket: id=" << resp.id << ", ";
    print_vec("short", resp.shortArray); std::cout << ", ";
    print_vec("int",   resp.intArray);   std::cout << ", ";
    print_vec("long",  resp.longArray);  std::cout << "\n";
    return true;
}

bool test_skill_slots(socket_t sock) {
    print_separator("Test 8: SkillSlotData (C fixed int32[] + float[])");

    SkillSlotData req;
    req.playerId = 1001;
    req.skillCount = req.cooldownCount = 3;  // use 3 of 8 slots
    req.skillIds[0]  = 101; req.skillIds[1]  = 205; req.skillIds[2]  = 310;
    req.cooldowns[0] = 1.5f; req.cooldowns[1] = 3.0f; req.cooldowns[2] = 0.5f;

    std::cout << "[SEND] SkillSlotData: playerId=" << req.playerId
              << ", skills=[";
    for (int i = 0; i < req.skillCount; ++i) {
        if (i) std::cout << ",";
        std::cout << req.skillIds[i] << "(cd:" << req.cooldowns[i] << ")";
    }
    std::cout << "] (" << req.skillCount << "/" << SkillSlotData::MAX_SKILLS << " slots)\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::SkillSlotData, body)) return false;

    PacketId respId; std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<SkillSlotData>(respBody);
    std::cout << "[RECV] SkillSlotData: playerId=" << resp.playerId
              << ", skills=[";
    for (int i = 0; i < resp.skillCount; ++i) {
        if (i) std::cout << ",";
        std::cout << resp.skillIds[i] << "(cd:" << resp.cooldowns[i] << ")";
    }
    std::cout << "] (" << resp.skillCount << " slots)\n";
    return true;
}

bool test_map_tile_row(socket_t sock) {
    print_separator("Test 9: MapTileRow (C fixed uint8[] + int16[])");

    MapTileRow req;
    req.rowIndex = 5;
    req.tileCount = req.heightCount = 10;
    uint8_t tiles[]   = {1, 2, 3, 0, 0, 4, 5, 255, 128, 64};
    int16_t heights[] = {10, 20, 30, 0, 0, -10, 50, 100, -100, 0};
    std::memcpy(req.tiles, tiles, sizeof(tiles));
    std::memcpy(req.heights, heights, sizeof(heights));

    std::cout << "[SEND] MapTileRow: row=" << req.rowIndex
              << ", tiles=[";
    for (int i = 0; i < req.tileCount; ++i) {
        if (i) std::cout << ",";
        std::cout << (int)req.tiles[i];
    }
    std::cout << "], heights=[";
    for (int i = 0; i < req.heightCount; ++i) {
        if (i) std::cout << ",";
        std::cout << req.heights[i];
    }
    std::cout << "] (" << req.tileCount << "/" << MapTileRow::MAX_TILES << ")\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::MapTileRow, body)) return false;

    PacketId respId; std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<MapTileRow>(respBody);
    std::cout << "[RECV] MapTileRow: row=" << resp.rowIndex
              << ", tiles=[";
    for (int i = 0; i < resp.tileCount; ++i) {
        if (i) std::cout << ",";
        std::cout << (int)resp.tiles[i];
    }
    std::cout << "], heights=[";
    for (int i = 0; i < resp.heightCount; ++i) {
        if (i) std::cout << ",";
        std::cout << resp.heights[i];
    }
    std::cout << "] (" << resp.tileCount << " tiles)\n";
    return true;
}

bool test_mixed_format(socket_t sock) {
    print_separator("Test 10: MixedFormatPacket (vector + C array + char array)");

    MixedFormatPacket req;
    req.id = 42;
    req.dynamicScores = {100, 200, 300, 400, 500};           // vector
    req.bonusCount = 3;                                       // use 3 of 4 slots
    req.fixedBonuses[0] = 10; req.fixedBonuses[1] = 20; req.fixedBonuses[2] = 30;
    const char* tagStr = "HELLO";
    req.tagLength = static_cast<int32_t>(std::strlen(tagStr));
    std::memcpy(req.tag, tagStr, req.tagLength);              // char array
    req.multiplier = 2.5;

    std::cout << "[SEND] MixedFormatPacket: id=" << req.id
              << ", dynamicScores(vector)=[";
    for (size_t i = 0; i < req.dynamicScores.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << req.dynamicScores[i];
    }
    std::cout << "], fixedBonuses(C arr)=[";
    for (int i = 0; i < req.bonusCount; ++i) {
        if (i) std::cout << ",";
        std::cout << req.fixedBonuses[i];
    }
    std::cout << "], tag(char arr)=\"";
    for (int i = 0; i < req.tagLength; ++i) std::cout << (char)req.tag[i];
    std::cout << "\", multiplier=" << req.multiplier << "\n";

    auto body = memorypack::Serialize(req);
    if (!send_packet(sock, PacketId::MixedFormatPacket, body)) return false;

    PacketId respId; std::vector<uint8_t> respBody;
    if (!recv_packet(sock, respId, respBody)) return false;

    auto resp = memorypack::Deserialize<MixedFormatPacket>(respBody);
    std::cout << "[RECV] MixedFormatPacket: id=" << resp.id
              << ", dynamicScores(vector)=[";
    for (size_t i = 0; i < resp.dynamicScores.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << resp.dynamicScores[i];
    }
    std::cout << "], fixedBonuses(C arr)=[";
    for (int i = 0; i < resp.bonusCount; ++i) {
        if (i) std::cout << ",";
        std::cout << resp.fixedBonuses[i];
    }
    std::cout << "], tag(char arr)=\"";
    for (int i = 0; i < resp.tagLength; ++i) std::cout << (char)resp.tag[i];
    std::cout << "\", multiplier=" << resp.multiplier << "\n";
    return true;
}

// ── Main ───────────────────────────────────────────────────────────────────────
int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    std::cout << "Connecting to " << SERVER_IP << ":" << SERVER_PORT << "...\n";
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to connect to server\n";
        CLOSE_SOCKET(sock);
        return 1;
    }
    std::cout << "Connected!\n";

    bool allPassed = true;
    allPassed &= test_login(sock);
    allPassed &= test_player_state(sock);
    allPassed &= test_chat_message(sock);
    allPassed &= test_score_update(sock);
    allPassed &= test_inventory(sock);
    allPassed &= test_buffer_data(sock);
    allPassed &= test_int_arrays(sock);
    allPassed &= test_skill_slots(sock);
    allPassed &= test_map_tile_row(sock);
    allPassed &= test_mixed_format(sock);

    std::cout << "\n==============================\n";
    if (allPassed)
        std::cout << "All tests passed!\n";
    else
        std::cout << "Some tests FAILED.\n";

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return allPassed ? 0 : 1;
}
