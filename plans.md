# 공개(Open Source) 준비 계획

> 작성: 2026-06-23 17:17 KST · 갱신: 2026-06-23 17:46 KST
> 목적: MemoryPackCpp 저장소를 외부에 공개하기 전 보완해야 할 항목 정리.
> 범례: ✅ 완료 · ⬜ 미진행

## 진행 현황

> 🔴→✅ **[코드 리뷰] 문자열 와이어 포맷 비호환(치명적) 수정 완료** — 자세한 내용은 아래 "문자열 포맷 수정" 절. C++↔실제 C# MemoryPack 양방향 바이트 일치 검증 완료.

| 항목 | 상태 |
|------|------|
| 🔴 문자열 와이어 포맷 C# 호환 수정 | ✅ (양방향 실측 검증) |
| P0-1 문서-구현 불일치 해소 (CMake 도입, A안) | ✅ |
| P0-2 `.gitignore` 보강 | ✅ |
| P0-3 `CLAUDE.md` 정리 (공개 유지, A안) | ✅ |
| P0-4 README 상태 배지 (stable) | ✅ |
| P1-1 CMake 빌드 시스템 | ✅ (P0-1과 함께 구현) |
| P1-2 단위 테스트 | ✅ (59 checks, 0 fail / MSVC 검증) |
| P1-4 컴파일러 최소 버전 명시 | ✅ |
| P1-3 CI 파이프라인 | ⬜ |
| P2 커뮤니티 자산 / README 언어·출처 | ⬜ |
| P3 선택 항목 | ⬜ |
| P4 코드 견고성 점검 | ⬜ (일부 ✅) |

---

## ✅ 이번 작업에서 완료

### 🔴 문자열 포맷 수정 (코드 리뷰 발견 — 치명적)
- **문제**: `WriteString`/`ReadString`이 `[int32 byteLen][utf8]`을 사용 → 실제 MemoryPack의 UTF-8 포맷 `[int32 ~byteCount][int32 utf16Length][utf8]`과 불일치. 문자열을 가진 모든 패킷과 채팅 샘플 전체가 실 C# 서버와 통신 불가였음.
- **수정**: `memorypack.hpp`의 `WriteString`/`ReadString`을 실제 MemoryPack 포맷으로 재구현. null(-1)/빈(0)/UTF-8(<=-2)/UTF-16(>0) 모두 처리, surrogate pair 포함 UTF-16↔UTF-8 변환과 utf16Length 계산 헬퍼 추가.
- **검증**:
  - 단위 테스트에 `test_string_format` 추가 — 실 MemoryPack 출력 기준 골든 바이트(멀티바이트·이모지 포함), 라운드트립, UTF-16 읽기. 전체 **테스트 통과(경고 0)**.
  - 별도 C++ 프로그램으로 **C++↔실제 C# MemoryPack 1.x 양방향 바이트 일치** 확인 (`LoginRequest{Username="Player1"}` → `02F8FFFFFF07000000506C61796572312A000000` 완전 일치, 역방향 역직렬화도 성공).
  - README / CLAUDE.md의 잘못된 String 포맷 설명 정정.


### P0-1. 문서-구현 불일치 해소 (A안: CMake 도입)
- 루트 `CMakeLists.txt` 추가 — header-only INTERFACE 타깃 `memorypack::memorypack`, `cxx_std_23` 요구, install/`find_package` 패키지 config 내보내기.
- `cmake/memorypackConfig.cmake.in` 추가.
- 이로써 README/CLAUDE.md의 "CMake 빌드 시스템 / 크로스플랫폼" 주장이 실제와 일치.
- **검증**: VS 2026(MSVC 14.51) + Ninja로 configure/build/test/install 전 과정 통과. `find_package`용 `memorypackTargets.cmake`, `memorypackConfig.cmake`, `memorypackConfigVersion.cmake` 정상 생성 확인.

### P0-2. `.gitignore` 보강
- Visual Studio(`.vs/`, `x64/`, `*.user` 등), .NET(`bin/`, `obj/`), CMake(`build/`, `out/`, `CMakeCache.txt` 등), 에이전트/에디터(`.claude/`, `.idea/`, `.vscode/`) 산출물 추가.

