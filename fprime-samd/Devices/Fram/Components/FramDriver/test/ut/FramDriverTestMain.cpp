// ======================================================================
// \title  FramDriverTestMain.cpp
// \brief  cpp file for FramDriver component test main function
// ======================================================================

#include "FramDriverTester.hpp"
#include "STest/Random/Random.hpp"

#define FRAM_TEST(suite, name, method) \
    TEST(suite, name) {                \
        Fram::FramDriverTester tester; \
        tester.method();               \
    }

FRAM_TEST(Nominal, Read, testRead)
FRAM_TEST(Nominal, Write, testWrite)
FRAM_TEST(Nominal, AcceptThenComplete, testAcceptThenComplete)
FRAM_TEST(Nominal, IndexRouting, testIndexRouting)
FRAM_TEST(Nominal, DeviceId, testDeviceId)
FRAM_TEST(Nominal, Opcodes, testOpcodes)
FRAM_TEST(Nominal, RequestFromCallback, testRequestFromCallback)
FRAM_TEST(Nominal, Footprint, testFootprint)
FRAM_TEST(Nominal, WriteProtect, testWriteProtect)
FRAM_TEST(Nominal, RandomScenario, testRandomScenario)
FRAM_TEST(OffNominal, InvalidSize, testInvalidSize)
FRAM_TEST(OffNominal, OutOfRange, testOutOfRange)
FRAM_TEST(OffNominal, Busy, testBusy)
FRAM_TEST(OffNominal, SpiErrors, testSpiErrors)
FRAM_TEST(OffNominal, NullBuffer, testNullBuffer)
FRAM_TEST(OffNominal, SpuriousReplies, testSpuriousReplies)
FRAM_TEST(OffNominal, StartupReadFailures, testStartupReadFailures)
FRAM_TEST(OffNominal, BeforeFirstSched, testBeforeFirstSched)
FRAM_TEST(OffNominal, Precedence, testPrecedence)

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    STest::Random::seed();
    return RUN_ALL_TESTS();
}
