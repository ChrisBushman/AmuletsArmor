# Endianness audit: on-disk / on-wire structs

**Status: audit complete, fixes implemented and compile-tested.** PowerPC
(G3/G4) is big-endian; every existing build of this game (DOS, Windows, and
the current Intel/ARM macOS port) is little-endian. All of the data below is
read/written as raw memory dumps via `FileRead`/`FileWrite` (`Source/FILE.C`,
a thin wrapper over POSIX `open()`/`read()`/`write()`), plain `fread`/
`fwrite`, or raw socket sends. A shared header, `Include/ENDIAN_AA.H`, adds
`EndianSwap16/32`, `EndianSwapS16/32`, and the `EndianLE16/32/S16/S32` macros
that are no-ops on little-endian builds and real swaps on big-endian ones —
see `tests/test_endian.c` for round-trip/known-value coverage of the
primitives themselves.

**What "fixed" means here**: every struct below now has byte-swap calls
wired in at the correct chokepoint. None of this has run on real big-endian
hardware yet — that's the next real test, not something achievable in this
environment. Everything has been verified to still compile and run correctly
on the existing little-endian Intel/ARM build (where every swap is a
provable no-op), plus a full clean rebuild + smoke test.

## Systemic pattern already in the code (used as the fix chokepoint)

Four subsystems already had a `TARGET_UNIX`-only "disk shadow struct" that
copies each field individually from a 32-bit on-disk layout into a
pointer-width-correct native struct (for 64-bit-host safety). These were the
natural place to add the byte-swap calls:

| Subsystem | Chokepoint | Status |
|---|---|---|
| Resource index | `RESOURCE.C: ILoadResourceIndexUnix` + `ResourceOpen` | Fixed |
| Object types | `OBJTYPE.C: IObjTypeExpandForUnix` | Fixed |
| Map animation | `MAPANIM.C: MapAnimateLoad` | Fixed |
| Scripts | `SCRIPT.C: IScriptLoad` | Fixed |

## Full struct inventory, by subsystem

### 1. Resource container format — `Include/IRESOURC.H` — **Fixed**
`T_resourceFileHeader` (`indexOffset`/`indexSize`/`numEntries`) and
`T_resourceEntryDisk32` (`fileOffset`/`size`/`lockCount`) swapped in
`RESOURCE.C`. `uniqueID` is a raw 4-char tag compared via the same
reinterpret-cast idiom on both sides — self-consistent, deliberately **not**
swapped (see "raw-tag fields" note below).

### 2. Map/BSP geometry format — `Include/VIEWFILE.H`, `Source/3D_IO.C` — **Fixed**
`T_wadHeader`, `T_directoryEntry`, `T_3dVertex`, `T_3dLine`, `T_3dSide`,
`T_3dSector`, `T_3dSegment`, `T_3dSegmentSector`, `T_3dNode`,
`T_3dObjectInFile`, `T_3dBlockMap` — per-element swap helpers
(`ISwap3dSegs`, `ISwap3dSides`, etc.) called right after each `FileRead`.
`T_3dReject` is raw bitfield bytes, no swap.

### 3. Object type table — internal to `Source/OBJTYPE.C` — **Fixed**
Header fields, per-stance fields, per-frame fields (`soundNum`/
`soundRadius`/`objectAttributes`), and per-pic `number` all swapped in
`IObjTypeExpandForUnix`. The disk-side reads (`p_typeDisk`, a live,
possibly-shared resource-cache buffer) are read through `EndianLE16`-wrapped
accessors *without* mutating that buffer in place — only the newly allocated
expanded copy (`p_copy`) is swapped, so re-locking an already-cached
resource can't double-swap it.

### 4. Map animation state table — internal to `Source/MAPANIM.C` — **Fixed**
`T_mapAnimWallState`, `T_mapAnimSideState`, `T_mapAnimSectorState`,
`T_mapAnimInitState`, and the `T_mapAnimStatesDisk32` header all swapped
once per load, after the raw data is copied into the native-layout buffer.

### 5. Script bytecode — internal to `Source/SCRIPT.C` — **Fixed**
- Header (`highestEvent`/`highestPlace`/`sizeCode`/`reserved[6]`/`number`)
  and the events/places `T_word16` arrays swapped once at load.
