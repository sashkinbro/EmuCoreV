package com.sbro.emucorev.core

object FrameLimit {
    const val UNLIMITED = 0
    val supportedValues: List<Int> = listOf(UNLIMITED, 30, 45, 60)

    fun normalize(value: Int): Int = value.takeIf(supportedValues::contains) ?: UNLIMITED
}
