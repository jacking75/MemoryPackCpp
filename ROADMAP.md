# MemoryPackCpp 발전 로드맵 — 상용 게임 서버 적용 · 오픈소스 성장

> 작성: 2026-08-30 · 기준 커밋: `a855f90` (main)
> **갱신: 2026-08-30 — §10의 1~14번 작업을 모두 수행하여 v0.2.0으로 반영 완료.**
> 아래 본문은 작업 착수 시점의 계획 그대로 남겨 두고, 각 항목의 결과를 §0.5 "완료 현황"에 정리했다.

## 0.5. 완료 현황 (v0.2.0)

| # | 작업 | 상태 | 결과 |
|---|------|------|------|
| 1 | §1 결함 수정 (1.1~1.10) | ✅ | 10건 전부 수정 + 회귀 테스트. `/W4 /WX`·`-Werror` 무경고 |
| 2 | 부록 A 문서/설정 정정 | ✅ | URL·버전·unmanaged struct 설명·String 예시·포트(25001/25002)·헤더 ASCII화 |
| 3 | `tools/FormatProbe` + 픽스처 | ✅ | 실제 MemoryPack 1.21.4로 **53개 픽스처** 생성, 미확정 포맷 전부 실측 확정 |
| 4 | interop 테스트 하니스 | ✅ | `interop_tests.cpp` 289개 체크. C++→C# 역방향은 `check-cpp` 22건 |
| 5 | CI 1차 | ✅ | `.github/workflows/ci.yml` 11개 잡 + fuzz.yml + release.yml |
| 6 | 중첩 객체 컬렉션 + 공개 `Write/Read` | ✅ | `WriteCollection`/`ReadCollection`, `MemoryPackFormatter<T>` 디스패치 |
| 7 | Union / Unmanaged / Nullable | ✅ | `std::variant`(wide tag 포함), `MEMORYPACK_UNMANAGED`, null 인코딩 4종 자동 선택 |
| 8 | `MEMORYPACK_DEFINE` 매크로 | ✅ | 수동 특수화와 바이트 동일함을 테스트로 검증 |
| 9 | no-exceptions + `std::expected` + 한도 + fuzz | ✅ | `_HAS_EXCEPTIONS=0` 빌드 실증, `ReaderOptions`, libFuzzer 하니스 |
| 10 | 벤치마크 + 핫패스 최적화 | ✅ | Google Benchmark 23개 + memcpy 기준선. `docs/benchmarks.md` |
| 11 | cs2cpp Roslyn 전환 | ✅ | 정규식→Roslyn, 타입 확장, 스냅샷 테스트 |
| 12 | `packet.hpp` + 샘플 정비 | ✅ | 프레이밍 헬퍼 + `PacketFrameParser`, C++ 서버↔C# 클라이언트 쌍과 크로스플랫폼 콘솔 채팅 클라이언트 추가, 샘플 헤더를 cs2cpp 생성물로 일원화(CI `--check`), 포트 25001~25003 |
| 13 | 문서 체계 + 예제 | ✅ | 영문 README + `README.ko.md` + `docs/` 11종 + `examples/` |
| 14 | 릴리스 준비 | ✅ | `CHANGELOG.md`, `tools/amalgamate.py`(단일 헤더), `vcpkg-port/` |

### 로드맵 작성 당시에는 미확정이었으나 실측으로 확정한 포맷

| 항목 | 실측 결과 |
|------|-----------|
| unmanaged struct | **자연 정렬(패딩 포함)** — `{byte,int}`는 8바이트. `Pack=1`일 때만 5바이트 |
| `Nullable<unmanaged T>` | `Nullable<T>` 구조체를 통째로 복사 (`int?`=8B, `Vec3?`=16B) |
| `Nullable<managed struct>` | `[1B 1][value]` / null은 `[1B 255]` |
| ValueTuple (전부 unmanaged) | 헤더 없이 memcpy. **CLR이 필드를 재배치** — `(int,float,double)`은 `double,int,float` 순 |
| `KeyValuePair<K,V>` | 헤더 없이 key 다음 value |
| Union | tag<250은 `[1B tag]`, 이상은 `[1B 250][2B tag]`, null은 `[1B 255]`. 뒤에 자체 헤더를 가진 값 |
| VersionTolerant 길이 | `<=127`은 1바이트, `<=65535`는 `0x84`+u16, 초과는 `0x82`+u32 |
| `List<bool>` | 요소당 1바이트 (비트팩킹 아님) |

### 작업 중 새로 발견해 처리한 것

| 발견 | 처리 |
|------|------|
| MSVC 최적화 빌드에서 `core.hpp`가 C4702 유발 (`/W4 /WX` 소비자 빌드 파손) | 헤더 내부에서 억제 |
| `VersionTolerantWriter` 소멸자가 예외를 전파해 `std::terminate` 가능 | 소멸자에서 오류 상태로만 기록 |
| deque/list/set/map 포맷터가 2GB 초과 시 조용히 빈 컬렉션 기록 | `CheckedLength`로 `LengthLimit` 보고 |
| CMake 4.x MSVC 기본값에 `/EHsc` 부재 → `<chrono>` C4530 | 테스트·예제·샘플 타깃에 명시 |
| **unmanaged struct의 패딩이 그대로 전송됨** (미정값이면 스택 내용 유출 + 바이트 비재현) | 라이브러리에서 고칠 수 없으므로 `docs/security.md`에 명시, API 주석·회귀 테스트 추가 |

### 이번 범위에서 의도적으로 제외한 것

| 항목 | 이유 |
|------|------|
| `GenerateType.CircularReference` | 게임 패킷에서 드물고 객체 동일성 테이블이 필요. 로드맵에서도 P3 |
| PFR 스타일 자동 리플렉션 / C++ 모듈 | 로드맵 P3(선택/장기). `MEMORYPACK_DEFINE`으로 실용적 목적은 달성 |
| `Version`/`Uri`/`BigInteger`/`BitArray` | 게임 패킷에서 사용 사례 없음. 필요 시 픽스처 절차로 추가 가능 |
| 벤치마크 수치 게시 | 재현 가능한 하드웨어에서 정식 측정 전까지 숫자를 지어내지 않음. 하니스만 제공 |

---

## 0. 한눈에 보기

### 현재 상태 평가

