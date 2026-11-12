# VStorm — product notes (things to do when we make the product)

Collected decisions and to-dos that only matter for the shipping product, not the prototype.

---

## Skins from the SD card (secure skin packs)

**Goal:** besides the built-in skins, the device can load extra skins from the SD card. The
files must not be editable or forgeable by users, and paid skins should not be copyable to
another unit.

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
5. Optionally copy the verified pack into the internal skins partition so the card can be
   removed. The settings page remembers the choice in (encrypted) NVS.

### Tools (PC side)

- `skin-pack` script: PNG layers + layout file → compress → encrypt → sign → `.vskin`.
- **Private signing key stays offline** (not in the repo, not on a build server). Losing it
  means no new skins; leaking it means anyone can sign skins. Keep a backup in a safe place.
- For device-bound packs, the store/registration site wraps the content key for the device
  ID the user typed in at registration (matches the air-gapped store idea in HANDOFF).

### Production setup checklist

- [ ] Flash size 16 MB in sdkconfig (firmware3 currently says 4 MB); partitions: factory app
      (or two OTA slots), NVS (encrypted), skins partition ~8 MB.
- [ ] SD card via SDMMC (Waveshare `04_SD_MMC` example has this board's pins).
- [ ] Secure Boot v2 + Flash Encryption (release mode) + NVS encryption.
- [ ] Per-device HMAC key burned into eFuse at the factory; record the device ID.
- [ ] **eFuse burns are permanent.** Prove the whole flow on a couple of sacrificial dev
      boards first, and keep unburned boards for development.
- [ ] Public signing key compiled into the firmware (protected by secure boot).
- [ ] Skin packer + signing done on an offline machine.

### Related

- Skin concepts: `design/skins/concepts/` (20 designs) and the default skin `design/skins/default/`.
- Size check: 21 skins ≈ 2–6 MB compressed vs ~41 MB raw; fits in 16 MB flash with room for
  SD-loaded and future downloaded skins.
- IP: no franchise names/logos/crest in shipped skins; Aurebesh only for free/personal packs
  (see `docs/UI-DESIGN.md` section 8 and HANDOFF).
