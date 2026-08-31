// Replays a corpus directory through the libFuzzer entry point without
// libFuzzer, so every input that ever crashed the reader stays covered by
// `ctest` on any compiler. Not a fuzzer: it explores nothing, it only replays.
//
//   fuzz_replay <corpus-dir> [more-dirs...]
//
// Exit code 0 means every input was consumed without crashing. A crash here is
// a real bug; there is no "expected failure" - reader errors are swallowed by
// the harness itself.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace {

bool ReplayFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "fuzz_replay: cannot open %s\n", path.string().c_str());
        return false;
    }
    const auto length = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<uint8_t> data(length);
    if (length > 0 && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(length))) {
        std::fprintf(stderr, "fuzz_replay: failed to read %s\n", path.string().c_str());
        return false;
    }
    (void)LLVMFuzzerTestOneInput(data.data(), data.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: fuzz_replay <corpus-dir> [more-dirs...]\n");
        return 1;
    }

    size_t total = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path dir(argv[i]);
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            std::fprintf(stderr, "fuzz_replay: not a directory: %s\n", argv[i]);
            return 1;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (!ReplayFile(entry.path())) return 1;
            ++total;
        }
    }

    std::printf("fuzz_replay: replayed %zu corpus file(s) without a crash\n", total);
    return 0;
}