| 영역 | 상태 | 요약 |
|------|------|------|
| 핵심 와이어 포맷 | 🟢 양호 | Object/Collection/String(UTF-8·UTF-16)/Primitive 는 실 C# MemoryPack 과 바이트 일치 검증됨 |
| C# 타입 커버리지 | 🟡 부족 | Union, Nullable\<T\>, unmanaged struct(memcpy), `List<사용자정의객체>`, Guid/DateTime, `bool[]` 등 **게임 패킷에서 흔한 타입이 미지원** |
| API 사용성 | 🟡 부족 | 타입마다 `IMemoryPackable<T>` 를 손으로 특수화해야 함. 제네릭 `Write/Read` 가 private. 컬렉션은 산술 타입만 가능 |
| 성능 | 🟡 보통 | 설계 방향(LE 고정 크기·memcpy)은 맞으나, `vector::insert/push_back` 경로·값 반환 API·문자열 복사 등 최적화 여지 큼. **벤치마크 부재** |
| 안정성/보안 | 🟡 보통 | 읽기 경계 검사는 있음. 그러나 예외 전용, 악의적 길이에 대한 `reserve` 폭주, size_t 오버플로 가능성, fuzzing 없음 |
| 테스트 | 🟡 보통 | 59 체크(자체 하니스). C#↔C++ 교차 검증은 **수동**(샘플 실행) — 자동화 없음. CI 없음 |
| 문서 | 🟡 보통 | 한국어 README 는 충실하나 **영문 부재**, 일부 잘못된 서술(부록 A), API 레퍼런스/타입 매핑 표 불완전 |
| cs2cpp | 🟡 보통 | 정규식 파서. 중첩 객체·enum 멤버·Nullable·Dictionary·Union 미지원, 테스트 없음 |
| 커뮤니티/배포 | 🔴 없음 | CI·릴리스 태그·CHANGELOG·CONTRIBUTING·패키지 매니저 등록 전무 |

### 우선순위 요약

| 우선순위 | 주제 | 관련 절 |
|---------|------|---------|
| **P0** (상용 적용 전 필수) | 정확성·안정성 결함 수정, C# 타입 커버리지 확장, 골든 픽스처 기반 자동 interop 테스트, CI | §1, §2-A, §4.3, §5 |
| **P1** (실전 사용성) | 보일러플레이트 제거(제네릭 Write/Read, 매크로), no-exceptions/`std::expected`, 성능 최적화+벤치마크, 패킷 프레이밍 헬퍼 | §2-B, §3, §4, §7 |
| **P2** (오픈소스 성장) | 영문 문서 체계, cs2cpp 고도화, 릴리스/패키지 매니저, 홍보 | §6, §8, §9 |
| **P3** (선택/장기) | C++ 모듈, PFR 스타일 자동 리플렉션, C++26 리플렉션 대비, 스키마 해시 | §2-B-5, §3.4, §6.3 |

---

## 1. 코드 리뷰에서 발견한 구체적 결함 (P0 — 즉시 수정)

아래는 `include/memorypack/memorypack.hpp` 를 정독하며 찾은 항목이다. 각 항목에 회귀 테스트를 추가한다.

### 1.1 `std::vector<bool>` 컴파일 불가 (🔴 버그)
- **위치**: `WriteVector` (`include/memorypack/memorypack.hpp:164`), `ReadVector` (`include/memorypack/memorypack.hpp:399`)
- **문제**: `std::vector<bool>` 은 비트 압축 특수화라 `.data()` 가 없다. C# `List<bool>` / `bool[]` 은 게임 패킷(플래그 배열)에서 흔한데, `WriteVector(std::vector<bool>)` / `ReadVector<bool>()` 이 컴파일조차 되지 않는다.
- **수정**: `if constexpr (std::is_same_v<T, bool>)` 분기로 바이트 단위 루프 처리(1요소 = 1바이트). 또는 `bool` 을 `static_assert` 로 막고 `std::vector<uint8_t>` 사용을 안내하되 편의 오버로드 제공.
- **완료 조건**: `test_collections` 에 `vector<bool>` 라운드트립 + 골든 바이트(`03 00 00 00 | 01 00 01`) 추가.

### 1.2 `ReadUnorderedMap` 의 무제한 `reserve` (🔴 DoS 취약)
- **위치**: `include/memorypack/memorypack.hpp:510`
- **문제**: 헤더에서 읽은 `len`(최대 2^31-1)으로 `result.reserve(len)` 을 **데이터 검증 전에** 호출한다. 악의적 클라이언트가 4바이트로 서버에 수 GB 할당을 유발할 수 있다. (`ReadStringVector` 는 `EnsureBytes(len*4)` 로 방어하고 있으나 `ReadUnorderedMap`/`ReadMap` 은 없음.)
- **수정**: 모든 컬렉션 읽기에서 **`len` 을 `Remaining() / 최소요소크기` 로 상한 검증** 후 reserve. 공통 헬퍼 `CheckCollectionLength(len, minElementSize)` 도입.
- **완료 조건**: "헤더 len=0x7FFFFFFF, 본문 0바이트" 입력에서 즉시 예외(또는 에러)로 종료하는 테스트.

### 1.3 `EnsureBytes` 의 정수 오버플로 (🟠 32비트 플랫폼 취약)
- **위치**: `include/memorypack/memorypack.hpp:606` — `if (pos_ + n > size_)`
- **문제**: `n = count * sizeof(T)` 가 32비트 `size_t` 에서 랩어라운드하면 검사를 통과하고 `memcpy` 가 버퍼 밖을 읽는다(모바일/콘솔 32비트 타깃, 또는 `-m32`).
- **수정**: `if (n > size_ - pos_)` 형태로 변경. `count * sizeof(T)` 계산 전에 `count > (size_ - pos_) / sizeof(T)` 검사.
- **완료 조건**: 단위 테스트 + CI 에 `-m32` 빌드 1잡(가능하면).

### 1.4 `std::vector<T>` 직렬화 시 2GB 초과 캐스팅 (🟠, plans.md P4 이월)
- **위치**: `WriteVector`, `WriteStringVector`, `WriteMap`, `WriteString` 의 `static_cast<int32_t>(size())`
- **수정**: `size() > INT32_MAX` 이면 예외/에러. 공통 헬퍼 `ToInt32Length(size_t)`.

### 1.5 `WriteObjectHeader` 가 예약 값을 허용 (🟠 프로토콜 오류 방지)
- **위치**: `include/memorypack/memorypack.hpp:120`
- **문제**: MemoryPack 은 멤버 수 **최대 249** (250~254 예약, 255 = null). 250 이상을 쓰면 C# 측이 Union/예약 코드로 오해한다.
- **수정**: `assert(memberCount <= 249)` + 디버그 빌드 검사. `WriteTuple` 도 `sizeof...(Ts) <= 249` `static_assert`.

### 1.6 UTF-16 디코딩의 서로게이트 검증 누락 (🟡)
- **위치**: `ReadUtf16String` (`include/memorypack/memorypack.hpp:556`)
- **문제**: high surrogate 뒤 유닛이 low surrogate 범위(DC00~DFFF)인지 확인하지 않고 합성한다. 고아 서로게이트는 3바이트 "WTF-8" 로 출력되어 잘못된 UTF-8 이 된다.
- **수정**: low 범위 검증, 실패 시 U+FFFD 치환(또는 옵션으로 에러).

