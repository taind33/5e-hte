###############################################################################
## Copyright (C) 2016-2023 Analog Devices, Inc. All rights reserved.
### SPDX short identifier: ADIBSD
###############################################################################

# constraints
# Define SPI clock
create_clock -name spi0_clk      -period 40   [get_pins -hier */EMIOSPI0SCLKO];

# ad9361
set_property  -dict {PACKAGE_PIN  W8    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_clk_in_p]           ; ## G06  FMC_HPC0_LA00_CC_P
set_property  -dict {PACKAGE_PIN  Y8    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_clk_in_n]           ; ## G07  FMC_HPC0_LA00_CC_N
set_property  -dict {PACKAGE_PIN  AE9   IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_frame_in_p]         ; ## D08  FMC_HPC0_LA01_CC_P
set_property  -dict {PACKAGE_PIN  AE8   IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_frame_in_n]         ; ## D09  FMC_HPC0_LA01_CC_N
set_property  -dict {PACKAGE_PIN  K4    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_p[0]]       ; ## H07  FMC_HPC0_LA02_P
set_property  -dict {PACKAGE_PIN  K3    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_n[0]]       ; ## H08  FMC_HPC0_LA02_N
set_property  -dict {PACKAGE_PIN  J5    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_p[1]]       ; ## G09  FMC_HPC0_LA03_P
set_property  -dict {PACKAGE_PIN  J4    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_n[1]]       ; ## G10  FMC_HPC0_LA03_N
set_property  -dict {PACKAGE_PIN  H4    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_p[2]]       ; ## H10  FMC_HPC0_LA04_P
set_property  -dict {PACKAGE_PIN  H3    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_n[2]]       ; ## H11  FMC_HPC0_LA04_N
set_property  -dict {PACKAGE_PIN  N7    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_p[3]]       ; ## D11  FMC_HPC0_LA05_P
set_property  -dict {PACKAGE_PIN  N6    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_n[3]]       ; ## D12  FMC_HPC0_LA05_N
set_property  -dict {PACKAGE_PIN  J1    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_p[4]]       ; ## C10  FMC_HPC0_LA06_P
set_property  -dict {PACKAGE_PIN  H1    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_n[4]]       ; ## C11  FMC_HPC0_LA06_N
set_property  -dict {PACKAGE_PIN  U9    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_p[5]]       ; ## H13  FMC_HPC0_LA07_P
set_property  -dict {PACKAGE_PIN  V9    IOSTANDARD LVDS DIFF_TERM_ADV TERM_100} [get_ports rx_data_in_n[5]]       ; ## H14  FMC_HPC0_LA07_N
set_property  -dict {PACKAGE_PIN  AD5   IOSTANDARD LVDS} [get_ports tx_clk_out_p]                                 ; ## G12  FMC_HPC0_LA08_P
set_property  -dict {PACKAGE_PIN  AD4   IOSTANDARD LVDS} [get_ports tx_clk_out_n]                                 ; ## G13  FMC_HPC0_LA08_N
set_property  -dict {PACKAGE_PIN  L1    IOSTANDARD LVDS} [get_ports tx_frame_out_p]                               ; ## D14  FMC_HPC0_LA09_P
set_property  -dict {PACKAGE_PIN  K1    IOSTANDARD LVDS} [get_ports tx_frame_out_n]                               ; ## D15  FMC_HPC0_LA09_N
set_property  -dict {PACKAGE_PIN  N9    IOSTANDARD LVDS} [get_ports tx_data_out_p[0]]                             ; ## H16  FMC_HPC0_LA11_P
set_property  -dict {PACKAGE_PIN  N8    IOSTANDARD LVDS} [get_ports tx_data_out_n[0]]                             ; ## H17  FMC_HPC0_LA11_N
set_property  -dict {PACKAGE_PIN  P7    IOSTANDARD LVDS} [get_ports tx_data_out_p[1]]                             ; ## G15  FMC_HPC0_LA12_P
set_property  -dict {PACKAGE_PIN  P6    IOSTANDARD LVDS} [get_ports tx_data_out_n[1]]                             ; ## G16  FMC_HPC0_LA12_N
set_property  -dict {PACKAGE_PIN  L7    IOSTANDARD LVDS} [get_ports tx_data_out_p[2]]                             ; ## D17  FMC_HPC0_LA13_P
set_property  -dict {PACKAGE_PIN  L6    IOSTANDARD LVDS} [get_ports tx_data_out_n[2]]                             ; ## D18  FMC_HPC0_LA13_N
set_property  -dict {PACKAGE_PIN  K9    IOSTANDARD LVDS} [get_ports tx_data_out_p[3]]                             ; ## C14  FMC_HPC0_LA10_P
set_property  -dict {PACKAGE_PIN  J9    IOSTANDARD LVDS} [get_ports tx_data_out_n[3]]                             ; ## C15  FMC_HPC0_LA10_N
set_property  -dict {PACKAGE_PIN  AE3   IOSTANDARD LVDS} [get_ports tx_data_out_p[4]]                             ; ## C18  FMC_HPC0_LA14_P
set_property  -dict {PACKAGE_PIN  AF3   IOSTANDARD LVDS} [get_ports tx_data_out_n[4]]                             ; ## C19  FMC_HPC0_LA14_N
set_property  -dict {PACKAGE_PIN  K2    IOSTANDARD LVDS} [get_ports tx_data_out_p[5]]                             ; ## H19  FMC_HPC0_LA15_P
set_property  -dict {PACKAGE_PIN  J2    IOSTANDARD LVDS} [get_ports tx_data_out_n[5]]                             ; ## H20  FMC_HPC0_LA15_N
set_property  -dict {PACKAGE_PIN  J7    IOSTANDARD LVCMOS18} [get_ports enable]                                   ; ## G18  FMC_HPC0_LA16_P
set_property  -dict {PACKAGE_PIN  H7    IOSTANDARD LVCMOS18} [get_ports txnrx]                                    ; ## G19  FMC_HPC0_LA16_N

