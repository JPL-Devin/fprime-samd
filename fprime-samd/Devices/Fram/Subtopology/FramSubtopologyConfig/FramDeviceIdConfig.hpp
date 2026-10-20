// ======================================================================
// \title  FramDeviceIdConfig.hpp
// \brief  Expected RDID response of the fitted F-RAM (part of FramSubtopologyConfig)
//
// Kept apart from FramSubtopologyConfig.hpp, whose SPI host settings pull in SAMD21-only
// headers, so the FramDriver and its native unit test can include just this constant.
// ======================================================================
#ifndef Fram_FramDeviceIdConfig_hpp
#define Fram_FramDeviceIdConfig_hpp

#include <Fw/FPrimeBasicTypes.hpp>

namespace Fram {

//! Device ID the startup RDID (9Fh) read must return (REQ-FRAM-014), in the order the bytes
//! arrive on MISO. Infineon datasheet 002-26764 Rev. *D (CY15B102QN/CY15V102QN, 2022-09-12),
//! "Read device ID (RDID, 9Fh)" and "Ordering information": the 9-byte ID of the CY15B102QN
//! is 7F7F7F7F7F7FC22A00 written MSB first (six 7Fh JEDEC continuation bytes, manufacturer
//! C2h, product ID 2A00h), and "the least significant data byte (Byte 0) shifts out first",
//! so the device clocks out 00 2A C2 7F 7F 7F 7F 7F 7F (confirmed on hardware). The 3.0 V
//! sibling CY15V102QN reads 7F7F7F7F7F7FC22A04 (product ID 2A04h). Change this when a
//! different part is fitted; a mismatch only raises FramDeviceIdMismatch, it does not block
//! reads or writes.
constexpr U8 EXPECTED_DEVICE_ID[9] = {0x00, 0x2A, 0xC2, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F};

}  // namespace Fram

#endif  // Fram_FramDeviceIdConfig_hpp
