// Test packets for variable_mac_tb.vhd from the board code in Mac.h, run against a fake FIFO.
//   c++ -std=c++11 -DZEDBOARD -DQUANT_VARIABLE -Ifake_xil -I../../../sw/framework/src gen_vectors.cpp -o gen_vectors
//   ./gen_vectors vectors.txt
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "Mac.h"

int main(int argc, char** argv) {
    fakeFifo().out = fopen(argc > 1 ? argv[1] : "vectors.txt", "w");
    if (!fakeFifo().out) return 1;

    std::size_t bad = 0;
    for (int bits : ML::MAC_WIDTHS) bad += ML::macSelfTest(bits);

    // Model group sizes plus word/packet boundaries
    std::mt19937 rng(487);
    const std::size_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 75, 288, 576, 800, 2048, 2049, 4100};
    for (int bits : ML::MAC_WIDTHS) {
        std::uniform_int_distribution<int> value(-(1 << (bits - 1)), (1 << (bits - 1)) - 1);
        for (int repeat = 0; repeat < 3; repeat++) {
            for (std::size_t n : sizes) {
                std::vector<ML::i8> w(n), a(n);
                long expected = 0;
                for (std::size_t i = 0; i < n; i++) {
                    // first pass: extreme values only
                    w[i] = (ML::i8)(repeat == 0 ? -(1 << (bits - 1)) : value(rng));
                    a[i] = (ML::i8)(repeat == 0 ? -(1 << (bits - 1)) : value(rng));
                    expected += w[i] * a[i];
                }
                if (ML::macGroup(w.data(), a.data(), n, bits) != expected) bad++;
            }
        }
    }
    fclose(fakeFifo().out);
    printf("%lu packets written, %zu software mismatches\n", fakeFifo().packets, bad);
    return bad ? 1 : 0;
}