set_property  -dict {PACKAGE_PIN  AC7   IOSTANDARD LVCMOS18} [get_ports gpio_status[0]]                           ; ## G21  FMC_HPC0_LA20_P
set_property  -dict {PACKAGE_PIN  AB7   IOSTANDARD LVCMOS18} [get_ports gpio_status[1]]                           ; ## G22  FMC_HPC0_LA20_N
set_property  -dict {PACKAGE_PIN  AH2   IOSTANDARD LVCMOS18} [get_ports gpio_status[2]]                           ; ## H25  FMC_HPC0_LA21_P
set_property  -dict {PACKAGE_PIN  AH1   IOSTANDARD LVCMOS18} [get_ports gpio_status[3]]                           ; ## H26  FMC_HPC0_LA21_N
set_property  -dict {PACKAGE_PIN  L5    IOSTANDARD LVCMOS18} [get_ports gpio_status[4]]                           ; ## G24  FMC_HPC0_LA22_P
set_property  -dict {PACKAGE_PIN  AG5   IOSTANDARD LVCMOS18} [get_ports gpio_status[5]]                           ; ## G25  FMC_HPC0_LA22_N
set_property  -dict {PACKAGE_PIN  AG6   IOSTANDARD LVCMOS18} [get_ports gpio_status[6]]                           ; ## D23  FMC_HPC0_LA23_P
set_property  -dict {PACKAGE_PIN  H8    IOSTANDARD LVCMOS18} [get_ports gpio_status[7]]                           ; ## D24  FMC_HPC0_LA23_N
set_property  -dict {PACKAGE_PIN  AC2   IOSTANDARD LVCMOS18} [get_ports gpio_ctl[0]]                              ; ## H28  FMC_HPC0_LA24_P
set_property  -dict {PACKAGE_PIN  AC3   IOSTANDARD LVCMOS18} [get_ports gpio_ctl[1]]                              ; ## H29  FMC_HPC0_LA24_N
set_property  -dict {PACKAGE_PIN  AC4   IOSTANDARD LVCMOS18} [get_ports gpio_ctl[2]]                              ; ## G27  FMC_HPC0_LA25_P
set_property  -dict {PACKAGE_PIN  AB2   IOSTANDARD LVCMOS18} [get_ports gpio_ctl[3]]                              ; ## G28  FMC_HPC0_LA25_N
set_property  -dict {PACKAGE_PIN  H9    IOSTANDARD LVCMOS18} [get_ports gpio_en_agc]                              ; ## H22  FMC_HPC0_LA19_P
set_property  -dict {PACKAGE_PIN  M6    IOSTANDARD LVCMOS18} [get_ports gpio_sync]                                ; ## H23  FMC_HPC0_LA19_N
set_property  -dict {PACKAGE_PIN  AG8   IOSTANDARD LVCMOS18} [get_ports gpio_resetb]                              ; ## H31  FMC_HPC0_LA28_P

set_property  -dict {PACKAGE_PIN  R8    IOSTANDARD LVCMOS18  PULLTYPE PULLUP} [get_ports spi_csn]                 ; ## D26  FMC_HPC0_LA26_P
set_property  -dict {PACKAGE_PIN  AF8   IOSTANDARD LVCMOS18} [get_ports spi_clk]                                  ; ## D27  FMC_HPC0_LA26_N
set_property  -dict {PACKAGE_PIN  AD2   IOSTANDARD LVCMOS18} [get_ports spi_mosi]                                 ; ## C26  FMC_HPC0_LA27_P
set_property  -dict {PACKAGE_PIN  T8    IOSTANDARD LVCMOS18} [get_ports spi_miso]                                 ; ## C27  FMC_HPC0_LA27_N

# clocks
create_clock -name rx_clk       -period  4.00 [get_ports rx_clk_in_p]                                             ;
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets i_system_wrapper/system_i/axi_ad9361/inst/i_dev_if/i_clk/i_rx_clk_ibuf/O];