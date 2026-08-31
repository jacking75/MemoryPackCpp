# v0.3.0 하드닝 계획 — 검증 공백 메우기

> 작성: 2026-08-31 · 기준 커밋: `d3760f5` (main, clean)
> 범위: 2026-08-31 검증 리뷰에서 나온 미검증 항목 중 **#2 ~ #5**.
> **#1(GCC/Clang·Linux/macOS·32비트·빅엔디안 다중 플랫폼 실증)은 이번 범위에서 제외**한다.
> 목적: "돌려봤더니 통과하더라"를 "안 돌리면 커밋이 막힌다"로 바꾸는 것.

---

## 0. 착수 전 확인된 사실 (실측, 2026-08-31)

계획을 세우기 전에 실제로 확인한 것들이다. 다음 세션에서 다시 조사할 필요 없다.

| 사실 | 확인 방법 | 계획에 미치는 영향 |
|------|-----------|-------------------|
| **MSVC ASan 사용 가능** | `VC/Tools/MSVC/14.51.36231/lib/x64/clang_rt.asan_*.lib` 존재 | clang 없이도 Windows에서 `/fsanitize=address` 실행 가능 → §2-B가 추가 설치 없이 오늘 가능 |
| **clang-cl 없음** | VS의 `Tools/Llvm/x64/bin/`에 `clang-format.exe`, `clang-tidy.exe`만 있음 | libFuzzer는 Windows 네이티브로 불가 → WSL 경유 |
| **WSL Ubuntu 설치됨** | `wsl --list` → `Ubuntu`, `docker-desktop` | libFuzzer+ASan+UBSan 실행 경로 확보 (§2-C) |
| **테스트 소스에 raw `try`/`catch`/`throw` 0건** | `grep` 결과 0 | no-exceptions 타깃은 **빌드 플래그만** 바꾸면 컴파일됨 (§3) |
| **`test_harness.hpp`가 이미 no-exceptions 대응** | `MPTEST_HAS_EXCEPTIONS`, `MPTEST_TRY/CATCH`, `CHECK_FAILS` | 하니스 수정 불필요 |
| **`CHECK_THROWS` 사용처가 단 1곳**, `CHECK_FAILS`는 18곳 | `grep -c` | 1곳만 옮기면 예외/무예외 빌드의 체크 수가 동일해짐 |
| **`interop_tests.cpp`가 `<filesystem>`, `<fstream>` 사용** | `#include` 목록 | `_HAS_EXCEPTIONS=0`에서 `<filesystem>` 컴파일이 위험 → no-exceptions 타깃은 `memorypack_tests`만 |
| **`MEMORYPACK_PP_FOREACH`/`PP_NARG` 존재** (최대 32개) | `core.hpp` | §5의 패딩 매크로에서 그대로 재사용 가능 |
| **`cs2cpp`가 `MEMORYPACK_UNMANAGED`를 생성** | `tools/cs2cpp/CppGenerator.cs:275` | §5에서 매크로를 바꾸면 생성기·스냅샷·샘플 헤더를 함께 갱신해야 함 |
| **평범한 셸에는 `cl`도 `ninja`도 PATH에 없다** | 이번 세션에서 `build-bench` 구성이 실패, `vcvars64.bat` 경유로 성공 | §4 스크립트는 **VS 개발 환경을 스스로 찾아야** 한다 |
| **워킹 트리 clean, TODO/FIXME 0건** | `git status`, `grep` | 이 계획이 첫 변경분이 된다 |

---

## 1. 작업 순서와 규모

의존 관계상 이 순서를 권장한다. 각 단계의 산출물이 다음 단계에 흡수된다.

| 순서 | 항목 | 규모 | 왜 이 순서인가 |
|---|------|------|----------------|
| 1 | **§3 no-exceptions 회귀 타깃** | 0.5일 | 가장 싸고, 즉시 `ctest`에 편입된다. 나머지 작업 중 헤더를 건드릴 때 안전망이 된다 |
| 2 | **§2 퍼징 3층 구조** | 2~3일 | 리더를 실제로 두들긴다. 여기서 나온 크래시가 §5 설계에 영향을 줄 수 있다 |
| 3 | **§5 unmanaged 패딩 대응** | 2~3일 | 유일하게 공개 API가 바뀌는 항목. 픽스처 53개가 회귀 기준이 된다 |
| 4 | **§4 검증 자동화 스크립트** | 1~2일 | 1~3의 결과물을 전부 흡수해 한 명령으로 만든다. 마지막이어야 빠뜨리지 않는다 |

각 단계는 독립 커밋 가능하며, 아래 "완료 조건"을 만족해야 닫는다.

---

## 2. 퍼징 — 하니스는 있는데 한 번도 안 돌렸다

### 문제

`tests/fuzz/fuzz_deserialize.cpp`는 잘 만들어져 있다. 12개 디코더 경로(객체·중첩 컬렉션·map·set·union·optional·raw reader API·프레임 파서)를 셀렉터 바이트로 분기하고, `ReaderOptions`를 조여서 OOM과 진짜 버그를 구분한다. 그런데 **실행된 적이 없고, 코퍼스도 없고, 빌드 시스템에 연결돼 있지도 않다.** clang이 없는 머신에서는 존재 자체를 잊게 된다.

