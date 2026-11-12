# VStorm — product notes (things to do when we make the product)

Collected decisions and to-dos that only matter for the shipping product, not the prototype.

---

## Skins from the SD card (secure skin packs)

**Goal:** besides the built-in skins, the device can load extra skins from the SD card. The
files must not be editable or forgeable by users, and paid skins should not be copyable to
another unit.

### Storage: skins live on the SD card, not in on-board flash

- **On-board flash holds only:** the firmware, settings (NVS), and **one default skin** as a
  fallback, so the device always boots to a working UI (no card, bad card, or bad file →
  default skin + a short "NO SKIN CARD" / "UNVERIFIED SKIN" notice).
- **Every other skin is a `.vskin` file on the SD card**, which ships in the box preloaded with
  the built-in Imperial skins (35 so far). New skins = copy a file onto the card. No on-board flash is used or
  written for skins.
- **Card speed doesn't matter at runtime.** A skin (≈100–300 KB compressed) is read once,
  when selected or at boot, verified, decrypted and decompressed into PSRAM; all drawing then
  comes from PSRAM. The card isn't touched again, so a cheap slow card is fine, and pulling
  the card while running doesn't crash anything (the loaded skin stays until reboot).
  SDMMC 4-bit on the S3 reads a skin in well under a second even on slow cards.
- **Wear:** skins are only ever read (reads don't wear SD or flash). Nothing is written to
  the card except by the user's PC.
- The signature check doubles as a corruption check for flaky cards.
- Enclosure: the SD slot should be reachable (or the card swappable) for users who buy skins.

### Art quality on the SD card (don't squeeze the art)

Built-in skins had to be small. Skins on the card don't, so author them for quality:
- The compression used so far is **lossless** (RLE): nothing is lost, it only wins on flat
  black areas. On the card we can store art that doesn't compress well - textures, brushed
  metal, gradients, fine panel detail, soft glows - at full quality, or even uncompressed
  RGB565 (330 KB per screen) for the fastest possible load.
- The real quality ceiling is the panel's **16-bit colour** (RGB565). Author art in full
  colour and dither when converting so gradients don't band.
- Never use lossy formats (JPEG-style) for UI art: they smear text and thin lines.
- Larger files are fine: even 6 screens x 330 KB = 2 MB per skin loads in about a second
  from a slow card, once, at skin change.

### What protects what

| Threat | Protection |
|---|---|
| Someone edits a skin file (swaps art, changes layout) | **Signature.** Every pack is signed with our private key (Ed25519). The device holds only the public key and refuses any pack whose signature doesn't match. Editing one byte breaks it. |
| Someone makes their own fake "official" skin | Same signature check: without our private key they can't produce a valid pack. |
| Someone opens the file to rip the artwork | **Encryption.** Pack contents are encrypted (AES-256-GCM); without the key it's noise. |
| Someone copies a paid skin to a friend's device | **Device binding.** The content key is wrapped for one device's unique key, which lives in eFuse and can't be read out. Another unit can't unwrap it. |
| Someone replaces the firmware to skip the checks | **Secure Boot v2** (only our signed firmware runs) + **Flash Encryption** (firmware and its keys can't be read off the chip). |

Signing is what stops tampering. Encryption stops copying and ripping. We want both.

Honest limit: no consumer device is uncrackable with unlimited time and lab equipment. This
setup makes it hard enough that casual tampering and copying aren't worth it, which is the
realistic goal.

### Pack format (`.vskin`, in `/skins/` on the SD card)

```
header   (plain)    magic "VSKN", format version, skin id, display name,
                    min firmware version, flags (free / device-bound),
                    device id hash (bound packs only)
key blob (plain)    content key, wrapped:
                      free packs  -> wrapped with the product key (in encrypted flash)
                      paid packs  -> wrapped with that device's eFuse-derived key
payload (encrypted) AES-256-GCM: layout description (which scope / meter / lamps /
                    rotor / key style, positions, palette) + compressed background
                    images (mostly-black RLE/LZ4, ~100-300 KB per skin)
signature           Ed25519 over header + key blob + payload
```

### On the device

1. Settings page 11 (SKIN) lists built-in skins plus any `.vskin` files on the card.
2. On select: check the signature first (reject → "UNVERIFIED SKIN", nothing loaded).
3. Unwrap the content key (product key, or the device key via the ESP32-S3 HMAC peripheral
   from an eFuse key that software can never read), decrypt, check the GCM tag.
4. Decompress the backgrounds into PSRAM (8 MB; one skin ≈ 2 MB unpacked) and rebuild the UI.
5. The settings page remembers the chosen skin (by skin id) in (encrypted) NVS; at boot it is
   loaded from the card again. If the card or file is missing, the default skin is used.

### Tools (PC side)

- `skin-pack` script: PNG layers + layout file → compress → encrypt → sign → `.vskin`.
- **Private signing key stays offline** (not in the repo, not on a build server). Losing it
  means no new skins; leaking it means anyone can sign skins. Keep a backup in a safe place.
- For device-bound packs, the store/registration site wraps the content key for the device
  ID the user typed in at registration (matches the air-gapped store idea in HANDOFF).

### Production setup checklist

- [ ] Flash size 16 MB in sdkconfig (firmware3 currently says 4 MB); partitions: factory app
      (or two OTA slots), NVS (encrypted). No skins partition: skins live on the SD card;
      only the default skin is built into the firmware.
- [ ] SD card via SDMMC (Waveshare `04_SD_MMC` example has this board's pins).
- [ ] Secure Boot v2 + Flash Encryption (release mode) + NVS encryption.
- [ ] Per-device HMAC key burned into eFuse at the factory; record the device ID.
- [ ] **eFuse burns are permanent.** Prove the whole flow on a couple of sacrificial dev
      boards first, and keep unburned boards for development.
- [ ] Public signing key compiled into the firmware (protected by secure boot).
- [ ] Skin packer + signing done on an offline machine.

### Related

- Skin concepts: `design/skins/concepts/` (20 designs), `design/skins/concepts2/` (15 free-layout designs) and the default skin `design/skins/default/`.
- Size check: the 35 skins are ≈ 2.3 MB of lossless RLE art built in today; 35 skins ≈ 3–10 MB compressed on the card (any card is plenty); one unpacked
  skin ≈ 2 MB of the 8 MB PSRAM.
- IP: no franchise names/logos/crest in shipped skins; Aurebesh only for free/personal packs
  (see `docs/UI-DESIGN.md` section 8 and HANDOFF).
