# cs2cpp — C# MemoryPack 패킷 정의 → C++ 헤더 변환 도구

C#의 `[MemoryPackable]` 패킷 정의 파일(.cs)을 읽어 C++ 헤더 파일(`.hpp`)을 자동 생성합니다.

파서는 **Roslyn**(`Microsoft.CodeAnalysis.CSharp`) 기반입니다. 정규식이 아니라 실제 C# 구문
트리를 읽으므로 `partial` 유무, `record`, `struct`, 중첩 네임스페이스, 특성(attribute) 인자
같은 것들을 정확히 인식합니다.

## 빌드

```bash
dotnet build tools/cs2cpp -c Release
```

## 사용법

```
cs2cpp <input...> [options]

  <input>                 .cs 파일 / 디렉터리 / 글롭 패턴 (여러 개 지정 가능)

옵션:
  -o, --output <path>     출력 .hpp 파일(입력이 하나일 때) 또는 출력 디렉터리
      --namespace <ns>    생성 코드를 감쌀 C++ 네임스페이스
      --style <s>         macro(기본) | explicit
      --nullable-strings  C#의 string? 을 std::optional<std::string> 으로 매핑
      --dispatch          디스패치 테이블/스키마 해시 강제 생성
      --no-dispatch       디스패치 테이블/스키마 해시 생성 안 함
      --emit-schema-hash-cs <path>
                          PacketSchemaHash 상수를 담은 C# 파일 생성
      --check             생성 결과를 기존 파일과 비교만 하고 쓰지 않음
                          (차이가 있으면 unified diff 출력 후 exit code 1)
      --verbose           상세 로그
  -h, --help              도움말
```

### 옵션 상세

| 옵션 | 설명 |
|------|------|
| `-o`, `--output` | 입력이 1개이고 값이 `.hpp` 같은 확장자를 가지면 그 파일에 씁니다. 값이 기존 디렉터리이거나 확장자가 없거나 입력이 2개 이상이면 **디렉터리**로 보고 `<입력파일명>.hpp` 를 만듭니다. 생략하면 입력과 같은 위치에 같은 이름의 `.hpp` 를 만듭니다. |
| `--namespace <ns>` | `enum` / `struct` / `using` 별칭을 `namespace <ns> { ... }` 안에 넣습니다. `MEMORYPACK_DEFINE` 등 매크로는 전역 스코프에 남고 타입 이름만 `<ns>::` 로 정규화됩니다. |
| `--style macro` | (기본) `MEMORYPACK_DEFINE(Type, m1, m2, ...)` 한 줄로 직렬화 코드를 만듭니다. 멤버 개수/순서가 한 곳에만 있어 코드가 훨씬 짧습니다. |
| `--style explicit` | `template<> struct IMemoryPackable<T> { Serialize / Deserialize }` 전체를 직접 펼칩니다(구버전 동작). 커밋된 샘플 헤더가 이 형태입니다. |
| `--nullable-strings` | 기본값은 호환성을 위해 `string?` → `std::string` 입니다. 이 플래그를 주면 `string?` → `std::optional<std::string>` 으로 바뀌고(널과 빈 문자열을 구분), `string`(non-nullable)은 그대로 `std::string` 입니다. |
| `--dispatch` / `--no-dispatch` | 아래 “디스패치 테이블” 참고. 기본값은 `PacketId` enum 이 발견되면 자동 ON. |
| `--emit-schema-hash-cs` | C# 쪽에 심을 `public const ulong PacketSchemaHash = 0x...;` 파일을 만듭니다. |
| `--check` | 드리프트 검사용. 아무것도 쓰지 않고 기존 파일과 비교합니다. 줄바꿈(CRLF/LF)은 무시합니다. |

`--style=explicit` 처럼 `=` 로 붙여 쓰는 형태도 지원합니다.

### 예

```bash
# 파일 하나 → 파일 하나
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs -o samples/ChatClient/packets.hpp

# 글롭 + 출력 디렉터리
dotnet run --project tools/cs2cpp -- "samples/**/Packets.cs" -o build/generated --style explicit

# 드리프트 검사 (커밋된 헤더가 생성기 출력과 같은지 확인)
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs -o samples/ChatClient/packets.hpp --check
```

