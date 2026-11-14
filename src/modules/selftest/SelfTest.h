#ifndef SleazyCrow_SelfTest_h
#define SleazyCrow_SelfTest_h

// On-hardware diagnostic battery for the dual-CC1101 sub-GHz TX path.
//
// Built only when -DSELFTEST_ON_BOOT=1 (see the `esp32dev-selftest` PlatformIO
// env). It runs once in setup(), *after* the radios are init()'d but *before*
// the CC1101 worker task and BLE start, so it owns the radios single-threaded.
// Everything prints over the USB serial console at 115200; the run ends in a
// halt loop so the log stays clean for capture. It writes nothing to flash/SD
// and is flashed only to a bench unit — reflash the release build to restore.
//
// Purpose: tell apart the candidate causes of "a .sub that works on a Flipper
// does nothing from the Crow" — pulse-timing error, dead/weak TX chain, power,
// and (relative) frequency offset between the two on-board radios.
class SelfTest {
public:
    static void run();
};

#endif