리더는 이 라이브러리에서 유일하게 신뢰할 수 없는 입력을 받는 부분이다. 경계 검사가 "테스트로 확인된" 수준이지 "퍼저에게 두들겨 맞은" 수준이 아니라는 게 현재 상태다.

### 목표

퍼징을 **선택적 수동 작업에서 상시 회귀 자산으로** 바꾼다. 세 층으로 나눈다.

- **A층 (코퍼스 리플레이)**: 컴파일러·새니타이저 무관. `ctest`에 항상 들어간다. 과거 크래시가 영원히 재발하지 않게 한다.
- **B층 (MSVC ASan)**: 이 머신에서 오늘 가능. 추가 설치 0. Windows에서 OOB를 잡는다.
- **C층 (WSL libFuzzer)**: 진짜 퍼징. 새 입력을 탐색한다. 리더를 건드릴 때만 돌린다.

핵심은 **C층에서 찾은 것이 A층으로 내려와 영구히 고정된다**는 흐름이다.

### 2-A. 코퍼스 리플레이를 ctest에 넣기

`fuzz_deserialize.cpp`는 `LLVMFuzzerTestOneInput`만 정의하고 `main`이 없다. 그래서 러너를 따로 두면 그대로 재사용된다.

**신규 `tests/fuzz/fuzz_replay.cpp`**

```cpp
// Replays a corpus directory through the libFuzzer entry point without
// libFuzzer, so every input that ever crashed the reader stays covered by
// `ctest` on any compiler. Not a fuzzer: it explores nothing, it only replays.
//
//   fuzz_replay <corpus-dir> [more-dirs...]
//
// Exit code 0 means every input was consumed without crashing. A crash here is
// a real bug; there is no "expected failure" - reader errors are swallowed by
// the harness itself.

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char** argv) { /* 디렉터리 순회 → 파일 읽기 → 호출 → 개수 출력 */ }
```

**`tests/CMakeLists.txt` 추가분**

```cmake
# -- Fuzz corpus replay -------------------------------------------------------
# The fuzz harness itself needs clang+libFuzzer, which is not available on every
# machine. Replaying the committed corpus needs neither, so the regression value
# of past crashes is always in `ctest`.
add_executable(memorypack_fuzz_replay fuzz/fuzz_replay.cpp fuzz/fuzz_deserialize.cpp)
memorypack_configure_test(memorypack_fuzz_replay)
add_test(NAME memorypack_fuzz_replay
         COMMAND memorypack_fuzz_replay "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus")
```

> 주의: `fuzz_deserialize.cpp`가 `/W4 /WX`를 통과하는지 확인할 것. 지금까지 한 번도 컴파일된 적 없으므로 경고가 남아 있을 가능성이 있다. 통과 못 하면 그 자체가 첫 수확이다.

**코퍼스 시드 — `tools/seed_fuzz_corpus.py` (신규)**

빈 코퍼스로 시작하면 퍼저가 유효한 헤더를 찾는 데만 한참 걸린다. 이미 53개의 **실제 C# MemoryPack 출력**(`tests/fixtures/*.bin`)이 있으니 이걸 시드로 쓴다. 단, 하니스의 첫 바이트는 타입 셀렉터이므로 앞에 붙여줘야 한다.

```python
# tests/fixtures/*.bin 각각에 대해 selector 0..11 중 의미 있는 것을 앞에 붙여
# tests/fuzz/corpus/<name>__sel<N>.bin 으로 쓴다.
# 셀렉터 대응표는 fuzz_deserialize.cpp 의 switch(selector % 12) 와 반드시 동기화.
```

셀렉터 매핑(현재 하니스 기준):

| sel | 타입 | 시드로 쓸 픽스처 예 |
|---|---|---|
| 0 | `Item` | `simple_packet.bin` |
| 1 | `Inventory` | `inventory.bin`, `dict_object_packet.bin` |
| 2 | `Everything` | `all_primitives.bin`, `many_members.bin` |
| 3 | `Deep` | `nested_object.bin`, `nested_list.bin` |
| 4 | `std::string` | `string_top_level*.bin` |
| 5 | `vector<int32>` | `int_list_top_level.bin`, `int_list_empty/null.bin` |
| 6 | `vector<string>` | `array_packet.bin` |
| 7 | `map<string,string>` | `dict_packet.bin` |
| 8 | `variant` | `union_top_level` 계열 |
| 9 | `optional<Item>` | `nullable_managed_holder*.bin` |
| 10 | raw reader | 아무거나 (전부) |
| 11 | frame parser | `simple_packet.bin` |

**완료 조건**
- `tests/fuzz/corpus/`에 시드가 커밋돼 있고 `ctest`가 13번째 테스트로 통과.
- `fuzz_deserialize.cpp`가 `/W4 /WX`로 무경고 컴파일.
- `python tools/seed_fuzz_corpus.py`가 멱등(재실행해도 diff 없음).

