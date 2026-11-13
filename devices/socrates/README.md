# Device: Qualcomm Technologies, Inc. 8550 socrates (socrates)

Made by `tools/new-device.py` on 2026-11-12, from the file socrates.dtb. Edit it as the port goes.

| What | Value |
|---|---|
| Board (the tree's `model`) | Qualcomm Technologies, Inc. 8550 socrates |
| Compatible | `qcom,kalama-mtp qcom,kalama qcom,mtp` |

## The SoC addresses

**This tree differs from the reference phone's** in 7 value(s) and lacks 4: `soc_ipa_base`, `soc_gsi_base`, `soc_rsc`, `soc_smem`, `soc_ipa_stream`, `soc_ipa_stream_uc`, `soc_ipa_fw`, `soc_cmd_db`, `soc_mpss_mem`, `modem_ipa_fw_at`, `soc_pas_mpss`. Those are this phone's own addresses; `soc.args` carries them to the kernel and the run scripts add the file to the command line.

| Argument | Value | Against the reference |
|---|---|---|
| `soc_ipa_base` | `0x3e40000` | **differs from the reference** (`0x1e40000`) |
| `soc_gsi_base` | `0x3e04000` | **differs from the reference** (`0x1e04000`) |
| `soc_ipcc` | `0x408000` | the same as the reference |
| `soc_aoss` | `0xc300000` | the same as the reference |
| `soc_rsc` | `0x17a20000` | **differs from the reference** (`0x18220000`) |
| `soc_apps_smmu` | `0x15000000` | the same as the reference |
| `soc_cmd_db` | `(not found)` | **missing**: the tree has no node this was read from; see the table in devices/redwood/README.md |
| `soc_mpss_mem` | `(not found)` | **missing**: the tree has no node this was read from; see the table in devices/redwood/README.md |
| `modem_ipa_fw_at` | `(not found)` | **missing**: the tree has no node this was read from; see the table in devices/redwood/README.md |
| `soc_smem` | `0x81d00000` | **differs from the reference** (`0x80900000`) |
| `soc_imem` | `0x14600000` | the same as the reference |
| `soc_ipa_stream` | `0x4a0` | **differs from the reference** (`0x480`) |
| `soc_ipa_stream_uc` | `0x4a2` | **differs from the reference** (`0x482`) |
| `soc_pas_mpss` | `(not found)` | **missing**: the tree has no node this was read from; see the table in devices/redwood/README.md |
| `soc_pas_ipa` | `0xf` | the same as the reference |
| `soc_ipa_fw` | `ipa_fws` | **differs from the reference** (`yupik_ipa_fws`) |

`soc.args` is what `tools/soc-args.py` read out of the tree; `pull-soc.sh` makes it again from the phone on
adb. The run scripts add it to the kernel command line. Where each argument comes from in the tree:
`devices/redwood/README.md`.

## What is this phone's own

The kernel, loader and user space know nothing about a phone: addresses, interrupts, rails and firmware
come from its tree and its Android. What is written for one phone is the list below. The head of each file
says where its numbers came from; that is the map of what to rewrite.

- [ ] **UEFI**: the firmware that starts the loader: devices/redwood/uefi/ is a port of Mu-Silicium; the vendor's drivers are extracted from your own phone (uefi/README.md)
- [ ] **Display, buttons, touch**: drivers/input/hakios.input.c, hakios.buttons.c, hakios.goodix.c (the reference phone's controller; another phone has another)
- [ ] **Storage**: hakios.ufs.c and hakios.ufshc.c (UFS); a phone with eMMC needs another driver
- [ ] **CPU clocks**: hakios.cpufreq.c
- [ ] **GPU**: hakios.adreno.c (Adreno 642L here: another generation needs its own firmware and registers)
- [ ] **Wi-Fi**: hakios.wifi.c, hakios.lx.c and wifi/lx/ (the WCN6750 radio of the reference phone)
- [ ] **Audio and battery**: hakios.adsp.c, hakios.audio.c, hakios.sound.c, hakios.speaker.c, hakios.mic.c, hakios.battery.c
- [ ] **Cellular**: hakios.mpss.c, hakios.qmi.c, hakios.nas.c, hakios.ipa.c, hakios.cell.c; soc.args carries the addresses, the IPA layout is for IPA 4.11 (docs/cellular.md)
- [ ] **Haptics and cameras**: hakios.haptic.c, hakios.camera.c

## Configuration

The kernel's settings are layered the way an Android kernel's are: the common kernel, the SoC's, this phone's
(`docs/configs.md`). Two short files make this phone a configuration:

    configs/socrates.config          CONFIG_DEVICE="socrates"   and whatever is true of this phone only
    configs/socrates_defconfig       #include common_defconfig
                                   #include <the SoC's fragment: sc7280.config for the SC7280 family>
                                   #include socrates.config

Then `make socrates_defconfig`. Turn on only what this phone has been shown to do; `tools/hakios-config.py check` says what the
choice needs.

The drivers are part of the configuration too (`docs/drivers-config.md`): every one is built unless a configuration leaves it out,
and the addresses, pins and rails in each are the reference phone's. Leave out in `configs/socrates.config` what has not been
ported to this phone yet (`# CONFIG_HAKIOS_GOODIX is not set`, ...; `configs/qemu_defconfig` leaves out all of them), and put
each one back when it has been made to work here. `tools/hakios-config.py show` lists what is built in.

## Firmware

Pulled from your own phone, never committed: `devices/redwood/pull-firmware.sh`, or
`pull-firmware-root.sh` where the firmware is readable by root only. Check with `make doctor`.

## Before you open a pull request

`tools/provenance-check.py` must pass: no vendor firmware, no UEFI drivers, no Apple files in the tree.

## The repository

A HakiOS device repository: what is true of this device and of no other. The kernel, the loader, the drivers and the tools are
the common repository's; the sm8550 family's facts are in `soc/`, carried with the device. How the three are put together:
[docs/porting/repositories.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/repositories.md).

| File | What |
|---|---|
| `metal.args` | what `tools/metal-run.sh` gives this phone: its cores, its panel's rate, the threads that copy frames, the touch poll |
| `device.conf` | what the device is, and how far each part of it works: its page in the documentation is made from it |
| `configs/socrates_defconfig` | the common kernel, the SoC's fragment, this device's: `make socrates_defconfig` |
| `configs/socrates.config` | what is this device's own: the drivers built in, the arguments it needs |
| `soc/` | its SoC family's layer (sm8550): `soc.conf`, `configs/sm8550.config`, the SoC's drivers; carried here so that this one repository is all the device needs |
| `drivers/` | drivers of its own, if it needs any (`drivers/<subsystem>/conf`, as in the common repository) |

Every phone driver of the common repository is left out to begin with: their addresses and pins are the phone they were written
for. Put one back in `configs/socrates.config` when it has been made to work here, and say so in `device.conf`.

Check with `tools/hakios-device.py check socrates`.

## The port, as it stands (12 November 2026)

The chain runs on the phone and XNU reaches user space. What was added, and why:

| Where | What |
|---|---|
| `u-boot/files/dts/upstream/src/arm64/qcom/sm8550-xiaomi-socrates.dts` | U-Boot's device tree: the panel as ABL leaves it (1440 x 3200, 32 bits, in the phone's `cont_splash_region` at `0xb8000000`), the phone's carve-outs that `sm8550.dtsi` does not carry, the keys, UFS, USB as a device, and `hakios-payload` at `0xf0000000` |
| `u-boot/files/configs/hakios_socrates_defconfig` | `qcom_defconfig` + the phone settings + `CONFIG_CMD_HAKIOS` + `CONFIG_EFI_QCOM_USB_DEVICE`; the fastboot buffer at `0x82800000` (the phone's own free window, ending where the modem's image begins) |
| `u-boot/files/board/qualcomm/hakios-socrates.env` | the same boot flow as redwood's: `hakios load` from ABL's ramdisk, then `hakios boot` |
| `xnuloader/loader.c` (common repository) | `platform_socrates`: GICv3 at `0x17100000`/`0x17180000`, peripherals from `0x100000`, 8 cores, the three Cortex-A510s as the slow ones; the loader takes the phone by the framebuffer address `0xb8000000` |

Two things had to be left out of the USB node, and both are the SoC's, not the phone's:

- `qcom,select-utmi-as-pipe-clk`: for an SoC with no pipe clock at all. SM8550's PHY is eUSB2
  (`qcom,sm8550-snps-eusb2-phy`), and the bits U-Boot would set are at the SDM845 QSCRATCH offsets.
- the NOC AXI clock `GCC_CFG_NOC_USB3_PRIM_AXI_CLK` (11): U-Boot's SM8550 clock driver has no gate for it and
  `clk_get_bulk()` fails the whole controller over one clock it does not know (`gcc@0x10000: unknown clock ID 11`).
  ABL leaves it running, as it leaves the display running.

### Building and running it

```sh
# WSL, with an AArch64 GCC, dtc, bison, flex and lld-link (the Android NDK has lld-link):
uefi/u-boot/apply.sh            # U-Boot at BASE with hakios.patch and every device's files
uefi/u-boot/build.sh socrates   # u-boot/build-socrates/u-boot.bin
xnuloader/build.sh              # loader.efi (PLACEHOLDER=1 xnuloader/mkidentity.sh without an IPSW)
xnuloader/mkpayload.py -o usb-payload.bin loader.efi=xnuloader/loader.efi --usb
python3 tools/mkubootimg.py -d socrates -l xnuloader/loader.efi -p usb-payload.bin -o xnu-usb.img

# Windows (fastboot sees the phone's USB; WSL does not, without usbipd):
adb reboot bootloader
fastboot boot xnu-usb.img       # U-Boot, then the loader: getvar product answers xnuloader
fastboot stage run-payload.bin && fastboot oem run
fastboot oem log && fastboot get_staged log.txt
```

A run of the tree's prebuilt kernel reaches `bsd_init`, the panel (`MiniIOMobileFramebuffer: display 1125x2436 on a panel of
1440x3200 (5760 bytes per row)`) and user space: with the open `hello` RAM disk as the root, the kernel prints
`Hello from user space on Darwin!`.

### What is next

- the kernel from source with `make socrates_defconfig` (this machine has no theos and no IPSW's SDK yet; the prebuilt kernel
  is redwood's configuration);
- `launchd`: Darwin's user space is an IPSW's `system.apfs`, which is not in the repository;
- the drivers one at a time, in the order `docs/porting/index.md` gives, starting with the SoC's `SOCINFO`/`CPUFREQ`/`THERMAL`,
  then touch and buttons;
- `soc.args` still lacks `soc_cmd_db`, `soc_mpss_mem`, `modem_ipa_fw_at` and `soc_pas_mpss`: the tree names those nodes
  without a `reg` (ABL fills them), so the modem step needs them from the phone's own firmware.
