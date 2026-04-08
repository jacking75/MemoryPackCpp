# MemoryPackCpp

C#의 [MemoryPack](https://github.com/Cysharp/MemoryPack) Binary Wire Format과 호환되는 **C++ header-only 라이브러리**.
C# 서버와 C++ 클라이언트 간 고성능 바이너리 직렬화/역직렬화를 목표로 한다.

## 프로젝트 구조

```
include/memorypack/    # header-only 라이브러리 (memorypack.hpp)
samples/
  CSharpServer/        # .NET 10 C# 소켓 서버 (Visual Studio 2026)
  CppClient/           # C++ 소켓 클라이언트 (CMake, Visual Studio 2026)
```

## 핵심 설계 원칙

- **Header-only**: `#include "memorypack/memorypack.hpp"` 하나로 사용
- **크로스플랫폼**: Windows, Linux, macOS 지원. CMake 빌드 시스템 사용
- **성능 최우선**: Zero-copy 설계, memcpy 기반, VarInt 없음, Little-Endian 고정 크기
- **C++23 이상**: std::optional, std::string_view, structured bindings 활용
- **Zero-encoding**: C# MemoryPack과 동일하게 메모리 레이아웃을 그대로 복사

## MemoryPack Wire Format 요약

| 타입 | 포맷 |
|------|------|
| Object | `[1B member_count] [members...]` (255=null) |
| Collection | `[4B int32 length] [elements...]` (-1=null) |
| String(UTF-8) | `[4B int32 byte_length] [utf8_bytes...]` (-1=null) |
| Primitives | Little-Endian 고정 크기 (bool=1B, int32=4B, float=4B, double=8B 등) |
| Union | member_count=250 이면 WideTag, 이어서 ushort 태그 |

## 빌드

### C++ 클라이언트 (Visual Studio 2026)
`samples/CppClient/CppClient.sln`을 Visual Studio 2026에서 열어서 빌드 (x64, C++23, PlatformToolset v144)

### C# 서버 (.NET 10)
```bash
cd samples/CSharpServer
dotnet build -c Release
```

## 구현 시 주의사항

1. **멤버 순서**: MemoryPack은 이름 없이 선언 순서대로 직렬화. C#과 C++ 순서 반드시 일치
2. **엔디안**: 항상 Little-Endian. Big-Endian 플랫폼에서는 바이트 스왑 필요
3. **String**: UTF-8 기본. C# 측에서 UTF-16 사용 시 C++도 맞춰야 함
4. **Unmanaged Struct**: C#에서 참조 타입 없는 struct는 Object Header 없이 메모리 직접 복사. C++에서 packed struct로 매핑
5. **Version Tolerance**: Deserialize 시 memberCount 체크로 새 멤버를 gracefully 무시

## 사용자 정의 타입 직렬화 (CRTP 패턴)

```cpp
// C++ 구조체 정의
struct MyPacket {
    int32_t id;
    std::string name;
};

// IMemoryPackable 특수화
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
        auto [cnt, isNull] = r.ReadObjectHeader();
        if (isNull) return;
        if (cnt >= 1) v.id = r.ReadInt32();
        if (cnt >= 2) { auto s = r.ReadString(); v.name = s.value_or(""); }
    }
};
}
```

## 샘플 프로그램

- **C# 서버** (.NET 10): 단일 클라이언트 TCP 소켓 서버. MemoryPack으로 패킷 직렬화/역직렬화
- **C++ 클라이언트**: TCP 소켓 클라이언트. 이 라이브러리로 패킷 직렬화/역직렬화
- **패킷 프로토콜**: `[2B packetId][4B bodyLength][body...]` 형식의 패킷 헤더
- 다양한 타입(primitive, string, collection, 중첩 객체)의 패킷을 교환하여 라이브러리 검증

## 코딩 컨벤션

- 네임스페이스: `memorypack`
- 클래스/구조체: PascalCase (MemoryPackWriter, MemoryPackReader)
- 메서드: PascalCase (WriteInt32, ReadString)
- 변수: camelCase
- 상수: UPPER_SNAKE_CASE
- 들여쓰기: 4 spaces
- 헤더 가드: `#pragma once`
