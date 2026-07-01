# Genesis / Mega Drive Core Integration Plan

Migration plan for integrating a **Sega Mega Drive / Genesis** core into Mesen2,
using the [ares](https://github.com/ares-emulator/ares) MD core
([`ares/md/`](../ares/md/) + [`ares/component/`](../ares/component/)) as the
**algorithmic reference**, but implemented as a **native Mesen2 core** in the same
style as [`Core/SMS/`](../Core/SMS/) and [`Core/PCE/`](../Core/PCE/).

This follows the conventions of [`skills/NDS.md`](NDS.md) and [`skills/3DS.md`](3DS.md).

---

## 1. Approach Selection

| # | Strategy | Reuses ares code? | Verdict |
|---|----------|-------------------|---------|
| A | Link ares MD verbatim (vendor nall + libco + sljit + qon) | Yes, verbatim | **Rejected** – see §2 |
| B | **Native Mesen2 port, ares as reference** | As a porting source only | **Chosen** |
| C | Libretro wrapper (`LibretroCore` like NDS/3DS) | Only ares-as-libretro | Rejected – user pointed at native source |

### Why native port, not linking ares?

The vendored [`ares/ares/ares.hpp`](../ares/ares/ares.hpp) is **not self-sufficient**:
it `#include`s ~25 `nall/` headers plus `<libco/libco.h>`, `<sljit.h>`, and the MD
core additionally needs `<qon/qon.h>`, `<qon/qoi2.h>`. None of these are present in
the repo, and every ares component (`m68000`, `ym2612`, `sn76489`, …) and every
`ares/md/*.cpp` transitively pulls in this whole stack through `<ares/ares.hpp>`.

Pulling in nall + libco + sljit + qon would:

- Inject a competing foundation library (nall `string`/`vector`/`serializer`/`image`)
  that **fights Mesen2's own infrastructure** (`Utilities/Serializer.h`,
  `Core/Shared/Audio/SoundMixer.h`, `BaseControlManager`, `IConsole`).
- Pull in libco (cooperative threads) and sljit (SH2 recompiler, M32X-only) that
  Mesen2's single-threaded `Emulator::RunFrame` model does not need.
- Make the Genesis core the odd one out — every other Mesen2 core (NES, SNES, SMS,
  PCE, GBA, WS) is native and shares the same `Serializer`/`SoundMixer`/input
  infrastructure, which is what makes save states, rewind, history viewer and movies
  work uniformly across consoles.

**Decision:** Treat [`ares/`](../ares/) as **reference source only**. It is **not**
added to the include path and **not** compiled into MesenCore. We write
[`Core/Genesis/`](../Core/Genesis/) as native Mesen2 code, porting the algorithms
from ares into classes that extend `ISerializable`, use the `SV()` serializer macro,
feed `SoundMixer::PlayBuffer`, and are driven by `Emulator::RunFrame`. This is the
same proven pattern as [`Core/SMS/SmsCpu.h`](../Core/SMS/SmsCpu.h).

### Porting scope (from ares LOC)

| ares subsystem | ares LOC | Mesen2 target | Notes |
|----------------|----------|---------------|-------|
| `component/processor/m68000` | 4 547 | `GenesisM68K.*` | Largest piece; port instructions, registers, EA, disassembler |
| `component/processor/z80` | 3 659 | `GenesisZ80.*` | APU CPU; can reuse patterns from `Core/SMS/SmsCpu` (also Z80-family) |
| `component/audio/ym2612` | 929 | `GenesisYm2612.*` | OPN2 FM synth; self-contained algorithm |
| `component/audio/sn76489` | 174 | `GenesisPsg.*` | PSG; tiny, port first as a warm-up |
| `component/eeprom/m24c` | 333 | `GenesisEeprom.*` | For battery saves on some carts |
| `md/vdp-performance` | 1 543 | `GenesisVdp.*` | Start with the **performance** variant (integer-only, faster); accuracy `md/vdp` variant (2 694 LOC) is a later swap |
| `md/cpu` + `md/bus` + `md/apu` | ~940 | `GenesisM68K` + `GenesisZ80` + `GenesisMemoryManager` | Bus/memory glue, M68000 I/O, Z80 bus arbitration |
| `md/cartridge` + `board` | 2 563 | `GenesisCart.*` + mappers | Standard, banked, SVS, J-Cart, Game Genie, Realtec |
| `md/controller` | 576 | `GenesisControlManager` + `GenesisController` | 3/6-button pad, Mega Mouse |
| `md/opn2` | 70 | folded into `GenesisYm2612` | Trivial wrapper |
| `md/system` | 319 | `GenesisConsole` | Lifecycle, region, power |

**Total ≈ 12–13k lines** of reference code to port. This is large but matches the
scale of existing native cores (e.g. `Core/SMS/SmsCpu.cpp` alone is ~53 KB).

---

## 2. SDK Dependencies

### What is NOT being added

| Missing dep | Why not |
|-------------|---------|
| `nall/` | Replaced by Mesen2's `Utilities/Serializer.h`, `Utilities/FastString.h`, STL, and `pch.h` types |
| `libco/libco.h` | ares' cooperative scheduler is unused; Mesen2 drives frames single-threaded via `Emulator::RunFrame` |
| `sljit.h` | Only needed for the SH2 recompiler (M32X). Phase 1-3 do not target M32X; a future M32X phase can add an interpreter SH2 instead |
| `qon/qon.h`, `qon/qoi2.h` | Image/asset helpers (QOI encoding for save-state previews). Mesen2 already has its own PNG preview path in `SaveStateManager` |

### What stays

- [`ares/`](../ares/) (the vendored `ares/`, `component/`, `md/` trees) remains in
  the repo **as a read-only reference** for porting. It is deliberately left off
  the include path and out of the build.
- All new code lives under [`Core/Genesis/`](../Core/Genesis/) and depends only on
  Mesen2's existing headers (`pch.h`, `Shared/Interfaces/IConsole.h`,
  `Utilities/Serializer.h`, `Shared/Audio/SoundMixer.h`, …).

### Infra mapping: ares concept → Mesen2 replacement

| ares / nall concept | Mesen2 native replacement |
|---------------------|---------------------------|
| `nall::serializer` / `ares::serializer` | `Utilities/Serializer.h` + `ISerializable::Serialize(Serializer& s)`, fields via `SV(var)` macro |
| `ares::MegaDrive::system.serialize()` | `GenesisConsole::Serialize(Serializer&)` calling `SV()` on each subsystem |
| `ares::Node::Stream` (audio) | `SoundMixer` + `IAudioDevice::PlayBuffer(int16_t*, uint32_t, uint32_t sampleRate, bool isStereo)` |
| `ares::Node::Input::Button` | `BaseControlManager` + `BaseControlDevice` (see `Core/SMS/SmsControlManager`) |
| `ares::Node::Video` framebuffer | `PpuFrameInfo` returned from `IConsole::GetPpuFrame()` |
| `ares::Scheduler` / `libco` | Single-threaded `RunFrame()` driven by `Emulator` (see `SmsConsole::RunFrame`) |
| `ares::Platform` callbacks | Direct method calls within `GenesisConsole` (no platform indirection needed) |
| `nall::string` / `nall::vector` | `string` / `vector` from Mesen2's `pch.h` |
| `nall::memory::copy` | `std::memcpy` |
| `ares::MegaDrive::Region` enum | Mesen2 `ConsoleRegion` (already has `Ntsc`/`Pal`/`NtscJapan`) |
| `ares::Cartridge` pak/VFS | `VirtualFile` + Mesen2 `BatteryManager` for SRAM |

---

## 3. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Mesen2 UI (C#)                        │
│  GenesisConfig, GenesisControllerView, EmuApi, DebugApi      │
└─────────────────────────────────────────────────────────────┘
                              │  P/Invoke
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              InteropDLL (EmuApiWrapper, etc.)                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  Core/Genesis/GenesisConsole                 │
│   implements IConsole (native, ISerializable)               │
│   - LoadRom → parse header, pick region, build cart          │
│   - RunFrame → step M68000 + Z80 until VDP frame advances    │
│   - GetPpuFrame → return VDP framebuffer as PpuFrameInfo     │
│   - Serialize → SV() each subsystem                          │
└─────────────────────────────────────────────────────────────┘
        ┌─────────────┬───────────────┬────────────────┐
        ▼             ▼               ▼                ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌───────────────┐
│ GenesisM68K  │ │ GenesisZ80   │ │ GenesisVdp   │ │ GenesisAudio  │
│ (M68000 CPU) │ │ (Z80 APU)    │ │ (VDP render) │ │ YM2612 + PSG  │
│ ISerializable│ │ ISerializable │ │ ISerializable│ │ → SoundMixer  │
└──────────────┘ └──────────────┘ └──────────────┘ └───────────────┘
        │             │               │
        ▼             ▼               ▼
