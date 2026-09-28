#pragma once

// Resource ids for the manager's embedded fonts and theme art. Shared between
// ManagerArt.rc and the code that looks them up, so the two cannot drift.
//
// Embedded rather than shipped beside the executable: a theme that silently
// falls back to Segoe UI because a file went missing from the zip is a bug
// report nobody can reproduce.

#define IDR_FONT_CINZEL           201
#define IDR_FONT_ALEGREYA_SANS    202
#define IDR_FONT_IM_FELL_SC       203
#define IDR_FONT_ALEGREYA         204
#define IDR_FONT_MARCELLUS_SC     205
#define IDR_FONT_INTER            206

#define IDR_ART_STONE             301
#define IDR_ART_PARCHMENT         302
#define IDR_ART_SLATE             303
#define IDR_ART_EMBLEM            304
