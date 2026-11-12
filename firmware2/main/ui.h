#pragma once
void ui_init(void);
/* BOOT button hooks (called from main's button callbacks, any task) */
void ui_request_lock(void);
void ui_request_factory_reset(void);
void ui_request_mute_toggle(void);