## 인식하는 C# 요소

| C# 요소 | C++ 생성 결과 |
|---------|--------------|
| `enum X : ushort { ... }` | `enum class X : uint16_t { ... }` (값 생략 시 자동 증가, 음수/16진수/비트연산 상수 지원) |
| `[MemoryPackable] class / struct / record` | `struct` + 직렬화 정의 (`partial` 유무 무관) |
| 자동 프로퍼티 `{ get; set; }`, `{ get; init; }`, `required` | 멤버 |
| `public` 필드 | 멤버 |
| positional record `record P(int A, string B)` | 멤버 |
| `[MemoryPackOrder(n)]` | 하나라도 지정되면 그 값 오름차순 정렬 → 지정 없는 멤버는 선언 순서로 뒤에 붙음 |
| `[MemoryPackIgnore]` | 멤버에서 제외 |
| `[MemoryPackable(GenerateType.VersionTolerant)]` | `VersionTolerantWriter` / `VersionTolerantReader` 기반 코드 |
| `[MemoryPackUnion(tag, typeof(X))]` (interface / abstract class) | `MEMORYPACK_UNION_TAG(X, tag)` + `using <Name> = std::variant<...>;` |
| 참조 타입 필드가 없는 `[MemoryPackable] struct` | `MEMORYPACK_UNMANAGED(Type, <크기>)` (.NET sequential layout 으로 크기 계산) |
| `[StructLayout(LayoutKind.Sequential, Pack = 1)]` | `#pragma pack(push, 1) ... #pragma pack(pop)` + 크기 재계산 |
| 파일 스코프 / 블록 / 중첩 네임스페이스 | 스키마 해시 C# 파일의 `namespace` 로 사용 |
| 프로퍼티 이름 (PascalCase) | 필드 이름 (camelCase, C++ 예약어면 뒤에 `_`) |
| `PacketId` enum | `constexpr size_t PACKET_HEADER_SIZE = 6;` + 디스패치 테이블 |

제외되는 것: `internal`/`private` 멤버, `static`, `const`, 식 본문 프로퍼티(`=> ...`),
본문이 있는 접근자를 가진 프로퍼티, `[MemoryPackIgnore]` 멤버.

## 타입 매핑

| C# | C++ | 비고 |
|----|-----|------|
| `bool` | `bool` | 1B |
| `byte` / `sbyte` | `uint8_t` / `int8_t` | 1B |
| `short` / `ushort` | `int16_t` / `uint16_t` | 2B |
| `int` / `uint` | `int32_t` / `uint32_t` | 4B |
| `long` / `ulong` | `int64_t` / `uint64_t` | 8B |
| `float` / `double` | `float` / `double` | 4B / 8B |
| `char` | `char16_t` | 2B (UTF-16 코드 유닛) |
| `string` | `std::string` | |
| `string?` | `std::string` (기본) / `std::optional<std::string>` (`--nullable-strings`) | |
| `List<T>?` / `T[]?` / `IList<T>` / `IReadOnlyList<T>` | `std::vector<T>` | |
| `List<UserType>` | `std::vector<UserType>` | |
| `Dictionary<K,V>` / `IDictionary` / `SortedDictionary` | `std::map<K,V>` | |
| `HashSet<T>` / `ISet<T>` / `SortedSet<T>` | `std::set<T>` | |
| `KeyValuePair<K,V>` | `std::pair<K,V>` | 헤더 없이 key + value |
| `int?` / `float?` / `MyStruct?` (`Nullable<T>`) | `std::optional<T>` | C# `Nullable<T>` 레이아웃 그대로 |
| `[MemoryPackable]` class 멤버 (참조 타입) | `std::optional<T>` | null 가능 |
| unmanaged `[MemoryPackable]` struct 멤버 | `T` + `MEMORYPACK_UNMANAGED(T, N)` | 헤더 없이 통째 복사 |
| 참조 타입 필드가 있는 struct 멤버 | `T` + 일반 오브젝트 헤더 | |
| enum 멤버 | 생성된 `enum class` | underlying 타입으로 직렬화 |
| 유니온 인터페이스 멤버 | `std::optional<Union>` | |
| `Guid` | `memorypack::Guid` | |
| `DateTime` | `memorypack::DateTime` | |
| `TimeSpan` | `memorypack::TimeSpan` | |
| `DateTimeOffset` | `memorypack::DateTimeOffset` | |
| `decimal` | `memorypack::Decimal` | |
| `Half` | `memorypack::Half` | |
| `Int128` / `UInt128` | `memorypack::Int128` / `memorypack::UInt128` | |
| `Vector2` / `Vector3` / `Vector4` / `Quaternion` | `memorypack::Vector2` … | System.Numerics |

