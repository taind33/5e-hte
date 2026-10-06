# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct D:\taind33\FPGA_Xilinx\ADI_v2\hdl\projects\fmcomms2\k26\fmcomms2_k26.sw\noos\5e_hte_pf\platform.tcl
# 
# OR launch xsct and run below command.
# source D:\taind33\FPGA_Xilinx\ADI_v2\hdl\projects\fmcomms2\k26\fmcomms2_k26.sw\noos\5e_hte_pf\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {5e_hte_pf}\
-hw {D:\taind33\FPGA_Xilinx\ADI_v2\hdl\projects\fmcomms2\k26\system_top.xsa}\
-proc {psu_cortexa53_0} -os {standalone} -arch {64-bit} -fsbl-target {psu_cortexa53_0} -out {D:/taind33/FPGA_Xilinx/ADI_v2/hdl/projects/fmcomms2/k26/fmcomms2_k26.sw/noos}

platform write
platform generate -domains 
platform active {5e_hte_pf}
bsp reload
bsp write
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform active {5e_hte_pf}
platform clean
platform generate
platform clean
platform generate
platform clean
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform config -updatehw {D:/taind33/FPGA_Xilinx/ADI_v2/hdl/projects/fmcomms2/k26/system_top.xsa}
bsp reload
bsp reload
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform clean
platform generate
platform generate -domains standalone_domain,zynqmp_fsbl,zynqmp_pmufw 
platform clean
platform generate
platform clean
platform generate
