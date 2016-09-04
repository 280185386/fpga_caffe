# Define the project for SDAccel
create_solution -name prj_winograd -dir . -force
#set_property platform vc690-admpcie7v3-1ddr-gen2 [current_project]
add_device -vbnv xilinx:adm-pcie-7v3:1ddr:1.0

# Host Compiler Flags
set_property -name host_cflags -value "-g -Wall -D FPGA_DEVICE -D C_KERNEL" -objects [current_project]

# Host Source Files
add_files "main.c"

# Kernel Definition
create_kernel winograd_pe -type c
add_files -kernel [get_kernels winograd_pe] "winograd_pe.c"

# Define Binary Containers
create_opencl_binary winograd_pe
set_property region "OCL_REGION_0" [get_opencl_binary winograd_pe]
create_compute_unit -opencl_binary [get_opencl_binary winograd_pe] -kernel [get_kernels winograd_pe] -name ocl_conv1
create_compute_unit -opencl_binary [get_opencl_binary winograd_pe] -kernel [get_kernels winograd_pe] -name ocl_conv2

#Compile the design for CPU based emulation
compile_emulation -flow cpu -opencl_binary [get_opencl_binary winograd_pe]

# Run the compiled application in CPU based emulation mode
run_emulation -flow cpu -args "winograd_pe.xclbin 1 64 2 1 64 224 112 112 2"
run_emulation -flow cpu -args "winograd_pe.xclbin 1 64 2 4 16 112 56 56 2"
run_emulation -flow cpu -args "winograd_pe.xclbin 1 512 2 256 2 14 7 8 2"

report_estimate

# Compile the application to run on the accelerator card
build_system

# Package the application binaries
package_system
