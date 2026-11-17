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

### The large type-4 reads, traced to one call

The chain below was measured on the phone, one diagnostic at a time; every line is a call that returns 5, in order:

```
hakios exec: read launchd.darwin: error 5 resid 16384
apfs: inode 60968: rsrc xattr flags 0x1 stream 70234 of 200749
apfs: stream 70234: enters for 16 at 0 of 200749
apfs: ext_begin 70234 at 0: the volume tree
apfs: apfs_seek 70234 type 8: seek_to: 5 (tree root 2506)
apfs: ext_begin 70234: 5
apfs: stream 70234: ext_begin at 0 of 200749: 5
apfs: inode 60968: rsrc head read: 5
```

So the extent lookup goes through the volume tree (not the file extent tree — `has_fext` is false on this
volume), into `apfs_seek` with `type 8` (`APFS_TYPE_FILE_EXTENT`), and `seek_to` is what answers EIO. That is
the one call left to instrument.

What separates the files that work from the ones that do not is the identifier alone: streams 123, 146 and 147
(resource forks of 1782, 17705 and 6354 bytes) seek fine, while 70234 and 70268 (200749 and 318163 bytes) fail
on the very same code path with the same volume. Tree root is 2506 in both cases, so the tree opens; the walk
inside `seek_to` is where they part. `omap_find`, which `seek_to` reaches for the object's mapping, has
`if (!omap->ready) return EIO;` as its first answer, and two more EIO returns inside its descent — those are
the next lines to report.

### The root cause, found

The last jump is inside the device layer, and it is one block:

```
apfs: nx_block 2632: cache miss, checked -1, first bytes 00 00 00 00
hakios dev_read: off 10780672 block 21056 chunk 4096, buffer 5a 5a 84 5a (base 0, devblock 512)
apfs: nx_block 2632: single read at 10780672 error 0, got 5a 5a 84 5a
apfs: node_at_h 2632: obj type 0, first bytes 5a 5a 84 5a 5a 5a 5a 5a
apfs: node_at_h 2632: not a B-tree node (type 0)
```

Everything about the request is right: the offset `10780672` is block 2632 × 4096, the device block `10780672 / 512 = 21056` is right for the 512-byte block size md0 reports, and the 4096 bytes asked of `buf_meta_bread` are right too. What comes back is `5a 5a 84 5a` — memory fill, not the `86 4e 38 89 70 0b 8d 66` that `userland/system.apfs` holds at that offset. **`buf_meta_bread` on md0 answers success and leaves the buffer uninitialised.**

That is the whole reason the large type-4 files fail and the small ones do not. Block 2632 is the B-tree node at depth 2 that a 200 KB or 318 KB resource fork needs and a 6 KB one never touches; `node_at_h` then reads `obj_type` 0 out of fill bytes, does not recognise a B-tree node, and answers EIO, which travels up as the read error, the cfile error and finally the executor's ENOEXEC.

Two things to look at, in this order:

1. **How md0 is built.** `apfs_mount_common` takes `devblock` from `DKIOCGETBLOCKSIZE` (512 here) and `APFS_META_CHUNK` is 4096, so `4096 % 512 == 0` passes its check; the read then goes to `buf_meta_bread(devvp, off / devblock, 4096, …)`. The resident image is a block of physical memory the loader filled and checked (`oem imgsum` gives `e5fd03a4a21d14cc` on the phone and the same number on the host file), so the bytes are there. What is not established is whether md0's own read path — its `d_strategy` or whatever stands in for one, reached through `buf_meta_bread` — copies them, and whether the buffer size it is asked for matches what it can deliver at that offset.
2. **The buffer size.** `5a` is the kernel's own fill, and different bytes (`5a 5a 84 5a`, `5a da 87 5a`) come back on different runs, which is what uninitialised memory looks like. A buffer that `buf_meta_bread` returns without a completed read would explain it: the request succeeded far enough to hand back a buffer, but no I/O finished.

A quick way to test the second: read that one block with `nx->dev.read` instead of `buf_meta_bread` (the branch that `cheap_requests` takes in `nx_block`), and see whether the bytes arrive. If they do, the fault is in the buffered path's setup for md0; if they do not, it is in md0's read itself.

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

## What Android's driver actually does, watched from the kernel