### 1.7 `Deserialize<T>` 가 잔여 바이트를 검사하지 않음 (🟡)
- **문제**: 본문이 예상보다 길어도(스키마 불일치 징후) 조용히 성공한다. 게임 서버 개발 중 C#/C++ 멤버 순서 실수를 조기에 잡기 어렵다.
- **수정**: `DeserializeExact<T>()` 또는 옵션 `requireFullConsumption` 추가. 디버그 빌드에서는 기본 경고.

### 1.8 누락 특수화가 **링커 에러**로 나타남 (🟡 DX)
- **위치**: `IMemoryPackable` 기본 템플릿 (`include/memorypack/memorypack.hpp:614`) — 선언만 있고 정의 없음.
- **문제**: 특수화를 빼먹으면 "unresolved external symbol IMemoryPackable<Foo>::Serialize" 라는 이해하기 어려운 링커 에러가 난다.
- **수정**: 기본 템플릿을 `static_assert(detail::always_false<T>, "IMemoryPackable<T> is not specialized for T. See docs/serialization.md")` 로 정의하거나, `MemoryPackable` concept 을 도입해 컴파일 타임에 명확한 메시지 출력.

### 1.9 `ReadVector` 이중 초기화 (🟢 성능)
- `std::vector<T> result(count)` 로 0 초기화 후 memcpy → `result.assign(first, last)` 로 단일 패스.

