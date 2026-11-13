set here [file dirname [file normalize [info script]]]
set pluto_build_threads 2
if {[info exists ::env(PLUTO_BUILD_THREADS)]} {set pluto_build_threads $::env(PLUTO_BUILD_THREADS)}
set_param general.maxThreads $pluto_build_threads
set root [file normalize "$here/.."]
set out "$root/build/fpga"
if {[file exists "$out/vivado"]} {error "Clean build/fpga/vivado before rebuilding"}
file mkdir $out
open_project "$here/base/vivado/multi_ul_trial.xpr"
set_property ip_repo_paths [list "$here/ip_repo"] [current_project]
update_ip_catalog
save_project_as -exclude_run_results profile_ul "$out/vivado"
import_files -force -fileset sources_1
import_files -force -fileset constrs_1
foreach pattern {*tetra_channelizer.v *tetra_ddc_cic_lane.v *tetra_fir_dec2.v} {
 set old [get_files -all -quiet $pattern];if {[llength $old]} {remove_files $old}
}
add_files -norecurse [list $here/rtl/tetra_channelizer.v $here/rtl/tetra_ddc_cic_lane.v $here/rtl/tetra_fir_dec2.v]
set oldmem [get_files -all -quiet *tetra_sin500.mem]
if {[llength $oldmem]} {remove_files $oldmem}
add_files -norecurse "$here/rtl/tetra_sin500.mem"
update_compile_order -fileset sources_1
open_bd_design [get_files system.bd]
set saved_nets {}
foreach pin [get_bd_pins tetra_chan/*] {
 set net [get_bd_nets -quiet -of_objects $pin]
 if {[llength $net]} {lappend saved_nets [list [file tail $pin] [get_bd_pins -of_objects $net -filter {NAME !~ */tetra_chan/* && NAME !~ tetra_chan/*}]]}
}
delete_bd_objs [get_bd_cells tetra_chan]
create_bd_cell -type module -reference tetra_channelizer tetra_chan
foreach pair $saved_nets {lassign $pair pin peers;connect_bd_net [get_bd_pins tetra_chan/$pin] [get_bd_pins $peers]}
set_property CONFIG.NUM_MI 7 [get_bd_cells axi_cpu_interconnect]
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 profile_config
set_property -dict [list CONFIG.NUM_PORTS 4 CONFIG.IN0_WIDTH 32 CONFIG.IN1_WIDTH 32 CONFIG.IN2_WIDTH 32 CONFIG.IN3_WIDTH 32] [get_bd_cells profile_config]
connect_bd_net [get_bd_pins profile_config/dout] [get_bd_pins tetra_chan/cfg]
for {set i 0} {$i<2} {incr i} {
 set name profile_gpio$i
 create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 $name
 set_property -dict [list CONFIG.C_GPIO_WIDTH 32 CONFIG.C_ALL_OUTPUTS 1 CONFIG.C_IS_DUAL 1 CONFIG.C_GPIO2_WIDTH 32 CONFIG.C_ALL_OUTPUTS_2 1 CONFIG.C_DOUT_DEFAULT 0x00000000 CONFIG.C_DOUT_DEFAULT_2 0x00000000] [get_bd_cells $name]
 set master [format M%02d [expr {$i+5}]]
 connect_bd_intf_net [get_bd_intf_pins axi_cpu_interconnect/${master}_AXI] [get_bd_intf_pins $name/S_AXI]
 connect_bd_net [get_bd_pins sys_ps7/FCLK_CLK0] [get_bd_pins axi_cpu_interconnect/${master}_ACLK] [get_bd_pins $name/s_axi_aclk]
 connect_bd_net [get_bd_pins sys_rstgen/peripheral_aresetn] [get_bd_pins axi_cpu_interconnect/${master}_ARESETN] [get_bd_pins $name/s_axi_aresetn]
 connect_bd_net [get_bd_pins $name/gpio_io_o] [get_bd_pins profile_config/In[expr {$i*2}]]
 connect_bd_net [get_bd_pins $name/gpio2_io_o] [get_bd_pins profile_config/In[expr {$i*2+1}]]
 assign_bd_address -offset [expr {0x43C00000+$i*0x10000}] -range 4K -target_address_space [get_bd_addr_spaces sys_ps7/Data] [get_bd_addr_segs $name/S_AXI/Reg]
}
validate_bd_design
save_bd_design
generate_target all [get_files system.bd]
reset_run synth_1
launch_runs synth_1 -jobs 1
wait_on_run synth_1
if {![string match "*Complete*" [get_property STATUS [get_runs synth_1]]]} {error "Synthesis failed"}
launch_runs impl_1 -to_step route_design -jobs 1
wait_on_run impl_1
if {![string match "*Complete*" [get_property STATUS [get_runs impl_1]]]} {error "Implementation failed"}
open_run impl_1
set sources [get_property SOURCE_PINS [get_clocks rx_clk]]
create_clock -name rx_clk -period 10.000 -waveform {0.000 5.000} [get_ports $sources]
report_timing_summary -file $out/timing_summary.rpt
report_utilization -file $out/utilization.rpt
foreach delay {max min} {if {[llength [get_timing_paths -quiet -delay_type $delay -slack_lesser_than 0 -max_paths 1]]} {error "Negative 100MHz timing slack: do not load"}}
write_checkpoint -force $out/validated100m.dcp
write_bitstream -force $out/system_top_100mhz.bit
puts "PROFILE_UL_100MHZ_VALIDATED"
close_project
