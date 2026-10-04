// Unit tests for the fecim_pkg function mirrors and parameter derivations.
// Golden values were computed independently in Python from the fecim_pkg.sv
// definitions; the RTL testbenches should pin the same vectors.

#include <cstring>

#include "fecim/params.h"
#include "fecim/primitives.h"
#include "test_harness.h"

using namespace fecim;

TEST(params_derived_values) {
    CHECK_EQ(PASSES, 2);
    CHECK_EQ(ACC_USED_W, 24);
    CHECK_EQ(ACC_MAX_MAG, std::int64_t{8'290'560});
}

TEST(crc8_check_value) {
    // Standard CRC-8 (poly 0x07, init 0, no reflect, no xorout) check value.
    const char* msg = "123456789";
    CHECK_EQ(crc8(reinterpret_cast<const std::uint8_t*>(msg), std::strlen(msg)),
             std::uint8_t{0xF4});
}

TEST(crc8_empty_and_residue) {
    CHECK_EQ(crc8(nullptr, 0), CRC_INIT);
    // Appending the CRC to the message yields a zero residue.
    std::uint8_t pkt[5] = {SYNC_H2F, 0x06, 0x00, 0x00, 0x00};
    pkt[4] = crc8(pkt, 4);
    CHECK_EQ(pkt[4], std::uint8_t{0xCC});
    CHECK_EQ(crc8(pkt, 5), std::uint8_t{0x00});
}

TEST(cell_hash16_golden) {
    CHECK_EQ(cell_hash16(0x0001, SEED_D2D),   std::uint16_t{0xD23B});
    CHECK_EQ(cell_hash16(0x1234, SEED_STUCK), std::uint16_t{0xEF31});
    CHECK_EQ(cell_hash16(0x3FFF, SEED_D2D),   std::uint16_t{0x07BE});
}

TEST(cell_hash16_wraps_at_16_bits) {
    // 0xFFFF * HASH_C0 overflows int; result must match the wrapped RTL value.
    CHECK_EQ(cell_hash16(0xFFFF, 0x0000), std::uint16_t{0x460D});
    CHECK_EQ(cell_hash16(0x0000, 0x0000), std::uint16_t{0x0000});
}

TEST(lfsr_zero_is_a_fixed_point) {
    // Why lane_seed() forces bit 0: a zero-seeded Galois LFSR never moves.
    CHECK_EQ(lfsr_step(0u), 0u);
}

TEST(lfsr_golden_and_never_zero) {
    std::uint32_t s = 0xACE1u;
    bool hit_zero = false;
    for (int i = 0; i < 1000; ++i) {
        s = lfsr_step(s);
        hit_zero |= (s == 0u);
    }
    CHECK(!hit_zero);
    CHECK_EQ(s, 0x1873CB03u);
}

TEST(lane_seed_nonzero_and_decorrelated) {
    for (int l = 0; l < NUM_LANES; ++l) {
        CHECK((lane_seed(0u, l) & 1u) == 1u);
        if (l > 0) CHECK(lane_seed(0u, l) != lane_seed(0u, l - 1));
    }
}
