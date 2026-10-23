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
void settings_output_size(unsigned *w, unsigned *h);   /* the display resolution last asked for */
int settings_render_size(int scale, unsigned *w, unsigned *h);   /* the render resolution a 3D render scale gives now; 1 = capped */
const char *settings_display_status(void);      /* "Output 2560 x 1440, 3D scene 1280 x 720": what the game was last asked for */
void settings_aspect_follow(float ratio);   /* key 4 changed the shape (-1 fit, 0 16:9, a ratio): display.aspect follows, not applied again */
void settings_note_controller(const char *kind); /* "xbox" / "playstation" / "nintendo" / "other": the last controller connected */
/* 1 while the iOS app holds the frame rate at 30 (hot device or Low Power Mode, ios/Sources/power.m): a new frame rate
   limit is stored as usual and applies when the hold ends. */
extern volatile int settings_fps_held;
