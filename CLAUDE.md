# MemoryPackCpp

C#의 [MemoryPack](https://github.com/Cysharp/MemoryPack) Binary Wire Format과 호환되는 **C++ header-only 라이브러리**.
C# 서버와 C++ 클라이언트(또는 그 반대) 사이의 고성능 바이너리 직렬화/역직렬화를 목표로 한다.

## 프로젝트 구조

```
include/memorypack/
  memorypack.hpp       # 통합 헤더 (core + containers + dotnet)
  core.hpp             # 라이터/리더, primitive, 문자열, 오브젝트, 컬렉션, Union, unmanaged struct
  containers.hpp       # std:: 컨테이너, optional, 스마트 포인터, variant(Union)
  dotnet.hpp           # Guid, DateTime, TimeSpan, decimal, Half, Int128, Vector2/3/4
  packet.hpp           # 선택적 TCP 패킷 프레이밍 (통합 헤더에 미포함)
cmake/                 # 설치/패키지 config 템플릿
tests/
  memorypack_tests.cpp # 단위 테스트 (의존성 없는 자체 하니스)
  interop_tests.cpp    # C# 골든 픽스처와 바이트 단위 대조
  interop_types.hpp    # FormatProbe C# 타입의 C++ 미러
  test_harness.hpp     # 공용 테스트 매크로
  fixtures/            # 실제 C# MemoryPack 출력 (커밋된 골든 바이트)
  fuzz/                # libFuzzer 하니스
benchmarks/            # Google Benchmark 스위트
examples/              # 컴파일 가능한 예제 프로그램
docs/                  # 와이어 포맷 명세 및 사용자 문서
tools/
  FormatProbe/         # 실제 C# MemoryPack으로 골든 픽스처 생성 (.NET 10)
  cs2cpp/              # C# [MemoryPackable] 정의 → C++ 헤더 생성 (.NET 10)
  amalgamate.py        # 단일 헤더 합성
samples/
  CSharpServer/        # .NET 10 C# 직렬화 테스트 서버 (포트 25001)
  CppClient/           # C++ 콘솔 테스트 클라이언트 (포트 25001)
  ChatServer/          # .NET 10 C# 채팅 서버 (포트 25002)
  ChatClient/          # C++ Win32 GUI 채팅 클라이언트 (포트 25002)
vcpkg-port/            # vcpkg 포트 (오버레이로 사용 가능)
CMakeLists.txt         # 루트 빌드
```

## 핵심 설계 원칙
- **Header-only**: `#include "memorypack/memorypack.hpp"` 하나로 사용
- **크로스플랫폼**: Windows, Linux, macOS. 리틀/빅엔디안 모두 지원
- **성능 최우선**: memcpy 기반, VarInt 없음, Little-Endian 고정 크기
- **C++23 이상**: `std::span`, concepts, `std::expected`, structured bindings 활용
- **신뢰할 수 없는 입력에 안전**: 모든 읽기 경계 검사, 할당 전 길이 검증, `ReaderOptions` 한도
- **예외 없이도 동작**: `-fno-exceptions` 빌드에서 오류 상태 + `std::expected`로 보고

## 가장 중요한 규칙 — 와이어 포맷은 추측하지 않는다

**포맷에 관한 모든 주장은 실제 C# MemoryPack 출력으로 증명해야 한다.**

- `tools/FormatProbe`가 실제 MemoryPack 패키지(1.21.4 고정)로 직렬화한 바이트를
  `tests/fixtures/*.bin`에 커밋한다. `tests/fixtures/report.txt`에 주석 달린 헥스 덤프가 있다.
- `tests/interop_tests.cpp`가 양방향을 검증한다: C++ 리더가 C# 바이트를 올바르게 읽고,
  C++ 라이터가 **바이트 단위로 동일한** 출력을 낸다.
- `FormatProbe check-cpp`가 C++가 만든 바이트를 C#으로 되읽어 확인한다.

새 타입을 지원할 때 절차:
1. `tools/FormatProbe/Types.cs`에 C# 타입 추가, `FixtureCases.cs`에 케이스 추가
2. `dotnet run --project tools/FormatProbe -- generate tests/fixtures`
3. `tests/fixtures/report.txt`에서 실제 바이트 확인 — 이것이 근거다
4. 그 바이트에 맞춰 구현
5. `tests/interop_tests.cpp`와 `FormatProbe`의 `check-cpp`에 케이스 추가

## MemoryPack Wire Format 요약

전체 명세는 `docs/wire-format.md` (실제 캡처한 바이트 포함).

| 타입 | 포맷 | null |
|------|------|------|
| Primitive | Little-Endian 고정 크기, 헤더 없음 | - |
| Object | `[1B memberCount][members...]` | `[1B 255]` |
| Collection | `[4B int32 count][elements...]` | `[4B -1]` |
| String (UTF-8) | `[4B ~utf8ByteCount][4B utf16Length][utf8...]` | `[4B -1]` |
| String (UTF-16) | `[4B utf16Length][UTF-16LE units]` | `[4B -1]` |
| Union | tag<250: `[1B tag]`, 이상: `[1B 250][2B tag]`, 뒤에 값 | `[1B 255]` |
| Unmanaged struct | 구조체 바이트 그대로 (패딩 포함, 오브젝트 헤더 없음) | - |
| VersionTolerant | `[1B count][len0..lenN-1][members]` | `[1B 255]` |

