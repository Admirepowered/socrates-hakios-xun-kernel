# Building the kernel on this machine (Linux, no root, behind a proxy)

What it took to get `make kernel` to build `hakios-kernel` here, in the order it was needed. Every step is a property of
this machine, not of the port.

## 1. The cross toolchain, without root

The AArch64 GCC, dtc, bison, flex and the OpenSSL headers came out of `.deb` files unpacked into `~/xcross/root` (no sudo):
`build/wsl-env.sh` puts them on `PATH`, `CPATH` and `LIBRARY_PATH`. `7z` (7-Zip 23, for `sysimage/unpack.sh`) is the same
trick: the `7zip` package's `/usr/lib/7zip/7z` behind a wrapper in `~/xcross/bin/7z`.

## 2. theos: the toolchain and the SDK

`xnu-deps/setup.sh` wants `$THEOS/toolchain/linux/iphone/bin` (Apple's cctools) and `$THEOS/sdks/iPhoneOS16.5.sdk`.
`github.com/theos/toolchain` is **not** the source of the first one — theos's own `bin/install-theos` downloads
`https://github.com/L1ghtmann/llvm-project/releases/latest/download/iOSToolchain-x86_64.tar.xz` (86 MB) and unpacks it
into `$THEOS/toolchain/`; the SDK is `theos/sdks`, sparse-cloned (`--filter=blob:none --sparse`, then
`git sparse-checkout set iPhoneOS16.5.sdk`, 219 MB).

## 3. GitHub is unreachable from WSL without the host's proxy

`github.com` resolves to `github.com.gyu.edu.cn` (a poisoned name) and every real GitHub address times out from WSL. The
Windows host runs a proxy on `127.0.0.1:48952` (WSL 2 forwards that), and with `https_proxy=http://127.0.0.1:48952` every
fetch works. `build/wsl-env.sh` sets it. The GitHub **API** is rate-limited from that shared address, so `git` (which does
not use the API) is the way to fetch; the release *assets* are fine.

`gitcode.com` mirrors (`gh_mirrors/xn/xnu` carries `xnu-8796.121.2` at the exact commit `apply.sh` wants) are a fallback
when the proxy is not up.

## 4. What the build still asked for

- `uuid/uuid.h` (`uuid-dev`): without it `xnu-build.sh exporthdrs` dies in `SETUP/config/openp.c`. It is not in
  `BUILD.md`'s package list for Linux, but it is needed.
- `dsymutil`: `xcrun` (the minimal replacement in `xnu-deps/host/bin`) only looks in `host/bin` and `/usr/bin`, and
  `setup.sh` links cctools' tools but not `dsymutil` — so `DSYMUTIL` expands empty and the dSYM step runs `--minimize` as
  a command. `ln -s "$THEOS/toolchain/linux/iphone/bin/dsymutil" xnu-deps/host/bin/dsymutil` fixes it (a Linux port of
  `setup.sh` should add `dsymutil` to its list).

## 5. The result

```
make-kernel: built: hakios-kernel (xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837)
make-kernel: (exit status 2 is the kext symbol-set checks failing, as they do on this configuration; ...)
```

About three minutes for the first build on this machine (the tree is on `/mnt/e`, 9p, so a native filesystem would be
faster), one to two minutes after that.

## 6. The build's own configuration

Without a `.config`, `tools/hakios-config.py` builds **all 25 drivers in** ("from the drivers' defaults"): the kernel that
came out of this build has redwood's drivers compiled in, not socrates's fragment. `make socrates_defconfig` (with
`configs/socrates.config` left out as it is) is what the port wants once a driver of its own exists.
