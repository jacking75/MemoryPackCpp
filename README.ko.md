# MemoryPackCpp

[![CI](https://github.com/jacking75/MemoryPackCpp/actions/workflows/ci.yml/badge.svg)](https://github.com/jacking75/MemoryPackCpp/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](#요구-사항)
[![Header-only](https://img.shields.io/badge/header--only-yes-brightgreen)](#설치)

**C#의 [MemoryPack](https://github.com/Cysharp/MemoryPack) 바이너리 와이어 포맷을
바이트 단위로 동일하게 구현한 C++23 header-only 라이브러리.**
C++ 게임 서버·클라이언트가 C#/Unity와 직접 통신하기 위해 만들어졌다.

[English README](README.md) · [와이어 포맷 명세](docs/wire-format.md) ·
[타입 매핑](docs/type-mapping.md) · [로드맵](ROADMAP.md)

> Cysharp와 제휴 관계가 아니다. MemoryPack 와이어 포맷을 독립적으로 구현한
> 비공식 프로젝트이며, 실제 C# 라이브러리 출력과 대조해 검증한다.

---

## 왜 필요한가

MemoryPack은 .NET 생태계에서 가장 빠른 범용 직렬화기이고 Unity 프로젝트에서 널리
쓰인다. 문제는 소켓 반대편이 C++일 때 시작된다. 전용 게임 서버, 네이티브
클라이언트, 툴 — 결국 두 번째 프로토콜을 손으로 만들게 되고, 스키마가 바뀔 때마다
두 언어를 모두 고쳐야 한다.

MemoryPackCpp는 포맷 자체를 구현해서 그 문제를 없앤다. 같은 멤버를 같은 순서로
정의하면 바이트가 일치한다.

**어떻게 보장하는가.** [`tools/FormatProbe`](tools/FormatProbe)가 실제 C#
MemoryPack 패키지로 53개 케이스를 직렬화해 [`tests/fixtures/`](tests/fixtures)에
바이트를 커밋한다. CI는 매 푸시마다 양방향을 모두 검증한다.

- C++ 리더가 그 C# 바이트를 기대값으로 디코딩하고,
- C++ 라이터가 **바이트 단위로 동일한** 출력을 다시 만들고,
- C#이 C++가 만든 바이트를 되읽는다.

Union, 패딩 포함 unmanaged struct, `Nullable<T>`, 서로게이트 페어, `Guid`/`DateTime`,
VersionTolerant 레이아웃, 길이 인코딩 경계까지 전부 포함된다. 바이트 하나라도
어긋나면 테스트가 실패한다.

---

## 빠른 시작

```cpp
#include "memorypack/memorypack.hpp"

struct LoginRequest {
    std::string userName;
    int32_t     level;
};
MEMORYPACK_DEFINE(LoginRequest, userName, level)   // 전역 스코프에 한 줄

int main() {
    auto bytes = memorypack::Serialize(LoginRequest{"Player1", 42});
    auto back  = memorypack::Deserialize<LoginRequest>(bytes);
}
```

대응하는 C#:

```csharp
[MemoryPackable]
public partial class LoginRequest
{
    public string? UserName { get; set; }
    public int Level { get; set; }
}
```

멤버 **순서**가 계약이다. MemoryPack은 이름을 기록하지 않는다. 멤버를 **뒤에
추가**하는 것은 양방향 모두 안전하지만, 순서를 바꾸거나 삭제하면 호환이 깨진다
(삭제가 필요하면 [VersionTolerant](docs/wire-format.md#versiontolerant-objects)
레이아웃을 쓴다).

---

## 설치

header-only다. `include/`를 include 경로에 추가하거나 CMake를 쓴다.

```cmake
# (A) FetchContent
include(FetchContent)
FetchContent_Declare(memorypack
    GIT_REPOSITORY https://github.com/jacking75/MemoryPackCpp.git
    GIT_TAG        v0.2.0)
FetchContent_MakeAvailable(memorypack)
target_link_libraries(my_app PRIVATE memorypack::memorypack)

# (B) 서브디렉터리
add_subdirectory(MemoryPackCpp)
target_link_libraries(my_app PRIVATE memorypack::memorypack)

# (C) 설치 후 find_package
find_package(memorypack CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE memorypack::memorypack)
```

[릴리스](https://github.com/jacking75/MemoryPackCpp/releases)의 **단일 헤더**를
받거나 직접 생성해도 된다.

```bash
python tools/amalgamate.py --include-packet -o dist/memorypack.hpp
```

### 헤더 구성

| 헤더 | 내용 |
|---|---|
| `memorypack/memorypack.hpp` | 통합 헤더 — 아래 셋을 포함 |
| `memorypack/core.hpp` | 라이터, 리더, primitive, 문자열, 오브젝트, 컬렉션, Union, unmanaged struct |
| `memorypack/containers.hpp` | `std::` 컨테이너, `optional`, 스마트 포인터, Union으로서의 `variant` |
| `memorypack/dotnet.hpp` | `Guid`, `DateTime`, `TimeSpan`, `decimal`, `Half`, `Int128` |
| `memorypack/packet.hpp` | 선택적 TCP 패킷 프레이밍 (통합 헤더에 포함되지 않음) |

### 요구 사항

- **C++23**: MSVC v143(Visual Studio 2022) 이상, GCC 13+, Clang 16+
- CMake 3.21 이상 (CMake로 빌드할 경우)
- .NET 10 SDK (C# 도구·샘플을 쓸 경우)

---

## 지원 타입

| C# | C++ | |
|---|---|---|
| `bool`, `byte`/`sbyte`, `short`/`ushort`, `int`/`uint`, `long`/`ulong`, `float`, `double` | 대응 고정폭 타입 | ✅ |
| `enum : T` | `enum class : T` | ✅ |
| `string` (읽기는 UTF-8/UTF-16 모두) | `std::string`, `std::u16string`, `std::string_view` | ✅ |
| `List<T>`, `T[]` | `std::vector`, `std::array`, C 배열, `std::deque`, `std::list` | ✅ |
| `List<bool>` | `std::vector<bool>` | ✅ |
| `List<사용자타입>`, 중첩 컬렉션 | `std::vector<T>`, `std::vector<std::vector<T>>` | ✅ |
| `Dictionary<K,V>` | `std::map`, `std::unordered_map` | ✅ |
| `HashSet<T>` | `std::set`, `std::unordered_set` | ✅ |
| `KeyValuePair<K,V>` | `std::pair` | ✅ |
| `Tuple<...>`, `ValueTuple` | `std::tuple`, `MEMORYPACK_UNMANAGED` 구조체 | ✅ |
| `[MemoryPackable] class` | `MEMORYPACK_DEFINE` 또는 수동 `IMemoryPackable<T>` | ✅ |
| `[MemoryPackable] struct` (unmanaged) | `MEMORYPACK_UNMANAGED(T, size)` | ✅ |
| `[MemoryPackUnion]` | `std::variant` + `MEMORYPACK_UNION_TAG` | ✅ |
| `Nullable<T>`, nullable 참조 | `std::optional`, `std::unique_ptr`, `std::shared_ptr` | ✅ |
| `Guid`, `DateTime`, `TimeSpan`, `DateTimeOffset`, `decimal`, `Half`, `Int128`, `char` | `memorypack::` 대응 타입, `char16_t` | ✅ |
| `Vector2/3/4`, `Quaternion` | `memorypack::Vector2/3/4`, `Quaternion` | ✅ |
| `GenerateType.VersionTolerant` | `VersionTolerantWriter` / `VersionTolerantReader` | ✅ |
| `GenerateType.CircularReference` | — | ❌ |

각 타입의 실제 바이트까지 포함한 전체 표: [docs/type-mapping.md](docs/type-mapping.md)

---

## 주요 기능

**한 줄 타입 정의.** `MEMORYPACK_DEFINE(T, a, b, c)`가 직렬화기를 생성한다.
멤버 개수와 순서를 한 곳에서만 관리하고 버전 관용성이 기본 내장된다. 특수한
경우를 위해 수동 `IMemoryPackable<T>` 특수화도 그대로 동작한다.

**버퍼 제어.** 새 버퍼, 호출자 소유 `std::vector`(패킷 헤더 자리를 미리 잡고 이어
쓰기), 힙 할당이 전혀 없는 고정 `std::array` 중에서 고를 수 있다. 리더는
`ReadStringView()`로 문자열을 복사 없이 참조하고, 기존 컨테이너를 그대로 채운다.

**신뢰할 수 없는 입력에 안전.** 모든 읽기가 경계 검사를 거치고, 선언된 컬렉션·문자열
길이를 **할당 전에** 실제 남은 바이트와 대조한다. 4바이트짜리 패킷이 수 GB를
요구할 수 없다. `ReaderOptions`로 컬렉션 길이·문자열 길이·중첩 깊이 상한을 따로
지정할 수 있다. libFuzzer 하니스가 ASan/UBSan 아래에서 디코더 전체를 무작위
바이트로 두들긴다.

**예외 없이 동작.** Unreal Engine과 대부분의 콘솔 툴체인은 예외를 끈다.
`-fno-exceptions`로 빌드하면 오류가 리더 오류 상태와 `std::expected` 기반
`TryDeserialize` / `TrySerializeTo` API로 전달된다.

**크로스플랫폼.** Windows, Linux, macOS. 리틀·빅엔디안 모두 지원한다
(unmanaged struct 고속 경로만 리틀엔디안 전용).

**코드 생성.** [`tools/cs2cpp`](tools/cs2cpp)가 C# `[MemoryPackable]` 정의를 읽어
대응하는 C++ 헤더를 만들어 준다. 양쪽이 어긋날 수 없다.

---

## 예제

```cpp
// 중첩 오브젝트와 컬렉션도 동일한 진입점 하나로 처리된다.
struct Item      { int32_t id; std::string name; int32_t count; };
struct Inventory { int32_t ownerId; std::vector<Item> items; };
MEMORYPACK_DEFINE(Item, id, name, count)
MEMORYPACK_DEFINE(Inventory, ownerId, items)
```

```cpp
// 할당 없는 핫패스: 라이터 하나를 재사용한다.
memorypack::MemoryPackWriter writer;
writer.Reserve(4096);
for (const auto& update : updates) {
    writer.Clear();
    writer.Write(update);
    send(sock, writer.Data(), writer.Size(), 0);
}
```

```cpp
// TCP 프레이밍: [2B packetId][4B bodyLength][body]
memorypack::PacketFrameParser parser;
parser.Feed(receivedBytes, [](uint16_t id, std::span<const uint8_t> body) {
    if (id == 101) handle(memorypack::Deserialize<LoginRequest>(body));
});
```

```cpp
// 악의적 입력은 할당 전에 거부된다.
memorypack::ReaderOptions limits;
limits.maxCollectionLength = 10'000;
limits.maxStringLength     = 64 * 1024;
memorypack::MemoryPackReader reader(untrustedBytes, limits);
```

실행 가능한 예제 10개가 [`examples/`](examples)에 있다.

---

## 빌드와 테스트

```bash
cmake -B build -DMEMORYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| 타깃 | 검증 내용 |
|---|---|
| `memorypack_tests` | 와이어 포맷, 버퍼 모드, 한도, 오류 처리, 프레이밍 |
| `memorypack_interop_tests` | 실제 C# MemoryPack 픽스처와의 바이트 일치 |

픽스처 재생성 (.NET 10 SDK 필요):

```bash
dotnet run --project tools/FormatProbe -- generate tests/fixtures   # C# 바이트 캡처
dotnet run --project tools/FormatProbe -- verify   tests/fixtures   # CI: 포맷 변경 감지
```

그 외 옵션: `-DMEMORYPACK_BUILD_SAMPLES=ON`, `-DMEMORYPACK_BUILD_BENCHMARKS=ON`,
`-DMEMORYPACK_BUILD_EXAMPLES=ON`

---

## 샘플

| 샘플 | 내용 |
|---|---|
| `CSharpServer` + `CppClient` | 지원하는 모든 데이터 타입을 실제 C# 서버와 TCP로 주고받으며 검증 |
| `CppServer` + `CsClient` | 반대 방향 — **C++ 서버**가 **C# 클라이언트**를 서비스 |
| `ChatServer` + `ChatClient` | 다중 사용자 채팅 앱 — 방, 브로드캐스트, 귓속말 (Win32 GUI) |
| `ChatServer` + `ChatClientConsole` | 같은 채팅 프로토콜의 크로스플랫폼 콘솔 클라이언트 |

실행 방법은 [samples/README.md](samples/README.md) 참고.

---

## 문서

| 문서 | 내용 |
|---|---|
| [api-reference.md](docs/api-reference.md) | 모든 공개 함수·타입·매크로 |
| [wire-format.md](docs/wire-format.md) | 실제 캡처한 바이트로 기술한 전체 바이너리 포맷 |
| [type-mapping.md](docs/type-mapping.md) | 모든 C# 타입과 C++ 대응 |
| [serialization.md](docs/serialization.md) | 사용자 타입 정의, 버전 관용, null 처리 |
| [performance.md](docs/performance.md) | 버퍼 재사용, zero-copy 읽기, unmanaged 고속 경로 |
| [security.md](docs/security.md) | 신뢰할 수 없는 입력, 한도, fuzzing, 위협 모델 |
| [error-handling.md](docs/error-handling.md) | 예외, `std::expected`, 예외 없는 빌드 |
| [compatibility.md](docs/compatibility.md) | 검증된 MemoryPack 버전, 컴파일러, 플랫폼 |
| [benchmarks.md](docs/benchmarks.md) | 벤치마크 실행 방법 |
| [faq.md](docs/faq.md) | 자주 묻는 질문과 함정 |
| [integration-unreal.md](docs/integration-unreal.md) | Unreal Engine 연동 |
| [integration-unity.md](docs/integration-unity.md) | Unity 연동 |
| [cs2cpp](tools/cs2cpp/README.md) | C# → C++ 코드 생성 도구 |

---

## 기여

버그 리포트, 와이어 포맷 관련 발견, PR 모두 환영한다.
[CONTRIBUTING.md](CONTRIBUTING.md)를 참고할 것. 단 하나의 원칙:
**와이어 포맷을 바꾸는 변경에는 반드시 실제 C# MemoryPack에서 캡처한 픽스처를
함께 제출해야 한다.** 포맷이 어떠해야 한다는 주장만으로는 받지 않는다.

보안 이슈: [SECURITY.md](SECURITY.md)

## 라이선스

MIT — [LICENSE](LICENSE) 참고.

MemoryPack 자체는 Cysharp, Inc.의 저작물이며 MIT 라이선스다. 이 프로젝트는 그
와이어 포맷을 독립적으로 구현한 것으로, Cysharp의 승인이나 제휴를 받지 않았다.
