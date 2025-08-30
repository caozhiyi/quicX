#include <cstdint>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::perror("open");
        return 1;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return 0;
    (void)LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    return 0;
}


