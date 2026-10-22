module Fram {
    @ The F-RAM protocol driver
    instance framDriver: Fram.FramDriver base id Fram.SubtopologyConfig.BASE_ID

    @ The SAMD21 SPI host the F-RAM hangs off. Configured from FramSubtopologyConfig.hpp;
    @ the deployment connects its DMA channels, chip-select GPIO and the main cycler.
    instance spiDriver: Samd21.SpiDriver base id Fram.SubtopologyConfig.BASE_ID + 0x100 {
        phase Fpp.ToCpp.Phases.configComponents """
        Fram::SubtopologyConfig::Spi::configurePins();
        Fram::spiDriver.configure(
            Fram::SubtopologyConfig::Spi::SERCOM,
            Fram::SubtopologyConfig::Spi::BAUD_RATE_KHZ,
            Fram::SubtopologyConfig::Spi::DATA_ORDER,
            Fram::SubtopologyConfig::Spi::CLOCK_POLARITY,
            Fram::SubtopologyConfig::Spi::CLOCK_PHASE,
            Fram::SubtopologyConfig::Spi::DATA_IN_PINOUT,
            Fram::SubtopologyConfig::Spi::DATA_OUT_PINOUT,
            Fram::SubtopologyConfig::Spi::RUN_IN_STANDBY,
            Fram::SubtopologyConfig::Spi::HARDWARE_CHIP_SELECT
        );
        """
    }

    topology Subtopology {
        instance framDriver
        instance spiDriver

        @ The deployment must also schedule both drivers in main context, SPI host first:
        @   cycler.cycleOut -> Fram.spiDriver.activeIn
        @   cycler.cycleOut -> Fram.framDriver.activeIn   (SPI host listed first)
        connections Fram {
            framDriver.spiWriteReadOut -> spiDriver.SpiWriteRead[Fram.SubtopologyConfig.SPI_SLOT]
            spiDriver.SpiWriteReadComplete[Fram.SubtopologyConfig.SPI_SLOT] -> framDriver.spiReplyIn
        }
    }
}
