package com.sbro.emucorev.ui.emulation

/** Keeps shared actions held while any standard, duplicate or combo button owns them. */
internal class TouchActionTracker(private val emit: (Int, Boolean) -> Unit) {
    private val owners = mutableMapOf<String, Set<Int>>()

    fun press(owner: String, actions: Set<Int>) {
        val previous = owners[owner].orEmpty()
        if (previous == actions) return
        owners.remove(owner)
        val heldByOthers = owners.values.flatten().toSet()
        (previous - actions - heldByOthers).forEach { emit(it, false) }
        if (actions.isNotEmpty()) owners[owner] = actions.toSet()
        (actions - previous - heldByOthers).forEach { emit(it, true) }
    }

    fun release(owner: String) = press(owner, emptySet())

    fun cancel() {
        val held = owners.values.flatten().toSet()
        owners.clear()
        held.forEach { emit(it, false) }
    }
}
