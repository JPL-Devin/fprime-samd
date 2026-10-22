// ======================================================================
// \title  FramSubtopologyConfig.hpp
// \brief  SPI host settings for the Fram.Subtopology spiDriver instance
//
// Default wiring: SERCOM1 on a SAMD21 Curiosity Nano, MOSI PA16 (PAD0), SCK PA17 (PAD1),
// MISO PA19 (PAD3), chip select driven by software through spiDriver.chipSelectGpioOut.
// A deployment that wires the F-RAM differently overrides this file (and the companion
// .fpp) in its own configuration module.
// ======================================================================
#ifndef Fram_FramSubtopologyConfig_hpp
#define Fram_FramSubtopologyConfig_hpp

#include <Fw/FPrimeBasicTypes.hpp>
#include "fprime-samd/Devices/Fram/FramConfig/FramDeviceIdConfig.hpp"
#include "fprime-samd/Drv/SpiDriver/SpiDriver.hpp"
#include "fprime-samd/Drv/Types/PinMux.hpp"
#include "fprime-samd/Drv/Types/SercomKindEnumAc.hpp"

namespace Fram {
namespace SubtopologyConfig {
namespace Spi {

//! SERCOM instance used as the SPI host
constexpr Samd21::SercomKind::T SERCOM = Samd21::SercomKind::SERCOM_1;

//! SCK rate. The CY15B102QN accepts READ at up to 40 MHz; 4 MHz keeps bring-up signals clean.
constexpr U32 BAUD_RATE_KHZ = 4000;

//! The F-RAM clocks opcodes, addresses and data most significant bit first
constexpr Samd21::SpiDriver::DataOrder DATA_ORDER = Samd21::SpiDriver::DataOrder::MSB;

//! SPI mode 0 (CPOL = 0, CPHA = 0); the device also accepts mode 3
constexpr Samd21::SpiDriver::ClockPolarity CLOCK_POLARITY = Samd21::SpiDriver::ClockPolarity::IdleLow;
constexpr Samd21::SpiDriver::ClockPhase CLOCK_PHASE = Samd21::SpiDriver::ClockPhase::SampleOnLeadingSck;

//! MISO on SERCOM PAD[3] (PA19)
constexpr Samd21::SpiDriver::DataInPinout DATA_IN_PINOUT = Samd21::SpiDriver::DataInPinout::PAD3;

//! MOSI on PAD[0] (PA16), SCK on PAD[1] (PA17)
constexpr Samd21::SpiDriver::DataOutPinout DATA_OUT_PINOUT = Samd21::SpiDriver::DataOutPinout::MOSI_0_SCK_1_CS_2;

//! The core clock may stop between transactions; nothing is in flight while the CPU sleeps
constexpr Samd21::SpiDriver::RunInStandby RUN_IN_STANDBY = Samd21::SpiDriver::RunInStandby::DISABLED;

//! Chip select is a GPIO driven through spiDriver.chipSelectGpioOut[SPI_SLOT]
constexpr Samd21::SpiDriver::HardwareChipSelect HARDWARE_CHIP_SELECT = Samd21::SpiDriver::HardwareChipSelect::DISABLED;

//! Route the SERCOM pads to the pins above. Called from the spiDriver configComponents phase.
inline void configurePins() {
    Samd21::PinMux::configure(PINMUX_PA16C_SERCOM1_PAD0);  // MOSI
    Samd21::PinMux::configure(PINMUX_PA17C_SERCOM1_PAD1);  // SCK
    Samd21::PinMux::configure(PINMUX_PA19C_SERCOM1_PAD3);  // MISO
}

}  // namespace Spi
}  // namespace SubtopologyConfig
}  // namespace Fram

#endif  // Fram_FramSubtopologyConfig_hpp
