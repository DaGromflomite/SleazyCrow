# Synthetic Subaru ProtoPirate test .sub — replicates PPSubaru::generatePulseData.
# PWM: te_short=800 (HIGH=1), te_long=1600 (HIGH=0), 64-bit, 80x preamble, sync -4000.
TE_SHORT, TE_LONG = 800, 1600
DATA = 0x1A2B3C4D5E6F7788  # arbitrary 64-bit test key

pulses = []
# preamble: 80 long HIGH/LOW pairs; last LOW is the sync gap (-4000)
for i in range(80):
    pulses.append(TE_LONG)
    pulses.append(-TE_LONG if i < 79 else -4000)
# 64 data bits MSB first: 1 -> short HIGH, 0 -> long HIGH; each followed by -short LOW
for bit in range(63, -1, -1):
    pulses.append(TE_SHORT if (DATA >> bit) & 1 else TE_LONG)
    pulses.append(-TE_SHORT)
# end: short HIGH + long LOW gap to trigger processData()
pulses.append(TE_SHORT)
pulses.append(-4000)

# write Flipper .sub, splitting RAW_Data across lines (~word-wrapped)
lines = ["Filetype: Flipper SubGhz RAW File","Version: 1",
         "Frequency: 433920000","Preset: FuriHalSubGhzPresetOok650Async","Protocol: RAW"]
row=[]; cur=0
for v in pulses:
    s=str(v); 
    if cur + len(s) + 1 > 480 and row:
        lines.append("RAW_Data: "+" ".join(row)); row=[]; cur=0
    row.append(s); cur += len(s)+1
if row: lines.append("RAW_Data: "+" ".join(row))
out="\n".join(lines)+"\n"
open(r'C:/Users/Timot/AppData/Local/Temp/claude/61b590c80dee0748/fbe0d0bf-9db0-4960-a0a4-e0294e3d65e4/scratchpad/PPTest_Subaru.sub','w',newline='\n').write(out)
print("pulses:",len(pulses),"| data=0x%016X"%DATA)
print(out[:300],"...")
