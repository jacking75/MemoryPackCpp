# cs2cpp — C# MemoryPack 패킷 정의 → C++ 헤더 변환 도구

C#의 `[MemoryPackable]` 패킷 정의 파일을 읽어 C++ 헤더 파일(`.hpp`)을 자동 생성합니다.

## 빌드

```bash
cd tools/cs2cpp
dotnet build -c Release
```

## 사용법

```bash
# 출력 파일 지정
dotnet run --project tools/cs2cpp -- <input.cs> <output.hpp>

# 출력 파일 생략 → 입력 파일과 같은 디렉토리에 같은 이름의 .hpp 생성
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs
# → samples/ChatServer/Packets.hpp
```

## 생성 내용

| C# 요소 | C++ 생성 결과 |
|---------|--------------|
| `enum PacketId : ushort` | `enum class PacketId : uint16_t` |
| `[MemoryPackable] class` | `struct` + `IMemoryPackable<T>` 특수화 |
| 프로퍼티 이름 (PascalCase) | 필드 이름 (camelCase) |
| 패킷 헤더 상수 | `constexpr size_t PACKET_HEADER_SIZE = 6;` |

## 타입 매핑

| C# | C++ | 크기 |
|----|-----|------|
| `bool` | `bool` | 1B |
| `byte` / `sbyte` | `uint8_t` / `int8_t` | 1B |
| `short` / `ushort` | `int16_t` / `uint16_t` | 2B |
| `int` / `uint` | `int32_t` / `uint32_t` | 4B |
| `long` / `ulong` | `int64_t` / `uint64_t` | 8B |
| `float` | `float` | 4B |
| `double` | `double` | 8B |
| `string?` | `std::string` | 4B + UTF-8 bytes |
| `List<T>?` / `T[]?` | `std::vector<T>` | 4B + elements |

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
    public List<float>? Matrix { get; set; }
}
```

**생성되는 C++:**
```cpp
struct TransformData {
    int32_t              id = 0;
    std::array<float, 4> quaternion = {};
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
- 어노테이션은 반드시 프로퍼티 **바로 윗줄**에 주석(`//`)으로 작성
- `fixed_array`의 상수명은 C++ 관례에 따라 `UPPER_SNAKE_CASE` 권장
- C# 측에서는 `List<T>` 또는 `T[]` 모두 가능 — Wire Format이 동일하므로 C++과 호환
- `std::array`는 arithmetic 타입만 지원 (라이브러리 제약)

## 예시

```bash
# ChatServer 패킷 (어노테이션 없음 — 전부 std::vector)
dotnet run --project tools/cs2cpp -- samples/ChatServer/Packets.cs samples/ChatClient/packets.hpp

# CSharpServer 패킷 (어노테이션으로 고정 배열 지정)
dotnet run --project tools/cs2cpp -- samples/CSharpServer/Packets.cs samples/CppClient/packets.hpp
```
