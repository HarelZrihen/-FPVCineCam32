# FPVCineCam32 v0.13.0

FPVCineCam32 is an ESP32-C3 / ESP32-S3 camera-control interface for FPV systems. It connects supported cameras over Bluetooth and integrates with Betaflight MSP for RC REC/STOP control and DJI/Betaflight OSD status.

**Requires Betaflight 2025.12 or later.**

## Tested cameras

- Blackmagic Pocket Cinema Camera 4K
- GoPro HERO12
- GoPro HERO13
- DJI Osmo Action 5 Pro

Other recent GoPro models using the same Open GoPro BLE protocol should be compatible but are not yet physically tested. Other Blackmagic cameras using the same Blackmagic Camera Control BLE protocol may also work but are currently unverified.

## Main functions

- REC/STOP from a Betaflight RC channel
- Camera status in DJI/Betaflight OSD
- ESP-hosted Web UI for camera setup and pairing
- Persistent camera pairing/reconnect
- Multiple saved GoPros with one active connection at a time

## Wiring

| Flight controller | ESP32-C3 SuperMini |
| --- | --- |
| FC TX | GPIO6 / ESP RX |
| FC RX | GPIO7 / ESP TX |
| GND | GND |

MSP baud: `115200`

### ESP32-S3

ESP32-S3 boards (DevKitC-1, S3 SuperMini/Zero and similar) are supported with the same wiring: GPIO6 is ESP RX and GPIO7 is ESP TX. Build and flash the `esp32s3` PlatformIO environment over the board's native USB port:

```
pio run -e esp32s3 -t upload
```

The browser installer currently ships the ESP32-C3 build only.

## Firmware installer

https://nicstz.github.io/-FPVCineCam32/

For normal firmware updates, flash without erasing unless a release note specifically says otherwise. This preserves saved settings and camera pairings.

The browser installer uses Web Serial. Chrome or Edge on Mac/PC is recommended. iPhone/iPad Safari cannot be used for Web Serial flashing. The ESP-hosted Web UI itself can be used from a phone after the firmware is installed.

## Betaflight

Enable MSP on the UART connected to the ESP32-C3, then configure the REC/STOP RC channel in the FPVCineCam32 Web UI.

GPIO6 RX and GPIO7 TX are fixed for the current ESP32-C3 SuperMini hardware profile.

## OSD

Enable Betaflight **Custom Message 1** and **Custom Message 2** in the OSD layout.

### GoPro

Custom Message 1 shows camera state:

- `CAM OFF`
- `CAM CONNECT`
- `CAM READY`
- `CAM REC`
- `CAM ERROR`

Custom Message 2 shows battery and remaining video time, for example:

`BAT 36% SD 2H:28`

Enable **Custom Message 3** to see the video mode read from the camera: resolution, frame rate and lens, for example `4K 60 LINEAR`. It refreshes every 5 seconds while the camera is not recording. If you typed a name for the active preset in the settings page, that name is shown instead.

### Blackmagic

Custom Message 1 shows camera state:

- `STBY`
- `REC`

Custom Message 2 shows remaining recording time for the active media, for example:

`MEDIA 1:23:45`

### DJI

Custom Message 1 shows camera state:

- `CAM OFF`
- `CAM CONNECT`
- `CAM READY`
- `CAM REC`

Custom Message 2 shows battery and remaining recording time, for example:

`BAT 36% SD 2H:28`

Custom Message 3 shows the video mode reported by the camera, for example `4K 60 RS+` (resolution, frame rate, stabilization), `4K SLOMO 4X`, `TIMELAPSE` or `PHOTO`. Action 4/5 Pro/6/360 report it in their status; the Osmo Nano reports resolution and frame rate. On the Action 2 this is experimental and may stay blank.

## GoPro notes

- Put the GoPro into pairing mode for first pairing.
- Multiple GoPros can be saved; one is connected at a time.
- The last active GoPro becomes the next reconnect target.
- Forget removes only that GoPro and its Bluetooth bond.
- If a GoPro has fully powered down and is no longer advertising over BLE, power it on normally before use. FPVCineCam32 will reconnect when the camera becomes available.
- Remote GoPro wake/power-on is not currently implemented.

## Blackmagic notes

- First pairing uses the 6-digit PIN shown by the camera.
- REC/STBY is based on camera feedback rather than only on the outgoing command.
- If no usable media is present, the OSD will not falsely show REC.
- Remaining record time follows the active media.

## DJI notes

- Wireless connection must be enabled on the camera.
- Approve the verification code on the camera.
- FPVCineCam32 will reconnect automatically after pairing. If it does not, restart FPVCineCam32.
- REC/STOP is controlled from the configured Betaflight RC channel.
- Battery level and remaining recording time are shown in the OSD where available.
- Forget camera removes only the saved DJI camera connection.

## DJI Osmo Nano notes

- Select **DJI Osmo Nano** as the camera system. The Nano uses the same DUML protocol as the Action 2, not the DJI R SDK.
- REC/STOP, record state, battery and remaining SD time are read from the camera.
- Custom Message 3 shows the active video mode (resolution and frame rate), for example `4K 60`.
- The Nano records a corrupted low-FPS clip if recording starts while it is asleep. FPVCineCam32 holds REC until the camera is awake in photo or video mode; the OSD shows `CAM SLEEP` meanwhile.
- Not yet tested on Nano hardware with FPVCineCam32.

## Setup Wi-Fi

Setup Wi-Fi is temporary. If no client connects during the setup window, Wi-Fi shuts down while BLE camera control, MSP and OSD continue running. Wi-Fi returns after reboot.

Connect to the SoftAP named `FPVCineCam32-XXXX` (XXXX is unique per device) with password `fpvcinecam32`, then browse to `http://192.168.4.1`.

## Current scope

FPVCineCam32 currently exposes the camera information most useful during FPV operation:

- Recording state
- Battery level where available
- Remaining recording time

Resolution, FPS, FOV, ISO, white balance and similar camera settings are not currently part of FPVCineCam32.
