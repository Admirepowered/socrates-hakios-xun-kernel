# The touch screen of the Redmi K60 Pro (socrates)

What the port established about the Goodix GT9916 on this phone, how each fact was measured, and what is still open.

## Where it stands

| Piece | State |
|---|---|
| Pins (TLMM at `0x0f100000`, `0x1000` per pin), reset, GPIO read/write | works — verified on the phone |
| Supplies `ldob12` 1.8 V and `ldob14` 3.2 V through RPMh | works — the register reads back what was written |
| Hand-driven SPI (`0xF1` + address + 3×`0xFF` to read, `0xF0` + address to write) | works — the chip answers |
| Chip identification | works — `BERLIN`, firmware `9916R` `05.80.02.63`, sensor 255 |
| IC description parse | works — command `0x10174`, frame data `0x10400`, touch data `0x10308`, panel 14400 × 32000 |
| Configuration commands (`0x0704` read, `0x0404` write, `0x0500` apply) | work — the chip acknowledges each with `0x80` |
| Writing the 1488-byte panel configuration | works — read back and compared equal |
| **Touch reports** | **not yet: the status byte never has bit 7 set** |

## What was measured, and how

Everything below came from the phone, not from a document.

- **The pin controller is at `0x0f100000`, a `0x1000` apart per pin.** The phone's own device tree says `pinctrl@f000000`, but every write into that window is dropped at every offset tried (all eight words of a pin's block were written and read back); `0x0f100000` takes them. `sm8550.dtsi` upstream says `pinctrl@f100000`.
- **RPMh slot sets are the 3.0 layout** (`DRV id 0x30000`): control `0x24`, status `0x28`, command enable `0x2c`, commands at `0x34` and 24 bytes apart — not the 2.7 layout the reference driver uses.
- **The high-power mode of this PMIC (PMIC4) is 7, not 4.** Passing 4 (retention) resets the phone outright.
- **`delay_for_interval` cannot be relied on**: past its spin threshold it blocks the thread waiting for a timer wakeup that never comes here. The driver counts the generic timer (`CNTPCT_EL0`, 19.2 MHz) instead.
- **The command frame** (from `brl_send_cmd` in `goodix_core.ko`): two zero bytes, the length at `[2]`, the command byte at `[3]`, and a 16-bit checksum of those two at `[4..5]`. Putting a data byte where the checksum belongs shifts the frame and the chip keeps whatever it was given instead of acknowledging.
- **The chip's description** (`brl_get_ic_info`): the addresses are 32-bit fields inside eight-byte pairs; the port reads command `0x10174`, frame `0x10400` and touch `0x10308`, and the panel's maximums come out as 14400 × 32000 — the same numbers Android's driver prints (`Touch-Data: 0x10308, 8`).
- **Android's own view of the pins** (`/sys/kernel/debug/gpio`): `gpio24 : out high func0 2mA pull down`, `gpio25 : in low func0 2mA pull down` — the interrupt line idles **low**, so a low line is not the same as "data waiting".
- **The interrupt is GPIO 25** (`/proc/interrupts`: `mscpio 25 Edge xiaomi_tp goodix_ts`, ~1500 hits).

## What was tried and did not work

