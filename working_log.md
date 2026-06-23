# 작업 로그

## 2026-06-23 18:00 KST — [코드 리뷰] 문자열 와이어 포맷 비호환(치명적) 수정
- 코드 리뷰 결과 `WriteString`/`ReadString`이 실제 MemoryPack UTF-8 포맷(`[~byteCount][utf16Length][utf8]`)과 달라 문자열 패킷 전체가 C# MemoryPack과 양방향 비호환임을 실측 확인.
- `memorypack.hpp`를 실제 포맷으로 재구현(null/빈/UTF-8/UTF-16 + surrogate 처리, utf16Length 계산). README/CLAUDE.md 포맷 설명 정정.
- 단위 테스트에 `test_string_format`(골든 바이트·멀티바이트·이모지·UTF-16 읽기) 추가. 전체 통과, 경고 0개.
- 별도 교차 검증: 수정된 C++ 직렬화 결과가 실제 C# MemoryPack 1.x 바이트와 완전 일치, 역방향 역직렬화도 성공.

## 2026-06-23 17:46 KST — 공개 준비 P0/P1 일부 구현 (CMake·테스트·문서)
- P0-1(A): 루트 `CMakeLists.txt` + `cmake/memorypackConfig.cmake.in` 추가 — header-only INTERFACE 타깃, install/`find_package` 지원. README/CLAUDE.md의 CMake·크로스플랫폼 주장과 구현 일치.
- P0-2: `.gitignore`에 VS/.NET/CMake/에디터 산출물 보강. P0-3: `CLAUDE.md`를 실제 구조와 일치하게 갱신. P0-4: README 배지 stable + License/C++23.
- P1-2: `tests/memorypack_tests.cpp`(의존성 없는 자체 하니스, 골든 바이트·경계 케이스 포함) + CTest 등록. 헤더 C4244 경고 1건 제거. P1-4: 컴파일러 최소 버전(MSVC v143/GCC13+/Clang16+) 명시.
- 검증: VS 2026(MSVC 14.51)+Ninja로 configure/build/test/install/샘플 빌드 전부 성공. 테스트 59개 통과, 경고 0개.

## 2026-06-23 17:17 KST — 외부 공개 준비 검토 및 plans.md 작성
- 저장소 전체(라이브러리 헤더, 샘플 4종, cs2cpp 도구, 문서, 빌드 설정)를 공개 관점에서 면밀히 검토.
- 핵심 발견: 문서가 주장하는 "CMake 빌드 시스템/크로스플랫폼"이 실제 구성과 불일치(CMakeLists 부재, VS 전용), 단위 테스트·CI·`.gitignore` 보강·커뮤니티 자산 누락.
- 민감 정보·하드코딩 경로·비밀키는 발견되지 않음. cs2cpp 도구는 경고/오류 0개 빌드 확인.
- 보완 항목을 P0~P4 우선순위로 정리하여 루트 `plans.md`에 기록.