┌──────────────────────────────────────────────────────────────┐
│           GenesisMemoryManager (M68000 + Z80 bus)            │
│  - 64KB Z80 RAM, 64KB M68K work RAM, cartridge ROM/RAM       │
│  - I/O space, VDP/PSG registers, Z80 bus arbiter             │
│  - Address translation for debugger/cheats                   │
└──────────────────────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────────────────────┐
│        GenesisCart + mapper board (standard/banked/SVP/…)     │
│        + GenesisControlManager (3/6-button pad)              │
└──────────────────────────────────────────────────────────────┘
```

No ares code at runtime. Every box above is a new Mesen2-native class. The ares
source is consulted while writing each class, then the ported code stands alone.

---

## 4. File Structure (to be created)

### Core (C++) — all native, all `ISerializable`

| File | Purpose | Ares reference |
|------|---------|----------------|
| `Core/Genesis/GenesisConsole.h/cpp` | `IConsole` impl; owns all subsystems; `RunFrame`, `LoadRom`, `Serialize` | `md/system/system.cpp`, `md/md.cpp` |
| `Core/Genesis/GenesisTypes.h` | `GenesisModel`, `GenesisRegion`, `GenesisPadType`, `GenesisConfig` | `md/system/system.hpp` |
| `Core/Genesis/GenesisM68K.h/cpp` | Motorola 68000 CPU (interpreter) | `component/processor/m68000/` |
| `Core/Genesis/GenesisM68K.Instructions.cpp` | Instruction table (split like `PceCpu.Instructions.cpp`) | `component/processor/m68000/instructions.cpp` |
| `Core/Genesis/GenesisZ80.h/cpp` | Z80 APU CPU | `component/processor/z80/` |
| `Core/Genesis/GenesisVdp.h/cpp` | VDP (start with `vdp-performance` algorithm) | `md/vdp-performance/` |
| `Core/Genesis/GenesisYm2612.h/cpp` | YM2612 / OPN2 FM synth | `component/audio/ym2612/`, `md/opn2/` |
| `Core/Genesis/GenesisPsg.h/cpp` | SN76489 PSG (TI) | `component/audio/sn76489/` |
| `Core/Genesis/GenesisMemoryManager.h/cpp` | M68000 + Z80 buses, I/O, bus arbitration | `md/bus/`, `md/cpu/io.cpp`, `md/apu/bus.cpp` |
| `Core/Genesis/GenesisCart.h/cpp` | Cartridge + mapper dispatch | `md/cartridge/cartridge.cpp`, `slot.cpp` |
| `Core/Genesis/GenesisCartBoard.h/cpp` | Individual mapper boards (standard, banked, SVP, J-Cart, Game Genie, Realtec) | `md/cartridge/board/` |
| `Core/Genesis/GenesisEeprom.h/cpp` | M24C EEPROM (battery carts) | `component/eeprom/m24c/` |
| `Core/Genesis/GenesisControlManager.h/cpp` | Input bridge Mesen2 → controller ports | `md/controller/` |
| `Core/Genesis/Input/GenesisController.h` | 3/6-button pad button mapping | `md/controller/control-pad/` |
| `Core/Genesis/GenesisVideoFilter.h` | Default video filter (XRGB8888 → Mesen2 framebuffer) | — |
| `Core/Genesis/Debugger/GenesisDebugger.h` | (Phase 2+) CPU/VDP debugger; return `{}` initially like NDS | `md/cpu/debugger.cpp`, `md/vdp/debugger.cpp` |

### Shared registration edits (unchanged from prior plan)

| File | Change |
|------|--------|
| [`Core/Shared/SettingTypes.h`](../Core/Shared/SettingTypes.h) | Add `ConsoleType::Genesis` (=9); `ControllerType::GenesisController`, `GenesisController6Button`; `struct GenesisConfig` mirroring `SmsConfig` (~line 701) |
| [`Core/Shared/RomInfo.h`](../Core/Shared/RomInfo.h) | Add `RomFormat::Genesis` |
| [`Core/Shared/MemoryType.h`](../Core/Shared/MemoryType.h) | Add `MemoryType::GenesisM68KRam`, `GenesisZ80Ram`, `GenesisVdpVram`, `GenesisCartridgeRam`, `GenesisCartridgeRom` |
| [`Core/Shared/CpuType.h`](../Core/Shared/CpuType.h) | Add `CpuType::GenesisM68K`, `CpuType::GenesisZ80` |
| [`Core/Shared/EmuSettings.cpp`](../Core/Shared/EmuSettings.cpp) | Add `case ConsoleType::Genesis:` block mirroring `ConsoleType::Sms` (~line 113) |
| [`Core/Shared/Emulator.cpp`](../Core/Shared/Emulator.cpp) | Add `TryLoadRom<GenesisConsole>(...)` to the factory list (~line 580) |

### Build system

| File | Change |
|------|--------|
| [`CMakeLists.txt`](../CMakeLists.txt) | **No new include paths needed** (ares not on path). The existing `file(GLOB_RECURSE CORE_SOURCES "Core/*.cpp")` picks up `Core/Genesis/*.cpp` automatically |
| [`makefile`](../makefile) | The existing `find Core -name '*.cpp'` picks up new files; **no changes** |
| `Core/Core.vcxproj` + `.filters` | Add the new `.cpp/.h` entries (template off the SMS project entries) |

### UI (C#)

| File | Purpose |
|------|---------|
| `UI/Config/GenesisConfig.cs` | Mirrors `GenesisConfig` struct |
| `UI/ViewModels/GenesisConfigViewModel.cs` | Settings VM |
| `UI/Views/GenesisControllerView.axaml(.cs)` | Controller mapping UI |
| `UI/Interop/EmuApi.cs` | Add `ConsoleType.Genesis`, `RomFormat.Genesis` |
| `UI/Config/InputConfig.cs` | Add `ControllerType.GenesisController` |

---

## 5. Phase 1 – Basic Video / Audio / Keyboard Input

**Goal:** a `.md`/`.bin`/`.gen` ROM boots, renders video, plays audio, responds to
the keyboard-mapped 3-button pad.

### 5.1 Bootstrap (do this first – unblocks everything)

1. Register the console **as a stub**: `GenesisConsole` with empty `RunFrame`,
   `GetSupportedExtensions() = { ".md", ".bin", ".gen", ".smd" }`, registered in
   `Emulator::TryLoadRom`. Add the `ConsoleType`/`RomFormat`/`ControllerType`
   enums. No ares code yet.
2. Build green on all three [`COMPILING.md`](../COMPILING.md) toolchains: Linux
   `make`, VS2022 MSVC, `./build-core-windows.sh` cross-compile.
3. **Verify `ares/` is NOT on the include path** — if anything in `Core/Genesis/`
   accidentally `#include <ares/...>` or `<nall/...>`, the build must fail loud.

**Validation:** Mesen2 launches, opens a `.md` file, shows a black screen, no crash.

### 5.2 Porting order (smallest, most-isolated pieces first)

Build bottom-up so each piece is unit-testable in isolation:

1. **`GenesisPsg`** (174 LOC ref) — SN76489. Pure algorithm (tone channels + noise
   LFSR). Output → `SoundMixer`. This is the warm-up that proves the audio path.
2. **`GenesisYm2612`** (929 LOC ref) — FM synth. Channel operators, timers, SSG-EG.
   Mix with PSG into the sound buffer.
3. **`GenesisM68K`** (4 547 LOC ref) — the big one. Port registers, effective
   address computation, instruction table, interrupts. Reference
   `component/processor/m68000/` faithfully but implement against Mesen2's
   `Serializer` (`SV()` on every register) and a Mesen2 `MemoryType`-aware bus
   interface (not `ares::Node`).
4. **`GenesisZ80`** (3 659 LOC ref) — APU CPU. Patterns are close to
   [`Core/SMS/SmsCpu.h`](../Core/SMS/SmsCpu.h) (also Z80-family); reuse the
   register-pair and `ISerializable` structure.
5. **`GenesisVdp`** (1 543 LOC ref, `vdp-performance`) — render background, sprites,
   DMA, FIFO, IRQ. Writes a 32-bit framebuffer consumed by `GetPpuFrame()`.
6. **`GenesisMemoryManager`** — M68000 bus (`md/bus/`, `md/cpu/io.cpp`) and Z80 bus
   (`md/apu/bus.cpp`) including the bus arbiter (BUSREQ/BUSACK) that lets the M68K
   claim the Z80 bus.
7. **`GenesisCart`** — start with the `standard` and `banked` boards only; SVP /
   J-Cart / Game Genie / Realtec come later.
8. **`GenesisControlManager` + `GenesisController`** — 3-button pad first, 6-button
   as a config toggle.
9. **`GenesisConsole`** — wire it all together: `LoadRom` parses the ROM header
   (country code at `0x1F0`), picks region, builds the cart, powers on; `RunFrame`
   steps until the VDP frame counter advances (mirror
   [`Core/SMS/SmsConsole.cpp:154`](../Core/SMS/SmsConsole.cpp)).

### 5.3 ROM loading

`GenesisConsole::LoadRom(VirtualFile& romFile)`:

1. Read ROM bytes.
2. Detect SMD interleaved format (0x200-byte header + 0x4000-byte odd/even blocks)
   and de-interleave to plain `.bin` order. (Reference: ares `cartridge/board/standard.cpp`.)
3. Region from header country code at `0x1F0`:
   - `J` / Japan → `ConsoleRegion::NtscJapan`
   - `U` / `F` / `B` / … (Americas/PAL without `E`) → `ConsoleRegion::Ntsc`
   - `E` / Europe → `ConsoleRegion::Pal`
   Also honor filename tags `(j)`, `(u)`, `(e)` like SMS does
   ([`Core/SMS/SmsConsole.cpp`](../Core/SMS/SmsConsole.cpp) `UpdateRegion`).
4. Build `GenesisCart` with the appropriate mapper board (default `standard`).
5. Power on: initialize M68K, Z80, VDP, audio, controllers.
6. Set `_romFormat = RomFormat::Genesis`.

### 5.4 Video

`GenesisConsole::GetPpuFrame()` returns the VDP's framebuffer as `PpuFrameInfo`:

```cpp
PpuFrameInfo GenesisConsole::GetPpuFrame() {
    PpuFrameInfo frame{};
    frame.FrameBuffer   = _vdp->GetFramebuffer();   // 32-bit XRGB8888
    frame.Width         = _vdp->GetWidth();          // 256 (H32) or 320 (H40)
    frame.Height        = _region == ConsoleRegion::Pal ? 240 : 224;
    frame.FrameCount    = _frameCount;
    frame.ScanlineCount = frame.Height;
    frame.FirstScanline = 0;
    return frame;
}
```

Start with the `vdp-performance` algorithm (integer-only, faster, 1 543 LOC vs
2 694 for the accuracy variant). The accuracy variant can be a later swap behind a
`GenesisConfig.VdpAccuracy` flag.

### 5.5 Audio

Both PSG and YM2612 produce stereo samples at the native MD rate
(≈ `master_clock / 768` ≈ 53 kHz NTSC / 53 kHz PAL). Mix them and resample to
Mesen2's output rate, then feed `SoundMixer` the same way SMS does:

```cpp
void GenesisConsole::RunFrame() {
    ...step CPUs...
    _psg->Run();
    _ym2612->Run();
    // _psg / _ym2612 push into a shared stereo int16 buffer;
    // drain into SoundMixer::PlayBuffer at the end of the frame
    _emu->GetSoundMixer()->PlayBuffer(_audioBuffer, _audioSampleCount, _sampleRate, true);
}
```

Reference: [`Core/SMS/SmsConsole.cpp:154`](../Core/SMS/SmsConsole.cpp) `RunFrame`
calls `_psg->PlayQueuedAudio()`; copy that pattern.

### 5.6 Input

`GenesisController` button enum (3-button first, 6-button fields added later):

```cpp
enum class GenesisController {
    Up = 0, Down, Left, Right,   // D-pad
    A, B, C,                     // 3-button face
    X, Y, Z, Mode, Start,        // 6-button extras (Phase 4)
};
```

Register `ControllerType::GenesisController` in `SettingTypes.h`.
`GenesisControlManager::CreateControllerDevice()` returns a `BaseControlDevice`
whose `SetInputState` latches the button bits; the M68K I/O port read
(`md/cpu/io.cpp` TH/TL protocol) consumes the latched state. The 3-button pad
handshake (TH high → read B/S/A/Up/Down; TH low → read Start/C/Left/Right) must be
ported faithfully from `md/controller/control-pad/control-pad.cpp`.

### 5.7 Phase 1 acceptance

- [ ] Sonic the Hedgehog boots, shows the Sega logo, plays the intro chime.
- [ ] Title screen scrolls; Start advances.
- [ ] In-game, D-pad + A/B/C move and jump.
- [ ] FPS counter shows ~60 (NTSC) / ~50 (PAL).
- [ ] No crash on ROM unload / reload.
- [ ] `grep -r '<ares/' Core/Genesis/` returns nothing (no ares leak).

---

## 6. Phase 2 – Save State / Load State

**Goal:** `GenesisConsole::Serialize` round-trips through Mesen2's
`SaveStateManager` (slots, auto-save, run-ahead).

### 6.1 Native serializer (no blob wrapping)

Because every subsystem is native and `ISerializable`, save state uses Mesen2's
`SV()` macro directly — **no opaque blob** (unlike the NDS libretro path in
[`skills/NDS.md`](NDS.md)). This is the big payoff of the native-port strategy:
save states are inspectable, version-tolerant, and uniform with other consoles.

```cpp
void GenesisConsole::Serialize(Serializer& s) {
    SV(_frameCount);
    SV(_region);
    SV(_model);
    SV(_romFormat);
    SV(_cpu);            // GenesisM68K::Serialize uses SV() on its registers
    SV(_apu);            // GenesisZ80
    SV(_vdp);
    SV(_memoryManager);
    SV(_cart);
    SV(_psg);
    SV(_ym2612);
    SV(_controlManager);
}
```

Each subsystem's `Serialize(Serializer& s)` mirrors the field list in the ares
`serialization.cpp` files (e.g. `component/processor/m68000/serialization.cpp`),
but rewritten with `SV()`.

### 6.2 Save-state size

Native MD state is typically 1–4 MB (M68K + Z80 + VDP VRAM + PSG + YM2612 + cart
RAM). The 50 MB cap in [`Utilities/Serializer.cpp`](../Utilities/Serializer.cpp)
(raised for NDS, see [`skills/NDS.md`](NDS.md)) is more than enough — **no change
required**.

### 6.3 Versioning & compatibility

- Bump `SaveStateManager::FileFormatVersion` only when the Mesen2-side layout
  changes (adding a field, reordering). Adding fields at the end with sensible
  defaults keeps old states loadable.
- Implement `ValidateSaveStateCompatibility(ConsoleType)` to reject cross-console
  states with the "SaveStateWrongSystem" message.
- Because state is native (not an opaque ares blob), ares upstream changes do
  **not** invalidate Mesen2 save states — only Mesen2-side schema changes do.

### 6.4 Battery / SRAM

`SaveBattery()` calls `GenesisCart::SaveBattery()`, which writes SRAM / EEPROM / RTC
through Mesen2's `BatteryManager` so `.srm` files land in Mesen2's save folder
(consistent with SMS/NES). The EEPROM algorithm ports from
[`component/eeprom/m24c/`](../ares/component/eeprom/m24c/) (333 LOC).

### 6.5 Phase 2 acceptance

- [ ] Save slot 1, load slot 1 — state identical.
- [ ] Cross-region load rejected with "wrong system" message.
- [ ] Reload ROM after loading state — no crash.
- [ ] SRAM persists across Mesen2 restart (battery file written).
- [ ] Run-ahead (`RunAheadFrames=1`) does not desync.

---

## 7. Phase 3 – History Viewer / Movie

**Goal:** rewind, history viewer scrub, movie record/playback work for Genesis.

Mesen2's `HistoryViewer` ([`Core/Shared/HistoryViewer.h`](../Core/Shared/HistoryViewer.h))
and `RewindManager` are **console-agnostic**: they store `RewindData` snapshots
captured every N frames via `IConsole::Serialize`, and replay recorded input
through `BaseControlManager`. Once Phase 2 is correct, Phase 3 mostly "just works"
— the only Genesis-specific work is determinism and rewind-buffer sizing.

### 7.1 Determinism (mandatory)

History Viewer and Movie replay require that `Serialize` → `unserialize` →
`RunFrame` reproduces identical state for identical input. Requirements:

- **No host-time leakage.** Do not seed any RNG from wall-clock time inside the
  core. RAM power-on randomization must use Mesen2's `EmuSettings::InitializeRam`
  (deterministic per `RamState`), mirroring SMS
  ([`Core/SMS/SmsConsole.cpp:349`](../Core/SMS/SmsConsole.cpp) `InitializeRam`).
- **M68K / Z80 / VDP must be cycle-deterministic.** The ares interpreter cores
  are; preserve that in the port (no host-dependent optimizations, no uninitialized
  memory reads).
- **Verify by round-trip test:** load state, run 1 frame, serialize; reload the
  same state, run 1 frame, serialize; the two serialized buffers must be
  byte-identical.

### 7.2 Rewind buffer sizing

At ~2 MB per snapshot and the default rewind depth, memory can balloon. Tune per
console in `GenesisConfig`:

- Capture every 10–15 frames for Genesis (vs the default for smaller cores).
- Reuse Mesen2's existing compressed-save-state path (LZ4/zlib) for rewind
  snapshots to cut memory ~3–5×.

### 7.3 History Viewer

`HistoryViewer::CreateSaveState` / `SaveMovie` build on the rewind buffer.
`GenesisControlManager` must:

- Extend `BaseControlManager` (it does) so the viewer can replay input state.
- `GenesisControlManager::Serialize` must store the latched input state for both
  ports so a movie can resume after a load-state. Mirror the SMS pattern in
  [`Core/SMS/SmsControlManager.h`](../Core/SMS/SmsControlManager.h).

### 7.4 Movie record / playback

Mesen2 movies (`Movies/`) record per-frame `ControlDeviceState` per port. Because
`GenesisControlManager` extends `BaseControlManager`, the recording infrastructure
is reused unchanged. The 6-button pad's `Mode` toggle is stateful — ensure that
selector state is serialized inside `GenesisController`'s state (reference:
`ares/md/controller/control-pad/control-pad.cpp`).

### 7.5 Phase 3 acceptance

- [ ] Hold Rewind — gameplay scrubs backward smoothly.
- [ ] Open History Viewer, seek to a past frame, "Resume from here" — continues
      deterministically.
- [ ] Record a movie (Sonic GH1 to end of act 1), stop, replay — same path.
- [ ] Save state mid-movie, load it, continue playback — no desync.

---

## 8. Key Fixes and Hints (anticipated – update as discovered)

### 8.1 No ares-global reload crash (improvement over wrapper approach)

Because the native port has **no ares globals**, the reload-ROM hazard from
[`skills/NDS.md`](NDS.md) §"Reload ROM Handling" (where two `NdsConsole` instances
shared libretro state) **does not apply**. Each `GenesisConsole` owns its own M68K
/Z80/VDP/audio instances. Mesen2's existing `Emulator::TryLoadRom` lifecycle (build
new console, then drop old) works as-is. Keep it that way: never introduce
file-scope mutable state in `Core/Genesis/`.

### 8.2 M68000 instruction coverage

The M68000 is the riskiest port (4.5k LOC, ~1 000 instructions). Strategy:

- Port in instruction-group batches (move, arithmetic, logic, branch, bit-manip,
  multiply/divide, privileged) with a per-batch test ROM from
  [EX68](https://github.com/BigEndianAtoZ/EX68) or equivalent.
- Keep the disassembler (`component/processor/m68000/disassembler.cpp`) — it is
  invaluable for debugger + diffing against ares.
- The M68000 has no opcode-level undefined behavior that matters for commercial
  games; port ares' behavior verbatim.

### 8.3 Z80 bus arbitration

The M68K can request the Z80 bus (BUSREQ) and the Z80 must halt. This is in
`md/apu/bus.cpp` and `md/cpu/io.cpp`. Port it faithfully — getting this wrong breaks
audio in any game that uses the Z80 for sound driver work (i.e. almost all of
them). Symptom: silent or garbled audio after the boot jingle.

### 8.4 VDP width changes mid-game

The VDP switches H32 (256 px) ↔ H40 (320 px) per frame (and rarely mid-frame).
`GetPpuFrame().Width` must reflect the **current** mode. Mesen2's `VideoDecoder`
handles width changes between frames; mid-frame changes are not supported by the
existing renderer — acceptable (most games change between frames only).

### 8.5 Region / clock rate

Master clock = `Colorburst × 15` (NTSC) / `Colorburst × 12` (PAL); see
[`ares/md/system/system.hpp`](../ares/md/system/system.hpp) line 46. Implement:

- `GetMasterClockRate()` → 15 × 315/88 MHz (NTSC) or 12 × PAL colorburst.
- `GetFps()` → ~59.92 (NTSC) / 50.00 (PAL).
- M68K clock = master / 7 ≈ 7.67 MHz; Z80 clock = master / 15 ≈ 3.58 MHz.

### 8.6 Dual-CPU debugger

`GetCpuTypes()` returns `{ CpuType::GenesisM68K }` initially. Adding
`CpuType::GenesisZ80` later requires `GetAbsoluteAddress` / `GetRelativeAddress`
for the Z80 bus (which sits behind the M68K bus bank register). Defer until Phase 2
to avoid scope creep, exactly as NDS returns an empty CPU list today
([`skills/NDS.md`](NDS.md) §"Known Limitations").

### 8.7 Keep ares types out of Mesen2 headers

All `Core/Genesis/*.h` files must compile with only `pch.h` + Mesen2 `Shared/`
headers. Never `#include <ares/...>` or `<nall/...>` in a header (or anywhere in
`Core/Genesis/`). Reference the ares source in comments only:

```cpp
// Ported from ares/component/audio/sn76489/sn76489.hpp
class GenesisPsg : public ISerializable { ... };
```

### 8.8 Serializer field naming

`SV(var)` records the field name as a string for debugger/compatibility. Keep
field names stable once shipped (renames break save states). Match ares' field
semantics but use Mesen2 naming conventions (`_camelCase` members).

### 8.9 Build: no include-path changes

Unlike the prior plan, **do not** add `ares/` or `3rdParty/ares` to
`CMakeLists.txt` or `makefile` include paths. If a build error mentions a missing
`<ares/...>` or `<nall/...>` header, it means a porting file accidentally kept an
ares include — remove it and re-port that piece.

---

## 9. Validation per COMPILING.md

After each phase, validate on all three toolchains in [`COMPILING.md`](../COMPILING.md):

### 9.1 Linux (primary dev loop)

```bash
make           # clang, default
# or
USE_GCC=true make
```

Run `bin/linux-x64/Release/Mesen`, load a `.md` ROM, confirm phase acceptance.

### 9.2 Windows (MSVC)

Open `Mesen.sln` in VS 2022, build `Release/x64`, set startup project to `UI`,
run. Add the new `Core/Genesis/*` files to `Core.vcxproj` and `.filters`.

### 9.3 Cross-compile (Linux/WSL → Windows)

```bash
dotnet publish UI/UI.csproj -c Release -r win-x64 --self-contained true -p:PublishDir=bin/SelfContained/
./build-core-windows.sh
```

Copy `build-windows-x64/bin/MesenCore.dll` to the UI output directory. Because no
ares code is compiled, `build-core-windows.sh` needs no ares-related changes — it
just builds the new `Core/Genesis/*.cpp` like any other core source.

### 9.4 Smoke-test ROM matrix

| ROM | Tests |
|-----|-------|
| Sonic the Hedgehog (JUE) | Boot, video, audio, input, region detect |
| Dr. Robotnik's Mean Bean Machine | SRAM save (Phase 2) |
| Virtua Racing | SVP (ssp1601) — defer; needs SVP mapper ported |
| Sonic & Knuckles + Sonic 3 | Lock-on mapper (`cartridge/board/standard`) |
| Phantasy Star IV | SRAM + 6-button pad |
| Any PAL ROM | 50 fps, PAL clock rate |

---

## 10. Phase Summary / Checklist

| Phase | Milestone | Key deliverables | Validates |
|-------|-----------|------------------|-----------|
| 0 | Bootstrap build | Stub `GenesisConsole`, enum registration, build green on Linux + cross-compile; ares NOT on include path | COMPILING.md toolchains |
| 1 | Video / audio / input | `GenesisPsg`, `GenesisYm2612`, `GenesisM68K`, `GenesisZ80`, `GenesisVdp` (perf), `GenesisMemoryManager`, `GenesisCart` (standard+banked), `GenesisControlManager` (3-button) | Sonic boots & plays |
| 2 | Save state | `Serialize` with `SV()` per subsystem, battery/SRAM, compatibility checks | Slot save/load, run-ahead |
| 3 | History viewer / movie | Determinism audit, rewind tuning, input recording | Rewind, seek, movie record/play |
| 4 | Polish | 6-button pad, accuracy VDP variant, SVP/J-Cart/Game Genie mappers, M32X (interpreter SH2), debugger | ROM matrix in §9.4 |

---

## 11. Troubleshooting (quick reference – extend as issues are found)

### Build error: `ares/ares.hpp: No such file or directory`
- A `Core/Genesis/*.cpp` accidentally kept an ares `#include`. Remove it and
  re-port that piece. ares must stay off the include path (§8.9).

### Black screen, audio plays
- `GetPpuFrame().FrameBuffer` null → check VDP framebuffer is populated (run one
  frame before reading).
- Width/height mismatch → VDP mode not being read (§8.4).

### No audio
- PSG/YM2612 not feeding `SoundMixer::PlayBuffer` (§5.5).
- Sample-rate mismatch — verify resampler fed native MD rate (§8.5).

### Audio cuts out after the boot jingle
- Z80 bus arbitration (§8.3) — M68K took the Z80 bus and never released it, or
  Z80 never got BUSACK.

### Input ignored
- `ControllerType::GenesisController` not registered in `InputConfig.cs` /
  `SettingTypes.h`.
- TH/TL handshake not ported (§5.6) — 3-button pad reads return all-ones.

### Save state fails to load
- Field renamed or reordered → bump `FileFormatVersion` and add a migration, or
  reset and accept old states are unloadable.
- Cross-console state → `ValidateSaveStateCompatibility` must reject.

### History viewer desync
- Non-deterministic serialize (§7.1) — check for uninitialized memory or
  host-time leakage in the ported M68K/Z80/VDP.

### M68000 crash on a specific game
- Missing/incorrect instruction — diff against
  `ares/component/processor/m68000/instructions.cpp` for the faulting opcode.
- Address error exception not raised on misaligned access — port the
  `AddressError` path from ares.

### Z80 hangs after M68K reset
- Z80 reset line driven by M68K I/O (`md/cpu/io.cpp`); port the reset register
  write. Symptom: audio works briefly then freezes.

---

## 12. Key Fixes & Hints (from actual build integration)

This section records concrete issues encountered during Phase 1 implementation
and their resolutions. Use this as a quick-reference when encountering similar
problems.

### 12.1 Template specialization vs. `if constexpr` (M68K)

**Problem:** `Read<Size>()` and `Write<Size, Order>()` were written as explicit
template specializations (`template<> Read<Byte>(...)`, etc.) but the compiler
errored: *"specialization after instantiation"*.

**Root cause:** The instruction table code (earlier in the same .cpp) calls
`Read<Long>()`, `Write<Word>(...)` etc. before the specializations appear, causing
implicit instantiation.

**Fix:** Replace all explicit specializations with a single template definition
using `if constexpr`:

```cpp
// BEFORE (broken):
template<> uint32_t Read<Byte>(uint32_t address) { ... }
template<> uint32_t Read<Word>(uint32_t address) { ... }
template<> uint32_t Read<Long>(uint32_t address) { ... }

// AFTER (works):
template<uint32_t Size> uint32_t Read(uint32_t address) {
    if constexpr(Size == Byte) { ... }
    else if constexpr(Size == Word) { ... }
    else if constexpr(Size == Long) { ... }
    __builtin_unreachable();
}
```

Apply the same pattern to `Write<Size, Order>`, `Extension<Size>`, and any other
size-templated functions.

### 12.2 Template parameter deduction for register reads/writes (M68K)

**Problem:** `Read(DataRegister{ea.reg})` fails because `Size` can't be deduced
from the function argument.

**Fix:** Always specify the template parameter explicitly:
- `Read<Size>(DataRegister{ea.reg})` instead of `Read(DataRegister{...})`
- `Read<Long>(AddressRegister{...})` for address register reads (always Long)
- `Write<Long>(AddressRegister{...}, value)` for address register writes

### 12.3 Macro name collisions (Z80)

**Problem:** `#define IX _r.ix` collides with `Registers::IX` (the prefix enum).
When code uses `_r.prefix == Registers::IX`, the macro expands `IX` to `_r.ix`,
producing the nonsensical `_r.prefix == Registers::_r.ix`.

**Fix:** Rename the convenience macros to avoid collision:
```cpp
#define rIX _r.ix   // was: #define IX _r.ix
#define rIY _r.iy   // was: #define IY _r.iy
```

### 12.4 `std::swap` not found in controller (GenesisController.h)

**Problem:** `swap(_upLatch, _downLatch)` fails — `swap` not declared.

**Fix:** Use `std::swap()` explicitly, or rely on `using std::swap;` being in scope
from `pch.h`. In header-only code, always qualify: `std::swap(a, b)`.

### 12.5 `SV_ARRAY` vs `SVArray` (serialization macro)

**Problem:** `SV_ARRAY(_vram, VRAMSize)` — macro not found.

**Fix:** The correct Mesen2 macro name is `SVArray` (camelCase), not `SV_ARRAY`
(uppercase with underscore). See `Utilities/Serializer.h`:
```cpp
#define SV(var) (s.Stream(var, #var))
#define SVArray(arr, count) (s.StreamArray(arr, count, #arr))
```

### 12.6 `H40()` is a method, not a variable (VDP)

**Problem:** Code uses `H40 ? 320 : 256` but `H40` is a `bool() const` method,
not a data member. This compiles in ares (where it's accessed as a property via
nall's introspection) but not in plain C++.

**Fix:** Always call it as a method: `vdp.H40() ? 320 : 256`.

### 12.7 FIFO::Advance() needs VDP reference (VDP)

**Problem:** `FIFO::Advance()` references `vdp._command`, `vdp._dma` etc. but
`vdp` is not a member of the FIFO struct.

**Fix:** Add a `GenesisVdp& vdp` parameter to `Advance()`, matching the pattern
already used by `FIFO::Run(GenesisVdp& vdp)`.

### 12.8 `InitializeRam` access (GenesisConsole)

**Problem:** `GenesisMemoryManager::Init()` calls `console->InitializeRam()` but
the method was declared `private` in `GenesisConsole`.

**Fix:** Move `InitializeRam()` to the `public:` section of `GenesisConsole`.

### 12.9 `<functional>` not in pch.h

**Problem:** `std::function` used in `GenesisM68K.h` and `GenesisZ80.h` for bus
callback lambdas, but `<functional>` was not included in `Core/pch.h`.

**Fix:** Add `#include <functional>` to `Core/pch.h`.

### 12.10 `bind` macro with cast expressions (M68K instruction table)

**Problem:** `bind(Pattern("0100 1010 1111 1100"), ILLEGAL, (uint16_t)Pattern(...))`
fails — the C-style cast `(uint16_t)` confuses the `__VA_ARGS__` capture in the
`bind` macro's lambda.

**Fix:** Pre-compute the value into a local variable:
```cpp
{
    uint16_t illegalOpcode = Pattern("0100 1010 1111 1100");
    _instructionTable[illegalOpcode] = [this, illegalOpcode]() {
        instructionILLEGAL(illegalOpcode);
    };
}
```

### 12.11 Bus arbitration pattern (MemoryManager)

The M68K/Z80 bus arbitration follows this state machine:
- M68K writes 0x0000 to Z80 bus request register → `_busreqLine = true`
- If Z80 is at a wait point, it acknowledges: `_busreqAck = true`
- While `_busreqAck` is true, Z80 RAM is accessible by M68K
- M68K writes 0x0001 to release → `_busreqLine = false`, `_busreqAck = false`
- Z80 reset: M68K writes 0x0000 to reset register → `_resetLine = true` (Z80 halted)

### 12.12 VDP scanline model (not coroutine)

ares uses a co-routine scheduler (`co_yield`) to interleave VDP slots with CPU
execution. Mesen2's model is explicit: `RunScanline()` processes an entire
scanline's worth of VDP slots in one call, then returns. The CPU is stepped
outside the VDP. This means:

- No `libco` dependency — the VDP is a straightforward function call
- Timing accuracy is at scanline granularity (not sub-scanline)
- DMA and FIFO timing are approximated — the FIFO is drained per-slot during
  `RunScanline()`, but CPU/VDP interleaving is at scanline boundaries

### 12.13 Explicit template instantiation (M68K)

The M68K .cpp ends with explicit instantiation declarations for all template
variants. When using `bool` non-type template parameters, use `true`/`false`
not `1`/`0`:

```cpp
// Correct:
template void Write<Byte, false>(uint32_t, uint32_t);
template void Write<Byte, true>(uint32_t, uint32_t);

// Wrong (GCC rejects: int → bool narrowing):
template void Write<Byte, 0>(uint32_t, uint32_t);
template void Write<Byte, 1>(uint32_t, uint32_t);
```

### 12.14 -Wswitch warnings for new enum values

**Problem:** Adding `MemoryType::GenesisM68KRam`, `ControllerType::GenesisController`,
etc. triggers `-Wswitch` warnings in existing debugger files that switch over these
enums without handling the new values.

**Fix:** Add `default: break;` to switch statements in affected files. Only files
with switches that are intentionally not handling the new values need this:
- `Core/GBA/Debugger/GbaDebugger.cpp` — MemoryType switch
- `Core/SNES/Coprocessors/ST018/St018.cpp` — MemoryType switch
- `Core/Shared/ControllerHub.h` — ControllerType switch

Other files with `-Wswitch` warnings (event managers, assemblers, etc.) also
benefit from `default: break;` but they are cosmetic only.

### 12.15 BusWait callback signature (M68K / Z80)

**Problem:** `GenesisM68K` declares `std::function<void(uint32_t)> BusWait` but
the bus callbacks are set in `GenesisMemoryManager::Init()` — the lambda must match
the exact signature.

**Fix:** Ensure the lambda in `GenesisMemoryManager::Init()` matches:
```cpp
_m68k->BusWait = [this](uint32_t cycles) { /* ... */ };
```
The `BusIdle` and `BusWait` callbacks are separate: `BusIdle` is for idle cycles
(no bus activity), `BusWait` is for wait states (e.g., Z80 bus arbitration).

### 12.16 VDP DMA bus read callback

**Problem:** The VDP needs to read from the M68K bus during DMA load operations,
but it has no direct access to the bus.

**Fix:** Add a `std::function<uint16_t(uint32_t address)> DmaRead` callback to
`GenesisVdp`. The memory manager wires this in `Init()`:
```cpp
_vdp->DmaRead = [this](uint32_t address) -> uint16_t {
    return DmaRead(address);
};
```
`DmaRead()` in the memory manager performs a word-aligned M68K bus read, respecting
bus arbitration (Z80 bus state, TMSS, etc.).

### 12.17 SMD deinterleaving

**Problem:** SMD (Super Magic Drive) format ROMs have 512-byte headers and
interleaved 16KB blocks (first 8KB = even bytes, second 8KB = odd bytes).

**Fix:** Detect SMD by checking if `(romData.size() % 0x4000) == 512`, then
deinterleave:
```cpp
if((romData.size() % 0x4000) == 512) {
    // Skip 512-byte header, deinterleave 16KB blocks
    for(uint32_t block = 0; block < numBlocks; block++) {
        for(uint32_t i = 0; i < blockSize; i++) {
            if(i < blockSize / 2)
                deinterleaved[block * blockSize + i * 2 + 1] = romData[512 + block * blockSize + i];
            else
                deinterleaved[block * blockSize + (i - blockSize/2) * 2] = romData[512 + block * blockSize + i];
        }
    }
}
```

### 12.18 ROM header SRAM parsing

**Problem:** Some ROMs report SRAM incorrectly in their headers (double size,
odd-byte addressing).

**Fix:** Parse the "RA" marker at offset $1B0, extract start/end addresses and
type byte. If type bit 0 is set (odd-byte SRAM), halve the computed size:
```cpp
if(romData[0x1B0] == 'R' && romData[0x1B1] == 'A') {
    _sramStart = (romData[0x1B2] << 24) | (romData[0x1B3] << 16) | ...;
    uint32_t sramEnd = ...;
    _sramSize = sramEnd - _sramStart + 1;
    if(sramType & 0x01) _sramSize = (_sramSize + 1) / 2;  // odd-byte
    if(_sramSize > 0x10000) _sramSize = 0x10000;           // cap at 64KB
}
```

### 12.19 Frame timing: scanline-based CPU stepping

**Problem:** The M68K and Z80 need to execute for approximately one scanline's
worth of cycles per scanline. Exact cycle counting requires instruction-level
timing.

**Fix:** Use approximate cycle counting in `RunFrame()`:
```cpp
uint32_t m68kCyclesPerScanline = GetMasterClockRate() / 7 / (uint32_t)GetFps() / scanlinesPerFrame;
while(cyclesRun < targetCycles && !_m68k->IsStopped()) {
    _m68k->ExecuteInstruction();
    cyclesRun += 4;  // approximate per-instruction cycle count
}
```
This is sufficient for Phase 1. Phase 2+ should track actual M68K/Z80 cycle
counts from each instruction for more accurate timing.

### 12.20 CMakeLists.txt: no changes needed

Mesen2 uses `file(GLOB_RECURSE CORE_SOURCES "Core/*.cpp")` in CMakeLists.txt,
which automatically picks up all `Core/Genesis/*.cpp` files. **No CMakeLists.txt
modifications are needed.** Similarly, the makefile uses `find Core -name '*.cpp'`
which also auto-discovers new files.

---

## 14. Phase 1 Implementation Status

### Build status: PASSING

All Genesis core files compile cleanly with `./build-core-windows.sh`. MesenCore.dll
builds successfully (15 MB). Only cosmetic -Wswitch warnings remain from existing
non-Genesis debugger files.

### Implemented files (Core/Genesis/)

| File | Lines | Status |
|------|-------|--------|
| `GenesisConsole.h/cpp` | 383 | Complete — IConsole impl, LoadRom, RunFrame, Serialize |
| `GenesisM68K.h/cpp` | 3268 | Complete — M68000 interpreter, all instructions, bus callbacks |
| `GenesisZ80.h/cpp` | 1260 | Complete — Z80 APU, NMOS mode, bus arbitration |
| `GenesisVdp.h/cpp` | 1389 | Complete — VDP with FIFO, DMA, layers, sprites, DAC |
| `GenesisYm2612.h/cpp` | 773 | Complete — OPN2 FM synth, 6 channels, SSG-EG |
| `GenesisPsg.h/cpp` | 191 | Complete — SN76489 PSG, 3 tone + noise |
| `GenesisMemoryManager.h/cpp` | 750 | Complete — M68K/Z80 bus, I/O, TMSS, SRAM |
| `GenesisControlManager.h/cpp` | 120 | Complete — 3-button pad, TH/TL protocol |
| `Input/GenesisController.h` | — | Complete — 3-button pad device |
| `GenesisTypes.h` | 68 | Complete — shared types, constants |

### Shared infrastructure changes

| File | Changes |
|------|---------|
| `Core/Shared/CpuType.h` | Added `GenesisM68K`, `GenesisZ80` |
| `Core/Shared/MemoryType.h` | Added `GenesisMemory`, `GenesisM68KRam`, `GenesisZ80Ram`, `GenesisZ80Bus`, `GenesisVdpVram`, `GenesisVdpVsram`, `GenesisVdpCram`, `GenesisCartridgeRom`, `GenesisCartridgeRam`, `GenesisPort` |
| `Core/Shared/SettingTypes.h` | Added `GenesisConfig` struct, `GenesisModel` enum, `RomFormat::Genesis` |
| `Core/Shared/Emulator.cpp` | Added `TryLoadRom<GenesisConsole>` to factory |
| `Core/Shared/EmuSettings.cpp` | Added `ConsoleType::Genesis` cases for serialization and overscan |
| `Core/Shared/ControllerHub.h` | Added `default: break;` for `-Wswitch` |
| `Core/GBA/Debugger/GbaDebugger.cpp` | Added `default: break;` for `-Wswitch` |
| `Core/SNES/Coprocessors/ST018/St018.cpp` | Added `default: break;` for `-Wswitch` |

### Next steps (Phase 1.9 → Phase 2)

1. **Runtime testing**: Load a Genesis ROM (e.g. Sonic) and verify video output,
   audio playback, and controller input work end-to-end
2. **M68K cycle accuracy**: Replace approximate cycle counting with actual
   per-instruction cycle tracking for proper timing
3. **Z80 interrupt wiring**: Connect VDP Hblank interrupt to Z80 IRQ line for
   audio driver synchronization
4. **SRAM persistence**: Test battery save with ROMs that use SRAM
5. **Phase 2**: Full save state / load state testing and validation

---

## 13. References

- [`COMPILING.md`](../COMPILING.md) – build & validation instructions
- [`skills/NDS.md`](NDS.md) – Libretro-core integration pattern (instance
  lifecycle, save state size, input mapping)
- [`skills/3DS.md`](3DS.md) – HW-rendering core integration (mostly N/A here;
  Genesis VDP is software-rendered)
- [`Core/Shared/Interfaces/IConsole.h`](../Core/Shared/Interfaces/IConsole.h) –
  the interface `GenesisConsole` must implement
- [`Core/SMS/SmsConsole.h`](../Core/SMS/SmsConsole.h) / `.cpp` – closest native
  analogue (Sega 8-bit); copy its structure and serializer style
- [`Core/PCE/`](../Core/PCE/) – second native analogue; note the
  `PceCpu.Instructions.cpp` split-file pattern for the M68000 instruction table
- [`Utilities/Serializer.h`](../Utilities/Serializer.h) – `SV()` macro and
  `Serializer` interface
- [`Core/Shared/Audio/SoundMixer.h`](../Core/Shared/Audio/SoundMixer.h) – audio
  output sink
- [`Core/Shared/Emulator.cpp`](../Core/Shared/Emulator.cpp) §`TryLoadRom` – console
  factory registration point (~line 580)
- [`ares/md/md.hpp`](../ares/md/md.hpp) – ares MD public entry point (reference)
- [`ares/md/system/system.cpp`](../ares/md/system/system.cpp) – ares MD lifecycle
  `load`/`power`/`run`/`serialize` (reference)
- [`ares/component/processor/m68000/`](../ares/component/processor/m68000/) – M68000
  reference (4.5k LOC, largest port)
- [`ares/component/audio/ym2612/`](../ares/component/audio/ym2612/) – YM2612 FM
  synth reference
- [`ares/md/vdp-performance/`](../ares/md/vdp-performance/) – preferred VDP
  reference variant (1.5k LOC, integer-only)

---

## 15. Runtime / Behavioral Fixes (black-screen debugging)

These are behavioral bugs discovered during runtime testing of the Genesis core.
Unlike the build/compilation fixes in §12, these manifest as incorrect emulation
behavior (typically: black screen, no DMA, wrong colors).

### 15.1 TMSS lockout bypass

**Symptom:** VDP registers/CRAM/VRAM never written; game appears dead after boot.

**Root cause:** Genesis Model 1 TMSS (TradeMark Security System) locks out VDP
access until the M68K writes "SEGA" (0x53454741) to address `0xA14000`. Without
this write, all VDP port reads/writes are ignored.

**Fix:** In `GenesisMemoryManager`, detect writes to `0xA14000`-`0xA14003` and
set a `_tmssUnlocked` flag. VDP port accesses (`0xC00000`-`0xC0001F`) are gated
on this flag: if not unlocked, reads return 0xFFFF and writes are silently dropped.

### 15.2 Z80 bus request for VDP DMA

**Symptom:** DMA Load (mode 0/1) reads all zeros from RAM; audio driver RAM
appears empty.

**Root cause:** DMA Load reads from the M68K bus, which includes Z80 RAM at
`0xA00000-0xA0FFFF`. The M68K can only access Z80 RAM when the Z80 bus has been
requested (`_busreqLine = true`) and acknowledged (`_busreqAck = true`). If the
game has not requested the Z80 bus, DMA reads from Z80 RAM return open-bus (0xFFFF
or 0x0000 depending on implementation).

**Fix:** `GenesisMemoryManager::DmaRead()` must check Z80 bus arbitration state
before accessing Z80 RAM, same as M68K normal bus reads.

### 15.3 VBlank interrupt delivery to M68K

**Symptom:** Game's VBlank ISR never runs; game hangs in a polling loop waiting
for VBlank.

**Root cause:** Two sub-issues:
1. The VDP's `_irq.vblank.transitioned` flag must be set when VBlank *starts*
   (vblank 0→1), and cleared by `Vedge()` (at hcounter=0 of the next scanline).
   Without the `transitioned` mechanism, Vedge fires the IRQ every scanline during
   VBlank instead of only once.
2. M68K interrupt polling must happen **before each instruction**, not once per
   scanline. The M68K checks for pending interrupts at the start of each
   instruction execution cycle.

**Fix:**
- Implement the `transitioned` flag in `Vblank()` and `Vedge()` matching ares.
- Move M68K interrupt polling into `ExecuteInstruction()` (called per-instruction)
  rather than once per scanline in `RunFrame()`.

### 15.4 FIFO::Advance missing third swap

**Symptom:** VDP FIFO permanently full; DMA operations blocked; no CRAM/VRAM writes
succeed after the first few.

**Root cause:** `FIFO::Advance()` was missing `std::swap(slots[2], slots[3])`.
The FIFO has 4 slots and requires 3 swaps per Advance to move data through the
pipeline:
```
swap(slots[0], slots[1]);  // entry exits slot[0]
swap(slots[1], slots[2]);  // data bubbles up
swap(slots[2], slots[3]);  // NEW entry from slot[3] enters pipeline
```
Without the third swap, data entering slot[3] stalls permanently, making the FIFO
appear full.

**Fix:** Add `std::swap(slots[2], slots[3])` to `FIFO::Advance()`.

### 15.5 DmaRead RAM offset mask (0xFFFE → 0xFFFF)

**Symptom:** DMA reads from RAM (0xE00000+) return 0; VRAM fill with zeros.

**Root cause:** `DmaRead()` for RAM regions used `address & 0xFFFE` to compute the
offset into the 64KB work RAM. The mask `0xFFFE` produces even-only offsets and can
cause out-of-bounds access for odd addresses, returning 0.

**Fix:** Change mask to `address & 0xFFFF`.

### 15.6 DmaRead ROM mirroring for 0x400000-0xBFFFFF

**Symptom:** DMA reads from ROM addresses above the actual ROM size return 0.

**Root cause:** Genesis cartridges are typically ≤ 4MB. The cartridge address
space is mirrored: `0x000000-0x3FFFFF` is the base, `0x400000-0x7FFFFF` and
`0x800000-0xBFFFFF` are mirrors. The code did not handle mirroring, so addresses
in the mirror regions read as 0.

**Fix:** For `0x400000-0xBFFFFF`, apply `address & 0x3FFFFE` to mirror into the
base ROM region.

### 15.7 CRAM lookup missing in DAC::Pixel()

**Symptom:** Screen near-black even when CRAM contains valid color data.

**Root cause:** `DAC::Pixel()` was using the raw 6-bit color index as a pixel value
instead of looking up the 9-bit CRAM color. A color index of 1 maps to a CRAM value
like 0x024, which when used directly as an 8-bit component is barely visible.

**Fix:** Add CRAM lookup in `DAC::Pixel()`: look up `_cram[colorIndex]` and expand
the 3-bit-per-channel value to 8-bit-per-channel for the framebuffer.

### 15.8 DrainFifo after WriteDataPort for DMA mode 2 (Fill)

**Symptom:** DMA Fill never executes; VRAM remains at initial zero values.

**Root cause:** In ares, M68K and VDP run as co-routines. After the M68K writes
the fill data to the data port, the VDP automatically gets time to process the
FIFO entry, which triggers `Advance()` to clear `_dma.wait`, allowing the Fill to
start. In Mesen2's sequential model (VDP runs a full scanline, then M68K runs),
the FIFO entry added by `WriteDataPort` is never processed until the NEXT VDP
scanline. During that scanline, `_dma.Run()` should theoretically find the right
conditions (pending=1, wait=0, mode=2, FIFO empty, !rambusy), but the timing of
rambusy management between `Slot()` and `_dma.Run()` in the scanline loop prevents
Fill from ever starting.

The core issue is that `WriteDataPort` calls `DrainFifo()` **before** adding the
FIFO entry (to make room), but not **after**. So the entry sits unprocessed.

**Fix:** Add a second `DrainFifo()` call **after** `FIFO::Write()` in
`WriteDataPort()`. This eagerly processes the just-written entry:
1. DrainFifo clears latency on all FIFO entries
2. `_fifo.Run()` processes the entry (lower byte, then upper byte)
3. `Advance()` captures fill data and clears `_dma.wait = 0`
4. DrainFifo's DMA branch finds pending=1, wait=0, mode=2, FIFO empty → Fill starts
5. The entire DMA Fill runs to completion within this single DrainFifo call

This simulates ares's co-routine yield behavior where the VDP processes pending
work after every M68K write.

### 15.9 DrainFifo after LATCH2 WriteControlPort for DMA mode 0/1

**Symptom:** DMA Load (memory-to-VDP) never executes during M68K execution.

**Root cause:** Same co-routine mismatch as §15.8. After LATCH2 sets
`_command.pending = 1` and clears `_dma.wait = 0` (for mode 0/1), the DMA is
ready to run. But in the sequential model, the VDP doesn't get time until the
next scanline. By then, the game may have overwritten the DMA command.

**Fix:** Add `DrainFifo()` after the LATCH2 handling in `WriteControlPort()`.
This immediately starts the DMA Load: `_dma.Synchronize()` sets `active=1`,
`_dma.Fetch()` reads source data from the bus, and `_dma.Load()` writes it into
the FIFO → VRAM/CRAM/VSRAM. The entire DMA transfer completes within this
DrainFifo call.

### 15.10 _command.address stability during DrainFifo

**Symptom:** FIFO writes target incorrect VRAM addresses after a DMA operation
runs inside DrainFifo.

**Root cause:** `DrainFifo()` executes DMA operations (Fill, Load, Copy) that
modify `_command.address` (each Fill/Load step increments it). If `WriteDataPort`
reads `_command.address` before DrainFifo but uses it after, the DMA's address
modifications corrupt the write target.

**Fix:** In `WriteDataPort`, save `_command.target` and `_command.address` into
local variables **before** calling `DrainFifo()`, then use the saved values for
`FIFO::Write()`:
```cpp
uint8_t  target = _command.target;
uint32_t address = _command.address;
DrainFifo();
_fifo.Write(target, address, data);
```

### 15.11 DMA source address high bits preservation

**Symptom:** DMA reads from ROM above 0x3FFFFF return 0 or wrong data.

**Root cause:** The DMA source address is 22-bit (stored in registers 21-23).
When incrementing the source during DMA, the high 6 bits (bits 16-21, stored in
register 23) must be preserved. Code that truncated the source to 16 bits
(`source & 0xFFFF`) lost these bits, causing reads from the wrong ROM address.

**Fix:** When incrementing DMA source, preserve the high bits:
```cpp
source = (source & 0x3F0000) | ((source + 1) & 0xFFFF);
```

### 15.12 CRAM address bounds (6-bit, 0-63 only)

**Symptom:** CRAM writes to indices > 63 corrupt memory or are silently dropped.

**Root cause:** CRAM has 64 entries (6-bit addressing). Code that computed CRAM
addresses without masking could produce out-of-range indices. The VDP command
address is 17-bit; for CRAM writes (target=3), the address is shifted right by 1
to produce the CRAM index, which can exceed 63 for addresses ≥ 128.

**Fix:** Guard all CRAM writes: `if(addr < CRAMSize) _cram[addr] = value;`.
Similarly, CRAM reads must clamp: `return (index < CRAMSize) ? _cram[index] : 0;`.

### Key architectural insight: sequential vs. co-routine model

The single most important pattern underlying bugs §15.8–15.10 is the mismatch
between ares's co-routine execution model and Mesen2's sequential model:

| Aspect | ares (co-routine) | Mesen2 (sequential) |
|--------|--------------------|---------------------|
| CPU/VDP interleaving | M68K writes, yields; VDP processes immediately | VDP runs full scanline, then M68K runs |
| DMA timing | DMA starts on the next VDP slot after LATCH2 | DMA only starts when DrainFifo is called |
| FIFO drain | Happens naturally as VDP slots process | Must be explicitly simulated by DrainFifo() |

The **DrainFifo()** function is the bridge between these models. It must be called
at every point where ares would yield from M68K to VDP:
- Before each VDP port write (to process any pending work first)
- After each VDP data port write (to process the just-written FIFO entry)
- After each VDP control port LATCH2 write (to start pending DMA)
- Before each VDP control port read (to report accurate FIFO/DMA status)

---

## 16. Unhandled Fix Points (black-screen diagnosis, as of 2026-07-01)

The black screen persists after the §15 fixes. The following is a comprehensive
inventory of **known but not-yet-implemented** or **suspected-buggy** areas,
ranked by likelihood of causing or contributing to the black screen. Each entry
has a **Status** field: `UNFIXED` (not yet addressed), `SUSPECTED` (implemented
but may be wrong), `STUB` (placeholder only).

### 16.1 Serialize skips load state — breaks framework re-init [UNFIXED]

**File:** `Core/Genesis/GenesisConsole.cpp` lines 466–497

**Problem:** `Serialize()` early-returns when loading:
```cpp
void GenesisConsole::Serialize(Serializer& s) {
    if(!s.IsSaving()) {
        GENESIS_DBG("Console::Serialize SKIP LOAD — preserving Power() state");
        return;
    }
    SV(_m68k); ...
}
```
The Mesen2 `Emulator` framework calls `Serialize()` with `IsSaving()==false`
during internal operations (e.g. rewind buffer capture, save-state load,
history snapshot). Skipping the load means subsystems never restore state.

**Fix:** Remove the early-return guard; perform a symmetric save/load using
`SV()` for every field. The `SV()` macro already handles both directions. If
load crashes, the real bug is in a subsystem's `Serialize()` — fix that, don't
skip the load.

### 16.2 Approximate cycle counting — M68K/Z80 timing [UNFIXED]

**File:** `Core/Genesis/GenesisConsole.cpp` lines 281–298

**Problem:** Each M68K instruction is charged a flat 4 cycles:
```cpp
while(cyclesRun < targetCycles && !_m68k->IsStopped() && m68kMaxInstr-- > 0) {
    _m68k->ExecuteInstruction();
    cyclesRun += 4; //approximate
}
```
Real M68K instructions take 4–158 cycles. A flat 4 means the CPU runs
**far too fast** relative to the VDP, completing an entire frame's worth of
game code in the first few scanlines. The game may finish its init sequence
and enter its main loop *before the VDP has finished the first scanline*,
causing VBlank polling to never see the VBlank transition (the VDP hasn't
reached VBlank yet by the time the M68K polls for it).

**Fix:** Have `ExecuteInstruction()` return the actual cycle count (ares
computes this via `BusWait()`/`BusIdle()` calls inside each instruction).
Accumulate the returned count instead of `+= 4`. Same for Z80.

### 16.3 Video filter stride mismatch in H32 mode [SUSPECTED]

**File:** `Core/Genesis/GenesisDefaultVideoFilter.h` line 39

**Problem:** The VDP framebuffer always uses `MaxWidth = 320` as the row
stride (`_framebuffer.data() + y * MaxWidth`), even in H32 (256-wide) mode.
But the video filter reads with `inWidth = _baseFrameInfo.Width`, which is 256
in H32 mode:
```cpp
uint32_t inWidth = _baseFrameInfo.Width; // 256 in H32
for(uint32_t y = 0; y < frame.Height; y++) {
    uint32_t* src = in + (y + overscan.Top) * inWidth + overscan.Left; // wrong stride!
```
This causes every row after the first to read from the wrong offset, producing
a sheared/garbled image. In H40 mode (320 wide) the stride matches and this is
not a problem — but many games use H32.

**Fix:** Use `GenesisVdp::MaxWidth` (320) as the input stride always, or have
`GetPpuFrame()` report the true framebuffer stride separately from the visible
width. The cleanest fix is to add a `Stride` field to `PpuFrameInfo` or to
hardcode 320 in the filter since the VDP always uses 320-wide rows.

### 16.4 GetLineBuffer VBlank range doesn't match VblankCheck [SUSPECTED]

**File:** `Core/Genesis/GenesisVdp.cpp` lines 276–288 vs 213–223

**Problem:** `VblankCheck()` sets VBlank at vcounter `0x0E0` (V28 NTSC) and
clears it at `0x1FF`. But `GetLineBuffer()` returns nullptr only for
`[0x0E8, 0x1F5)`:
```cpp
if(_region == ConsoleRegion::Ntsc && y >= 0x0E8 && y < 0x1F5) return nullptr;
```
This means lines `0x0E0–0x0E7` and `0x1F5–0x1FF` are treated as visible
(GetLineBuffer returns a pointer) even though VBlank is active. While
`IsDisplayEnable()` is false during VBlank so DAC::Pixel fills background
color, the wrong framebuffer rows get written. More importantly, the range
boundaries don't match ares's vdp-performance `GetLineBuffer()`.

**Fix:** Align the null-return range with the actual VBlank active range from
`VblankCheck()`. For NTSC V28: return nullptr for `[0x0E0, 0x1FF]` (the full
VBlank region). Cross-check against ares's `vdp-performance/serialization.cpp`
and `vdp-performance/vdp.cpp` `GetLineBuffer()`.

### 16.5 M68K interrupt delivery timing [SUSPECTED]

**File:** `Core/Genesis/GenesisMemoryManager.cpp` lines 110–124

**Problem:** `CheckInterrupts` is called per-instruction (good), but the
VBlank IRQ is only delivered if `6 > ipl` (SR interrupt mask < 6). At
power-on, `SR.i = 7` (mask 7), which **blocks all interrupts including
VBlank**. The game's boot code must lower `SR.i` (via `move.w #$2000,sr` or
similar) before VBlank can be delivered. If the game code never executes far
enough to lower the mask (due to timing issues from §16.2), VBlank is never
delivered and the game hangs in a polling loop.

**Diagnostic:** The debug log already prints "VBlank IRQ BLOCKED by SR.i" —
check if this message persists past the first few hundred instructions. If it
does, the game is stuck before lowering the interrupt mask.

**Fix:** This is likely a symptom of §16.2 (timing) rather than a standalone
bug. Fix cycle counting first.

### 16.6 BusIdle / BusWait callbacks are no-ops [UNFIXED]

**File:** `Core/Genesis/GenesisMemoryManager.cpp` lines 104–109

**Problem:**
```cpp
_m68k->BusIdle = [this](uint32_t cycles) { /* empty */ };
_m68k->BusWait = [this](uint32_t cycles) { /* empty */ };
```
These callbacks are supposed to account for bus cycle consumption (M68K bus
arbitration with Z80, VDP DMA stealing cycles, etc.). With empty callbacks,
the M68K has no concept of elapsed time within instructions. This compounds
with §16.2 — even if `ExecuteInstruction()` returned a cycle count, the
internal `BusWait()` calls that compute it are silently discarded.

**Fix:** Maintain a `_m68kCycleCount` field in `GenesisMemoryManager` (or in
`GenesisM68K` itself). `BusWait` and `BusIdle` add to it.
`ExecuteInstruction()` returns the delta. This is the prerequisite for fixing
§16.2.

### 16.7 Audio not synchronized per-scanline [UNFIXED]

**File:** `Core/Genesis/GenesisConsole.cpp` lines 302–306

**Problem:** PSG and YM2612 are only run once at the end of the frame:
```cpp
_psg->Run();
_psg->PlayQueuedAudio();
_ym2612->Run();
_ym2612->PlayQueuedAudio();
```
In ares, audio chips are ticked per-scanline (or per-cycle) to maintain sample
rate accuracy. Running them once per frame produces ~60 sample batches instead
of ~262, causing audio buffer underruns/overruns and incorrect sample timing.
This doesn't cause the black screen but must be fixed for functional audio.

**Fix:** Call `_psg->Run()` and `_ym2612->Run()` inside the per-scanline loop
(after M68K/Z80 for that scanline), passing the scanline's cycle count.

### 16.8 Region auto-detection not implemented [STUB]

**File:** `Core/Genesis/GenesisConsole.cpp` lines 441–446

**Problem:**
```cpp
case ConsoleRegion::Auto:
    //Default to NTSC for now; could parse ROM header region string
    _region = ConsoleRegion::Ntsc;
