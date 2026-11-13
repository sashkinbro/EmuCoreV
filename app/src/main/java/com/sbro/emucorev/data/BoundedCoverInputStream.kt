package com.sbro.emucorev.data

import java.io.FilterInputStream
import java.io.InputStream

/** Applies the cover byte budget to content providers as well as ordinary files. */
internal class BoundedCoverInputStream(
    input: InputStream,
    private val maxBytes: Long = CoverDiskCache.MAX_BYTES
) : FilterInputStream(input) {
    init { require(maxBytes > 0) }
    private var bytes = 0L
    override fun read(): Int = `in`.read().also { if (it >= 0) account(1) }
    override fun read(buffer: ByteArray, offset: Int, length: Int): Int =
        `in`.read(buffer, offset, length).also { if (it > 0) account(it.toLong()) }
    override fun skip(count: Long): Long = `in`.skip(count).also(::account)
    private fun account(count: Long) {
        require(count <= maxBytes - bytes) { "Artwork exceeds byte budget" }
        bytes += count
    }
}
