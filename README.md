# flipper-logic-analyzer
Source: https://github.com/g3gg0/flipper-logic_analyzer
I'm in the process of bringing this in line with the latest flipper firmwares. Right now it loads on my flipper, next I need to test its functionality for bugs. Here are the steps I used to upload:
1. Install [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) (`pip install --upgrade ufbt`)
2. Install [PulseView](https://www.sigrok.org/wiki/Downloads)
3. Clone this repo: `git clone https://github.com/ecopsychologer/flipper-logic-analyzer`
4. Change directories into the repo: `cd flipper-logic-analyzer`
5. Run the app `ufbt launch APPID=logic_analyzer`
6. Launch PulseView and connect to channels C0, C1, C3, B2, B3, A4, A6, A7  
   Try to start pulseview from the terminal
   if you encounter problems with the configuration pulseview via gui

       pulseview -d ols:conn=/dev/ttyACM1

#### continued instructions from the original developer:
Then start PulseView and add a new "Openbench Logic Sniffer (ols)" and select
the second Flipper serial port. The first port remains available for the
Flipper CLI. Port numbers depend on the host, so check them after launching
the app rather than assuming `/dev/ttyACM1`.

When arming, you can now look at the trace in PulseView.

## Input pull controls

While the analyzer is idle, press **Left** to cycle the GPIO input bias through
`P:FLT` (floating, the default), `P:DN` (internal pull-down), and `P:UP`
(internal pull-up). Pull-down gives disconnected inputs a stable low level for
manual 3.3 V tests without external resistors. Use floating mode when the
signals are already driven by the circuit under test.

The selected pull applies to all eight analyzer inputs. If the `T:10K` test
clock is active, A7 remains a timer output; disabling the clock restores A7 as
an input with the selected pull.

## Timing loopback

To check sampling timing against a hardware-generated reference clock:

1. Connect a jumper between PA7 (physical pin 2) and PC0 (physical pin 16).
2. Launch the logic analyzer and press **Right**. The display changes from
   `T:OFF` to `T:10K` and PA7 outputs a timer-driven 10 kHz square wave.
3. Capture at 100 or 200 kHz in PulseView. Channel 0 should alternate in runs
   of five samples at 100 kHz or ten samples at 200 kHz.
4. Press **Right** again to stop the test clock before removing the jumper. The
   app also stops the clock and restores PA7 as an input when exiting.

Changes:
 - all 8 channels supported Channel 0 is C0, Channel 1 is C1, ... Channel 7 is A7
 - the 200 kHz maximum matches the default used by the libsigrok OLS driver
   and is verified by reading back the timer-driven PA7 output
 - sampling uses absolute CPU-cycle deadlines; if firmware work delays a sample,
   the app waits for a new period instead of adding false catch-up samples;
   `O:n` reports the number of clock restarts after the capture
 - capture starts immediately when no trigger is configured
 - masked trigger values and up to four sequential SUMP trigger stages are supported
 - the requested capture ratio retains pre-trigger samples in a circular buffer;
   ring ordering is finalized after sampling to avoid a trigger-time gap
 - sample capacity is sized at startup from the largest contiguous heap block;
   the app reserves 32 KiB for firmware services, requires space for at least
   16,384 samples, and reports the resulting capacity to PulseView
 - the second USB serial port carries analyzer data while the first remains
   available for the Flipper CLI

Run the portable capture and SUMP tests with `make -C tests test`. The same
command runs in CI.

Discussion thread: https://discord.com/channels/740930220399525928/1074401633615749230
 
