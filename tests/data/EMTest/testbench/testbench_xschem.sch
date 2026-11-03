v {xschem version=Schematic 26.1.1 file_version=1.2}
G {}
K {schematic.header="<Qucs Schematic 26.1.1>" schematic.view=<View=0,-50,1100,700,0.9,0,0> schematic.view=<Grid=10,10,1> schematic.view=<DataSet=testbench.dat> schematic.view=<DataDisplay=testbench.dpl> schematic.view=<OpenDisplay=1> schematic.view=<showFrame=0> schematic.view="<FrameText0=EMTest cmim_2u3_tm1fix S-parameter TB>" schematic.view="<FrameText1=Drawn By: LibMan>" schematic.view=<FrameText2=Date:> schematic.view=<FrameText3=Revision:>}
V {}
S {}
E {}
N 600 310 600 350 {lab=GND}
N 200 310 200 350 {lab=GND}
N 200 250 369 250 {lab=}
N 431 250 600 250 {lab=}
C {analogLib/Pac.sym} 200 280 1 0 {name=P1 num=1 z0=50 ac=1 qucs.type=Pac rotate=1 mirror=0 core.primitive=analogLib/Pac.symbol.room}
C {analogLib/GND.sym} 200 350 0 0 {name=* lab=0 qucs.type=GND rotate=0 mirror=0 core.primitive=analogLib/GND.symbol.room}
C {cmim_2u3_tm1fix.sym} 400 250 0 0 {name=X1 qucs.type=Lib qucs.model=cmim_2u3_tm1fix rotate=0 mirror=0}
C {analogLib/Pac.sym} 600 280 1 0 {name=P2 num=2 z0=50 ac=0 qucs.type=Pac rotate=1 mirror=0 core.primitive=analogLib/Pac.symbol.room}
C {analogLib/GND.sym} 600 350 0 0 {name=* lab=0 qucs.type=GND rotate=0 mirror=0 core.primitive=analogLib/GND.symbol.room}
C {devices/code_shown.sym} 150 80 0 0 {name=SP1 value=".control
sp dec 33 100Meg 100G
write testbench_sp.raw
.endc" only_toplevel=true qucs.type=.SP rotate=0 mirror=0}
C {devices/code_shown.sym} 150 420 0 0 {name=NutmegEq1 value="* Qucs NutmegEq (S-param post)
.control
let S11_dB=dB(S_1_1)
let S21_dB=dB(S_2_1)
let S12_dB=dB(S_1_2)
let S22_dB=dB(S_2_2)
.endc
" only_toplevel=true qucs.type=NutmegEq rotate=0 mirror=0}
C {analogLib/launcher.sym} 890 460 0 0 {name=h5 descr="load waves" tclcommand="xschem raw_read $netlist_dir/testbench_sp.raw ac"}
B 2 720 120 1080 400 {flags=graph mode=Smith z0=50 y1=-0.002 y2=1 ypos1=0 ypos2=2 divy=5 subdivy=1 unity=1 x1=100000000 x2=100000000000 divx=5 subdivx=1 xlabmag=1.0 ylabmag=1.0 node="s_2_1" color=4 dataset=-1 unitx=1 logx=0 logy=0}
