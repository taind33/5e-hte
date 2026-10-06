// ***************************************************************************
// ***************************************************************************
// Copyright (C) 2016-2023 Analog Devices, Inc. All rights reserved.
//
// In this HDL repository, there are many different and unique modules, consisting
// of various HDL (Verilog or VHDL) components. The individual modules are
// developed independently, and may be accompanied by separate and unique license
// terms.
//
// The user should read each of these license terms, and understand the
// freedoms and responsibilities that he or she has by using this source/core.
//
// This core is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.
//
// Redistribution and use of source or resulting binaries, with or without modification
// of this file, are permitted under one of the following two license terms:
//
//   1. The GNU General Public License version 2 as published by the
//      Free Software Foundation, which can be found in the top level directory
//      of this repository (LICENSE_GPL2), and also online at:
//      <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>
//
// OR
//
//   2. An ADI specific BSD license, which can be found in the top level directory
//      of this repository (LICENSE_ADIBSD), and also on-line at:
//      https://github.com/analogdevicesinc/hdl/blob/main/LICENSE_ADIBSD
//      This will allow to generate bit files and not release the source code,
//      as long as it attaches to an ADI device.
//
// ***************************************************************************
// ***************************************************************************

`timescale 1ns/100ps

module system_top (

  output [1:0]gpio_delay,
  output [0:0]le1_att_spi,
  output [0:0]le2_att_spi,
  output [0:0]le3_att_spi,
  output qspi_adc_clk,
  input qspi_adc_miso,
  output qspi_adc_mosi,
  output [0:0]qspi_adc_cs,
  input rs232_rx_ac_pwr,
  input rs232_rx_dc_pwr,
  output rs232_tx_ac_pwr,
  output rs232_tx_dc_pwr,
  input rs422_rx_pc,
  output rs422_tx_pc,
  output spi1_att_clk,
  output spi1_att_data,

  // input   [12:0]  gpio_bd_i,
  // output  [ 7:0]  gpio_bd_o,
  
//  input tele_rx,
//  output tele_tx,

  input           rx_clk_in_p,
  input           rx_clk_in_n,
  input           rx_frame_in_p,
  input           rx_frame_in_n,
  input   [ 5:0]  rx_data_in_p,
  input   [ 5:0]  rx_data_in_n,
//  output          tx_clk_out_p,
//  output          tx_clk_out_n,
//  output          tx_frame_out_p,
//  output          tx_frame_out_n,
//  output  [ 5:0]  tx_data_out_p,
//  output  [ 5:0]  tx_data_out_n,

  output          enable,
  output          txnrx,

  output          gpio_resetb,
  output          gpio_sync,
  output          gpio_en_agc,
  output  [ 3:0]  gpio_ctl,
  input   [ 7:0]  gpio_status,

  output          spi0_ad9361_csn,
  output          spi0_ad9361_clk,
  output          spi0_ad9361_mosi,
  input           spi0_ad9361_miso
);

  // internal signals

  wire    [94:0]  gpio_i;
  wire    [94:0]  gpio_o;
  wire    [ 2:0]  spi0_csn;

  // defaults

  assign gpio_resetb = gpio_o[46:46];
  assign gpio_sync = gpio_o[45:45];
  assign gpio_en_agc = gpio_o[44:44];
  assign gpio_ctl = gpio_o[43:40];
  // assign gpio_bd_o = gpio_o[20:13];

  assign gpio_i[94:40] = gpio_o[94:40];
  assign gpio_i[39:32] = gpio_status;
  assign gpio_i[31:13] = gpio_o[31:13];
  // assign gpio_i[12: 0] = gpio_bd_i;

  assign spi0_ad9361_csn = spi0_csn[0];

  // instantiations

  system_wrapper i_system_wrapper (
    .gpio_delay(gpio_delay),
    .le1_att_spi(le1_att_spi),
    .le2_att_spi(le2_att_spi),
    .le3_att_spi(le3_att_spi),
    .qspi_adc_clk(qspi_adc_clk),
    .qspi_adc_miso(qspi_adc_miso),
    .qspi_adc_mosi(qspi_adc_mosi),
    .qspi_adc_cs(qspi_adc_cs),
    .rs232_rx_ac_pwr(rs232_rx_ac_pwr),
    .rs232_rx_dc_pwr(rs232_rx_dc_pwr),
    .rs232_tx_ac_pwr(rs232_tx_ac_pwr),
    .rs232_tx_dc_pwr(rs232_tx_dc_pwr),
    .rs422_rx_pc(rs422_rx_pc),
    .rs422_tx_pc(rs422_tx_pc),
    .spi1_att_clk (spi1_att_clk),
    .spi1_att_data (spi1_att_data),
    .enable (enable),
    .gpio_i (gpio_i),
    .gpio_o (gpio_o),
    .gpio_t (),
    .rx_clk_in_n (rx_clk_in_n),
    .rx_clk_in_p (rx_clk_in_p),
    .rx_data_in_n (rx_data_in_n),
    .rx_data_in_p (rx_data_in_p),
    .rx_frame_in_n (rx_frame_in_n),
    .rx_frame_in_p (rx_frame_in_p),
    .spi0_csn (spi0_csn),
    .spi0_ad9361_miso (spi0_ad9361_miso),
    .spi0_ad9361_mosi (spi0_ad9361_mosi),
    .spi0_ad9361_clk (spi0_ad9361_clk),
//    .spi1_csn (),
//    .spi1_miso (1'b0),
//    .spi1_mosi (),
//    .spi1_sclk (),
    .tdd_sync_i (1'b0),
    .tdd_sync_o (),
    .tdd_sync_t (),
//    .tele_rx(tele_rx),
//    .tele_tx(tele_tx),
    .tx_clk_out_n (),
    .tx_clk_out_p (),
    .tx_data_out_n (),
    .tx_data_out_p (),
    .tx_frame_out_n (),
    .tx_frame_out_p (),
    .txnrx (txnrx),
    .up_enable (gpio_o[47]),
    .up_txnrx (gpio_o[48]));

endmodule
