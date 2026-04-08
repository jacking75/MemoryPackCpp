# MemoryPackCpp  
![Status](https://img.shields.io/badge/status-in%20development-yellow)  
  
C#의 [MemoryPack](https://github.com/Cysharp/MemoryPack) Binary Wire Format과 호환되는 **C++ header-only 직렬화 라이브러리**.

C# 서버와 C++ 클라이언트 간 고성능 바이너리 직렬화/역직렬화를 위해 설계되었다.

## 특징

- **Header-only** — `#include "memorypack/memorypack.hpp"` 하나로 사용
- **C++23** — `std::optional`, `std::span`, structured bindings 등 활용
- **크로스플랫폼** — Windows, Linux, macOS 지원
- **Zero-encoding** — VarInt 없이 Little-Endian 고정 크기로 메모리 레이아웃을 그대로 복사
- **유연한 버퍼** — 내부 vector, 외부 vector, 고정 크기 배열 등 다양한 버퍼 모드 지원

## 설치

header-only 라이브러리이므로 `include/` 디렉터리를 프로젝트의 include 경로에 추가하면 된다.

```cpp
#include "memorypack/memorypack.hpp"
```

## 빠른 시작

### 1. 구조체 정의

```cpp
struct LoginRequest {
    std::string userName;
    int32_t level;
};
```

### 2. IMemoryPackable 특수화

C# 측의 MemoryPack 직렬화 순서와 **반드시 동일한 멤버 순서**로 Serialize/Deserialize를 구현한다.

```cpp
namespace memorypack {
template<>
struct IMemoryPackable<LoginRequest> {
    static void Serialize(MemoryPackWriter& w, const LoginRequest* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);       // 멤버 2개
        w.WriteString(v->userName);   // 첫 번째 멤버
        w.WriteInt32(v->level);       // 두 번째 멤버
    }

    static void Deserialize(MemoryPackReader& r, LoginRequest& v) {
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) { auto s = r.ReadString(); v.userName = s.value_or(""); }
        if (cnt >= 2) v.level = r.ReadInt32();
    }
};
} // namespace memorypack
```

### 3. 직렬화 / 역직렬화

```cpp
// 직렬화
LoginRequest req{ "Player1", 42 };
std::vector<uint8_t> data = memorypack::Serialize(req);

// 역직렬화
LoginRequest result = memorypack::Deserialize<LoginRequest>(data);
```

## API 레퍼런스

### MemoryPackWriter

직렬화를 수행하는 클래스. 세 가지 버퍼 모드를 지원한다.

#### 버퍼 모드

```cpp
// 1. 기본 — 내부 vector (자동 확장)
MemoryPackWriter writer;

// 2. 외부 std::vector<uint8_t> (자동 확장, 호출자 소유)
std::vector<uint8_t> myBuffer;
myBuffer.reserve(1024);
MemoryPackWriter writer(myBuffer);

// 3-a. 외부 고정 버퍼 — raw 포인터 + 크기
uint8_t rawBuf[4096];
MemoryPackWriter writer(rawBuf, sizeof(rawBuf));

// 3-b. 외부 고정 버퍼 — std::array
std::array<uint8_t, 4096> arrBuf;
MemoryPackWriter writer(arrBuf);
```

> 고정 크기 버퍼에서 용량을 초과하면 `std::runtime_error`("fixed buffer overflow")를 던진다.

#### 쓰기 메서드

| 메서드 | 설명 |
|--------|------|
| `WriteObjectHeader(uint8_t n)` | 오브젝트 헤더 (멤버 수) |
| `WriteNullObjectHeader()` | null 오브젝트 (0xFF) |
| `WriteCollectionHeader(int32_t n)` | 컬렉션 헤더 (요소 수) |
| `WriteNullCollectionHeader()` | null 컬렉션 (-1) |
| `WriteBool(bool)` | bool (1바이트) |
| `WriteInt8` / `WriteUInt8` | 8비트 정수 |
| `WriteInt16` / `WriteUInt16` | 16비트 정수 |
| `WriteInt32` / `WriteUInt32` | 32비트 정수 |
| `WriteInt64` / `WriteUInt64` | 64비트 정수 |
| `WriteFloat(float)` | 32비트 부동소수점 |
| `WriteDouble(double)` | 64비트 부동소수점 |
| `WriteEnum<T>(T)` | enum (underlying 정수 타입으로 직렬화) |
| `WriteString(const std::string&)` | UTF-8 문자열 |
| `WriteNullString()` | null 문자열 |
| `WriteOptionalString(const std::optional<std::string>&)` | nullable 문자열 |
| `WriteVector<T>(const std::vector<T>&)` | 산술 타입 벡터 (bulk copy) |
| `WriteArray(const T*, int32_t)` | C 스타일 고정 배열 |
| `WriteArray(const std::array<T, N>&)` | `std::array` 고정 배열 |
| `WriteStringVector(const std::vector<std::string>&)` | 문자열 벡터 |
| `WriteMap(const std::map<K, V>&)` | `std::map` |
| `WriteMap(const std::unordered_map<K, V>&)` | `std::unordered_map` |
| `WriteTuple(const std::tuple<Ts...>&)` | `std::tuple` (Object로 직렬화) |
| `WriteBytes(std::span<const uint8_t>)` | raw 바이트 |

#### 버퍼 접근 메서드

| 메서드 | 설명 |
|--------|------|
| `Data()` | 데이터 포인터 (모든 모드) |
| `Size()` | 현재 쓰여진 바이트 수 (모든 모드) |
| `GetSpan()` | `std::span<const uint8_t>` 반환 (모든 모드) |
| `GetBuffer()` | `const std::vector<uint8_t>&` 반환 (vector 모드만) |
| `TakeBuffer()` | 내부 vector를 move로 꺼냄 (기본 생성자만) |
| `RemainingCapacity()` | 고정 버퍼 남은 용량 |
| `Clear()` | 쓰기 위치 리셋 (버퍼 재사용) |
| `Reserve(size_t)` | vector 용량 예약 |

### MemoryPackReader

역직렬화를 수행하는 클래스.

```cpp
// 생성 방법
MemoryPackReader reader(data.data(), data.size());       // raw 포인터 + 크기
MemoryPackReader reader(std::span<const uint8_t>(data)); // span
```

#### 읽기 메서드

| 메서드 | 반환 타입 | 설명 |
|--------|-----------|------|
| `ReadObjectHeader()` | `std::pair<uint8_t, bool>` | {멤버수, isNull} |
| `PeekIsNull()` | `bool` | 다음 바이트가 null인지 확인 |
| `ReadCollectionHeader()` | `int32_t` | 컬렉션 길이 (-1 = null) |
| `ReadBool()` | `bool` | |
| `ReadInt8()` / `ReadUInt8()` | `int8_t` / `uint8_t` | |
| `ReadInt16()` / `ReadUInt16()` | `int16_t` / `uint16_t` | |
| `ReadInt32()` / `ReadUInt32()` | `int32_t` / `uint32_t` | |
| `ReadInt64()` / `ReadUInt64()` | `int64_t` / `uint64_t` | |
| `ReadFloat()` | `float` | |
| `ReadDouble()` | `double` | |
| `ReadEnum<T>()` | `T` | enum (underlying 정수로 읽고 캐스팅) |
| `ReadString()` | `std::optional<std::string>` | null이면 nullopt |
| `ReadVector<T>()` | `std::vector<T>` | 산술 타입 벡터 |
| `ReadArray(T*, int32_t)` | `int32_t` (읽은 수) | C 스타일 배열로 읽기 |
| `ReadArray<T, N>()` | `std::array<T, N>` | `std::array`로 읽기 |
| `ReadStringVector()` | `std::vector<std::string>` | 문자열 벡터 |
| `ReadMap<K, V>()` | `std::map<K, V>` | `std::map`으로 읽기 |
| `ReadUnorderedMap<K, V>()` | `std::unordered_map<K, V>` | `std::unordered_map`으로 읽기 |
| `ReadTuple<Ts...>()` | `std::tuple<Ts...>` | `std::tuple`로 읽기 |

#### 상태 메서드

| 메서드 | 설명 |
|--------|------|
| `Position()` | 현재 읽기 위치 |
| `Remaining()` | 남은 바이트 수 |
| `IsEnd()` | 끝에 도달했는지 |
| `Advance(size_t n)` | n 바이트 건너뛰기 |

### 최상위 함수

```cpp
// 한 줄로 직렬화
std::vector<uint8_t> data = memorypack::Serialize(myObject);

// 한 줄로 역직렬화
MyType obj = memorypack::Deserialize<MyType>(data);
MyType obj2 = memorypack::Deserialize<MyType>(std::span<const uint8_t>(ptr, size));

// 기존 객체에 역직렬화
MyType obj3;
memorypack::Deserialize(ptr, size, obj3);
```

## 외부 버퍼 사용 예시

### 외부 vector에 직렬화

서버/클라이언트에서 송신 버퍼를 미리 할당해두고 재사용하는 패턴:

```cpp
// 송신 버퍼를 미리 확보
std::vector<uint8_t> sendBuffer;
sendBuffer.reserve(4096);

// 패킷 헤더 공간 확보 (예: [2B packetId][4B bodyLength])
sendBuffer.resize(6);
sendBuffer[0] = 0x01; // packetId (low byte)
sendBuffer[1] = 0x00; // packetId (high byte)

// 헤더 이후부터 직렬화 — writer는 sendBuffer에 이어서 쓴다
size_t headerSize = sendBuffer.size();
MemoryPackWriter writer(sendBuffer);
LoginRequest req{ "Player1", 42 };
memorypack::IMemoryPackable<LoginRequest>::Serialize(writer, &req);

// body 길이를 헤더에 기록
int32_t bodyLen = static_cast<int32_t>(sendBuffer.size() - headerSize);
std::memcpy(sendBuffer.data() + 2, &bodyLen, 4);

// sendBuffer를 소켓으로 전송
send(sock, reinterpret_cast<const char*>(sendBuffer.data()), sendBuffer.size(), 0);

// 버퍼 재사용
sendBuffer.clear();
```

### 고정 크기 배열에 직렬화

힙 할당 없이 스택 메모리만 사용하는 패턴:

```cpp
// std::array 사용
std::array<uint8_t, 256> stackBuf;
MemoryPackWriter writer(stackBuf);

LoginRequest req{ "Test", 10 };
memorypack::IMemoryPackable<LoginRequest>::Serialize(writer, &req);

// writer.Data(), writer.Size()로 직렬화된 데이터 접근
send(sock, reinterpret_cast<const char*>(writer.Data()), writer.Size(), 0);

// 재사용
writer.Clear();
```

```cpp
// C 스타일 배열도 가능
uint8_t rawBuf[512];
MemoryPackWriter writer(rawBuf, sizeof(rawBuf));

// ... 직렬화 ...

// 남은 용량 확인
size_t remaining = writer.RemainingCapacity();
```

## 버퍼에 uint8_t를 사용하는 이유

이 라이브러리는 버퍼 타입으로 `char[]`가 아닌 `uint8_t`를 사용한다.

**`char`의 부호(signed/unsigned)는 플랫폼마다 다르다.** C++ 표준에서 `char`의 부호는 implementation-defined이다.

| 플랫폼 | char 부호 |
|--------|-----------|
| MSVC (Windows) | 기본 signed (-128 ~ 127) |
| GCC/Clang (x86 Linux) | 기본 signed |
| ARM 일부 컴파일러 | unsigned (0 ~ 255) |

바이너리 데이터를 다룰 때 이것이 문제가 된다:

```cpp
// char가 signed인 플랫폼에서
char buf[4];
buf[0] = 0xFF;          // 경고 발생 가능. -1로 저장됨
int val = buf[0];       // val == -1 (의도: 255)

// uint8_t는 항상 명확
uint8_t buf[4];
buf[0] = 0xFF;          // 항상 255
int val = buf[0];       // val == 255
```

| 항목 | `char` | `uint8_t` |
|------|--------|-----------|
| 부호 | 플랫폼마다 다름 | 항상 unsigned (0~255) |
| 의도 | 문자(텍스트) | 바이트(바이너리 데이터) |
| 비트 연산 | signed이면 정의되지 않은 동작 가능 | 항상 안전 |
| 크로스플랫폼 | 부호 차이로 인한 버그 가능 | 일관된 동작 보장 |

`uint8_t`를 사용함으로써 "이 버퍼는 텍스트가 아니라 raw 바이트"라는 의도를 명확히 하고, 크로스플랫폼에서 부호 관련 버그를 원천 차단한다.

## MemoryPack Wire Format 상세

MemoryPack은 **Zero-encoding** 방식의 바이너리 직렬화 포맷이다. VarInt를 사용하지 않으며 모든 값을 **Little-Endian 고정 크기**로 직접 복사한다.

### Primitive 타입

메모리에 저장된 그대로 Little-Endian으로 기록한다. 별도의 헤더가 없다.

| C++ 타입 | 크기 | 설명 |
|----------|------|------|
| `bool` | 1B | 0 = false, 1 = true |
| `int8_t` / `uint8_t` | 1B | |
| `int16_t` / `uint16_t` | 2B | |
| `int32_t` / `uint32_t` | 4B | |
| `int64_t` / `uint64_t` | 8B | |
| `float` | 4B | IEEE 754 |
| `double` | 8B | IEEE 754 |

### Object (구조체)

Object는 **멤버 수 헤더**(1바이트) 뒤에 각 멤버가 선언 순서대로 이어진다. 멤버 이름은 기록되지 않는다.

```
[1B member_count] [member_0] [member_1] ... [member_N-1]
```

- `member_count = 255 (0xFF)` → null 오브젝트
- `member_count = 250 (0xFA)` → Union의 WideTag 모드 (이어서 `uint16_t` 태그)

예시 — `{ id: int32 = 42, name: string = "ABC" }`:
```
02                      ← member_count = 2
2A 00 00 00             ← int32 리틀엔디안 42
03 00 00 00             ← string 바이트 길이 3
41 42 43                ← "ABC" (UTF-8)
```

### Collection (배열/리스트)

Collection은 **요소 수 헤더**(4바이트 int32) 뒤에 요소들이 연속된다.

```
[4B int32 length] [element_0] [element_1] ... [element_N-1]
```

- `length = -1 (0xFFFFFFFF)` → null 컬렉션

예시 — `vector<int32> { 10, 20, 30 }`:
```
03 00 00 00             ← length = 3
0A 00 00 00             ← 10
14 00 00 00             ← 20
1E 00 00 00             ← 30
```

### String (문자열)

문자열은 **바이트 길이 헤더**(4바이트 int32) 뒤에 UTF-8 바이트가 이어진다. 문자 수가 아닌 **바이트 수**를 기록한다.

```
[4B int32 byte_length] [utf8_bytes...]
```

- `byte_length = -1` → null 문자열

예시 — `"Hello"`:
```
05 00 00 00             ← 바이트 길이 5
48 65 6C 6C 6F          ← "Hello" (UTF-8)
```

### Union

Union은 Object Header의 `member_count` 값으로 구분한다.

- `member_count = 250` → WideTag 모드. 뒤에 `uint16_t` 태그가 따라온다.
- 그 외 → 일반 오브젝트

```
[1B 250] [2B uint16 tag] [union_body...]
```

### 포맷 요약 다이어그램

```
Object:     [1B cnt] [members...]           cnt=255 → null
Collection: [4B len] [elements...]          len=-1  → null
String:     [4B len] [utf8_bytes...]        len=-1  → null
Primitive:  [raw LE bytes]                  고정 크기
Union:      [1B 250] [2B tag] [body...]     WideTag
```

## 샘플 프로그램

### 프로젝트 구조

```
samples/
  CSharpServer/    # C# 에코 서버 (.NET 10) — 포트 9000
  CppClient/       # C++ 테스트 클라이언트 — 포트 9000
  ChatServer/      # C# 채팅 서버 (.NET 10) — 포트 9001
  ChatClient/      # C++ 채팅 클라이언트 (Win32 GUI) — 포트 9001
```

모든 샘플은 공통 패킷 프로토콜을 사용한다:

```
[2B uint16 packetId] [4B int32 bodyLength] [body...]
```

### Sample 1: CppClient + CSharpServer (직렬화 테스트)

라이브러리가 지원하는 **모든 데이터 타입**의 직렬화/역직렬화를 검증하는 테스트 프로그램이다.

**CSharpServer** (C# .NET 10)는 단일 클라이언트 TCP 에코 서버로, 수신한 패킷을 역직렬화한 뒤 값을 변환하여 응답한다.

**CppClient** (C++23)는 콘솔 클라이언트로, 10가지 테스트를 순차 실행하고 결과를 검증한다.

#### 테스트하는 데이터 타입

| 패킷 | 테스트 대상 |
|------|------------|
| LoginRequest / LoginResponse | `string`, `int32`, `bool` |
| PlayerState | `float` (좌표 x, y, z) |
| ChatMessage | `int64` (타임스탬프), `string` |
| ScoreUpdate | `vector<int32>`, `double` |
| InventoryData | `vector<string>`, `vector<int32>` |
| BufferData | `vector<uint8>`, `vector<int8>` (바이트 배열) |
| IntArrayPacket | `vector<int16>`, `vector<int32>`, `vector<int64>` |
| SkillSlotData | C 스타일 고정 배열 `int32[8]`, `float[8]` |
| MapTileRow | C 스타일 고정 배열 `uint8[64]`, `int16[64]` |
| MixedFormatPacket | vector + 고정 배열 + char 배열 혼합 |

#### 실행 방법

```bash
# 1. C# 서버 시작
cd samples/CSharpServer
dotnet run

# 2. C++ 클라이언트 실행
# Visual Studio에서 samples/CppClient/CppClient.sln 열고 빌드 후 실행
# 또는 빌드된 실행 파일 직접 실행
```

서버가 포트 9000에서 대기한 상태에서 클라이언트를 실행하면, 10개의 테스트가 순차적으로 수행되고 각 테스트의 성공/실패가 콘솔에 출력된다.

### Sample 2: ChatClient + ChatServer (채팅 애플리케이션)

실제 서비스에 가까운 **멀티 유저 채팅 애플리케이션**으로, MemoryPack의 실전 활용을 보여준다.

**ChatServer** (C# .NET 10)는 다중 클라이언트를 지원하는 채팅 서버이다.
- Thread-per-client 모델로 동시 접속 처리
- 방(Room) 관리: 생성, 입장, 퇴장
- 방 내 메시지 브로드캐스트
- 1:1 귓속말(Private Chat) 라우팅
- 유저 입장/퇴장 알림 자동 전송
- 중복 닉네임 방지

**ChatClient** (C++23)는 Win32 GUI 채팅 클라이언트이다.
- 로그인 → 방 입장 → 채팅/귓속말 흐름
- 백그라운드 수신 스레드에서 패킷 수신, `WM_NET_PACKET` 커스텀 메시지로 UI 스레드에 전달
- 방 멤버 목록, 채팅 히스토리, 메시지 입력 UI 제공

#### 패킷 타입 (ID 101~108)

| ID | 패킷 | 방향 | 설명 |
|----|------|------|------|
| 101 | LoginRequest | C→S | 로그인 요청 (username) |
| 102 | LoginResponse | S→C | 로그인 결과 (success, message) |
| 103 | RoomJoinRequest | C→S | 방 입장 요청 (roomName) |
| 104 | RoomJoinResponse | S→C | 방 입장 결과 (success, existingUsers) |
| 105 | RoomChat | 양방향 | 방 메시지 (senderName, message) |
| 106 | PrivateChat | 양방향 | 귓속말 (senderName, targetName, message) |
| 107 | UserEntered | S→C | 유저 입장 알림 (username) |
| 108 | UserLeft | S→C | 유저 퇴장 알림 (username) |

#### 실행 방법

```bash
# 1. 채팅 서버 시작
cd samples/ChatServer
dotnet run

# 2. 채팅 클라이언트 실행 (여러 인스턴스 가능)
# Visual Studio에서 samples/ChatClient/ChatClient.sln 열고 빌드 후 실행
```

서버가 포트 9001에서 대기한 상태에서 클라이언트를 여러 개 실행하여 채팅을 테스트할 수 있다.

### 샘플 비교

| | CppClient + CSharpServer | ChatClient + ChatServer |
|---|---|---|
| 목적 | 직렬화 타입 테스트 | 실전 채팅 앱 |
| 포트 | 9000 | 9001 |
| 패킷 수 | 11종 | 8종 |
| 동시 접속 | 단일 클라이언트 | 다중 클라이언트 |
| UI | 콘솔 | Win32 GUI |
| 테스트 범위 | 모든 데이터 타입 | string 중심 |

## 주의사항

1. **멤버 순서** — MemoryPack은 이름 없이 선언 순서대로 직렬화한다. C#과 C++ 구조체의 멤버 순서가 반드시 일치해야 한다.
2. **엔디안** — 항상 Little-Endian. Big-Endian 플랫폼에서는 라이브러리가 자동으로 바이트 스왑을 수행한다.
3. **문자열** — UTF-8 기본. C# 측에서 UTF-16을 사용하는 경우 C++에서도 맞춰야 한다.
4. **Version Tolerance** — Deserialize 시 `memberCount`를 체크(`cnt >= N`)하여 새로 추가된 멤버를 안전하게 무시할 수 있다.

## 빌드

### C++ 샘플 (Visual Studio 2026)
각 샘플 디렉터리의 `.sln` 파일을 Visual Studio 2026에서 열어서 빌드한다 (x64, C++23).

### C# 서버 (.NET 10)
```bash
cd samples/CSharpServer   # 또는 samples/ChatServer
dotnet build -c Release
```
