################################################################################
### Copyright (C) 2016-2023 Analog Devices, Inc. All rights reserved.
#### SPDX short identifier: ADIBSD
################################################################################

## constraints
## 5EFM
#set_property  -dict {PACKAGE_PIN  AE15    IOSTANDARD LVCMOS33} [get_ports i_adf4360_lock_detect];
#set_property  -dict {PACKAGE_PIN  AD14    IOSTANDARD LVCMOS33} [get_ports o_adf4360_data];
#set_property  -dict {PACKAGE_PIN  AH13    IOSTANDARD LVCMOS33} [get_ports o_adf4360_le];
#set_property  -dict {PACKAGE_PIN  AG13    IOSTANDARD LVCMOS33} [get_ports o_adf4360_sclk];

#set_property  -dict {PACKAGE_PIN  AB3    IOSTANDARD LVCMOS18} [get_ports o_rx_lna];
#set_property  -dict {PACKAGE_PIN  AA10    IOSTANDARD LVCMOS33} [get_ports o_rx_lo_pwr];

#set_property  -dict {PACKAGE_PIN  AB11    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_1[0]];
#set_property  -dict {PACKAGE_PIN  W14    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_1[1]];
#set_property  -dict {PACKAGE_PIN  AA11    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_1[2]];
#set_property  -dict {PACKAGE_PIN  AB13    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_1[3]];
#set_property  -dict {PACKAGE_PIN  AD10    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_1[4]];

#set_property  -dict {PACKAGE_PIN  AG10    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_2[0]];
#set_property  -dict {PACKAGE_PIN  AC12    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_2[1]];
#set_property  -dict {PACKAGE_PIN  AH10    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_2[2]];
#set_property  -dict {PACKAGE_PIN  AD12    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_2[3]];
#set_property  -dict {PACKAGE_PIN  AF11    IOSTANDARD LVCMOS33} [get_ports o_adrf5700_2[4]];

## Tele UART
#set_property PACKAGE_PIN AC8 [get_ports tele_tx];
#set_property IOSTANDARD LVCMOS18 [get_ports tele_tx];

## EMIO UART 0
#set_property PACKAGE_PIN H12 [get_ports uart0_rx];
#set_property IOSTANDARD LVCMOS33 [get_ports uart0_rx];

#set_property PACKAGE_PIN J12 [get_ports uart0_tx];
#set_property IOSTANDARD LVCMOS33 [get_ports uart0_tx]

## Adl5904
#set_property PACKAGE_PIN E1 [get_ports o_adl5904_rst];
#set_property IOSTANDARD LVCMOS18 [get_ports o_adl5904_rst];

#set_property PACKAGE_PIN E2 [get_ports i_adl5904_q];
#set_property IOSTANDARD LVCMOS18 [get_ports i_adl5904_q];

## Monitor
#set_property PACKAGE_PIN F7 [get_ports i_cmd[5]];
#set_property IOSTANDARD LVCMOS18 [get_ports i_cmd[5]];

#set_property PACKAGE_PIN G8 [get_ports i_cmd[4]];
#set_property IOSTANDARD LVCMOS18 [get_ports i_cmd[4]];

#set_property PACKAGE_PIN F6 [get_ports i_cmd[3]];
#set_property IOSTANDARD LVCMOS18 [get_ports i_cmd[3]];

#set_property PACKAGE_PIN A1 [get_ports i_cmd[2]];
#set_property IOSTANDARD LVCMOS18 [get_ports i_cmd[2]];

#set_property PACKAGE_PIN A2 [get_ports i_cmd[1]];
#set_property IOSTANDARD LVCMOS18 [get_ports i_cmd[1]];

#set_property PACKAGE_PIN B3 [get_ports i_cmd[0]];
#set_property IOSTANDARD LVCMOS18 [get_ports i_cmd[0]];

#set_property PACKAGE_PIN E4 [get_ports ad7490_spi_miso];
#set_property IOSTANDARD LVCMOS18 [get_ports ad7490_spi_miso];

#set_property PACKAGE_PIN E3 [get_ports ad7490_spi_clk];
#set_property IOSTANDARD LVCMOS18 [get_ports ad7490_spi_clk];

#set_property PACKAGE_PIN B1 [get_ports ad7490_spi_mosi];
#set_property IOSTANDARD LVCMOS18 [get_ports ad7490_spi_mosi];

#set_property PACKAGE_PIN C1 [get_ports ad7490_cs_l];
#set_property IOSTANDARD LVCMOS18 [get_ports ad7490_cs_l];

#set_property PACKAGE_PIN F10 [get_ports io_sda];
#set_property IOSTANDARD LVCMOS33 [get_ports io_sda];

#set_property PACKAGE_PIN J11 [get_ports o_scl];
#set_property IOSTANDARD LVCMOS33 [get_ports o_scl];

#set_property PACKAGE_PIN G6 [get_ports o_action_fire[0]];
#set_property IOSTANDARD LVCMOS18 [get_ports o_action_fire[0]];

#set_property PACKAGE_PIN A3 [get_ports o_action_fire[1]];
#set_property IOSTANDARD LVCMOS18 [get_ports o_action_fire[1]];

## Clock 50Mhz
#set_property PACKAGE_PIN G11 [get_ports clk_50mhz];
#set_property IOSTANDARD LVCMOS33 [get_ports clk_50mhz];
#set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets clk_50mhz_IBUF_inst/O];

