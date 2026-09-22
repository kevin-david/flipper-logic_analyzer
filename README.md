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
Then start PulseView and add a new "Openbench Logic Sniffer (ols)" and select the Flipper serial port.

When arming, you can now look at the trace in PulseView.

Changes:
 - all 8 channels supported Channel 0 is C0, Channel 1 is C1, ... Channel 7 is A7
 - sample rates up to 100 kHz are supported
 - capture starts immediately when no trigger is configured
 - if a trigger mask is defined, capture starts when a selected signal changes
 - sample count is capped to 16384

Discussion thread: https://discord.com/channels/740930220399525928/1074401633615749230
 
