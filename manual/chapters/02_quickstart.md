# Quickstart

The fastest way to get beats out of the box.

\draftnote{The factory SD card image is not described in the project documents. Replace
steps 1 and 2 with the real download location and folder list once the factory image exists.}

1. Prepare an SD card formatted **FAT32** and copy the factory image files to its root folder.
   \verify{Factory card image name, download location and contents.}
2. Insert the card with the power off, then power on. The display shows the start-up screen
   while the card is read. Start-up can take a few seconds because the firmware repairs and
   settles the card contents before audio begins.
3. Press **Play**. The step LEDs start running.
4. Press some of the 16 step buttons in Voice mode. Track 1 plays.
5. Press voice button 2 and set some more steps. Track 2 plays as well.
6. Move a slider to change the level of a voice.
7. Press the **Load/Save** mode button. The Load page opens. Choose **Bank** and turn the encoder
   to another slot; the Bank loads when you confirm.
8. Press **Performance** mode, then one of the SEQ buttons to switch to another Scene.

## If something is wrong

- **Silence:** check that a plug is in a MAIN or OUT2 jack, check the voice slider, and check
  the voice's output route and fader mode.
- **`Img BAD s:...` at start-up:** the firmware image in flash failed its integrity check.
  Re-install the firmware; see the Firmware Update chapter.
- **Samples show as stale:** the sample catalogue on this device is from an older firmware.
  Reinstall samples with **Load > Samples** (see the Storage chapter).
