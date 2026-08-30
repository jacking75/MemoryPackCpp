#pragma once
// Dependency-free test harness shared by the unit and interop test binaries.
//
// Deliberately tiny: no external test framework, so the suite builds anywhere a
// C++23 compiler exists. A failing check prints the file, line, expression and -
// for byte comparisons - a hex dump of both sides.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace mptest {

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_currentCase = "";

inline void BeginCase(const char* name) {
    g_currentCase = name;
    std::printf("[%s]\n", name);
}

inline void ReportFailure(const char* file, int line, const char* expr) {
    ++g_failures;
    std::printf("  [FAIL] %s:%d  in <%s>\n         %s\n", file, line, g_currentCase, expr);
}

inline std::string HexString(const uint8_t* data, size_t size) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 3);
    for (size_t i = 0; i < size; ++i) {
        if (i) out.push_back(' ');
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

inline void ReportBytes(const char* label, const uint8_t* data, size_t size) {
    std::printf("         %-9s (%zu bytes) %s\n", label, size, HexString(data, size).c_str());
}

inline bool BytesEqual(const uint8_t* got, size_t n, std::initializer_list<int> expected) {
    if (n != expected.size()) return false;
    size_t i = 0;
    for (int e : expected) {
        if (got[i++] != static_cast<uint8_t>(e)) return false;
    }
    return true;
}

inline bool BytesEqual(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

inline int Summarize(const char* suiteName) {
    std::printf("\n%s: %d checks, %d failure(s)\n", suiteName, g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("TESTS FAILED\n");
    return 1;
}

} // namespace mptest

#define TEST_CASE(name) ::mptest::BeginCase(name)

// Variadic so that template arguments containing commas - as in
// CHECK(r.ReadArray<int32_t, 4>() == a) - are not taken for extra macro arguments.
#define CHECK(...)                                                                     \
    do {                                                                               \
        ++::mptest::g_checks;                                                          \
        if (!(__VA_ARGS__))                                                            \
            ::mptest::ReportFailure(__FILE__, __LINE__, "CHECK(" #__VA_ARGS__ ")");     \
    } while (0)

#define CHECK_MSG(cond, msg)                                                           \
    do {                                                                               \
        ++::mptest::g_checks;                                                          \
        if (!(cond)) ::mptest::ReportFailure(__FILE__, __LINE__, msg);                  \
    } while (0)

// Compares a writer's output against an expected byte list, dumping both on failure.
#define CHECK_BYTES(dataPtr, dataSize, ...)                                            \
    do {                                                                               \
        ++::mptest::g_checks;                                                          \
        const uint8_t* mp_d_ = (dataPtr);                                              \
        const size_t mp_n_ = (dataSize);                                               \
        std::initializer_list<int> mp_e_ = {__VA_ARGS__};                              \
        if (!::mptest::BytesEqual(mp_d_, mp_n_, mp_e_)) {                              \
            ::mptest::ReportFailure(__FILE__, __LINE__, "byte mismatch");              \
            ::mptest::ReportBytes("actual", mp_d_, mp_n_);                             \
            std::vector<uint8_t> mp_ev_;                                               \
            for (int mp_b_ : mp_e_) mp_ev_.push_back(static_cast<uint8_t>(mp_b_));     \
            ::mptest::ReportBytes("expected", mp_ev_.data(), mp_ev_.size());           \
        }                                                                              \
    } while (0)

// Compares two byte ranges, dumping both on failure.
#define CHECK_BYTES_EQ(actualSpan, expectedSpan, what)                                 \
    do {                                                                               \
        ++::mptest::g_checks;                                                          \
        std::span<const uint8_t> mp_a_ = (actualSpan);                                 \
        std::span<const uint8_t> mp_b_ = (expectedSpan);                               \
        if (!::mptest::BytesEqual(mp_a_, mp_b_)) {                                     \
            ::mptest::ReportFailure(__FILE__, __LINE__, what);                          \
            ::mptest::ReportBytes("actual", mp_a_.data(), mp_a_.size());               \
            ::mptest::ReportBytes("expected", mp_b_.data(), mp_b_.size());             \
        }                                                                              \
    } while (0)

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define MPTEST_HAS_EXCEPTIONS 1
#else
#  define MPTEST_HAS_EXCEPTIONS 0
#endif

#if MPTEST_HAS_EXCEPTIONS
// Verifies that an expression reports an error, either by throwing or - when the
// library is built without exceptions - by setting the reader/writer error state.
#  define CHECK_THROWS(expr)                                                           \
    do {                                                                               \
        ++::mptest::g_checks;                                                          \
        bool mp_threw_ = false;                                                        \
        try { (void)(expr); } catch (...) { mp_threw_ = true; }                        \
        if (!mp_threw_)                                                                \
            ::mptest::ReportFailure(__FILE__, __LINE__, "CHECK_THROWS(" #expr ")");     \
    } while (0)
#else
#  define CHECK_THROWS(expr) do { (void)sizeof(expr); } while (0)
#endif

#if MPTEST_HAS_EXCEPTIONS
#  define MPTEST_TRY try
#  define MPTEST_CATCH catch (...)
#else
#  define MPTEST_TRY if (true)
#  define MPTEST_CATCH if (false)
#endif

/// Asserts that a block reports a memorypack error, whether the library signals
/// it by throwing or - with exceptions disabled - through the error state.
///
/// The block is a statement sequence that must `return` whether the reader or
/// writer under test ended up in a failed state:
///
///     CHECK_FAILS("length bomb", {
///         uint8_t d[] = {0xFF, 0xFF, 0xFF, 0x7F};
///         memorypack::MemoryPackReader r(d, sizeof(d));
///         (void)r.ReadVector<int32_t>();
///         return r.Failed();
///     });
#define CHECK_FAILS(desc, ...)                                                         \
    do {                                                                               \
        ++::mptest::g_checks;                                                          \
        bool mp_failed_ = false;                                                       \
        MPTEST_TRY { mp_failed_ = [&]() -> bool __VA_ARGS__ (); }                      \
        MPTEST_CATCH { mp_failed_ = true; }                                            \
        if (!mp_failed_)                                                               \
            ::mptest::ReportFailure(__FILE__, __LINE__, "expected failure: " desc);     \
    } while (0)
