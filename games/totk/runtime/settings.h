/* The player's settings at run time (runtime/settings.c); the table and file format are in settings_schema.h. */
#pragma once
#include "settings_schema.h"

enum { SETTINGS_APPLIED = 0, SETTINGS_RESTART = 1, SETTINGS_INVALID = -1 };

void settings_init(const char *file);           /* once; NULL or "" keeps defaults (and the environment) without a file */
int settings_ios(void);                         /* 1 on iPhone and iPad: picks the iOS defaults */
int settings_get(const char *key);              /* -1 for an unknown key */
int settings_set(const char *key, int value);   /* SETTINGS_APPLIED (live), SETTINGS_RESTART (next start), SETTINGS_INVALID */
int settings_pending_restart(const char *key);  /* 1: changed since the game started and only applies at the next start */
int settings_save(void);
void settings_observe(void (*fn)(const char *key, int value));   /* called after every change, on the caller's thread */
/* The window's drawable and the screen's native width in pixels (the Mac frontend, on resize): "Match the window", "Display
   native" and "Fill the screen" follow them. */
void settings_display_size(unsigned drawable_w, unsigned drawable_h, unsigned screen_w);
const char *settings_display_status(void);      /* "Output 2560 x 1440, 3D scene 1280 x 720": what the game was last asked for */
void settings_note_controller(const char *kind); /* "xbox" / "playstation" / "nintendo" / "other": the last controller connected */