The missing measurement was taken with KernelPatch on the working phone: a small KPM hooks
`goodix_spi_read` and prints every read the vendor driver makes, its address, its length and the first bytes that
come back. The module lives in `E:\Project\KernelPatch\kpms\socrates-spy`; it is loaded with
`su 10307 -c "truncate su module load /data/local/tmp/spy.kpm"` (uid 10307 is the manager, which passes the
authentication), and its output goes to the kernel log.

What it showed, over a hundred frames:

| Address | Length | Rate | First byte |
|---|---|---|---|
| **`0x10400`** | **2500** | ~30 Hz | **`0x80`** |
| `0x10174` | 16 | on commands | `0x80` after the write (the acknowledge) |

So the report is read from **`0x10400`**, not from `0x10308` (the word the chip's own description gives, which this
port had been reading and which never carries the status bit), and the acknowledge of a command is **byte 0** of the
sixteen read back, not byte 1.

The 2500 bytes are laid out like this, from captures on both systems:

| Offset | What |
|---|---|
| `0x00` | `0x80` when there is data, plus the sequence number in `0x01` |
| `0x04-0x07` | `08 e8 08 01`, the same in every frame on both systems |
| `0x08-0x0b` | `07 00 00 <sequence>` |
| `0x0c-0x0f`, `0x10-0x13` | frame and time counters, stepping every frame |
| `0x14-0x1f` | mode words — and here the two systems differ: Android has `04 b6 10 00 20 00`, this port's driver has `02 38 04 00 f0 00` |
| `0x20-0x47` | a table Android fills with `05 05 9f 01 a5 04 a2 a6 00` repeating; all zeros under this port |
| `0x50-0x6f` | zeros |
| `0x70-0xb1` | more mode and channel words |
| `0xb2` onward | the raw capacitance frame: 16-bit values, `11 29 31 29 37 29 ...`, one per node |

With the driver changed to read `0x10400` (`touch_android=1`), the chip answers on HakiOS exactly as it does on
Android: `80 37 00 e8 08 e8 08 01 ...`, status bit set, counters stepping. **The chip has been reporting all along;
the port was reading the wrong address.** What is not yet known is which bytes of the header carry a finger's
coordinates — the captures taken so far had no finger on the panel at the moment the buffer was read, and the raw
frame region has no coordinates in it, only capacitance.

The next measurement is the same one again with a finger held on the panel while the header is dumped: the offsets
that move are the coordinates.

## Every read the Android driver makes

Both read entry points (`goodix_spi_read` and `goodix_spi_read_bra`) were hooked and every read was logged with its
address, its length and its first bytes. Over a swipe, Android's driver asks the chip for exactly these:

| Address | Length | When |
|---|---|---|
| `0x10400` | 2500 | ~30 times a second, the report/frame buffer |
| `0x10174` | 16 | on commands; byte 0 of the answer is the `0x80` acknowledge |
| `0x10070` | 2, then 193 | occasionally — the chip's own configuration |

Nothing else. **There is no read of a small report structure anywhere**, and the 2500-byte buffer at `0x10400` was
compared byte for byte between a finger held at (250, 700) and one held at (1000, 2200): the whole capacitance frame
changes, as it must, and in the header only counters and three small words move —

```
000: 45184 vs 45696   (status 0x80 and the sequence number)
00e:  1008 vs  1012   (a counter)
010: 10861 vs 10865   (a counter, +4 per frame)
012:   384 vs   128   (0x0180 vs 0x0080)
01e:    36 vs    30
072:    20 vs    15
094:  9749 vs 36885   (0x2615 vs 0x9015, and the low byte is the same in both)
```

None of those is a coordinate. So **this firmware does not hand out touch positions over the transport at all**.
That matches what the vendor module's strings said from the start: Xiaomi's THP (`thp_send_cmd_to_hal`,
`thp_send_event_to_hal`) takes the frame to a userspace HAL, and the HAL is what finds the fingers and injects them
through `uinput`. The kernel driver on this phone is a transport and a frame reader; the touch processing is not in
it.

What that means for the port: the chip, its supplies, its reset, its SPI mode handshake, its command channel, its
configuration and its 2500-byte frame at `0x10400` all work under HakiOS — the driver reads `80 .. .. e8 08 e8 08 01`
and a frame counter stepping at 30 Hz. What is missing is the step that turns that frame into points, which on this
phone lives in a userspace daemon this port has not brought up. Peak detection over the 0x1000-odd capacitance nodes
is the work, and the code that does it is Xiaomi's, not the chip's.

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