- 오브젝트 헤더 예약값: **250~254**. 멤버는 최대 **249**개.
- VersionTolerant 멤버 길이: `<=127`은 1바이트, `<=65535`는 `0x84`+uint16, 그 외 `0x82`+uint32.

## 빌드

### 요구 사항
- C++23 컴파일러: MSVC v143(VS 2022) 이상, GCC 13+, Clang 16+
- CMake 3.21 이상
- .NET 10 SDK (C# 도구/샘플)

### 라이브러리 + 테스트
```bash
cmake -B build -DMEMORYPACK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

옵션: `-DMEMORYPACK_BUILD_SAMPLES=ON`, `-DMEMORYPACK_BUILD_BENCHMARKS=ON`,
`-DMEMORYPACK_BUILD_EXAMPLES=ON`

### 픽스처 재생성 / 검증
```bash
dotnet run --project tools/FormatProbe -- generate tests/fixtures
dotnet run --project tools/FormatProbe -- verify   tests/fixtures
```

### 단일 헤더
```bash
python tools/amalgamate.py --include-packet -o dist/memorypack.hpp
```

## 구현 시 주의사항

1. **멤버 순서**: MemoryPack은 이름 없이 선언 순서대로 직렬화. C#과 C++ 순서 반드시 일치
2. **엔디안**: 항상 Little-Endian. 빅엔디안에서는 라이브러리가 자동 스왑하지만
   **unmanaged struct 경로만은 리틀엔디안 전용**(컴파일 타임 차단)
3. **String**: C++는 항상 UTF-8로 쓴다. C# 리더가 헤더 부호로 인코딩을 판별하므로
   C#이 UTF-16을 쓰더라도 C++이 맞출 필요는 없다
4. **Unmanaged Struct**: C#에서 참조 타입이 없는 struct는 Object Header 없이 메모리 그대로 복사.
   **자연 정렬(패딩 포함) 표준 레이아웃**으로 매핑한다. `Pack=1`이 C#에 명시된 경우에만 `#pragma pack(1)`.
   `MEMORYPACK_UNMANAGED(T, size)`의 크기 단언이 레이아웃 어긋남을 컴파일 타임에 잡는다
5. **Version Tolerance**: 멤버는 **뒤에만 추가** 가능. 순서 변경/삭제는 호환 파괴.
   삭제가 필요하면 VersionTolerant 레이아웃 사용
6. **null 인코딩 4종**: `MyClass?`=`FF`, `string?`/`List<T>?`=`FFFFFFFF`,
   `int?`=Nullable 구조체 통째 복사. `std::optional<T>`가 T에 맞춰 자동 선택하지만
   `Nullable<managed struct>`만은 `WriteNullableObject`/`ReadNullableObject`로 명시
7. **헤더는 ASCII 전용**: 소비자가 비UTF-8 코드페이지(CP949 등)에서 include할 때
   MSVC C4819를 피하기 위함. 박스 드로잉 문자 금지

## 사용자 정의 타입 직렬화

권장 — 매크로 (전역 스코프에서 호출):
```cpp
struct PlayerState { int32_t id; float x, y, z; std::string name; };
MEMORYPACK_DEFINE(PlayerState, id, x, y, z, name)
```

특수한 경우 — 수동 특수화:
```cpp
namespace memorypack {
template<>
struct IMemoryPackable<MyPacket> {
    static void Serialize(MemoryPackWriter& w, const MyPacket* v) {
        if (!v) { w.WriteNullObjectHeader(); return; }
        w.WriteObjectHeader(2);
        w.WriteInt32(v->id);
        w.WriteString(v->name);
    }
    static void Deserialize(MemoryPackReader& r, MyPacket& v) {
        const auto h = r.ReadObjectHeader();
        if (h.isNull) return;
        if (h.count >= 1) v.id = r.ReadInt32();
        if (h.count >= 2) r.ReadString(v.name);
    }
};
}
```

기타: `MEMORYPACK_UNMANAGED(T, size)`, `MEMORYPACK_UNION_TAG(T, tag)`,
`MEMORYPACK_DEFINE_EMPTY(T)`

## 테스트 정책

- 동작을 바꾸는 변경에는 반드시 테스트를 추가한다
- 와이어 포맷을 바꾸는 변경에는 반드시 실제 C#에서 캡처한 픽스처를 함께 추가한다
- 라이브러리 헤더는 `/W4 /WX /permissive-`(MSVC), 테스트는 추가로
  `-Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion`로 무경고여야 한다

## 코딩 컨벤션
- 네임스페이스: `memorypack`
- 클래스/구조체: PascalCase (MemoryPackWriter, MemoryPackReader)
- 메서드: PascalCase (WriteInt32, ReadString)
- 변수: camelCase
- 상수: UPPER_SNAKE_CASE
- 들여쓰기: 4 spaces
- 헤더 가드: `#pragma once`
- 헤더 주석은 ASCII만 사용