### 2-B. MSVC ASan 빌드 (설치 불필요, 오늘 가능)

**루트 `CMakeLists.txt`에 옵션 추가**

```cmake
set(MEMORYPACK_SANITIZE "" CACHE STRING
    "Sanitizers for the test targets: address, undefined, address+undefined (MSVC: address only)")
```

`tests/CMakeLists.txt`의 `memorypack_configure_test()` 안에서 처리:

```cmake
if(MEMORYPACK_SANITIZE)
    if(MSVC)
        # MSVC ships only AddressSanitizer. /RTC1 is incompatible with it, and
        # CMake's Debug flags add /RTC1 - build ASan in RelWithDebInfo.
        if(NOT MEMORYPACK_SANITIZE MATCHES "address")
            message(FATAL_ERROR "MSVC supports only -DMEMORYPACK_SANITIZE=address")
        endif()
        target_compile_options(${target} PRIVATE /fsanitize=address)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    else()
        target_compile_options(${target} PRIVATE -fsanitize=${MEMORYPACK_SANITIZE} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${MEMORYPACK_SANITIZE})
    endif()
endif()
```

**실행 (VS 개발자 셸에서)**

```bat
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DMEMORYPACK_BUILD_TESTS=ON -DMEMORYPACK_SANITIZE=address
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

**함정 3가지 (미리 적어둔다)**
1. `/RTC1`과 `/fsanitize=address`는 공존 불가 → `CMAKE_BUILD_TYPE=Debug` 금지, `RelWithDebInfo` 사용.
2. 실행 시 `clang_rt.asan_dynamic-x86_64.dll`이 PATH에 있어야 한다. VS 개발자 셸이면 자동, 아니면 `VC/Tools/MSVC/<ver>/bin/Hostx64/x64`를 PATH에 추가.
3. MSVC ASan에는 **UBSan이 없다.** UB 검출은 C층(WSL)에서만 된다. 이걸 문서에 명시해서 "ASan 돌렸으니 UB도 봤다"는 착각을 막을 것.

**완료 조건**: ASan 빌드에서 `ctest` 전체(리플레이 포함) 통과, 리포트 0건.

### 2-C. WSL libFuzzer로 실제 퍼징

```bash
wsl -d Ubuntu
sudo apt update && sudo apt install -y clang lld       # 최초 1회
cd /mnt/f/github/MemoryPackCpp

clang++ -std=c++23 -g -O1 -Iinclude \
    -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
    tests/fuzz/fuzz_deserialize.cpp -o /tmp/fuzz_deserialize

/tmp/fuzz_deserialize tests/fuzz/corpus \
    -max_total_time=1800 -rss_limit_mb=2048 \
    -artifact_prefix=/tmp/fuzz-artifacts/
```

> 이건 **퍼징 호스트로 WSL을 쓰는 것**이지, Linux를 지원 플랫폼으로 주장하는 게 아니다. #1은 여전히 범위 밖이다. (clang이 여기서 뱉는 경고는 참고용으로만 보고, 플랫폼 지원 주장에는 쓰지 않는다.)

**크래시를 찾았을 때의 절차** — 이게 이 절의 핵심이다.

1. `-minimize_crash=1 <artifact>`로 최소 재현 입력을 줄인다.
2. 줄인 파일을 `tests/fuzz/corpus/crash_<yyyymmdd>_<짧은설명>.bin`으로 **커밋**한다.
3. A층 리플레이가 이제 실패한다 → 이게 회귀 테스트다.
4. 헤더를 고친다.
5. `ctest` 통과 → 커밋. 픽스처 53개도 여전히 통과해야 한다(와이어 포맷을 바꾸지 않았다는 증명).

**`docs/security.md` 갱신**

"돌려보세요"에서 "언제 얼마나 돌렸다"로 바꾼다. 이 프로젝트의 "추측하지 않고 실측한다" 원칙을 퍼징에도 적용하는 것이다.

```markdown
### Fuzzing log