### P0-3. `CLAUDE.md` 정리 (A안: 공개 유지)
- 프로젝트 구조를 실제와 일치(4개 샘플 + `tools/cs2cpp` + `tests/` + `CMakeLists.txt`).
- 빌드 섹션에 요구 사항(컴파일러 버전)·CMake 빌드·테스트 실행·dotnet 빌드 추가.
- 미커밋 공백/EOF 개행 문제 정리.

### P0-4. README 상태 배지
- `status-in-development` → `status-stable`, License(MIT)·C++23 배지 추가.

### P1-2. 단위 테스트
- `tests/memorypack_tests.cpp` — 외부 의존성 없는 자체 하니스. primitives/string/object/collection/array/map/tuple/enum 라운드트립, **C# 호환 골든 바이트**, version tolerance, 고정 버퍼 오버플로·리더 underflow·잘못된 문자열 길이 등 경계/예외 케이스. **59개 체크 전부 통과.**
- `tests/CMakeLists.txt` — CTest 등록, `/W4 /permissive- /utf-8`(MSVC) / `-Wall -Wextra -Wpedantic`.
- 부수 수정: 테스트가 표면화한 **헤더 경고 C4244**(int→uint8_t) 1건을 `memorypack.hpp`에서 제거 → `/W4` 경고 0개.

### P1-4. 컴파일러 최소 버전 명시
- README 요구 사항에 **MSVC v143(VS2022) 이상, GCC 13+, Clang 16+**, CMake 3.21+ 명시. CLAUDE.md에도 반영.

### (부가) 샘플 CMake 빌드
- `samples/CMakeLists.txt` 추가 — CppClient(전 플랫폼, POSIX 소켓 폴백 있음), ChatClient(Windows 전용 GUI). `-DMEMORYPACK_BUILD_SAMPLES=ON`. **MSVC로 빌드 성공 확인.**

---

## ⬜ 남은 항목

### P1-3. CI 파이프라인 (GitHub Actions)
- `.github/workflows/ci.yml`: C++ 매트릭스(Windows MSVC / Linux GCC·Clang / macOS) CMake 빌드+`ctest`, C# `dotnet build`(서버·cs2cpp). 가능하면 빅엔디안(QEMU s390x) 1잡으로 `endian_convert` 실증.
- **왜**: 크로스플랫폼·호환성 주장을 매 커밋 자동 증명. (현재는 Windows/MSVC 로컬 검증만 완료.)

### P2. 커뮤니티 자산 / 정책 결정
- `CONTRIBUTING.md`, `CHANGELOG.md`(+ `v0.1.0` 태그/Release), 이슈·PR 템플릿, `.editorconfig`, `.gitattributes`(줄바꿈 정규화), `SECURITY.md`(선택).
- **원본 프로젝트 출처/비제휴 고지**: Cysharp/MemoryPack(MIT)의 와이어 포맷을 독립 구현한 비공식 프로젝트임을 README에 명시.
- **README 언어 결정**: 현재 전부 한국어. 국제 공개 시 영문 README 권장.

### P3. 선택
- 단일 헤더 릴리스 아티팩트, vcpkg/Conan 포트, 벤치마크, Doxygen + GitHub Pages.

### P4. 코드 견고성 점검
- ✅ C4244(int→uint8_t) 헤더 경고 제거(완료).
- ⬜ 쓰기 측 `size()`→`int32_t` 캐스팅 오버플로 방어(2GB 초과 입력).
- ⬜ 빅엔디안 경로 실증(P1-3 CI와 연계).
- ⬜ 라이브러리 헤더 C4819: 비UTF-8 코드페이지(예: CP949) 사용 소비자가 헤더 include 시 경고 가능. 대응 옵션 — 소비 측 `/utf-8` 권장 문구, 헤더에 UTF-8 BOM 추가, 또는 헤더 주석 ASCII화. (이번에 테스트·샘플 빌드에는 `/utf-8`을 적용해 경고 제거.)

---

## 제안 처리 순서
1. **P2 출처 고지 + README 언어 결정** — 공개 직전 가장 눈에 띄는 부분.
2. **P1-3 CI** — 크로스플랫폼 주장의 자동 실증.
3. **P2 나머지 커뮤니티 자산 + v0.1.0 태그/Release.**
4. **P3/P4**는 공개 후 점진 개선.
