#pragma once
/* Fonts the firmware embeds with EMBED_FILES; build.sh turns them into C arrays. */
#include <stdint.h>
extern const uint8_t aurebesh_ttf_start[];
extern const unsigned aurebesh_ttf_len;
extern const uint8_t aureng_ttf_start[];
extern const unsigned aureng_ttf_len;
#define aurebesh_ttf_end (aurebesh_ttf_start + aurebesh_ttf_len)
#define aureng_ttf_end   (aureng_ttf_start + aureng_ttf_len)
