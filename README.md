# fpv-rook-web-betaflight

The source code for the Betaflight flight controller used by [fpv rook](https://fpvrook.com). It builds Betaflight as a WebAssembly module, `bf.wasm`, that runs in the browser.

Betaflight is free software under the GNU General Public License, version 3. This repository is under the same licence. See [LICENSE](LICENSE).

## Build

You need [Zig](https://ziglang.org) 0.16, git, and a network connection.

```sh
git clone https://github.com/mfleming0101/fpv-rook-web-betaflight.git
cd fpv-rook-web-betaflight
zig build
```

The build downloads Betaflight commit [0e533ba](https://github.com/betaflight/betaflight/tree/0e533ba76cb9129458578f7fa2134df1449596ba), checks it against the hash in `build.zig.zon`, applies `0e533ba.patch`, and compiles the result. The output is `zig-out/bin/bf.wasm`.

Betaflight puts the build date and time in its version string, so two builds differ only in those bytes.

## Files

| File | Contents |
|---|---|
| `0e533ba.patch` | The changes to the Betaflight source. |
| `platform.c` | The platform that Betaflight runs on in the browser: clock, sensor and radio inputs, motor outputs, settings storage, and the exported functions. |
| `dyad_stub.c` | Replaces the dyad TCP library with memory buffers, so the CLI works without a network. |
| `srcs.txt` | The Betaflight source files to compile. |
| `bf.ts` | Loads `bf.wasm` in the browser and runs it as a flight controller: sensor and radio inputs, motor outputs, arming state, and CLI settings. |
| `build.zig`, `build.zig.zon` | The build script and the pinned Betaflight download. |

## Changes to Betaflight

- `src/main/pg/pg.h`: adds the WebAssembly linker section names for the parameter-group registry.
- `src/main/target/SITL/target.h`: Betaflight calculates the attitude from the gyro and accelerometer, as on a real flight controller, and does not get it from the simulator.
- `src/main/drivers/accgyro/accgyro_virtual.c`: the virtual gyro reports as a BMI270, so the gyro rate is 3.2 kHz.
- `src/main/fc/rc_controls.c`: the "stick commands disable" mode also works when USB HID is not compiled in.