`#include` 는 실제 사용된 타입에 맞춰 `<string>`, `<vector>`, `<array>`, `<map>`, `<set>`,
`<utility>`, `<optional>`, `<variant>`, `<span>` 중 필요한 것만 내보냅니다.

### 매핑되는 코드 형태

- `--style=macro` (기본)에서는 모든 멤버가 `MEMORYPACK_DEFINE` 의 제네릭 디스패치
  (`w.Write(v->x)` / `r.Read(v.x)`)를 탑니다.
- `--style=explicit` 에서는 구버전과의 호환을 위해 primitive / `std::string` /
  `std::vector<산술타입>` / `std::vector<std::string>` / 고정 배열에는
  `WriteInt32`, `WriteString`, `WriteVector`, `WriteStringVector`, `WriteArray` 같은
  전용 API 를 쓰고, 나머지(map/set/pair/optional/중첩 객체/enum/.NET 타입)는
  제네릭 `w.Write` / `r.Read` 를 씁니다.
- 아래 경우는 매크로로 표현할 수 없어 `--style=macro` 에서도 **항상** explicit 코드가 나옵니다.
  - `[cpp:fixed_array]` / `[cpp:std_array]` 어노테이션이 붙은 타입
  - `GenerateType.VersionTolerant` 타입
- unmanaged struct 는 `MEMORYPACK_UNMANAGED`, 유니온은 `MEMORYPACK_UNION_TAG` 로 나갑니다.
- 멤버가 없는 타입은 `MEMORYPACK_DEFINE_EMPTY` 가 나갑니다.

### 선언 순서