- Reading the report buffer whole (2500 bytes, the length the chip's own handler uses) — same answer.
- Clearing the report address before each read (`touch_arm`) — it *destroys* the report (byte 0 becomes 0); acknowledging after the read (`touch_ack`) is accepted but the chip sends no next frame.
- Downloading the panel configuration. The commands are acknowledged and the 1488 bytes read back equal, but afterwards **the frame data at `0x10400` stops changing** — the chip stops scanning. Android's own driver never downloads it on this phone (`config id is equal 0x640fdf7d, skiped`), so the chip already carries a configuration and this port's copy is not what it needs.
- Repainting the framebuffer to keep the panel refreshing — no change.
- ftrace/kprobes on Android to capture the driver's own reads — **kprobes do not fire at all on this kernel** (a probe on `vfs_read` also yields nothing).
- Android's `read_cfg` sysfs node — the callback it needs is not registered (`EINVAL`).

## The boot: where it stands now

The userland starts. `=== init: process 1, started as /redwood/sbin/init ===`, and it runs its 36 checks — the file
system, mmap and page-in, fork, exec, pipes, 300 files in a directory — of which **35 pass**. The two that do not are
both the same fault, described below.

### What was wrong, in the end

`decmpfs_init` registered `CMP_Type1` only (bytes stored as they are). Every file in this image is compressed:
`/sbin/launchd` (34 KB) is **type 3**, one zlib stream in the `com.apple.decmpfs` attribute, and anything too large for
an attribute — `/sbin/launchd.darwin` (525 KB), `dyld` (922 KB) — is **type 4**, 64 KB zlib pieces in the
com.apple.ResourceFork attribute. With no decoder for either, `decmp_get_func()` answered NULL, the read failed, and what
the exec was handed was not the file at all. That is the errno 8 that looked like a bad Mach-O.

Both are now registered (`bsd/kern/decmpfs.c`, `Type3Reg` and `Type4Reg`, decoding through the kernel's own zlib, the one
APFS already uses). With type 3 in place the userland started.

### The remaining fault, located exactly

Reading a large type 4 file fails with EIO, and the chain is now pinned down step by step:

```
hakios exec: read launchd.darwin: error 5 resid 16384          (the first PAGE_SIZE of an image)
hakios apfs: cached read of launchd.darwin at 0 for 16384: 5
apfs: inode 60968: cfile_init: 5 (read 65536 at 0)
apfs: inode 60968: rsrc xattr flags 0x1 stream 70234 of 200749
apfs: inode 60968: rsrc head read: 5                           (apfs_read_stream of 16 bytes failed)
```

The pattern across every file read in a boot is clean:

| inode | resource fork | stream id | result |
|---|---|---|---|
| 54 | 1782 bytes | 123 | read |
| 95 | 17705 bytes | 146 | read |
| 97 | 6354 bytes | 147 | read |
| 60968 (`launchd.darwin`) | 200749 bytes | **70234** | **EIO** |
| 61038 (`dyld`) | 318163 bytes | **70268** | **EIO** |

Small resource forks read, large ones do not, and the split follows the stream id — the small ids (123–147) work, the
large ones (70234, 70268) do not. `apfs_read_stream` walks the extents with `ext_begin`/`ext_next` and copies each block
through `nx_block(vol->nx, addr, &blk, false)`; diagnostics placed at `ext_begin`, at the length check and at the
physical-range check all stayed silent, so the failure is at the block read inside that loop or at `ext_next` returning
something other than ENOENT. That is where the next run's diagnostic belongs.

### A trap that costs a boot each time

`oem run` must be preceded by a fresh `oem ramdisk` and `stage` of `userland/system.apfs`. Without it the resident image
is out of date and the boot stops at `load_init_program: failed loading /sbin/launchd: errno 8` — a face of the same
message that has nothing to do with decmpfs, and which sent more than one round chasing the wrong thing. The loader's
`oem imgsum` reports a checksum of what is staged: `68ded9e137bce937 of 93aa5000 bytes` for the image built at 00:37.

## The boot, as far as it got, and what each experiment settled

The touch screen cannot be judged on a phone whose userland never starts, so a good part of the work went into the
boot. What was established, in the order it was:

- **The image is not the problem.** `oem imgsum` in the loader (a 64-bit FNV over the staged resident image, rolled over
  eight bytes at a time) answers `68ded9e137bce937 of 93aa5000 bytes` on the phone and the same number comes out of
  `userland/system.apfs` on the host. The 2.47 GB arrive over USB byte for byte.
- **The file system image is not the problem either.** With the kernel's APFS checksum the algorithm is Apple's
  complement-form fletcher-64 over the block after its own checksum field; written that way, blocks 0, 1 and 2632 all
  agree and the first three thousand blocks check out on the host. A kernel that logs a disagreement and carries on
  boots no further, so that warning was a red herring.
- **The compressed `/sbin` was not the problem.** Rebuilding the image with `mkapfs --plain sbin` makes `/sbin/launchd`
  a plain 34560-byte ARM64 Mach-O (`0xfeedfacf`, `MH_EXECUTE`, every segment in bounds, `LC_CODE_SIGNATURE` ending
  exactly at the file's end) — and the kernel still answers errno 8 when it tries to exec it.
- **The kernel tree was not the problem.** Rebuilding a clean tree with `xnu/apply.sh` (Apple's release, `hakios.patch`,
  `files/`, and `drivers/` by `drivers/map`) and building it from scratch gives the same errno 8.

`nsx: unexpected SIGKILL of init` with `namespace 9 code 0x1` is `OS_REASON_EXEC` / `EXEC_EXIT_REASON_BAD_MACHO`, which is
what the exec returned; both faces of the same failure.

What is left unproven is whether a userland starts at all on this phone at present. Note for whoever picks this up: the
system image is only built when `userland/big` exists, and `build.sh` fills that directory from the programs and
libraries over 4 MB that the optional Turnip and Metal builds put in `root/lib` and `root/bin`. Without those builds, a
`./build.sh` run quietly skips the system image and leaves the previous one in place — which is why a "rebuilt" image can
turn out to be the old one.

## What the chip's memory holds, address by address

Read together, at every poll, so that each region's job is known rather than assumed:

| Address | What it is |
|---|---|
| `0x10174` | the command area — a frame written here is acknowledged with `0x80` |
| `0x10308` | the report buffer — it holds `00 00 14 00 31 00 00 0d` and never changes |
| `0x10400` | a frame counter, `80 11 00 e8`, `80 22 00 e8`, `80 33 00 e8` — one step per frame, so the chip is scanning |
| `0x13588`, `0x14280` | live 16-bit sample arrays that change with the panel and with a finger |
| `0x10218` | a small number (`0x27`–`0x29`) and zeros; writing the configuration here stops the frame counter |

The interrupt line is **asserted high**: the pin is configured with a pull-down (as Android's own table has it:
`gpio25 : in low func0 2mA pull down`), so a high level is the chip asking, not an idle line. `touch_int_high=1` reads it
that way.

## Getting a userland to start

`/sbin/launchd` in the image is decmpfs **type 4** (zlib in the resource fork) and this kernel registers only **type 1**, so
`exec` answers ENOEXEC and the boot panics at the first process. `userland/build.sh` now builds the system image with
`mkapfs --plain sbin`, which stores `/sbin` uncompressed — the image grows by a few tens of kilobytes and the binaries
become readable. That matters for the touch as well: with no userland there is no display driver running, and the panel is
a still image, which some touch controllers will not report over.

## The one open question

The report area at `0x10308` holds a structured record — `00 00 14 00 | 31 00 00 0d | 52 0d 4e 01 | 00 00 65 01` repeated, where `0x0d52` (3410) and `0x014e` (334) look like coordinates in the 14400 × 32000 space — but its first byte never has bit 7 set, which is what the chip's own handler tests for a touch (`if ((byte0 & 0x80) == 0)` → no touch; bit 6 with the event in byte 2 is a chip request).

So either the record does not start where the port reads it, or the chip is not in the mode that produces reports. Reading `0x10308` on the phone while Android is running would settle which — Android's `read_cfg` node is the obvious route and is not registered, and its kprobes do not fire.

## What is in the driver now

`drivers/input/hakios.socrates-touch.c` runs the whole bring-up and carries the switches used to get here: `touch_stage` (1 enter, 2 map, 3 supplies, 4 pins, 5 reset, 6 the whole probe), `touch_stride`, `touch_pin_base`, `touch_tlmm_size`, `touch_probe_off`, `touch_probe_phys`, `touch_probe_pin`, `touch_addr` (report address), `touch_cmdaddr` (command address), `touch_cfgread` (read and write the configuration), `touch_dbg` (1 raw reports, 2 scan the neighbours), `touch_bigread`, `touch_arm`, `touch_ack`, `touch_intdump`, `touch_paint`, `touch_slow`, `touch_no_gpio`, `touch_no_power`, `touch_no_rpmh`, and `no_init` in the kernel (hold the kernel up instead of starting `/sbin/launchd`).

The 1488-byte panel configuration is embedded in the driver (`socrates_touch_config`), taken from `/odm/firmware/goodix_cfg_group_m11.bin` on the phone; `build/goodix_core.ko` is the phone's own driver module, with symbols, for anything that needs looking up again.
