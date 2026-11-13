# Socrates — where the port stands, and what to do next

A handover for whoever continues. Everything here was established by runs on the phone or by reading the phone's own
driver module; nothing is assumed. The measurements themselves are in [TOUCH.md](TOUCH.md).

## What works, verified on the device

The touch driver `drivers/input/hakios.socrates-touch.c` brings the Goodix GT9916 up completely:

```
goodix: I/O supply on (1.8 V)
goodix: analog supply on (3.2 V)
goodix: the chip took the SPI mode byte (0xC900 = 0x1)
goodix: chip "BERLIN", firmware "9916R" 05.80.02.63, sensor 255
goodix: touch report at 0x10308, coordinates up to 14400 x 32000
goodix: config: command 04 answered 00 80 00 04 08 00
goodix: config: 1488 bytes written and read back the same
goodix: goodix touch screen ready
```

To get there these had to be found and fixed; every one of them is a trap the next person would fall into again:

| What looked true | What is true |
|---|---|
| The pin controller is at `0xf000000` (the device tree says so) | It is at **`0x0f100000`**, `0x1000` per pin. Every write into `0xf000000` is dropped at every offset |
| The RPMh slot sets use the 2.7 layout of the reference driver | They are the **3.0** layout: control `0x24`, status `0x28`, command enable `0x2c`, commands from `0x34`, 24 bytes apart |
| The high-power mode of a PMIC is 4 (retention) | On this PMIC4 it is **7**; 4 resets the phone outright |
| `delay_for_interval` waits | Past its spin threshold it waits for a timer wakeup that never comes here. Count `CNTPCT_EL0` (19.2 MHz) instead |
| `assert_wait(NULL, ...)` blocks | It panics (NO_EVENT); a real event object is needed, as `no_init` does |
| A command frame is `[len][cmd][data][checksum]` | It is `[0][0][length][command][checksum16]` — the checksum of the two bytes before it, at `[4..5]`. Put anything else there and the chip silently keeps what it was given instead of answering `0x80` |

The chip's description parses to command `0x10174`, frame data `0x10400`, touch data `0x10308` and a panel of 14400 × 32000 —
the same numbers Android's own driver prints (`Touch-Data: 0x10308, 8`).

## What does not work

**The chip never presents a touch report.** The first byte of the report buffer at `0x10308` never has bit 7 set, which is
exactly what the chip's own handler tests (`if ((byte0 & 0x80) == 0) → no touch`; bit 6 with the event in byte 2 is a chip
request). Meanwhile the frame counter at `0x10400` steps every frame, so the chip is scanning.

Ruled out by measurement, so nobody repeats them:

- **The report address** — four addresses read side by side every poll. `0x10308` is constant, `0x10400` counts frames,
  `0x13588` and `0x14280` hold live 16-bit sample arrays. No status byte with bit 7 anywhere.
