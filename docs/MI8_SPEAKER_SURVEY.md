# Mi 8 speaker bring-up: read-only survey

The speaker path is not implemented yet. A successful iOS playback request does
not imply that audio reaches the hardware: the existing redwood audio device
has no registered Mi 8 sink.

The temporary, read-only survey built with `tools/build-speaker-survey-kernel.sh`
was run on the device on 2026-11-01. It observed the following at startup:

* QUP wrapper 0 AHB branch status registers were `0`, while the serial engine 5
  branch status was `0x80004220` (clock-off bit set). Consequently the survey
  deliberately skipped all access to that serial engine.
* GPIO 58–63, 76, 85 and 86 each had configuration `0x1`; none was configured
  for its required alternate audio or I2C function at the time of the survey.
* The stock device tree identifies the speaker amplifier as TAS2557 on I2C
  address `0x4c`, with reset GPIO 76 and the I2C controller at `0x00894000`.
  The original stock vendor configuration identifies QUAT_MI2S_RX as the
  speaker backend. GPIO 85 and 86 are the control bus pins.
* The stock device reported calibration data in `/persist/audio/tas2557_cal.bin`.
  It was not modified. A matching stock firmware file was found in the user's
  original ROM; its use and distribution rights have not been established.
* The stock mixer describes 24-bit output while the amplifier device tree has
  `ti,i2s-bits=16`. These refer to different configuration layers and must be
  reconciled from hardware-specific evidence before an output stream is tried.

The test did not access amplifier registers, change pin or clock configuration,
or play audio. The system automatically returned to TWRP after a save; snapshot
generation 32 passed a full CRC check. The diagnostic log is stored locally at
`[本地实验目录]`.

Further work requires a verified hardware-specific amplifier initialization,
including the original calibration and protection behavior, and a supported
DSP-to-QUAT_MI2S playback path. The existing driver for a different phone is
useful architectural context but is not a pin or amplifier configuration for
this device. Avoid copying Linux driver code into XNU without first resolving
license compatibility.

Hardware register references (consulted for facts, not copied source):

* https://github.com/torvalds/linux/blob/master/drivers/clk/qcom/gcc-sdm845.c
* https://github.com/torvalds/linux/blob/master/drivers/pinctrl/qcom/pinctrl-sdm845.c