| Date | Host | Build | Execs | Corpus | Crashes |
|---|---|---|---|---|---|
| 2026-09-xx | WSL Ubuntu, clang <ver> | fuzzer+asan+ubsan | xxx M | NN files | 0 |
```

**완료 조건**
- 30분 세션 크래시 0건, 로그 표에 기록.
- 코퍼스가 커밋되고 A층이 그걸 리플레이한다.
- 크래시가 나왔다면: 수정 + 코퍼스 추가 + 픽스처 53개 여전히 통과.

---

## 3. no-exceptions 빌드 — 회귀 방지 장치가 없다

### 문제

`docs/compatibility.md`는 `_HAS_EXCEPTIONS=0`을 "✅ built and tested"라고 적고 있다. 사실이었겠지만 **한 번 수동으로 확인한 것**이고, CMake 타깃도 `ctest` 항목도 없다. 헤더를 고치다 이 경로가 깨져도 아무도 모른다. Unreal Engine과 일부 콘솔 툴체인이 이 모드로 빌드하므로, 조용한 회귀는 곧 "언리얼에서 안 됨"이 된다.

### 다행인 점

조사해보니 거의 공짜다.
- 테스트 소스에 raw `try`/`catch`/`throw`가 **0건**이다. 전부 하니스 매크로를 거친다.
- `test_harness.hpp`가 이미 `MPTEST_HAS_EXCEPTIONS`로 분기하고, `CHECK_FAILS`는 예외/에러상태 양쪽을 받는다.

즉 **빌드 플래그만 바꾼 두 번째 타깃**이면 된다.

### 구현

**`tests/CMakeLists.txt` 추가분**

```cmake
# -- The same unit tests, built without C++ exceptions -------------------------
# Unreal Engine and some console toolchains disable exceptions; the library then
# reports through the reader/writer error state and std::expected. Without this
# target that path regresses silently.
#
# Only the unit tests: interop_tests.cpp uses <filesystem>, which MSVC's STL does
# not support with _HAS_EXCEPTIONS=0.
add_executable(memorypack_tests_noexcept memorypack_tests.cpp)
target_link_libraries(memorypack_tests_noexcept PRIVATE memorypack::memorypack)
target_include_directories(memorypack_tests_noexcept PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(memorypack_tests_noexcept PRIVATE MEMORYPACK_NO_EXCEPTIONS)
if(MSVC)
    target_compile_definitions(memorypack_tests_noexcept PRIVATE _HAS_EXCEPTIONS=0)
    target_compile_options(memorypack_tests_noexcept PRIVATE
        /W4 /WX /permissive- /utf-8 /EHs-c-)
else()
    target_compile_options(memorypack_tests_noexcept PRIVATE -fno-exceptions
        -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion)
endif()
add_test(NAME memorypack_tests_noexcept COMMAND memorypack_tests_noexcept)
```

**함정**
- 이 타깃에는 `memorypack_configure_test()`를 쓰면 안 된다. 그 함수가 `/EHsc`를 붙이는데 `/EHs-c-`와 충돌해 D9025가 난다.
- CMake가 `CMAKE_CXX_FLAGS`에 `/EHsc`를 넣지 않는지 확인할 것. 현재 CMake 4.3.1은 넣지 않는다(`tests/CMakeLists.txt`의 기존 주석이 그 근거). 넣는 버전이면 `string(REGEX REPLACE "/EHsc" "" ...)`가 필요하다.
- `_HAS_EXCEPTIONS=0`은 **모든 번역 단위에 일관되게** 적용돼야 한다. 헤더온리라 문제없지만, 나중에 이 타깃에 소스를 추가할 땐 주의.

**부수 작업: `CHECK_THROWS` 1곳을 `CHECK_FAILS`로 이관**

현재 `CHECK_THROWS`는 무예외 모드에서 no-op이고 `g_checks`를 증가시키지도 않는다. 즉 두 빌드의 체크 수가 달라진다. 사용처가 딱 1곳뿐이므로 `CHECK_FAILS`로 옮기면 **양쪽 빌드가 정확히 같은 개수를 보고**하게 되고, 이게 곧 "무예외 모드가 같은 커버리지를 갖는다"는 증거가 된다.

이관 후에는 `CHECK_THROWS` 매크로를 하니스에서 지워 재발을 막는 것도 고려. (남겨두려면 무예외 분기에서도 `++g_checks`를 하도록 고칠 것.)

**완료 조건**
- `ctest`가 `memorypack_tests_noexcept`를 포함해 통과.
- `memorypack_tests`와 `memorypack_tests_noexcept`가 **동일한 체크 수**를 출력.
- `/W4 /WX` 무경고.
- `docs/compatibility.md`의 "✅ built and tested" 옆에 "(ctest 상시 검증)" 취지 문구 추가.

**선택 확장**: `-fno-rtti` 상당(MSVC `/GR-`)도 같은 방식으로 타깃 하나 더. 문서는 "by construction"이라고만 하고 있는데 이것도 공짜로 실증 가능하다.

---

## 4. 검증 자동화 — 사람이 기억해야만 돌아간다

### 문제

호스팅 CI를 두지 않는 건 의도된 결정이므로 되돌리지 않는다. 문제는 그 대안인 README의 "Full verification" 체크리스트가 **9개 명령을 사람이 순서대로 치는 것**이고, 실패해도 눈으로 봐야 알고, 샘플 E2E는 아예 수동이라는 점이다.

그리고 이번 세션에서 실제로 겪은 문제: **평범한 셸에서는 `cl`도 `ninja`도 PATH에 없다.** `cmake -B build-bench`가 "CMake was unable to find a build program corresponding to Ninja"로 실패했고, `vcvars64.bat`을 거쳐야 했다. 체크리스트를 그대로 복사해 붙이면 실패하는 환경이 기본값이라는 뜻이다.

### 목표

체크리스트 전체를 **한 명령, 하나의 종료 코드**로 만든다. CI를 안 두는 대신, 로컬 검증을 CI만큼 무뇌하게 만든다.

### 4-A. `tools/verify.ps1` (신규)

```powershell
# Runs the full verification checklist and exits non-zero if anything fails.
#
#   pwsh tools/verify.ps1              # everything
#   pwsh tools/verify.ps1 -Quick       # skip dotnet, samples and ASan
#   pwsh tools/verify.ps1 -Asan        # additionally build and test under ASan
#
# Locates the Visual Studio developer environment itself: neither cl.exe nor
# ninja.exe is on PATH in a plain shell.
param([switch]$Quick, [switch]$Asan)
```

**단계 (각각 성공/실패를 표로 요약)**

| # | 단계 | `-Quick`에서 | 근거 |
|---|------|-------------|------|
| 1 | VS 개발 환경 진입 (`vswhere` → `Launch-VsDevShell.ps1`) | 실행 | PATH 문제 해결 |
| 2 | 구성 + 빌드 (Release, tests/samples/examples ON) | 실행 | README 1번 |
| 3 | `ctest` (단위·interop·예제·**no-exceptions**·**퍼즈 리플레이**) | 실행 | README 1번 + §2-A + §3 |
| 4 | `FormatProbe verify tests/fixtures` | 건너뜀 | README 2번 |
| 5 | interop 바이너리로 `build/cpp-fixtures` 생성 → `FormatProbe check-cpp` | 건너뜀 | README 3번 |
| 6 | `dotnet test tools/cs2cpp.Tests` | 건너뜀 | README 4번 |
| 7 | `cs2cpp --check` × 2 (샘플 헤더 드리프트) | 건너뜀 | README 4번 |
| 8 | **샘플 E2E ×2** (아래 4-B) | 건너뜀 | 현재 완전 수동 |
| 9 | ASan 빌드 + `ctest` | `-Asan`일 때만 | §2-B |

**출력 형태** (실패해도 끝까지 돌고 마지막에 요약, CI 로그처럼)

```
  [ OK ] configure + build            12.4s
  [ OK ] ctest (14 tests)              1.1s
  [ OK ] fixtures vs C# MemoryPack     3.2s   53/53
  [FAIL] cs2cpp drift check            2.0s   samples/CppClient/packets.hpp
  ...
  3 of 9 steps failed. See above.
```

### 4-B. 샘플 E2E 자동화

이번 세션에서 손으로 한 절차를 그대로 스크립트화한다. 두 방향 다 이미 자기 검증형이라(불일치 시 non-zero 종료) 감싸기만 하면 된다.

```
방향 1:  dotnet run --project samples/CSharpServer -c Release   (백그라운드)
         → 25001 LISTENING 될 때까지 대기 (타임아웃 30s)
         → ./build/samples/CppClient.exe        → 종료 코드 0 이어야 함
         → 서버 종료

방향 2:  ./build/samples/CppServer.exe                          (백그라운드)
         → 25003 LISTENING 될 때까지 대기
         → dotnet run --project samples/CsClient -c Release     → 종료 코드 0
         → 서버 종료
```

**함정**: 서버 프로세스를 확실히 죽여야 한다. `dotnet run`은 자식 프로세스를 띄우므로 PID 하나만 kill하면 포트가 남는다. `Start-Process -PassThru` 후 프로세스 트리를 정리하거나, 포트가 닫힐 때까지 확인하는 로직을 넣을 것. (이번 세션에서 `taskkill /F /IM CppServer.exe`로 처리했고 TIME_WAIT가 남았다 — 재실행 시 바인드 실패 가능성.)

### 4-C. 선택: pre-push 훅

```bash
git config core.hooksPath .githooks
```

`.githooks/pre-push`가 `verify.ps1 -Quick`을 돌린다. **강제하지 않고 opt-in**으로 둔다 — 기여자 환경에 .NET이 없을 수 있고, 이 저장소는 의도적으로 CI를 두지 않는 곳이므로 훅을 필수화하면 취지에 어긋난다. `CONTRIBUTING.md`에 활성화 방법만 안내.

### 4-D. 문서 정리 (같은 커밋에 묶기)

- `README.md`의 "Full verification": 맨 앞에 `pwsh tools/verify.ps1` 한 줄을 놓고, 기존 9개 명령은 "스크립트가 하는 일"로 아래 남긴다. 명령을 지우지 말 것 — POSIX 사용자와 문서 가치가 있다.
- `tests/CMakeLists.txt:30`의 `# ctest runs both; CI additionally runs the interop binary...` → CI가 없으므로 `tools/verify.ps1`를 가리키도록 수정.
- `CONTRIBUTING.md`: "체크리스트를 돌리세요" → "`verify.ps1`을 돌리고 요약을 PR에 붙이세요".

**완료 조건**
- 깨끗한 셸(개발자 셸 아님)에서 `pwsh tools/verify.ps1`이 처음부터 끝까지 돌고 `$LASTEXITCODE -eq 0`.
- 일부러 픽스처 한 바이트를 깨뜨리면 해당 단계만 `[FAIL]`이 뜨고 종료 코드가 non-zero.
- `-Quick`이 .NET 없는 환경에서도 성공.

### 선택: `tools/verify.sh`

WSL에서 퍼징을 돌릴 거라면 POSIX 미러가 있으면 편하다. 다만 #1이 범위 밖이므로 **우선순위 낮음**. 만들더라도 "지원 플랫폼 주장"이 아니라 "퍼징 호스트 편의"로 위치를 명확히 할 것.

---

## 5. unmanaged struct 패딩 — 문서로만 막고 있다

### 문제

`WriteUnmanaged`는 구조체 바이트를 그대로 복사한다. 자연 정렬 구조체에는 멤버 사이에 패딩이 있고, **그 패딩 바이트가 그대로 와이어에 실린다.** C++는 객체가 *값 초기화*(`T v{};`)됐을 때만 패딩 내용을 보장한다.

```cpp
struct Padded { uint8_t tag; int32_t value; };   // 8 = 1 + 3(패딩) + 4
Padded a;          // 패딩 3바이트 미정 → 스택에 있던 것이 전송된다
Padded b{};        // 패딩 포함 전체가 0
```

결과는 두 가지다. **재현 불가능한 출력**(콘텐츠 해싱·중복 제거·골든 테스트가 깨짐)과 **정보 유출**(값마다 최대 `sizeof(T) - 멤버합` 바이트의 프로세스 메모리가 상대에게 간다).

`docs/security.md#unmanaged-struct-padding`에 훌륭하게 설명돼 있고 API 주석도 있다. 하지만 **컴파일러도 라이브러리도 아무것도 막아주지 않는다.** 게다가 최적화 수준에 따라 우연히 0이 되기도 해서 테스트에서 놓치기 딱 좋다. 게임 서버 패킷에 unmanaged struct를 쓰는 것이 이 라이브러리의 주 용도임을 생각하면, 문서에만 의존하는 건 약하다.

### 설계 방향

와이어 포맷은 **절대 바꾸지 않는다**. 올바르게 값 초기화된 값의 출력 바이트는 지금과 100% 동일해야 한다(픽스처 53개가 그 증거). 바꾸는 건 "실수하기 쉬움"뿐이다.

매크로를 3종으로 나눈다.

| 매크로 | 용도 | 비용 |
|--------|------|------|
| `MEMORYPACK_UNMANAGED(T, size)` | 기존. 유지하되 문서에서 "unchecked"로 격하 | 0 |
| `MEMORYPACK_UNMANAGED_EXACT(T, size, m1, ...)` | **패딩이 없음을 컴파일 타임에 증명**. `Pack=1` 구조체와 자연스럽게 빈틈없는 레이아웃용 | 0 |
| `MEMORYPACK_UNMANAGED_SCRUBBED(T, size, m1, ...)` | 패딩이 있는 구조체용. 값 초기화된 임시본을 거쳐 **패딩이 항상 0** | 스택 임시 1개 + 멤버별 복사 |

`MEMORYPACK_PP_FOREACH`/`PP_NARG`가 이미 있으니 그대로 쓴다.

### 5-A. `MEMORYPACK_UNMANAGED_EXACT` — 패딩 없음을 증명

멤버 크기의 합이 `sizeof(T)`와 같으면 패딩이 없다. `std::has_unique_object_representations_v`는 **쓰면 안 된다** — `float`/`double` 때문에 패딩 없는 `Vec3{float x,y,z}`에서도 `false`가 나온다. 반드시 멤버 합으로 계산할 것.

`constexpr` 람다는 MSVC에서 까다로우니 특성화 구조체를 쓰는 쪽이 안전하다.

```cpp
#define MEMORYPACK_DETAIL_MEMBER_SIZE(name) + sizeof(((MemorypackProbeType*)nullptr)->name)

#define MEMORYPACK_UNMANAGED_EXACT(Type, ExpectedSize, ...)                               \
    static_assert(std::is_trivially_copyable_v<Type>,                                     \
                  #Type " must be trivially copyable to map a C# unmanaged struct");      \
    static_assert(sizeof(Type) == (ExpectedSize),                                         \
                  #Type " must be " #ExpectedSize " bytes to match the C# layout");       \
    namespace memorypack { namespace detail {                                             \
    template<> struct UnmanagedProbe<Type> {                                              \
        using MemorypackProbeType = Type;                                                 \
        static constexpr size_t MemberBytes =                                             \
            0 MEMORYPACK_PP_FOREACH(MEMORYPACK_DETAIL_MEMBER_SIZE, __VA_ARGS__);          \
    };                                                                                    \
    }}                                                                                    \
    static_assert(memorypack::detail::UnmanagedProbe<Type>::MemberBytes == sizeof(Type),  \
                  #Type " has padding between members, so its padding bytes would go on " \
                  "the wire. Use MEMORYPACK_UNMANAGED_SCRUBBED, or [StructLayout(Pack=1)]");\
    /* ... IsUnmanaged / MemoryPackFormatter 특수화는 기존 매크로와 동일 ... */
```

`sizeof(((T*)nullptr)->m)`은 미평가 문맥이므로 널 역참조가 실제로 일어나지 않는다. `offsetof` 기반보다 이식성이 좋다.

### 5-B. `MEMORYPACK_UNMANAGED_SCRUBBED` — 패딩을 0으로 고정

```cpp
#define MEMORYPACK_DETAIL_COPY_MEMBER(name) memorypackTmp.name = v.name;

#define MEMORYPACK_UNMANAGED_SCRUBBED(Type, ExpectedSize, ...)                            \
    /* ... trivially_copyable / sizeof static_assert 동일 ... */                          \
    namespace memorypack {                                                                \
    template<> struct IsUnmanaged<Type> : std::true_type {};                              \
    template<> struct MemoryPackFormatter<Type> {                                         \
        static void Serialize(MemoryPackWriter& w, const Type& v) {                       \
            Type memorypackTmp{};   /* value-init zeroes members AND padding */           \
            MEMORYPACK_PP_FOREACH(MEMORYPACK_DETAIL_COPY_MEMBER, __VA_ARGS__)             \
            w.WriteUnmanaged(memorypackTmp);                                              \
        }                                                                                 \
        static void Deserialize(MemoryPackReader& r, Type& v) { r.ReadUnmanaged(v); }     \
    };                                                                                    \
    }
```

**꼭 문서화할 제약**: 멤버는 스칼라이거나 그 자체가 패딩 없는 타입이어야 한다. 중첩 구조체 멤버를 대입하면 암시적 복사 대입이 그 구조체의 패딩까지 복사할 수 있다. 중첩이 있으면 안쪽도 `_SCRUBBED`로 등록하고 멤버별로 대입할 것.

### 5-C. 패딩 탐지 테스트

패딩 유출을 **측정**하는 테스트를 넣는다. 0x00으로 채운 객체와 0xFF로 채운 객체에 같은 멤버 값을 넣고 바이트를 비교하면, 다른 바이트가 곧 패딩이다.

```cpp
template<typename T, typename Fill>
bool SerializesDeterministically(Fill fill) {
    T a; std::memset(&a, 0x00, sizeof a); fill(a);
    T b; std::memset(&b, 0xFF, sizeof b); fill(b);
    return std::memcmp(&a, &b, sizeof a) == 0;
}
```

`test_unmanaged_padding` 케이스에서:
- `PaddedPair`를 **기존** `MEMORYPACK_UNMANAGED`로 등록한 경우 → `false`(위험이 실재함을 고정)
- 같은 타입을 `_SCRUBBED`로 등록한 경로 → 직렬화 출력이 두 경우 모두 동일 → `true`
- `PackedPair`(`Pack=1`) → `_EXACT`가 컴파일 통과

이 테스트가 "문서가 경고하는 그 일이 실제로 일어난다"의 증거이자, 수정이 실제로 먹혔다는 증거가 된다.

### 5-D. 파급 범위 — 함께 고쳐야 하는 곳

`MEMORYPACK_UNMANAGED` 사용처 전수(조사 완료):

| 파일 | 대상 | 조치 |
|------|------|------|
| `include/memorypack/dotnet.hpp:366-369` | `Vector2/3/4`, `Quaternion` | 전부 float만 → `_EXACT`로 전환 (패딩 없음이 증명됨) |
| `tests/interop_types.hpp:297-301` | `Vec3`, `PaddedStruct`, `PackedStruct`, `ValueTuple2i`, `ValueTupleIFD` | `PaddedStruct`만 `_SCRUBBED`, 나머지 `_EXACT` 시도 |
| `tests/memorypack_tests.cpp:79,97,98` | `PlainVec3`, `PaddedPair`, `PackedPair` | 5-C 테스트의 소재로 사용 |
| `tests/fuzz/fuzz_deserialize.cpp:86` | `Vec3` | `_EXACT` |
| `benchmarks/memorypack_bench.cpp:88` | `Vec3` | `_EXACT` + **스크럽 경로 벤치마크 추가** |
| `examples/05_unmanaged_struct.cpp:59,68` | `Vec3`, `PaddedStat` | 세 매크로의 차이를 보여주는 예제로 개편 |
| **`tools/cs2cpp/CppGenerator.cs:275`** | 생성기 | **가장 중요** |

**cs2cpp 연동** — 여기가 실질적 가치가 나오는 지점이다. cs2cpp는 Roslyn으로 C# struct를 파싱하므로 **멤버 목록을 이미 알고 있다.** 따라서:

- C#에 `[StructLayout(Pack = 1)]`이 있으면 → `MEMORYPACK_UNMANAGED_EXACT(T, size, m1, ...)`
- 아니면 → `MEMORYPACK_UNMANAGED_SCRUBBED(T, size, m1, ...)`

이렇게 하면 **생성된 코드는 패딩 문제가 구조적으로 불가능**해진다. 손으로 매크로를 쓰는 사람만 위험을 감수하게 되고, 그건 문서로 커버되는 수준이다.

연쇄 작업:
1. `CppGenerator.cs` 수정
2. `tools/cs2cpp.Tests` 스냅샷 80개 갱신 (변경분 눈으로 확인)
3. `samples/CppClient/packets.hpp`, `samples/ChatClient/packets.hpp` 재생성
4. `cs2cpp --check` ×2 통과 확인 (스키마 해시가 바뀌는지 확인 — 바뀌면 안 됨. 해시는 C# 정의 기반이므로 생성 코드 변경과 무관해야 정상)

### 5-E. 선택: 읽기 쪽 스크럽

`ReadUnmanaged`는 `sizeof(T)` 전체를 memcpy하므로, 상대가 보낸 패딩 바이트가 그대로 내 객체에 들어온다. 그 객체를 다시 직렬화하면 **상대의 바이트를 상대에게 되돌려준다**(에코). 위험도는 낮지만, 미들박스/릴레이 서버라면 의미가 있다. `_SCRUBBED`의 `Deserialize`도 임시본을 거치도록 하면 대칭이 맞는다. 비용 대비 판단은 구현 시점에.

### 완료 조건

- **픽스처 53개 전부 여전히 통과** (와이어 포맷 무변경 증명) — 이게 1순위 게이트.
- `FormatProbe check-cpp` 22건 통과.
- `test_unmanaged_padding`이 패딩 유출을 검출하고, `_SCRUBBED` 경로에서 결정론적임을 증명.
- cs2cpp 스냅샷 80개 통과, 샘플 헤더 `--check` 통과.
- 벤치마크에 `WriteUnmanaged` vs `_SCRUBBED` 수치 기록 (`docs/benchmarks.md`).
- `docs/security.md#unmanaged-struct-padding`에 "이제 이렇게 막는다" 절 추가, `docs/api-reference.md`에 매크로 3종 표.
- `CLAUDE.md`의 "사용자 정의 타입 직렬화" 절에 `_EXACT`/`_SCRUBBED` 반영.

---

## 6. 이 작업이 끝나면 달라지는 것

| 항목 | 지금 | 이후 |
|------|------|------|
| 퍼징 | 하니스만 있고 실행 이력 0 | 코퍼스가 `ctest`에 상시 편입, 실행 로그가 문서에 남음 |
| ASan | 미실행 | Windows에서 추가 설치 없이 `ctest` 전체 실행 가능 |
| no-exceptions | 수동 1회 확인, 회귀 시 무경보 | `ctest` 항목, 예외 빌드와 체크 수 동일 |
| 검증 | 9개 명령 수동, 샘플 E2E는 완전 수동 | `pwsh tools/verify.ps1` 한 줄, 종료 코드 하나 |
| 패딩 | 문서 경고만 | 컴파일 타임 증명 또는 런타임 스크럽, 생성 코드는 구조적으로 안전 |

**여전히 남는 것**: #1(GCC/Clang·Linux/macOS·32비트·빅엔디안 실증). 이번 범위 밖이므로 `docs/compatibility.md`의 "not yet built" 표기는 **그대로 정확하게 유지**할 것. WSL에서 퍼징을 돌렸다는 이유로 "Linux 지원 확인됨"이라고 쓰면 안 된다.

---

## 부록 A. 함께 정리할 문서 드리프트 (덤, 각 10분)

계획 항목은 아니지만 조사 중 발견한 것들이다. 관련 절을 건드리는 김에 같이 고치면 된다.

| 위치 | 문제 | 조치 | 묶을 곳 |
|------|------|------|---------|
| `tests/CMakeLists.txt:30` | "CI additionally runs the interop binary..." — CI는 `d3760f5`에서 제거됨 | `tools/verify.ps1` 참조로 교체 | §4-D |
| `docs/dev/plans.md` | 2026-06-23 구 계획서. "P1-3 CI ⬜", "P1-2 단위 테스트 ✅ (59 checks)"라고 적혀 있으나 실제는 295+ 체크이고 CI는 의도적 부재 | 맨 위에 "이 문서는 `ROADMAP.md`와 이 문서로 대체됨 (2026-08-31)" 배너 추가, 또는 `plans-2026-06.md`로 이름 변경 | 아무 커밋 |

## 부록 B. 각 항목 재확인용 명령

작업 후 해당 항목만 빠르게 확인할 때 쓴다.

```bash
# §3 no-exceptions
ctest --test-dir build -R noexcept --output-on-failure

# §2-A 코퍼스 리플레이
ctest --test-dir build -R fuzz_replay --output-on-failure

# §5 패딩 (와이어 포맷 무변경이 1순위 게이트)
ctest --test-dir build --output-on-failure
dotnet run --project tools/FormatProbe -c Release -- verify tests/fixtures
./build/tests/memorypack_interop_tests "" build/cpp-fixtures
dotnet run --project tools/FormatProbe -c Release -- check-cpp build/cpp-fixtures

# §4 전체
pwsh tools/verify.ps1
```