C++ 쪽 `struct` 와 직렬화 정의는 **의존성 순서**로 재정렬됩니다.
(C# 에서 `Inventory` 가 `Item` 보다 먼저 선언돼 있어도 C++ 에서는 `Item` 이 먼저 나옵니다.)
`struct` 내부 멤버 순서는 절대 바뀌지 않습니다 — 와이어 포맷이 순서에 의존하기 때문입니다.

## C++ 주석 어노테이션

C# 프로퍼티 위에 특수 주석을 달면 C++ 코드 생성 방식을 제어할 수 있습니다.
어노테이션이 없으면 `List<T>`는 기본적으로 `std::vector<T>`로 변환됩니다.

### `[cpp:fixed_array(크기, 상수명)]` — C 고정 배열

C 스타일 고정 배열 + count 추적 변수를 생성합니다.
게임에서 고정 크기 슬롯(스킬, 인벤토리 등)에 적합합니다.

**C# 입력:**
```csharp
[MemoryPackable]
public partial class SkillSlotData
{
    public int PlayerId { get; set; }

    // [cpp:fixed_array(8, MAX_SKILLS)]
    public List<int>? SkillIds { get; set; }

    // [cpp:fixed_array(8, MAX_SKILLS)]
    public List<float>? Cooldowns { get; set; }
}
```

**생성되는 C++:**
```cpp
struct SkillSlotData {
    static constexpr int32_t MAX_SKILLS = 8;

    int32_t playerId = 0;
    int32_t skillIds[MAX_SKILLS] = {};
    int32_t skillCount = 0;        // 사용 개수 (직렬화 안 됨)
    float   cooldowns[MAX_SKILLS] = {};
    int32_t cooldownCount = 0;     // 사용 개수 (직렬화 안 됨)
};
```

**직렬화/역직렬화:**
```cpp
// Serialize — count만큼만 전송
w.WriteArray(v->skillIds, v->skillCount);

// Deserialize — 읽은 개수를 count에 저장
v.skillCount = r.ReadArray(v.skillIds, SkillSlotData::MAX_SKILLS);
```

**규칙:**
- `count` 변수는 멤버 수(memberCount)에 포함되지 않음 (C# 프로퍼티와 1:1 대응)
- 동일 상수명을 여러 필드에서 사용하면 `static constexpr`은 한 번만 생성됨
- count 변수명은 필드명에서 자동 유도:
  - `skillIds` → `skillCount` (복수형 "Ids" 제거)
  - `cooldowns` → `cooldownCount` (복수형 "s" 제거)
  - `tiles` → `tileCount`
  - `heights` → `heightCount`
  - `tag` → `tagCount` (복수형이 아니면 그대로 `Count` 부착)
- 산술 타입 컬렉션에만 쓸 수 있습니다 (라이브러리 제약)

### `[cpp:std_array(크기)]` — std::array

고정 크기 `std::array<T, N>`을 생성합니다.
크기가 항상 고정인 데이터(쿼터니언, 행렬 등)에 적합합니다.

**C# 입력:**
```csharp
[MemoryPackable]
public partial class TransformData
{
    public int Id { get; set; }

    // [cpp:std_array(4)]
    public List<float>? Quaternion { get; set; }

    // [cpp:std_array(16)]
    public float[]? Matrix { get; set; }
}
```

**생성되는 C++:**
```cpp
struct TransformData {
    int32_t               id = 0;
    std::array<float, 4>  quaternion = {};
    std::array<float, 16> matrix = {};
};
```

**직렬화/역직렬화:**
```cpp
// Serialize — 전체 배열 전송
w.WriteArray(v->quaternion);

// Deserialize — std::array로 읽기
v.quaternion = r.ReadArray<float, 4>();
```

### 어노테이션 혼합 사용

하나의 struct에 `std::vector`, C 고정 배열, `std::array`를 자유롭게 혼합할 수 있습니다.

```csharp
[MemoryPackable]
public partial class MixedPacket
{
    public int Id { get; set; }
    public List<int>? DynamicScores { get; set; }     // → std::vector<int32_t>

    // [cpp:fixed_array(4, MAX_BONUSES)]
    public List<int>? FixedBonuses { get; set; }      // → int32_t[MAX_BONUSES] + count

    // [cpp:std_array(3)]
    public List<float>? Position { get; set; }         // → std::array<float, 3>

    public double Multiplier { get; set; }
}
```

### 어노테이션 문법 요약

| 주석 | C++ 생성 결과 | 직렬화 API |
|------|-------------|-----------|
| (없음) | `std::vector<T>` | `WriteVector` / `ReadVector<T>` |
| `// [cpp:fixed_array(N, NAME)]` | `T field[NAME]` + `int32_t count` | `WriteArray(arr, count)` / `ReadArray(arr, max)` |
| `// [cpp:std_array(N)]` | `std::array<T, N>` | `WriteArray(arr)` / `ReadArray<T, N>()` |

**주의사항:**
- 어노테이션은 프로퍼티 앞의 주석(`//` 또는 `/* */`)에 작성 (특성 `[...]` 위/아래 모두 인식)
- `fixed_array`의 상수명은 C++ 관례에 따라 `UPPER_SNAKE_CASE` 권장
- C# 측에서는 `List<T>` 또는 `T[]` 모두 가능 — Wire Format이 동일하므로 C++과 호환
- 두 어노테이션 모두 arithmetic 타입만 지원 (라이브러리 제약)

## 디스패치 테이블과 스키마 해시

`PacketId` (또는 `~PacketId`로 끝나는) enum 이 있고, 그 멤버 이름과 같은 이름의
`[MemoryPackable]` 타입이 하나라도 있으면 아래 두 가지가 추가로 생성됩니다.
`--no-dispatch` 로 끌 수 있습니다.

```cpp
// ── Packet dispatch table ──────────────────────────────────────────────────────
template<typename Handler>
bool DispatchPacket(PacketId id, std::span<const uint8_t> body, Handler&& handler) {
    switch (id) {
    case PacketId::LoginRequest: { auto v = memorypack::Deserialize<LoginRequest>(body); handler(v); return true; }
    case PacketId::LoginResponse: { auto v = memorypack::Deserialize<LoginResponse>(body); handler(v); return true; }
    default: return false;
    }
}

// ── Schema hash ────────────────────────────────────────────────────────────────
inline constexpr uint64_t PACKET_SCHEMA_HASH = 0xA7CEC854C3062379ULL;
```

`Handler` 는 각 패킷 타입에 대한 `operator()` 오버로드를 가진 함수 객체입니다.
`PacketId` 멤버와 이름이 일치하는 패킷 타입이 없는 값(예: `Heartbeat`)은 `default` 로 빠집니다.

### 스키마 해시 계산 방식

프로토콜 버전이 서로 맞는지 접속 시점에 비교하기 위한 값입니다.
아래 정규 문자열의 UTF-8 바이트에 대한 **FNV-1a 64bit** 해시입니다.

```
TypeName(0:CsType;1:CsType;...)\n      ← 타입마다 한 줄, 선언(의존성) 순서
```

- 멤버는 직렬화 순서(`[MemoryPackOrder]` 적용 후) 이고 인덱스가 앞에 붙습니다.
- `CsType` 은 **공백을 모두 제거한 원본 C# 타입 문자열** 입니다 (예: `List<int>?`, `Dictionary<int,string>?`).
- FNV-1a 파라미터: offset basis `0xcbf29ce484222325`, prime `0x100000001b3`.

같은 값을 stdout 으로도 출력하므로 C# 쪽에 그대로 심을 수 있고,
`--emit-schema-hash-cs <path>` 로 아래 같은 파일을 바로 만들 수도 있습니다.

```csharp
// <auto-generated> cs2cpp
namespace ChatServer;

public static class PacketSchema
{
    public const ulong PacketSchemaHash = 0xA7CEC854C3062379UL;
}
```

## 생성물 드리프트 검사 (`--check`)

`--check` 는 파일을 쓰지 않고, 생성 결과와 기존 파일을 비교해 다르면 unified diff 를
찍고 exit code 1 로 끝납니다. 커밋된 헤더가 생성기 출력과 어긋나지 않는지 확인할 때 씁니다.

```bash
dotnet run --project tools/cs2cpp -- samples/CSharpServer/Packets.cs \
    -o samples/CppClient/packets.hpp --check
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs \
    -o samples/ChatClient/packets.hpp --check
```

커밋된 `samples/CppClient/packets.hpp` 와 `samples/ChatClient/packets.hpp` 는 위 두 명령의
**생성 결과 그대로**입니다(기본 옵션 = `--style macro`, `PacketId` enum 이 있으므로
디스패치 테이블 포함). 손으로 고치지 말고 `Packets.cs` 를 고친 뒤 다시 생성하세요.

이 저장소에는 호스팅 CI 가 없으므로, 어느 쪽이든 수정한 뒤에는 위 두 명령을 직접 돌려
어긋나지 않았는지 확인해야 합니다.

## 테스트

```bash
dotnet test tools/cs2cpp.Tests
```

`tools/cs2cpp.Tests/Cases/*.cs` 를 생성기에 통과시킨 결과가 `tools/cs2cpp.Tests/Expected/*.hpp`
와 일치하는지 검사하는 스냅샷 테스트입니다. 생성기 출력을 의도적으로 바꿨다면 아래처럼
기대 파일을 다시 만듭니다.

```bash
CS2CPP_UPDATE_SNAPSHOTS=1 dotnet test tools/cs2cpp.Tests
```