```
The ROM header at offset `0x1F0` contains a region string (e.g. "JUE",
"USA", "EUR", "JAP"). Auto-detection always picks NTSC, which causes wrong
timing (262 vs 313 scanlines) and wrong VBlank ranges for PAL ROMs.

**Fix:** Read the region string from `romData[0x1F0..0x1FF]`. If it contains
'E' but not 'U' or 'J', select PAL. Otherwise select NTSC. This affects
`GetFps()`, `GetMasterClockRate()`, and VDP `UpdateScreenParams()`.

### 16.9 TMSS BIOS not implemented [STUB]

**File:** `Core/Genesis/GenesisMemoryManager.cpp` lines 170–174

**Problem:** When `_tmssEnable` is true and `_romEnable` is false, ROM reads
return `0xFFFF` (open bus) instead of the TMSS BIOS. On real Model 1
hardware, the TMSS BIOS maps at `0x000000` until "SEGA" is written to
`0xA14000`. Without the BIOS, the M68K reset vector points to garbage (0xFFFF)
and the CPU jumps to an invalid address.

**Workaround in place:** `_tmssEnable` defaults to false (non-TMSS Model),
so `_romEnable` is true and ROM reads work. But if a user selects Model 1
with TMSS, the system breaks.

**Fix:** Either (a) implement the TMSS BIOS ROM (dumped, ~2KB), or (b) force
`_tmssEnable = false` in all configurations and document that TMSS is not
supported. Option (b) is sufficient for Phase 1.

### 16.10 GetConsoleState empty stub [STUB]

**File:** `Core/Genesis/GenesisConsole.cpp` lines 433–436

**Problem:**
```cpp
void GenesisConsole::GetConsoleState(BaseState& state, ConsoleType consoleType) {
    //Phase 1 stub — will populate for debugger in a future phase
}
```
This is used by the debugger and rewind system. An empty stub means the
debugger shows no register state, and rewind snapshots may lack CPU state.

**Fix:** Populate `BaseState` with M68K registers (D0–D7, A0–A7, PC, SR),
Z80 registers, VDP registers, and frame count. See `Core/NES/NesConsole.cpp`
`GetConsoleState()` for the pattern.

### 16.11 GetRelativeAddress not implemented [STUB]

**File:** `Core/Genesis/GenesisMemoryManager.cpp` lines 804–809

**Problem:** Always returns `{-1, MemoryType::None}`. This breaks debugger
breakpoints, cheat codes, and memory viewers that need to convert absolute
addresses to CPU-relative addresses.

**Fix:** Implement the reverse mapping of `GetAbsoluteAddress()` — scan ROM,
RAM, SRAM, Z80 RAM, and VDP memory regions to find which contains the given
absolute address, then compute the CPU-relative offset.

### 16.12 Framebuffer never cleared between frames [SUSPECTED]

**File:** `Core/Genesis/GenesisVdp.cpp` line 32

**Problem:** `_framebuffer` is initialized to 0 in the constructor but never
cleared between frames. If a scanline's `GetLineBuffer()` returns nullptr
(during VBlank or border), those framebuffer rows retain stale pixels from
the previous frame. On the very first frame, they're black (0) — contributing
to the black screen if the VDP hasn't written to all visible rows yet.

**Fix:** Clear the framebuffer to the background color at the start of each
frame (when vcounter wraps to topline), or clear only the rows that
`GetLineBuffer()` returns nullptr for. Alternatively, fill the entire
framebuffer with `_cram[_io.backgroundColor]` at the start of `RunFrame()`.

### 16.13 Z80 held in reset at startup (expected, but verify release) [SUSPECTED]

**File:** `Core/Genesis/GenesisZ80.cpp` lines 37–38, 51–53

**Problem:** At power-on, `_resetLine = false`, so `ExecuteInstruction()`
does nothing (`if(!_resetLine || _busreqLatch) { Wait(1); return; }`). The
M68K must write to `0xA11100` (Z80 bus request) and `0xA11200` (Z80 reset)
to release the Z80. If the M68K isn't executing the game's init code
(due to §16.2 timing), the Z80 never starts, and no audio driver runs.

**Diagnostic:** Add a log in `SetReset(true)` to confirm the M68K releases
the Z80. If this never fires, the M68K isn't reaching the Z80 init code.

**Fix:** This is likely a symptom of §16.2. Verify by checking if the M68K
ever writes to `0xA11200`.

### 16.14 No 6-button controller / Mega Mouse support [UNFIXED]

**File:** `Core/Genesis/Input/GenesisController.h`

**Problem:** Only the 3-button pad is implemented. Games that probe for
6-button controllers at startup may hang or behave unexpectedly if the
controller doesn't respond with the 6-button ID sequence.

**Fix:** Add a 6-button controller mode that responds to the TH toggle
sequence with the extra button data after the 3rd read. Add a `GenesisModel`
setting to select 3-button vs 6-button.

### 16.15 VDP Hblank IRQ not connected to Z80 [UNFIXED]

**File:** `Core/Genesis/GenesisMemoryManager.cpp` (missing wiring)

**Problem:** The VDP generates Hblank interrupts for the M68K (level 4), but
the Z80's IRQ line is never connected to the VDP. Some Z80 audio drivers
poll the VDP's Hblank via the YM2612 timer or VDP status register, but others
rely on a direct IRQ. Without this, Z80 audio drivers that use IRQ-driven
timing won't sync properly.

**Fix:** In the per-scanline loop, call `_z80->SetIrq(_vdp->GetHblankIrq())`
after `_vdp->RunScanline()`. Clear the IRQ when the Z80 acknowledges it.

### 16.16 VDP register write logging incomplete for mode register 4 [SUSPECTED]

**File:** `Core/Genesis/GenesisVdp.cpp` (register write handler)

**Problem:** VDP register 11 (mode register 4) controls H32/H40 mode
switching and interlace. If the game writes to register 11 to switch from
H40 to H32 (or vice versa), the `_io.displayWidth` flag changes, but
`_latch.displayWidth` is only updated at the start of `RunScanline()`. A
mid-scanline mode switch would corrupt rendering. Also, `UpdateScreenParams()`
is only called when vcounter == bottomline, so a mode change mid-frame
doesn't take effect until the next frame.

**Fix:** This is a known limitation of the sequential model. For Phase 1,
document it; for Phase 2, add mid-frame mode switch handling.

### 16.17 ROM size check off-by-one in ReadRomWord [SUSPECTED]

**File:** `Core/Genesis/GenesisMemoryManager.cpp` lines 456–463

**Problem:**
```cpp
uint16_t ReadRomWord(uint32_t address) {
    if(address >= _romSize) return 0xFFFF;
    return ((uint16_t)_rom[address] << 8) | _rom[address + 1];
}
```
If `address == _romSize - 1` (odd, but address is masked to even so this
can't happen with `& 0xFFFE`), `_rom[address + 1]` is out of bounds. With
the even-mask, `address` is at most `_romSize - 2`, so `address + 1` is
`_romSize - 1` (in bounds). However, if `_romSize` is odd (shouldn't happen
after power-of-2 padding, but worth guarding), the last byte is never read.

**Fix:** Change the check to `if(address + 1 >= _romSize) return 0xFFFF;`
for safety, even though the padding should prevent this.

### 16.18 No debugger / disassembler integration [STUB]

**File:** `Core/Genesis/GenesisConsole.h`

**Problem:** `GetCpuTypes()` returns the CPU types, but there are no
`GenesisM68KDebugger`, `GenesisZ80Debugger`, or `GenesisVdpDebugger` classes.
The Mesen2 debugger can't step through M68K/Z80 code, set breakpoints, or
view VDP state. This blocks the "debug the black screen" workflow — without a
debugger, diagnosing where the M68K gets stuck requires reading
`OutputDebugStringA` logs.

**Fix:** Create debugger classes following the `Core/SMS/SmsDebugger`
pattern. At minimum, implement an M68K disassembler and register view. This
is a Phase 2 task but would greatly accelerate black-screen diagnosis.

---

### Priority order for fixing the black screen

1. **§16.2** (cycle counting) — most likely root cause. The M68K runs too
   fast, finishes init before VDP reaches VBlank, and the game's VBlank poll
   never succeeds.
2. **§16.6** (BusIdle/BusWait) — prerequisite for §16.2; without real cycle
   accumulation, `ExecuteInstruction()` can't return a meaningful count.
3. **§16.1** (Serialize skip-load) — may cause framework-level state
   corruption; fix regardless.
4. **§16.12** (framebuffer clear) — ensures stale pixels don't mask the
   real rendering.
5. **§16.3** (filter stride) — only affects H32 mode; fix after the screen
   shows *something*.
6. **§16.4** (GetLineBuffer range) — minor rendering artifact; fix after
   the screen is visible.

### Diagnostic checklist for the black screen

- [ ] Check debug log: does `displayEnable` ever transition from 0→1?
      (Search for "VDP::Reg1 CHANGED displayEnable".) If never, the M68K
      isn't executing the game's VDP init code.
- [ ] Check debug log: does `SR.i` ever drop below 7? (Search for "VBlank
      IRQ BLOCKED".) If it stays at 7, the M68K never lowers the interrupt
      mask.
- [ ] Check debug log: does the M68K PC reach the game's main loop?
      (Search for "M68K MAINLOOP".) If not, the M68K is stuck in init.
- [ ] Check debug log: does `firstNonBlack` ever become ≥ 0 in the RunFrame
      debug output? If always -1, no pixels are being written.
- [ ] Check debug log: what is `bgColor` / `CRAM[0]`? If both 0, the game
      hasn't written to CRAM yet (VDP init hasn't run).
- [ ] Verify the M68K reset vector: `M68K::Power` log should show PC pointing
      to a valid ROM address (typically 0x000000–0x0000FF for the boot
      vector, jumping to the game's entry point).

### 17. Post-§16 Runtime Fixes (blue-screen → working itest ROM)

These fixes were applied after the §16 items. They were discovered by running
Charles MacDonald's `itest` ROM (illegal instruction test), which displays
blue while testing, green on pass, red on fail.

#### 17.1 SV vs SVI: Array elements in Serialize produce duplicate keys [FIXED]

**File:** `Core/Genesis/GenesisM68K.cpp` (Serialize), `GenesisConsole.cpp`,
`GenesisControlManager.cpp`, `GenesisVdp.cpp`, `GenesisYm2612.h/cpp`

**Symptom:** After `Power()` correctly initializes all registers, the
framework's initial save-then-load cycle (`RewindManager::InitHistory`) corrupts
every M68K register. D0–D7 become 0x0000FFFF, A0–A7 become 0xFFFFFFFF, PC
becomes 0x04DC (the `hang` loop) instead of 0x0404 (the reset entry point).
The CPU never executes the ROM's reset code.

**Root cause:** The `SV()` macro passes `index=-1` to `Serializer::Stream()`.
When a loop uses `SV(_r.d[i])`, the macro stringifies the expression literally
as `"_r.d[i]"` (with the letter `i`, not the index number). All 8 iterations
produce the **same key**: `"GenesisM68K._r.d[i]"`. On the save pass, each
iteration overwrites the previous value. On the load pass, only the last-saved
value is found, so every register gets the same (wrong) value.

The `SVI()` macro passes the loop variable `i` as the index parameter:
```cpp
#define SV(var)  (s.Stream(var, #var))       // index = -1
#define SVI(var) (s.Stream(var, #var, i))    // index = loop variable i
```
With `SVI`, `NormalizeName()` replaces `[i]` with `[0]`, `[1]`, ... `[7]`,
producing unique keys for each element.

**Fix:** Replace all `SV(arr[i])` in loops with `SVI(arr[i])`. Also convert
range-for loops (`for(auto& x : arr) { SV(x.field); }`) to indexed for loops
(`for(int i=0; i<N; i++) { SVI(arr[i].field); }`). Affected files:

| File | What changed |
|------|-------------|
| `GenesisM68K.cpp` Serialize | `SV(_r.d[i])` → `SVI(_r.d[i])`, same for `_r.a[i]` |
| `GenesisConsole.cpp` Serialize | `SV(_romBank[i])` → `SVI(_romBank[i])` |
| `GenesisControlManager.cpp` Serialize | `SV(_ports[i].*)` → `SVI(_ports[i].*)` |
| `GenesisVdp.cpp` Serialize | Range-for loops converted to indexed for+SVI for FIFO slots, layer mappings, sprite cache/mappings/visible |
| `GenesisYm2612.h` | `Channel` changed to inherit `ISerializable`, `SerializeOp` → `Serialize` override |
| `GenesisYm2612.cpp` | `SV(op.*)` → `SVI(operators[i].*)`, `_channels[n].SerializeOp(s)` → `SVI(_channels[i])` |

**Key rule:** Always use `SVI()` (not `SV()`) inside indexed for-loops over
arrays. Use `SVArray()` for raw arrays. Range-for loops (`for(auto& x : ...)`)
with `SV(x.field)` generate duplicate keys and must be converted.

#### 17.2 MOVE.B to An treated as valid instruction [FIXED]

**File:** `Core/Genesis/GenesisM68K.cpp` BuildInstructionTable(), MOVE loop

**Symptom:** The itest ROM's illegal instruction test loops through 11,529
opcodes. After ~1,159 iterations (opcode `$1068`), the test stalls — D7 stops
decrementing and the CPU enters the `hang` loop (PC=0x04DC, opcode=0x4EF9/JMP).
The screen stays blue indefinitely.

**Root cause:** The MOVE instruction table binds `MOVE<Byte>` for ALL effective
address modes, including `toMode==1` (address register direct as destination).
On a real 68000, `MOVE.B ..., An` is illegal — there is no `MOVEA.B`
instruction. Only `MOVEA.W` and `MOVEA.L` are valid.

The existing code had a fix for `fromMode==1` (MOVE.B *from* An) but missed
`toMode==1` (MOVE.B *to* An):
```cpp
// Already present (correct):
if(fromMode == 1) { _instructionTable[opcode | 1 << 12] = instructionILLEGAL; }
// Missing (BUG):
if(toMode == 1)   { _instructionTable[opcode | 1 << 12] = instructionILLEGAL; }
```

When opcode `$1068` (MOVE.B with An destination) was executed as a valid
instruction, it overwrote address register A0 with a byte value. The test ROM's
`jsr (a3)` returned normally (no exception), then `jmp error` executed. The
error handler tried to write the red color via `move.w #$000E, (a0)`, but A0
had been corrupted by the MOVE.B, so the VDP write went to the wrong address
and the screen stayed blue instead of turning red.

**Fix:** Add `if(toMode == 1) { _instructionTable[opcode | 1 << 12] = instructionILLEGAL; }`
to the MOVE table building loop, making MOVE.B to An trigger an illegal
instruction exception, matching real 68000 behavior.

**Note:** All other byte+An combinations in the instruction table are already
correctly handled — ADD, SUB, CMP have explicit `if(mode == 1) instructionILLEGAL`
overrides for byte size, and ORI, ANDI, CLR, TST, etc. use `if(mode == 1) continue`
to skip the binding entirely.