- **Bytecode-embedded immediates** (the part flagged as "needs a follow-up
  look" in the original audit — now resolved): the VM format embeds raw
  `T_word16`/`T_sword16`/`T_sword32` operands at opcode-dependent byte
  offsets, so there's no single array to bulk-swap. Fixed at the read
  chokepoints instead: the `ScriptGetCodeWord` macro (used by variable
  lookups) and the 8/16/32-bit immediate cases in `IScriptGetAny`.

### 6. World/persistent record store — internal to `Source/VM.C` — **Skipped: dead code**
The file's own header comment says "NOT USED!" and there are zero callers of
its public API anywhere else in the codebase. Confirmed via grep, not fixed
— no value in swapping data nothing reads.

### 7. Precomputed trig tables — `Source/3D_TRIG.C` — **Fixed**
Not actually a cache with a runtime-regeneration fallback, as first
assumed: `MathInitialize` (the live startup path) unconditionally loads a
single file, `mdat.res`, and calls `exit(1)` if it's missing.
`MathSine`/`MathCosine`/`MathTangent`/`MathInvCosine` are all stubbed to
`return 0` in this codebase (their real bodies are commented out, and only
the dead `MathInitializeOld` still has working math) — so there is no
"regenerate instead" option available; swapping is the only path. Swapped
in place right after the `FileRead` sequence in `MathInitialize`:
`G_arcTanTable[256][256]` (`T_word16`), `G_cosTable`/`G_invCosTable`/
`G_sinTable`/`G_tanTable[1024]` (`T_sword32`). `P_shadeIndex[16384]` and
`G_translucentTable[256][256]` are byte tables, no swap.

### 8. Character/player save file — `Include/STATS.H`, `Source/STATS.C` — **Fixed**
`T_playerStats` (including nested `T_pastPlaces`/`T_pastPlace`) swapped via
`ISwapPlayerStats`, an involution (swap-and-swap-again restores the
original): called once after `fread` on load, and around the `fwrite` on
save (swap → write → swap back) rather than allocating a scratch copy of
the large struct. `T_statsSavedCharacterID`'s peeked `name`/`password`/
status/mail fields are bytes only, no swap needed.

### 9. Inventory records appended to the save file — **Fixed**
`T_inventoryItemStruct` (`locx`/`locy`/`picwidth`/`picheight`/`objecttype`)
and nested `T_equipItemDescription` (`effectData[8][3]`/`useable`) swapped
via `ISwapInventoryItem` in `INVENTOR.C`, applied to the write-side scratch
copy (`itemCopy`, never the live item) and right after each read.

### 10. Sound/music sample streams — `Source/SOUND.C` — **Fixed**
Background music (`SoundSetBackgroundMusic`, unconditionally 16-bit PCM per
its own `is16Bit = TRUE`) has its sample buffer swapped in place after
`FileRead`. The SOS-library streaming path (`ISoundStartStreamIO`,
`ICheckForNextMusicUpdate`) is DOS-only dead code under `TARGET_UNIX` (uses
`_SOS_SAMPLE`/`sosDIGI*`/Watcom-only types) — confirmed unreachable, not
fixed.

### 11–13. Networking — **Fixed, with one real bug caught and corrected along the way**
- **Transport framing** (`Include/PACKET.H`): `header.id`/`header.checksum`
  swapped in `PACKETDT.C`'s `PacketSendShort`/`PacketSendLong`/
  `PacketSendAnyLength` (computed fresh every call, safe to swap every
  call) and swapped back in `PacketReceiveData`.
- **Struct layout parity with Windows**: added `PACK` to every packet
  struct in `PACKET.H`/`SYNCPACK.H` that lacked it (`T_retransmitPacket`,
  `T_townUIMessagePacket`, `T_playerIDSelf(Packet)`,
  `T_gameRequestJoinPacket`, `T_gameRespondJoinPacket`, `T_gameStartPacket`,
  `T_ackPacket`, `T_syncronizePacket`) — the Windows VC2013 project builds
  with `StructMemberAlignment=1Byte` project-wide (matching the DOS Watcom
  `/zp1` flag), so *every* wire struct in the real shipped Windows binary is
  fully byte-packed already; these were the ones a GCC build would
  otherwise have padded differently, breaking cross-compiler wire
  compatibility independent of endianness.
- **Application payload dispatch**: `CmdQSwapPacketPayload` in
  `CMDQUEUE.C` swaps the command-specific payload (ACK, PLAYER_ID_SELF,
  GAME_REQUEST/RESPOND_JOIN, GAME_START, SYNC, MESSAGE) exactly once per
  packet instance, called from `ICmdQSendPacket` (outgoing, on the
  retry-queue copy, so retransmits don't re-swap) and
  `CmdQUpdateAllReceives` (incoming, right after `PacketGet`).
- **Bug caught during this audit and fixed**: the per-tick sync packet
  (`ClientSyncUpdate` in `CSYNCPCK.C`) calls `PacketSend()` **directly**,
  bypassing `CmdQSendShortPacket`/`CmdQSendLongPacket`/`ICmdQSendPacket`
  entirely (for latency — sync data isn't queued/acked like other
  commands). That meant `CmdQSwapPacketPayload` never fired for the game's
  hottest packet type. Fixed by exporting `CmdQSwapPacketPayload` and
  adding `ISyncSendPacket()` in `CSYNCPCK.C`, which sends a throwaway
  swapped *copy* at all three of that file's direct `PacketSend` call sites
  (the main per-tick send, the "resend last packet" path, and retransmit
  history replay) — the original `packet`/`G_lastPacket`/packet-history
  entries stay host-native throughout, since they're also used locally for
  self-delivery (`ClientSyncPacketProcess`) and retransmit lookups.
- **Bug caught during this audit and fixed**: `PLAYER_ACTION_ID_SELF`
  packs two raw name-character bytes per `actionData[]` slot via a
  symmetric pointer-cast on both the send side (`ClientSyncSendIdSelf`)
  and receive side (`IFindByName((T_byte8*)p_actionData)`) — not a numeric
  value. Blanket-swapping it as `T_word16` would reverse each character
  pair (verified: this breaks even same-endianness communication, not just
  cross-endian). `CmdQSwapPacketPayload`'s SYNC case now skips the
  `actionData[]` swap specifically when `actionType == PLAYER_ACTION_ID_SELF`;
  every other action type's `actionData[]` is a genuine numeric field per
  `SYNCPACK.H`'s action table and is still swapped normally.
- Checksum is write-only — confirmed via grep that nothing anywhere reads
  `header.checksum` back for verification, so its cross-platform semantics
  don't affect correctness either way.

### 14. Picture/bitmap headers — `Include/GRAPHICS.H`'s `T_bitmap` — **Fixed (found and fixed this session)**
Missed in the original pass. Every picture resource starts with a raw
little-endian `sizex`/`sizey` `T_word16` pair. Fixed via a new
`ResourceIsFreshLoad()` primitive on `RESOURCE.H` (TRUE if the *next*
`ResourceLock()` call will actually read fresh disk bytes, vs. return an
already-cached — and already-fixed-up — block; must be checked *before*
`ResourceLock()`, which flips the resource to "in memory" as a side effect
of loading). `PictureLock`/`PictureLockQuick` in `PICS.C` now swap the
header exactly once, right after a fresh load. A new
`PictureLockDataAsBitmap()` covers the other call sites that cast
`PictureLockData`'s result straight to `T_bitmap*` (`BANNER.C`, `PLAYER.C`,
`TESTME.C`, `SPELLS.C`).

### 15. Spell definitions — `Include/SPELTYPE.H`'s `T_spellStruct` — **Fixed (found and fixed this session)**
`type`/`subtype`/`duration`/`durationmod`/`power`/`powermod`/`cost`/
`costmod`/`sound` swapped via `ISwapSpellStruct` in `SPELLS.C`, gated on
`ResourceIsFreshLoad()` (the resource stays locked for the rest of the game
once a class's spells are loaded, so later re-locks must not re-swap).

### 16. Weapon overlay animation — `Source/OVERLAY.C`'s `T_overlayAnimation` — **Fixed (found and fixed this session)**
Two distinct formats in one file:
- `T_overlayAnimation` (`lengthAnimation` + `animation[3][140].xOffset/
  yOffset`) is a fixed-size dense struct — swapped once at load via
  `ISwapOverlayAnimation`, gated on `ResourceIsFreshLoad()`.
- The per-frame pixel data (`G_images[]`) is a **different, custom
  variable-length RLE stream** (`entry`/`size` `T_word16` values
  interleaved with raw pixel runs, terminated by `0xFFFF`) — can't be
  bulk-swapped since operand positions depend on prior values, same class
  of problem as the script bytecode. Fixed at the three read sites in
  `IDrawLayer` instead (`EndianLE16` wrapped around each
  `*((T_word16*)p_data)` dereference).

### 17. Shared address/ID type — `T_directTalkUniqueAddress` (`DITALK.H`)
Raw 6-byte address, embedded in most packets above. Byte-array-only —
confirmed no swap needed, and confirmed nothing accidentally swaps it as
part of a containing struct.

## Confirmed *not* endian-sensitive (checked, not just assumed)

- **Raw-tag fields**: `RESOURCE_FILE_UNIQUE_ID`, `DOUBLE_LINK_LIST_TAG`,
  `INIFILE_TAG`, `LIGHT_TAG`, `MAP_ANIMATION_TAG`,
  `STATE_MACHINE_INSTANCE_TAG` (and their `_DEAD_TAG` twins) are all the
  same `(*((T_word32*)"xyz!"))` idiom — computed via the identical
  reinterpret-cast on both the writing and reading side, on whatever host
  is running at the time, so they're inherently self-consistent and must
  **not** be swapped (swapping would break the comparison, not fix
  anything).
- **`T_bitfont`** (`BUTTON.C`, via `ResourceLock`) — `fontID`/`height`/
  `widths[256]`/`p_data[]` are all single bytes.
- **`T_palette`** (`VIEW.C`, via `PictureLockData`) — `T_byte8[256][3]`,
  no multi-byte fields.
- **Plain-text resource files** (`.TXT` description files, loaded via
  `PictureLockData` and treated as byte strings) — confirmed in
  `GUILDUI.C`, `LOOK.C`, `CLIENT.C` (×5), `CONTROL.C` (×2), `MESSAGE.C`,
  `INVENTOR.C`, `STATS.C` (×2), `MAP.C` (×2). Byte-copied and
  null-terminated, never interpreted as multi-byte fields.
- **`LIGHT.C`'s light-table resource load** — the `PictureLockData` cast to
  `T_word16*` is inside `#if 0` dead code; the live path generates the
  table at runtime instead (`// Instead, let's generate the .LIT file at
  load time`), entirely in host-native memory, never serialized.
- **`G_colorizeTables`** (`COLORIZE.C`) — declared and used as `T_byte8*`,
  a palette-remap byte table, no multi-byte fields.
- Dev/debug-only paths not part of normal gameplay I/O: `TESTME.C`'s
  `PullOut()` picture-export routine, `3D_VIEW.C`'s commented-out
  `G_floorList` dump, `MEMORY.C`'s debug `fwrite(..., stdout)`.
- `CONFIG.INI`/`.INI`-style config files (`INIFILE.C`, `CONFIG.C`) — text.

## What's genuinely left

1. **Real big-endian testing** — nothing above has run on actual (or
   emulated) PPC hardware. All verification so far is: compiles cleanly,
   behaves identically on the existing little-endian build (every swap
   call is a provable no-op there), and the `EndianSwap16/32/S16/S32`
   primitives have known-value + round-trip unit test coverage
   (`tests/test_endian.c`). The higher-level per-struct swap functions
   themselves are not unit-tested (most are `static` to their `.C` file)
   — first real validation will be loading an actual map/resource file
   under `AA_BIG_ENDIAN=1` or on real hardware.
2. **Cross-platform determinism** beyond byte order — e.g. confirming the
   lockstep multiplayer sim's math (RANDOM.C, fixed-point arithmetic)
   produces bit-identical results on x86 and PPC. Not part of this audit.
