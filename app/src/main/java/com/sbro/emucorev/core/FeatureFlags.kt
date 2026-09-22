package com.sbro.emucorev.core

/**
 * Cheat support is fully implemented in the core and the UI (parser, pointer
 * interpreter, manager, in-game tab, online catalog), but the public cheat
 * databases only match very specific game dumps: most packs silently do
 * nothing and a wrong one can corrupt a running game. Until upstream support
 * or a verified pack set makes it dependable, the cheat manager and the
 * in-game Cheats tab stay hidden from users.
 *
 * The whole feature is kept in the code base behind this switch: set it to
 * true to show the drawer entry, the in-game tab and the settings toggle
 * again. Nothing else needs to change.
 */
const val CHEATS_ENABLED = false
