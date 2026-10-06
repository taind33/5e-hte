/*******************************************************************************
 * @file    app_config.h
 * @brief   Config file for ADI's no-OS AD9361/API driver, at
 *          noos/drivers/rf-transceiver/ad9361/ .
 *
 *          IMPORTANT - why this file lives here and must stay here:
 *          three of the vendor driver's own translation units hardcode the
 *          path to this file with a relative include that is NOT affected
 *          by which .c files are excluded from the Vitis build:
 *              noos/drivers/rf-transceiver/ad9361/ad9361.c:
 *                  #include "../../../../ad9361/src/app_config.h"
 *              noos/drivers/rf-transceiver/ad9361/ad9361_api.c:
 *                  #include "../../../../ad9361/src/app_config.h"
 *              noos/drivers/rf-transceiver/ad9361/ad9361_util.h:
 *                  #include "../../../../ad9361/src/app_config.h"
 *          (a 4th file, ad9361_conv.c, does a plain #include "app_config.h"
 *          resolved via the compiler's -I search path, which must therefore
 *          also see this exact file - i.e. "ad9361/src" must stay on the
 *          project's include path, even though ad9361/src/main.c itself is
 *          excluded from the build and is reference-only.)
 *
 *          Because of that, this file is NOT "just reference material" like
 *          ad9361/src/main.c is - it is live, load-bearing configuration for
 *          the vendor driver, physically pinned to this path by vendor code
 *          we are not allowed to modify. A previous pass mistakenly created
 *          a second, corrected copy at src/app_config.h, believing the whole
 *          ad9361/src folder was inert - that copy was never actually seen
 *          by the compiler (none of the above 4 files, nor anything else,
 *          resolves to it) and has been removed to avoid the false
 *          impression that it did anything. This is the one and only
 *          app_config.h in the project now.
 *
 *          Content trimmed from ADI's original reference (which left
 *          XILINX_PLATFORM commented out, relying on an Eclipse project
 *          preprocessor symbol that was never actually set - see
 *          INTEGRATION_NOTES.md "app_config.h path bug" section) down to
 *          only what this board (Xilinx ZynqMP / K26I, single AD9361, no
 *          FMCOMMS5/Altera/Linux/IIO paths) needs.
 ******************************************************************************/
#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#define HAVE_SPLIT_GAIN_TABLE   1
#define HAVE_TDD_SYNTH_TABLE    1

#define AD9361_DEVICE           1 /* this board uses a real AD9361 */
#define AD9364_DEVICE           0
#define AD9363A_DEVICE          0

#define XILINX_PLATFORM
/* Not needed: XPS_BOARD_K26I is already defined by this project's own
 * xparameters.h (verified 2026-09-08 - grepped, not guessed), which
 * parameters.h relies on to pick the right GPIO reset pin/SPI/GPIO device
 * IDs for this board. Everything else below (ALTERA_PLATFORM,
 * LINUX_PLATFORM, FMCOMMS5, ADI_RF_SOM*, DMA_IRQ_ENABLE, DMA_EXAMPLE,
 * AXI_ADC_NOT_PRESENT, TDD_SWITCH_STATE_EXAMPLE, IIO_SUPPORT) stays
 * undefined on purpose - none of it applies to this board or this delivery
 * (AD9361 RF sampling/DMA/IIO is Phase 2, see README.md). */

#define HAVE_VERBOSE_MESSAGES /* prints errors/warnings during bring-up */

#endif /* APP_CONFIG_H_ */
