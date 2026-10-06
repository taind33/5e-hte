/*******************************************************************************
 * @file    parameters.h
 * @brief   AD9361/no-OS parameter mapping for this project, trimmed from
 *          ADI's reference ad9361/src/parameters.h down to only the
 *          Xilinx ZynqMP/K26I, single-AD9361, no-DMA-yet path this project
 *          actually uses (see main.c's AD9361 bring-up). The old reference
 *          file kept
 *          branches for Zynq-7000/PS7, MicroBlaze/fabric-UART, FMCOMMS5
 *          (dual AD9361), and Linux/generic platforms - none of which apply
 *          here, so they're gone rather than dead-code-guarded.
 ******************************************************************************/
#ifndef PARAMETERS_H_
#define PARAMETERS_H_

#include <xparameters.h>

/* AD9361 core base addresses - genuinely conditional on what this
 * project's specific .xsa generates (single vs. "_0_"-suffixed instance
 * naming depends on Vivado IP versions), not something to hardcode. */
#ifdef XPAR_AXI_AD9361_0_BASEADDR
#define AD9361_RX_0_BASEADDR    XPAR_AXI_AD9361_0_BASEADDR
#define AD9361_TX_0_BASEADDR    (XPAR_AXI_AD9361_0_BASEADDR + 0x4000)
#else
#define AD9361_RX_0_BASEADDR    XPAR_AXI_AD9361_BASEADDR
#define AD9361_TX_0_BASEADDR    (XPAR_AXI_AD9361_BASEADDR + 0x4000)
#endif

#define RX_CORE_BASEADDR        AD9361_RX_0_BASEADDR
#define TX_CORE_BASEADDR        AD9361_TX_0_BASEADDR

/* K26I-specific PS peripheral IDs/pins (verified against the xparameters.h
 * you supplied - XPAR_PSU_GPIO_0_DEVICE_ID and XPAR_PSU_SPI_0_DEVICE_ID
 * both exist for real; GPIO_RESET_PIN=124 is ADI's documented K26I/ZCU102
 * AD9361 reset-line EMIO number, not board-specific to 5E-HTE). */
#define GPIO_DEVICE_ID          XPAR_PSU_GPIO_0_DEVICE_ID
#define GPIO_RESET_PIN          124

/* SHARED WITH THE F1956 ATTENUATORS - see board_app.c's SHARED-SPI-BUS
 * NOTE for why init ORDER matters (AD9361 first, then BoardApp_Init()). */
#define SPI_DEVICE_ID           XPAR_PSU_SPI_0_DEVICE_ID
#define SPI_CS                  0

/* AXI-DMAC channel that captures AD9361 RX ADC samples to memory (used by
 * src/stream_framer.c for the RF FFT spectrum stream, 2026-09-11). Same
 * dual-name defensive pattern as AD9361_RX_0_BASEADDR above - the real
 * xparameters.h you supplied defines both XPAR_AXI_DMAC_0_BASEADDR and
 * XPAR_AXI_AD9361_ADC_DMA_BASEADDR as the same physical address
 * (0x9C400000), just two Vivado-generated aliases for the same IP
 * instance; prefer the generic DMAC name, fall back to the AD9361-specific
 * one if a different Vivado version only emits that one. */
#ifdef XPAR_AXI_DMAC_0_BASEADDR
#define RX_DMA_BASEADDR         XPAR_AXI_DMAC_0_BASEADDR
#else
#define RX_DMA_BASEADDR         XPAR_AXI_AD9361_ADC_DMA_BASEADDR
#endif

#endif /* PARAMETERS_H_ */