### 1.10 문서/설정 불일치 (🟡, 부록 A 참조)
- 저장소 URL: README·`CMakeLists.txt` 의 `heungbae/MemoryPackCpp` ↔ 실제 origin `jacking75/MemoryPackCpp`.
- README "MSVC v143(VS2022)" ↔ `.vcxproj` `PlatformToolset v144`.
- CLAUDE.md "unmanaged struct 는 packed struct" — **틀림**(§2-A-3 참고).
- README/CLAUDE.md "C# 이 UTF-16 사용 시 C++ 도 맞춰야 함" — **불필요**(C# 리더는 헤더 부호로 UTF-8/UTF-16 을 자동 판별하므로 C++ 이 항상 UTF-8 로 써도 됨).

---

## 2. 기능 확장

### 2-A. C# 타입 커버리지 완성 (P0)

게임 패킷에서 실제로 쓰이는 C# 타입 중 미지원 항목. **모든 항목은 실제 C# MemoryPack 출력(골든 바이트)으로 먼저 포맷을 확정한 뒤 구현한다(§5.1 FormatProbe).** 아래 "포맷" 은 MemoryPack 소스 기준의 예상이며 실측으로 확정해야 한다.

#### 2-A-1. 중첩 객체 컬렉션 — `List<T>` / `T[]` (T = `[MemoryPackable]` 클래스) 🔴 최우선
- **현황**: `WriteVector/ReadVector` 가 `std::is_arithmetic_v<T>` 로 제한. `List<ItemInfo>` 같은 가장 흔한 패킷을 만들 수 없다(수동 루프 필요).
- **포맷**: `[int32 count][obj0][obj1]...` — 각 요소는 자체 Object Header 포함.
- **API**:
  ```cpp
  template<typename T> void WriteCollection(const std::vector<T>& v);   // 모든 직렬화 가능 T
  template<typename T> std::vector<T> ReadCollection();
  template<typename T> void ReadCollection(std::vector<T>& out);         // in-place
  ```
  내부적으로 `T` 가 산술이면 기존 bulk memcpy 경로, 아니면 `WriteValue/ReadValue` 루프. `vector<vector<T>>`, `vector<string>`, `vector<optional<T>>` 도 같은 경로로 자연 지원.
- **완료 조건**: 골든 픽스처 `List<Item>`(문자열 포함 객체) 양방향 일치.

#### 2-A-2. Union (`[MemoryPackUnion]`) → `std::variant` 🔴
- **포맷**: `WriteUnionHeader(ushort tag)`: tag < 250 이면 `[1B tag]`, 아니면 `[1B 250][2B tag]`; null 은 `[1B 255]`. 헤더 뒤에 구체 타입이 **자체 Object Header 를 포함해** 직렬화된다. (현재 README 의 "Union: [1B 250][2B tag]" 는 wide tag 경우만 서술 — 수정 필요.)
- **API**:
  ```cpp
  void WriteUnionHeader(uint16_t tag);  void WriteNullUnionHeader();
  std::optional<uint16_t> ReadUnionHeader();            // nullopt = null
  template<typename... Ts> void WriteUnion(const std::variant<Ts...>&);
  template<typename... Ts> std::variant<Ts...> ReadUnion();
  ```
  태그 매핑은 `MemoryPackUnionTag<T>::value` 특성 또는 `MEMORYPACK_UNION(Base, (0, A), (1, B))` 매크로로 선언. cs2cpp 가 자동 생성(§6).
- **완료 조건**: C# `[MemoryPackUnion(0, typeof(A))] [MemoryPackUnion(1, typeof(B))] partial interface IShape` 골든 픽스처 양방향 일치, wide tag(≥250) 케이스 포함.

#### 2-A-3. Unmanaged struct (memcpy 계열) 🔴
- **포맷**: C# 에서 참조 타입을 전혀 포함하지 않는 `[MemoryPackable] struct`(예: `Vector3`, `struct Pos { int X; float Y; }`)는 **Object Header 없이 `Unsafe.SizeOf<T>()` 바이트를 통째로 복사**한다. 패딩 포함, `LayoutKind.Sequential` 기본 정렬. **`#pragma pack(1)` 이 아니다** — C++ 도 자연 정렬 표준 레이아웃 struct 를 쓰고 `static_assert(sizeof(T) == N)` 으로 크기를 고정한다. C# 에 `[StructLayout(Pack=1)]` 이 있을 때만 C++ 도 `#pragma pack(push,1)`.
- **API**:
  ```cpp
  template<typename T> requires std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>
  void WriteUnmanaged(const T& v);      // LE 에서 memcpy. BE 는 필드별 스왑이 불가하므로 static_assert 로 거부하거나 사용자 스왑 훅 제공
  template<typename T> T ReadUnmanaged();
  template<typename T> void WriteUnmanagedVector(const std::vector<T>&);   // List<Vector3> = [int32 n][n*sizeof(T)]
  ```
  옵트인 마커 `template<> struct IsUnmanaged<Vec3> : std::true_type {}` 또는 `MEMORYPACK_UNMANAGED(Vec3, 12)`.
- **주의**: `bool` 은 1B, `char` 는 2B(UTF-16 유닛). `decimal`/`Guid`/`DateTime` 도 이 범주.
- **완료 조건**: 패딩이 있는 struct(`byte, int`)·`Vector3`·`List<Vector3>` 골든 픽스처 일치. `sizeof` 검사 테스트.

#### 2-A-4. `Nullable<T>` (C# `int?`, `float?`, `Vector3?`) 🟠
- **포맷 후보**: (a) `NullableFormatter` 방식 `[1B: 255=null | 1=hasValue][T]`, (b) unmanaged 로 취급되어 `Nullable<T>` 메모리 레이아웃(`bool hasValue + 패딩 + T`) memcpy. 소스 생성기가 멤버를 unmanaged 로 판정하는지에 따라 다르므로 **반드시 FormatProbe 로 실측**(멤버로 쓰일 때와 최상위로 쓰일 때 모두).
- **API**: `WriteNullable(const std::optional<T>&)` / `std::optional<T> ReadNullable<T>()`.

#### 2-A-5. Nullable 참조 객체 → `std::optional<T>` / `std::unique_ptr<T>` 🟠
- **현황**: `PeekIsNull()` 만 있고 편의 API 없음. `Serialize(const T*)` 가 null 을 받지만 읽기 쪽 대칭 API 가 없다.
- **API**: `WriteOptional(const std::optional<T>&)`, `std::optional<T> ReadOptional<T>()`, `unique_ptr`/`shared_ptr` 오버로드.

#### 2-A-6. `Dictionary`/`HashSet`/`KeyValuePair` 골든 검증 🟠
- 현재 `WriteMap/ReadMap` 은 C++ 라운드트립만 테스트됨. C# `Dictionary<int,string>`, `Dictionary<string, Item>`, `HashSet<int>` 골든 픽스처로 확정. `std::set`/`std::unordered_set` 지원 추가. `KeyValuePair<K,V>` 가 unmanaged 일 때 memcpy 인지 확인.

#### 2-A-7. `ValueTuple` vs `Tuple` 🟠
- 현재 `WriteTuple` 은 `[1B count][items]` 를 쓴다. C# `Tuple<>`(클래스)/참조 포함 `ValueTuple` 은 이 형식이 맞지만, **`(int, int)` 처럼 완전 unmanaged 인 ValueTuple 은 memcpy 될 가능성**이 높다 → FormatProbe 로 확정 후 `WriteTuple` 문서/동작 분기.

#### 2-A-8. 문자열 관련 🟠
- `std::string_view` / `const char*` 오버로드 (`WriteString(std::string_view)`), `std::u16string_view`/`std::wstring`(Windows) 입력 시 UTF-16 포맷으로 직접 쓰기 옵션(`WriteStringUtf16`) — Unreal `FString` 사용자에게 유용.
- 읽기: `std::optional<std::string_view> ReadStringView()` — UTF-8 페이로드를 **복사 없이** 버퍼를 가리키는 뷰로 반환(진짜 zero-copy). UTF-16 페이로드면 `nullopt` 반환하고 `ReadString()` 폴백 안내.
- `ReadString(std::string& out)` in-place 오버로드(버퍼 재사용).
- 쓰기 시 UTF-8 유효성 검증 옵션(디버그): 잘못된 UTF-8 을 보내면 C# 측 `utf16Length` 불일치로 역직렬화 실패.

#### 2-A-9. 기타 .NET 타입 (P1)
| C# | 포맷(예상, 실측 필요) | C++ 제안 |
|----|-----------|---------|
| `Guid` | 16B (.NET 내부 레이아웃: int32, int16, int16, byte[8]) | `memorypack::Guid` struct + 문자열 변환 |
| `DateTime` | 8B `ulong _dateData`(상위 2비트 Kind + 62비트 ticks) | `memorypack::DateTime` + `std::chrono` 변환 |
| `TimeSpan` | 8B ticks | `std::chrono::duration` 변환 |
| `DateTimeOffset` | 16B(DateTime 8B + short 2B + 패딩) | |
| `char` | 2B UTF-16 유닛 | `char16_t` |
| `decimal` | 16B | 128비트 struct(변환 헬퍼는 선택) |
| `Half` | 2B | `std::float16_t`(C++23) 또는 `uint16_t` |
| `Int128/UInt128` | 16B | `__int128` 또는 struct |
| `Memory<T>`/`ArraySegment<T>`/`ReadOnlyMemory<T>` | Collection 과 동일 | `std::span<const T>` 쓰기 |
| `System.Numerics.Vector2/3/4, Quaternion, Matrix4x4` | memcpy | 2-A-3 로 커버 |
| `Version`, `Uri`, `BigInteger`, `BitArray` | 별도 포맷 | P3 |

#### 2-A-10. VersionTolerant / CircularReference (P2)
- `[MemoryPackable(GenerateType.VersionTolerant)]` 은 멤버별 바이트 길이 정보가 추가되어 미지 멤버를 건너뛸 수 있는 포맷. 정확한 배치(고정 int32 vs VarInt, 헤더 뒤 일괄 vs 멤버 앞)는 MemoryPack 소스/실측으로 확정 후 `ReadVersionTolerantObjectHeader` 류 API 로 지원. 장기 운영 서비스의 프로토콜 진화에 유용하므로 P2.
- CircularReference 는 게임 패킷에서 드묾 → P3.

### 2-B. API 사용성 (P1)

#### 2-B-1. 공개 제네릭 `Write<T>` / `Read<T>` 와 concept 기반 디스패치
- 현재 private `WriteValue/ReadValue`(`include/memorypack/memorypack.hpp:297`, `:579`)를 public 으로 승격하고 `bool/산술/enum/string/optional/vector/array/map/set/tuple/variant/IMemoryPackable` 전부를 컴파일 타임 디스패치. 사용자는 `w.Write(anything)` 하나만 알면 된다.
- `memorypack::Serializable<T>` concept 정의 → 잘못된 타입에 명확한 에러.

#### 2-B-2. 보일러플레이트 제거 매크로
```cpp
struct PlayerState { int32_t id; float x, y, z; std::string name; };
MEMORYPACK_DEFINE(PlayerState, id, x, y, z, name);   // Serialize/Deserialize + version tolerance 자동 생성
```
- 멤버 수·순서를 한 곳에서 선언 → C# 과의 순서 불일치 실수를 줄임. 249 초과 `static_assert`.
- 기존 수동 특수화 방식은 그대로 유지(하위 호환).

#### 2-B-3. 값 반환 API 의 in-place 대안 (핫패스 할당 제거)
- `Serialize(const T&, std::vector<uint8_t>& out)`, `size_t SerializeTo(std::span<uint8_t>, const T&)`, `ReadVector(std::vector<T>&)`, `ReadString(std::string&)`, `Deserialize(std::span, T& out)`(있음) 정비.
- `MemoryPackWriter` 를 **이동 가능**하게(현재 `vec_ == &ownedBuffer_` 때문에 삭제됨 `include/memorypack/memorypack.hpp:112` — 이동 시 포인터 재설정으로 해결). 컨테이너/코루틴에서 보관 가능해짐.
- `std::span<std::byte>` / `char` 버퍼 생성자(asio 등 바이트 타입이 다른 네트워크 라이브러리 호환).

#### 2-B-4. 리더 상태·진단
- `ReadObjectHeader()` 반환을 이름 있는 struct(`ObjectHeader{ count, isNull }`)로.
- 에러에 **오프셋·기대 타입** 포함(§4.2).
- `Seek(pos)`, `Reset()`, `SubReader(len)`(중첩 길이 제한) 추가.

#### 2-B-5. (P3) 자동 리플렉션
- Boost.PFR 방식(구조화 바인딩 + arity 추론)으로 매크로 없이 aggregate 직렬화. C++26 리플렉션(P2996) 채택 시 정식 경로로 전환. 컴파일 시간·컴파일러 호환성 비용이 있으므로 `memorypack/reflect.hpp` 옵션 헤더로 분리.

#### 2-B-6. 헤더 구조
- `memorypack.hpp`(umbrella) 아래 `core.hpp`(writer/reader/primitive), `std_containers.hpp`, `dotnet_types.hpp`(Guid/DateTime), `union.hpp`, `packet.hpp`(§7) 로 분리. `<map>/<unordered_map>/<tuple>` 무조건 include 로 인한 컴파일 시간 증가 완화. 단일 헤더 배포본은 릴리스 시 스크립트로 합성.
- `MEMORYPACK_VERSION_MAJOR/MINOR/PATCH` 매크로 및 `memorypack::Version` 상수 추가(현재 없음).

---

## 3. 성능 (P1)

**원칙: 측정 없이 최적화하지 않는다.** 먼저 벤치마크를 만들고, 이후 각 항목의 개선 폭을 기록한다.

### 3.1 벤치마크 인프라 (`benchmarks/`)
- Google Benchmark 를 CMake `FetchContent` 옵션(`MEMORYPACK_BUILD_BENCHMARKS`)으로 도입.
- 시나리오: (a) 소형 패킷 `PlayerState`(5 멤버) 직렬화/역직렬화 100만 회, (b) `vector<int32>` 1K/64K 요소, (c) 문자열 중심 패킷(채팅), (d) 중첩 객체 리스트 `vector<Item>` 100개, (e) 고정 버퍼 vs 내부 vector vs 외부 vector.
- 기준선: raw `memcpy`, 그리고 가능하면 C# MemoryPack 자체 수치(BenchmarkDotNet)와 나란히 표기. 선택적으로 FlatBuffers/protobuf/msgpack-c 비교(README 의 설득 자료).
- 결과를 `docs/benchmarks.md` 에 표·차트로 기록, CI 에서 회귀 감시(선택).

### 3.2 Writer 핫패스
- 현재 `AppendByte → push_back`, `AppendBytes → vector::insert` (`include/memorypack/memorypack.hpp:313~333`). 모든 4/8바이트 쓰기가 `insert` 를 거친다.
- 개선: 내부/외부 vector 모드에서도 `size_t pos_` 를 별도로 관리하고 `EnsureCapacity(n)`(기하급수 증가) 후 `memcpy` + `pos_ += n`. 최종 `Size()` 반환 시 `vec_->resize(pos_)` 정합. 예상 개선: 소형 primitive 쓰기 2~4배(벤치로 확인).
- `WriteString` 의 `utf16_length_from_utf8` 는 O(n) 스캔이 불가피하나, ASCII 전용 fast path(8바이트 단위 고비트 검사)로 대부분의 게임 문자열을 가속.

### 3.3 Reader 핫패스
- `ReadVector` 이중 초기화 제거(§1.9), `ReadStringView` (§2-A-8) 로 문자열 복사 제거, `ReadCollection(out&)` 으로 재할당 제거.
- `EnsureBytes` 를 `[[unlikely]]` 힌트 + 뺄셈 형태로.

### 3.4 컴파일 시간
- 헤더 분리(§2-B-6), 템플릿 인스턴스 최소화. `-ftime-trace` 로 측정치 기록.
- (P3) C++23 `import memorypack;` 모듈 인터페이스 제공(MSVC/Clang).

---

## 4. 안정성 · 보안 (P0/P1)

### 4.1 예외 없는 모드 + `std::expected` (P1, Unreal/콘솔 필수)
- Unreal Engine·다수 콘솔 툴체인은 예외를 끈다. 현재 라이브러리는 모든 오류를 `std::runtime_error` 로 던진다.
- 설계:
  ```cpp
  enum class MemoryPackError { None, BufferUnderflow, BufferOverflow, InvalidHeader, LengthLimit, InvalidString, /*...*/ };
  template<typename T> std::expected<T, MemoryPackError> TryDeserialize(std::span<const uint8_t>);
  std::expected<int32_t, MemoryPackError> TryReadInt32();  // Try* 계열
  ```
  `MEMORYPACK_NO_EXCEPTIONS` 정의 시 throw 경로를 `MEMORYPACK_FAIL(err)`(리더를 error 상태로 전이 + 이후 읽기는 no-op)로 대체. 예외 모드에서는 `MemoryPackException : std::runtime_error` (에러 코드·오프셋 포함) 로 통일.
- **완료 조건**: `-fno-exceptions` 로 테스트 전체 빌드·통과하는 CI 잡.

### 4.2 진단 정보
- 오류 메시지에 `offset`, 읽으려던 타입, (가능하면) 현재 객체 타입명/멤버 인덱스 포함: `"buffer underflow at offset 17 while reading Int32 (PlayerState member #3)"`. 디버그 전용 컨텍스트 스택을 `MEMORYPACK_DEBUG_CONTEXT` 로 옵트인.

### 4.3 신뢰할 수 없는 입력에 대한 한도 (P0)
- `ReaderOptions{ maxCollectionLength, maxStringBytes, maxDepth }` — 기본값은 버퍼 크기 기반(§1.2), 서버에서는 패킷 정책에 맞춰 축소 가능.
- 모든 `reserve` 는 검증 후.

### 4.4 Fuzzing + Sanitizer
- `tests/fuzz/fuzz_deserialize.cpp` (libFuzzer, `-fsanitize=fuzzer,address,undefined`): 모든 `Read*` 와 샘플 패킷 타입 전체를 임의 바이트로 역직렬화. 크래시 0 을 목표.
- CI 에 ASan/UBSan 잡, 주 1회 스케줄 fuzz 잡(10분).
- MSVC `/analyze`, `clang-tidy`(`.clang-tidy` 커밋), `cppcheck` 를 CI 에 추가.

### 4.5 경고 무결성
- GCC/Clang: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`, MSVC: `/W4 /WX /permissive-` 로 라이브러리 헤더가 깨끗함을 CI 로 보장. 소비자 측 `-Wconversion` 에서 경고가 나지 않도록 `int32_t↔size_t` 캐스팅 정리.
- C4819(비 UTF-8 코드페이지) 대응: 헤더 주석을 ASCII 로 유지(현재 `ReadStringVector` 에 한국어 주석 1곳 있음 `include/memorypack/memorypack.hpp:448` — 제거) 또는 BOM. plans.md P4 이월.

### 4.6 스레드 안전성 명문화
- Writer/Reader 는 인스턴스별 비공유, 전역 상태 없음 → 문서에 명시. `IMemoryPackable` 특수화가 전역 가변 상태를 갖지 않을 것을 가이드.

---

## 5. 테스트 · CI (P0)

### 5.1 골든 픽스처 기반 자동 interop 테스트 (핵심)
현재 C#↔C++ 교차 검증은 샘플을 손으로 실행해야 한다. 이를 **커밋된 바이너리 픽스처**로 자동화한다.

- `tools/FormatProbe/`(C# 콘솔, MemoryPack 버전 고정): 검증 대상 타입 전부(§2-A 의 모든 항목 + 기존 타입)를 직렬화해 `tests/fixtures/<case>.bin` + `tests/fixtures/manifest.json`(타입, 값, MemoryPack 버전) 생성.
- C++ `tests/interop_tests.cpp`: 각 픽스처를 (1) 역직렬화해 값 비교, (2) 다시 직렬화해 바이트 동일성 비교.
- 역방향: C++ 이 생성한 바이트를 C# xunit 프로젝트(`tests/InteropTests.CSharp/`)가 역직렬화해 검증. CI 에서 C++ 테스트가 `tests/fixtures/out/*.bin` 을 쓰고 `dotnet test` 가 읽는다.
- MemoryPack 버전은 `Directory.Packages.props` 로 고정(현재 샘플은 `1.*` 부동)하고, 별도 잡에서 `latest` 로도 실행해 **상위 포맷 변경을 조기 감지**.
- **완료 조건**: `docs/compatibility.md` 에 "MemoryPack vX.Y.Z 기준 N개 타입 바이트 일치" 자동 생성 표.

### 5.2 단위 테스트 보강
- 누락: `Deserialize(const uint8_t*, size_t, T&)`, `ReadStringVector` 의 null 요소, `ReadObjectHeader` 250(WideTag)/예약값, enum 음수/대형 underlying, `float` NaN/Inf 비트 보존, 빈 객체(멤버 0), `TakeBuffer` 후 재사용, 외부 vector 에 헤더 선기록 후 이어쓰기(README 예제 그대로), map/tuple/enum 골든 바이트.
- 테스트 하니스는 의존성 없는 현재 방식을 유지하되 `TEST_CASE` 매크로·섹션 카운트·실패 시 hex dump 출력을 추가. (필요하면 doctest 단일 헤더를 `FetchContent` 옵션으로.)

### 5.3 CI 매트릭스 (`.github/workflows/ci.yml`)
| 잡 | 내용 |
|----|------|
| windows-msvc | VS2022 v143 + VS2026 v144, Debug/Release, `/W4 /WX`, ctest |
| linux-gcc | GCC 13/14, `-Werror`, ctest |
| linux-clang | Clang 17/18 + libc++, ctest |
| macos | Apple Clang(Xcode 15+), ctest |
| sanitizers | Clang ASan+UBSan, 테스트 + 픽스처 |
| no-exceptions | `-fno-exceptions -fno-rtti` 빌드(§4.1) |
| big-endian | QEMU s390x(docker `s390x/ubuntu` + qemu-user-static) 에서 ctest → `endian_convert` 실증 |
| dotnet | `dotnet build` (샘플 2종 + cs2cpp) + FormatProbe 픽스처 재생성 후 diff 0 검증 + C# interop 테스트 |
| samples | `-DMEMORYPACK_BUILD_SAMPLES=ON` 빌드 |
| cs2cpp-check | 샘플 `Packets.cs` → 생성 헤더가 커밋본과 동일한지(`--check`) |
| docs | 예제 코드(`examples/*.cpp`) 컴파일, 링크 체크 |
- 배지 3종(CI, codecov 선택, release)을 README 상단에.

---

## 6. cs2cpp 도구 고도화 (P1~P2)

### 6.1 파서 교체: 정규식 → Roslyn
- 현재 `tools/cs2cpp/Program.cs` 의 `Regex` 파서는 `public partial class` 만, `{ get; set; }` 프로퍼티만 인식. record/struct/필드/`init`/`required`/속성 인자/중첩 클래스/제네릭을 놓친다.
- `Microsoft.CodeAnalysis.CSharp` 로 구문 트리 기반 파싱. 입력은 파일·디렉터리·glob 다중 지원, `--namespace`, `--output-dir`, `--check`(CI 용), `--verbose`.

### 6.2 타입 지원 확대 (생성기)
| C# 요소 | 현재 | 목표 |
|---------|------|------|
| 중첩 `[MemoryPackable]` 멤버 | `/* TODO */` 출력 | `IMemoryPackable<T>::Serialize` 호출 생성 |
| `List<사용자 타입>` | 미지원 | `WriteCollection/ReadCollection` |
| enum 멤버 | 미지원 | `WriteEnum/ReadEnum` |
| `int?` 등 Nullable | 미지원 | `std::optional` + 확정된 포맷 |
| `Dictionary<K,V>`, `HashSet<T>` | 미지원 | `WriteMap`/set |
| `[MemoryPackable] struct`(unmanaged) | 미지원 | `WriteUnmanaged` + `static_assert(sizeof)` + `StructLayout(Pack)` 반영 |
| `[MemoryPackUnion]` | 미지원 | `std::variant` + 태그 매핑 |
| `[MemoryPackOrder]`, `[MemoryPackIgnore]`, `[MemoryPackInclude]` | 미지원 | 순서/포함 규칙 반영 |
| `GenerateType.VersionTolerant` | 미지원 | 포맷 확정 후 |
| `Guid/DateTime/TimeSpan` | 미지원 | §2-A-9 타입 |
| 매크로 출력 모드 | — | `MEMORYPACK_DEFINE(...)` 한 줄로 생성하는 `--style=macro` |

### 6.3 게임 서버용 부가 생성물
- **패킷 디스패치 테이블**: `enum PacketId` ↔ 타입 매핑을 읽어 `template<typename F> bool DispatchPacket(PacketId, std::span<const uint8_t>, F&&)` 생성 → 서버/클라이언트에서 `switch` 보일러플레이트 제거.
- **스키마 해시**: 모든 패킷 정의(이름·멤버 타입·순서)의 해시를 C#/C++ 양쪽 상수로 생성 → 접속 시 교환해 프로토콜 불일치를 즉시 검출.
- `static_assert` 로 멤버 수/크기 검증 코드 삽입.

### 6.4 품질
- xunit 스냅샷 테스트(`tools/cs2cpp.Tests/`): 입력 `.cs` → 기대 `.hpp` 비교. 샘플 2종의 생성 결과가 커밋본과 동일한지 CI 검사(§5.3).
- `dotnet tool` 패키징(`dotnet tool install -g MemoryPackCpp.Cs2Cpp`), MSBuild 타깃 예제(빌드 시 자동 생성).
- 영문 README.

---

## 7. 실전 통합 헬퍼 · 샘플 (P1)

### 7.1 `memorypack/packet.hpp` (선택적 헤더)
샘플 4종이 각자 구현하는 `[2B id][4B len]` 프레이밍을 라이브러리 옵션 헤더로 제공. 코어는 순수 직렬화로 유지.
```cpp
struct PacketHeader { uint16_t id; int32_t bodyLength; };   // 6B, LE
template<typename T> void WritePacket(MemoryPackWriter&, uint16_t id, const T& body);  // 헤더 자리 예약 → 본문 → 길이 패치
class PacketFrameParser { /* TCP 스트림 재조립: Feed(span) → 완성 프레임 콜백, 최대 길이 검증 */ };
```
- 헤더 형식은 템플릿 정책으로 교체 가능하게(예: `[4B len][2B id]`, 압축/암호화 플래그 등).

### 7.2 샘플 추가/정비
- **C++ 서버 ↔ C# 클라이언트** 샘플(사용자 맥락인 "C++ 게임 서버 + Unity/C# 클라이언트" 방향). 크로스플랫폼 콘솔 서버(raw 소켓 또는 standalone asio `FetchContent`), 다중 접속, `packet.hpp` + cs2cpp 디스패처 사용.
- ChatClient(Win32 전용) 외에 **크로스플랫폼 콘솔 채팅 클라이언트** 추가 → Linux/macOS CI 에서 샘플 전부 빌드.
- 샘플의 `packets.hpp` 를 cs2cpp 생성물로 일원화하고 CI 에서 동일성 검사.
- 샘플 포트는 개발 규칙(TCP 25001~25199)에 맞게 변경 검토(현재 9000/9001). 문서 동시 갱신.
- Unreal Engine 통합 노트: `FString`↔UTF-8/UTF-16, 예외 비활성 빌드(§4.1), `TArray` 어댑터 예시(`docs/integration-unreal.md`). Unity 는 C# MemoryPack 그대로 → "Unity 클라이언트 ↔ 이 라이브러리 C++ 서버" 가이드.

---

## 8. 문서화 (P1~P2)

### 8.1 구조 개편
```
README.md            # 영문 (기본). 가치 제안 → 30초 Quick Start → 지원 타입 표 → 벤치 → 링크
README.ko.md         # 한국어 (현재 README 를 이관·갱신)
docs/
  wire-format.md     # 포맷 명세 전체(Object/Collection/String/Union/Nullable/Unmanaged/VersionTolerant), 헥스 예시
  type-mapping.md    # C# ↔ C++ 전체 매핑 + 지원 상태(✅/🚧/❌) — 부록 B 를 기준으로 유지
  serialization.md   # IMemoryPackable 수동 특수화, MEMORYPACK_DEFINE, 버전 관용(cnt>=N) 규칙, null 처리
  performance.md     # 버퍼 재사용, 고정 버퍼, string_view, in-place 읽기, 벤치 결과
  security.md        # 신뢰 불가 입력, 한도 옵션, fuzzing 현황, 취약점 신고 절차
  error-handling.md  # 예외/expected/no-exceptions 모드
  compatibility.md   # 검증된 MemoryPack 버전·컴파일러 매트릭스(CI 자동 생성)
  faq.md             # "왜 string 헤더가 int32 2개인가", "VarInt 가 없는 이유", "링커 에러 unresolved IMemoryPackable", "C4819"
  integration-unreal.md / integration-unity.md
  cs2cpp.md          # 도구 문서 영문판(현재 tools/cs2cpp/README.md 는 한국어)
samples/README.md    # 샘플 실행 가이드(현재 README 의 샘플 절 이관)
examples/            # 컴파일되는 짧은 예제(packet framing, nested, union, optional, fixed buffer) — CI 빌드
CHANGELOG.md, CONTRIBUTING.md, CODE_OF_CONDUCT.md, SECURITY.md
```
- API 레퍼런스: Doxygen(`docs/Doxyfile`) → GitHub Pages 자동 배포. 헤더 주석을 Doxygen 형식으로 정리(현재 일부만).

### 8.2 README(영문) 핵심 구성
1. 한 문장 가치 제안: "Header-only C++23 library that speaks Cysharp MemoryPack's wire format byte-for-byte — for C++ game servers/clients talking to C#/Unity."
2. 배지(CI/License/Release/vcpkg), 비제휴 고지(Cysharp 원 프로젝트 링크).
3. 30초 Quick Start(`MEMORYPACK_DEFINE` 한 줄 버전).
4. 지원 타입 표(✅/🚧/❌), 컴파일러/플랫폼 표.
5. 벤치마크 표 1개.
6. cs2cpp 소개 5줄.
7. 문서 링크, 로드맵 링크, 기여 안내.

### 8.3 기존 문서 정정 목록 → 부록 A

---

## 9. 오픈소스 성장 · 배포 (P2)

### 9.1 릴리스 체계
- SemVer, `v0.1.0` 태그(현재 상태) → §1 수정 후 `v0.2.0` → §2-A/§4 완료 시 `v1.0.0-rc`. `CHANGELOG.md`(Keep a Changelog).
- GitHub Release 에 단일 헤더 아티팩트(`memorypack.hpp` 합성본) 첨부, 릴리스 워크플로 자동화.

### 9.2 패키지 매니저
- vcpkg 포트 PR(가장 효과 큼), Conan Center 레시피, CPM.cmake 스니펫, xmake/Meson wrap(선택). `find_package(memorypack)` 는 이미 동작.
- 패키지명 충돌 검토: CMake 프로젝트/타깃명 `memorypack` 은 향후 공식 C++ 포트가 나올 경우 충돌 소지 → `memorypackcpp` 로 바꿀지 v1.0 전에 결정.

### 9.3 저장소 위생
- `plans.md`, `working_log.md` 는 루트에서 `docs/dev/` 로 이동(또는 제거). `.editorconfig`, `.gitattributes`(`* text=auto`, `*.bin binary`), `.clang-format`, `.clang-tidy`, 이슈/PR 템플릿, `good first issue` 라벨, Discussions 활성화, 저장소 설명·토픽(`memorypack`, `serialization`, `cpp23`, `header-only`, `game-server`, `unity`, `csharp-interop`), 소셜 프리뷰 이미지.

### 9.4 홍보
- Cysharp/MemoryPack 저장소 Discussions/이슈에 포트 소개(공식 TypeScript 포트처럼 README 링크 요청 가능성).
- r/cpp, r/gamedev, 한국 게임 서버 커뮤니티, 기술 블로그 글("C# MemoryPack 을 C++ 게임 서버에서 쓰기" + 벤치 + 포맷 해설).
- Unreal/Unity 통합 문서와 데모 GIF.

---

## 10. 다음 세션 착수 순서 (구체 작업 단위)

각 단위는 독립 커밋 가능하며 완료 조건을 만족해야 닫는다.

| # | 작업 | 산출물 | 완료 조건 |
|---|------|--------|-----------|
| 1 | §1 결함 수정 일괄 (1.1~1.9) | `memorypack.hpp`, `tests/memorypack_tests.cpp` | 신규 회귀 테스트 포함 전체 통과, `/W4 /WX`·`-Werror` 무경고 |
| 2 | 부록 A 문서/설정 정정 | README, CLAUDE.md, CMakeLists.txt, vcxproj | URL·툴셋·포맷 서술 정확 |
| 3 | `tools/FormatProbe` + 픽스처 생성 | `tools/FormatProbe/`, `tests/fixtures/*.bin`, `manifest.json` | §2-A 의 모든 미확정 포맷(Nullable/ValueTuple/unmanaged/Union/Dictionary/bool[]/Guid/DateTime/VersionTolerant) 바이트 확보 및 `docs/wire-format.md` 초안에 기록 |
| 4 | interop 테스트 하니스 | `tests/interop_tests.cpp`, `tests/InteropTests.CSharp/` | 기존 지원 타입 전부 픽스처 양방향 통과 |
| 5 | CI 1차 | `.github/workflows/ci.yml` | Windows/Linux/macOS + dotnet 잡 녹색, README 배지 |
| 6 | §2-A-1 중첩 객체 컬렉션 + §2-B-1 공개 `Write/Read` | 헤더 | `List<Item>` 픽스처 통과 |
| 7 | §2-A-2 Union/variant, §2-A-3 Unmanaged, §2-A-4/5 Nullable/optional | 헤더 | 각 픽스처 통과 |
| 8 | §2-B-2 `MEMORYPACK_DEFINE` 매크로 | 헤더, examples | 샘플 `packets.hpp` 를 매크로 버전으로 재작성해도 픽스처 통과 |
| 9 | §4.1 no-exceptions + `std::expected`, §4.3 한도, §4.4 fuzz/sanitizer CI | 헤더, `tests/fuzz/`, CI | `-fno-exceptions` 잡·ASan 잡 녹색, fuzz 10분 크래시 0 |
| 10 | §3 벤치마크 + Writer/Reader 핫패스 최적화 | `benchmarks/`, `docs/benchmarks.md` | 최적화 전/후 수치 기록 |
| 11 | §6 cs2cpp Roslyn 전환 + 타입 확장 + 스냅샷 테스트 | `tools/cs2cpp/` | 샘플 생성물 CI 동일성 검사 통과 |
| 12 | §7 `packet.hpp` + C++ 서버 샘플 + 콘솔 채팅 클라이언트 | `include/memorypack/packet.hpp`, `samples/` | 전 플랫폼 CI 샘플 빌드 |
| 13 | §8 문서 체계(영문 README, docs/) + Doxygen Pages | 문서 | 링크 체크 통과, 예제 컴파일 잡 통과 |
| 14 | §9 릴리스 `v0.2.0`/`v1.0.0-rc`, vcpkg 포트, 홍보 | 태그, 릴리스 노트 | — |

> 권장: 1→2→3→4→5 를 먼저 끝내면 이후 모든 기능 작업이 "픽스처 통과" 라는 객관적 기준 위에서 진행된다.

---

## 부록 A. 현재 문서·설정의 오류/불일치 (정정 대상)

| 위치 | 현재 서술 | 정정 |
|------|-----------|------|
| README 설치 절, `CMakeLists.txt` `HOMEPAGE_URL` | `github.com/heungbae/MemoryPackCpp` | 실제 origin 은 `github.com/jacking75/MemoryPackCpp` |
| README 요구 사항 | "MSVC v143(VS2022) 이상" | 샘플 `.vcxproj` 는 `v144`(VS2026) 고정 → v143 으로 낮추거나 문서를 v144 로 통일 |
| CLAUDE.md 주의사항 4 | "Unmanaged Struct … C++에서 packed struct로 매핑" | **자연 정렬(패딩 포함) 표준 레이아웃**으로 매핑. `Pack=1` 은 C# 에 명시된 경우만 |
| README/CLAUDE.md 주의사항 3 | "C# 측에서 UTF-16 사용 시 C++도 맞춰야 함" | 불필요. C# 리더는 헤더 부호로 UTF-8/UTF-16 을 자동 판별. C++ 은 항상 UTF-8 로 써도 호환 |
| README Union 절 | "member_count = 250 → WideTag, `[1B 250][2B tag][body]`" | tag < 250 이면 `[1B tag][body]`, ≥250 이면 `[1B 250][2B tag][body]`, null 은 255. body 는 자체 Object Header 포함 |
| README Object 예시 | `03 00 00 00 ← string 바이트 길이 3` | 문자열 헤더는 `FC FF FF FF 03 00 00 00`(~3, utf16Len 3). 같은 절의 String 예시·테스트 골든 바이트와 불일치 |
| CLAUDE.md 설계 원칙 | "Zero-copy 설계" | 현재 리더는 `std::string/vector` 로 복사. `ReadStringView`/in-place API 도입 후에 zero-copy 라 표기 |
| README 특징 | "std::string_view 활용" | 현재 API 에 `string_view` 오버로드 없음 → 추가 후 유지 |
| CLAUDE.md, README 샘플 포트 | 9000/9001 | 전역 개발 규칙(TCP 25001~25199) 반영 여부 결정 후 동기화 |
| `include/memorypack/memorypack.hpp:448` | 한국어 주석 | ASCII 로 교체(C4819 방지) |

## 부록 B. C# ↔ C++ 타입 지원 현황 (2026-08-30 기준)

| C# | C++ | 상태 |
|----|-----|------|
| `bool, (s)byte, (u)short, (u)int, (u)long, float, double` | 대응 고정폭 타입 | ✅ 골든 검증 |
| `enum : T` | `enum class : T` | ✅ (골든 픽스처 추가 필요) |
| `string`(UTF-8/UTF-16) | `std::string` | ✅ 골든 검증 |
| `List<T>/T[]` (T 산술) | `std::vector<T>`, C 배열, `std::array` | ✅ 골든 검증 |
| `List<bool>/bool[]` | — | ❌ 컴파일 불가(§1.1) |
| `List<string>` | `std::vector<std::string>` | ✅ |
| `List<T>` (T 객체) | — | ❌ (§2-A-1) |
| `Dictionary<K,V>` | `std::map/unordered_map` | 🚧 C++ 라운드트립만 |
| `HashSet<T>` | — | ❌ |
| `Tuple<>`/`ValueTuple` | `std::tuple` | 🚧 unmanaged ValueTuple 포맷 미확정 |
| `[MemoryPackable] class` | `IMemoryPackable<T>` 수동 특수화 | ✅ |
| `[MemoryPackable] struct`(unmanaged) | — | ❌ (§2-A-3) |
| `Nullable<T>` | — | ❌ (§2-A-4) |
| nullable 참조 객체 | `Serialize(const T*)` 만 | 🚧 (§2-A-5) |
| `[MemoryPackUnion]` | — | ❌ (§2-A-2) |
| `Guid, DateTime, TimeSpan, decimal, char, Half, Int128` | — | ❌ (§2-A-9) |
| `VersionTolerant`, `CircularReference` | — | ❌ (§2-A-10) |