- **The interrupt** — the pin is pulled down (Android's own table: `gpio25 : in low func0 2mA pull down`) and the chip holds
  the line high; `touch_int_high=1` reads it that way. Polarity is not the problem.
- **Acknowledging** — `touch_ack=1` writes the one zero byte the chip's handler writes after a report. Accepted; no next
  frame follows. Writing it *before* the read (`touch_arm=1`) destroys the report instead.
- **Downloading the configuration** — the commands are acknowledged and the 1488 bytes read back equal, but afterwards the
  frame counter stops: `0x10218` is not the configuration area and writing there stops the scan. Android's driver never
  downloads a configuration on this phone (`config id is equal 0x640fdf7d, skiped`), so the chip already carries one.
- **Repainting the framebuffer** (`touch_paint=1`) — no change.
- **ftrace/kprobes on Android** — probes can be created but never fire on that kernel; a probe on `vfs_read` yields nothing
  either, so the instrument itself is dead there.
- **Android's `read_cfg` sysfs node** — `/sys/devices/platform/goodix_ts.0/read_cfg` needs a callback that is not registered
  (`EINVAL`).

What is still needed: the GT9916's report and mode documentation, or one reading of `0x10308` taken on the phone while
Android is running the same touch screen. That single number settles whether the chip lacks a configuration or the port
reads the wrong place.

## The boot, which is the second half of the problem

Without a userland the panel shows a still frame, and some touch controllers only report while the panel is refreshed — so
the boot matters for the touch too. It stops at:

```
load_init_program: attempting to load /sbin/launchd
load_init_program: failed loading /sbin/launchd: errno 8
panic: Process 1 exec of /sbin/launchd failed, errno 8
```

`errno 8` is ENOEXEC, and in `kern_exec.c` it is what `exec_activate_image` returns when **no image activator claims the
file** (`error` still `-1` after the loop). `exec_mach_imgact` claims a file when its first bytes are `MH_MAGIC_64` and the
file type is `MH_EXECUTE` — and `/sbin/launchd` in the image *is* that: `0xfeedfacf`, ARM64, `MH_EXECUTE`, every segment in
bounds, `LC_CODE_SIGNATURE` ending exactly at the file's end.

Ruled out, each by an experiment:

| Suspect | How it was ruled out |
|---|---|
| The transfer to the phone | `oem imgsum` (now in the loader) computes a 64-bit FNV over the staged resident image on the phone, eight bytes at a time: `68ded9e137bce937 of 93aa5000 bytes` — the same number the file gives on the host |
| The image's file system | The kernel's APFS checksum is Apple's complement-form fletcher-64 over the block after its own checksum field; written that way blocks 0, 1 and 2632 all agree and the first three thousand blocks are clean. With the kernel merely reporting a disagreement and carrying on, the boot went no further, so those warnings are not the cause |
| The compressed `/sbin` | Rebuilt the image with `mkapfs --plain sbin`; `/sbin/launchd` becomes a plain 34560-byte Mach-O and the exec still fails |
| The kernel source tree | `xnu/apply.sh` (Apple's release, `hakios.patch`, `files/`, and `drivers/` by `drivers/map`) rebuilt from scratch, then `make kernel` from nothing: same errno 8 |

A diagnostic is built into the kernel and has **not yet been run** (the phone went off the bus before it could be):

```c
printf("hakios exec: vdata %02x %02x %02x %02x %02x %02x %02x %02x, size %lld\n", ...);
```

It prints the first eight bytes the activators are handed. If they are not `cf fa ed fe`, the fault is on the reading side
(VFS/APFS) and not in the exec logic; if they are, the exec path itself needs the next measurement.

## The first thing to try

Run that kernel once (the phone shows nothing on the bus at the moment of writing; a forced power cycle brings it back):

```sh
make kernel JOBS=12
xnuloader/mkpayload.py -o build/x.bin kernel=hakios-kernel ramdisk=userland/root.apfs \
    --cmdline "loader_return_after=110 epoch=$(date +%s) sb_delay=8 -v cpus=1 touchscreen=1 touch_stage=6"
# fastboot: oem ramdisk ; stage userland/system.apfs ; stage build/x.bin ; oem run ; oem log
```

## Building the images again — read this before trying

`userland/build.sh` only builds the system image when `userland/big` exists, and `big/` is filled from the programs and
libraries over 4 MB that the **optional Turnip and Metal builds** put in `root/lib` and `root/bin`. Without those, a
`./build.sh` run prints nothing about `system.apfs` and **leaves the previous image in place** — which is how a "rebuilt"
image can turn out to be the old one.

The sysroot those builds need comes from the IPSW: `sysimage/unpack.sh <ipsw>` (7.4 GB out, needs `7z` and `lz4`). This
machine currently has neither those tools nor the unpacked sysroot, so the images cannot be rebuilt here as things stand.

## What the phone's own driver says, for reference

`build/goodix_core.ko` is the phone's module, with symbols. Useful addresses: `brl_read_version 0x2d67c`,
`brl_get_ic_info 0x2db40`, `brl_event_handler 0x2f188`, `brl_after_event_handler 0x2fecc`, `brl_send_cmd 0x2c4d8`,
`brl_power_on 0x2b098`, `brl_reset 0x2bef8`, `brl_select_spi_mode 0x31934`, `goodix_spi_read 0x2224`,
`goodix_spi_write` (found by IDA only), `brl_read_config 0x2d0f8`, `brl_send_config 0x2c884`, `send_cfg_cmd 0x31b28`,
`goodix_tools_ioctl 0x42bbc`.

The panel configuration is embedded in the driver as `socrates_touch_config` (1488 bytes), cut from
`/odm/firmware/goodix_cfg_group_m11.bin` on the phone; its identity is `0x640fdf7d` at offset 169 of that file.
