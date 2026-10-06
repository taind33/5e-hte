# Usage with Vitis IDE:
# In Vitis IDE create a Single Application Debug launch configuration,
# change the debug type to 'Attach to running target' and provide this 
# tcl script in 'Execute Script' option.
# Path of this script: D:\taind33\FPGA_Xilinx\ADI_v2\hdl\projects\fmcomms2\k26\fmcomms2_k26.sw\noos\5e_hte_app_system\_ide\scripts\systemdebugger_5e_hte_app_system_standalone.tcl
# 
# 
# Usage with xsct:
# To debug using xsct, launch xsct and run below command
# source D:\taind33\FPGA_Xilinx\ADI_v2\hdl\projects\fmcomms2\k26\fmcomms2_k26.sw\noos\5e_hte_app_system\_ide\scripts\systemdebugger_5e_hte_app_system_standalone.tcl
# 
connect -url tcp:127.0.0.1:3121
source C:/Xilinx/Vitis/2022.2/scripts/vitis/util/zynqmp_utils.tcl
targets -set -nocase -filter {name =~"APU*"}
rst -system
after 3000
targets -set -filter {jtag_cable_name =~ "my vendor co my board 0ABC01A" && level==0 && jtag_device_ctx=="jsn-my board-0ABC01A-04724093-0"}
fpga -file D:/taind33/FPGA_Xilinx/ADI_v2/hdl/projects/fmcomms2/k26/fmcomms2_k26.sw/noos/5e_hte_app/_ide/bitstream/system_top.bit
targets -set -nocase -filter {name =~"APU*"}
loadhw -hw D:/taind33/FPGA_Xilinx/ADI_v2/hdl/projects/fmcomms2/k26/fmcomms2_k26.sw/noos/5e_hte_pf/export/5e_hte_pf/hw/system_top.xsa -mem-ranges [list {0x80000000 0xbfffffff} {0x400000000 0x5ffffffff} {0x1000000000 0x7fffffffff}] -regs
configparams force-mem-access 1
targets -set -nocase -filter {name =~"APU*"}
set mode [expr [mrd -value 0xFF5E0200] & 0xf]
targets -set -nocase -filter {name =~ "*A53*#0"}
rst -processor
dow D:/taind33/FPGA_Xilinx/ADI_v2/hdl/projects/fmcomms2/k26/fmcomms2_k26.sw/noos/5e_hte_pf/export/5e_hte_pf/sw/5e_hte_pf/boot/fsbl.elf
set bp_9_3_fsbl_bp [bpadd -addr &XFsbl_Exit]
con -block -timeout 60
bpremove $bp_9_3_fsbl_bp
targets -set -nocase -filter {name =~ "*A53*#0"}
rst -processor
dow D:/taind33/FPGA_Xilinx/ADI_v2/hdl/projects/fmcomms2/k26/fmcomms2_k26.sw/noos/5e_hte_app/Debug/5e_hte_app.elf
configparams force-mem-access 0
targets -set -nocase -filter {name =~ "*A53*#0"}
con
