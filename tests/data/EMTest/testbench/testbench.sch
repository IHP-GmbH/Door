<Qucs Schematic 26.1.1>
<Properties>
  <View=0,-50,1100,700,0.9,0,0>
  <Grid=10,10,1>
  <DataSet=testbench.dat>
  <DataDisplay=testbench.dpl>
  <OpenDisplay=1>
  <showFrame=0>
  <FrameText0=EMTest cmim_2u3_tm1fix S-parameter TB>
  <FrameText1=Drawn By: LibMan>
  <FrameText2=Date:>
  <FrameText3=Revision:>
</Properties>
<Symbol>
</Symbol>
<Components>
  <Pac P1 1 200 280 18 -26 0 1 "1" 1 "50 Ohm" 1 "0 dBm" 0 "1 GHz" 0 "26.85" 0 "true" 0>
  <GND * 1 200 350 0 0 0 0>
  <Lib X1 1 400 250 35 -16 0 0 "EMTest" 0 "cmim_2u3_tm1fix" 0>
  <Pac P2 1 600 280 18 -26 0 1 "2" 1 "50 Ohm" 1 "0 dBm" 0 "1 GHz" 0 "26.85" 0 "true" 0>
  <GND * 1 600 350 0 0 0 0>
  <.SP SP1 1 150 80 0 61 0 0 "log" 1 "100 MHz" 1 "100 GHz" 1 "101" 1 "no" 0 "1" 0 "2" 0 "no" 0 "no" 0>
  <NutmegEq NutmegEq1 1 150 420 -30 16 0 0 "SP1" 1 "S11_dB=dB(S_1_1)" 1 "S21_dB=dB(S_2_1)" 1 "S12_dB=dB(S_1_2)" 1 "S22_dB=dB(S_2_2)" 1>
</Components>
<Wires>
  <200 250 369 250 "" 0 0 0 "">
  <431 250 600 250 "" 0 0 0 "">
  <200 310 200 350 "" 0 0 0 "">
  <600 310 600 350 "" 0 0 0 "">
</Wires>
<Diagrams>
  <Smith 720 400 360 280 3 #c0c0c0 1 00 1 0 1 1 1 0 4 1 1 0 1 1 315 0 225 1 0 0 "" "" "">
	<"ngspice/ac.s_2_1" #0000ff 1 3 0 0 0 0>
  </Smith>
</Diagrams>
<Paintings>
  <Text 350 40 14 #000000 0 "cmim_2u3_tm1fix lookalike (EMTest)">
</Paintings>
