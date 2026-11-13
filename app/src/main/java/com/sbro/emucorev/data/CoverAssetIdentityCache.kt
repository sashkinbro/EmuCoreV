package com.sbro.emucorev.data

/** Local identities are valid only for the exact bundled catalog bytes that produced them. */
internal class CoverAssetIdentityCache(
    private val fingerprint: String,
    private val read: (String) -> String?,
    private val write: (String, String) -> Unit
) {
    fun find(titleId: String, style: VitaCoverStyle): VitaCoverAsset? = runCatching {
        val serial = VitaCoverIndex.normalizeTitleId(titleId) ?: return null
        val fields = read(key(serial, style))?.split('|') ?: return null
        if (fields.size != 5 || fields[0] != fingerprint || fields[1] != serial) return null
        val id = fields[3].toLongOrNull() ?: return null
        VitaCoverAsset(id, style, fields[2], "covers/${if (style == VitaCoverStyle.THREE_D) "3d/" else ""}$id.webp", fields[4])
    }.getOrNull()

    fun remember(titleId: String, asset: VitaCoverAsset) {
        val serial = VitaCoverIndex.normalizeTitleId(titleId) ?: return
        val value = listOf(fingerprint, serial, asset.revision, asset.igdbId, asset.sha256).joinToString("|")
        val key = key(serial, asset.style)
        if (read(key) != value) write(key, value)
    }

    fun isKnownMissing(titleId: String, style: VitaCoverStyle): Boolean {
        val serial = VitaCoverIndex.normalizeTitleId(titleId) ?: return false
        return read(key(serial, style)) == "$fingerprint|$serial|missing"
    }

    fun rememberMissing(titleId: String, style: VitaCoverStyle) {
        val serial = VitaCoverIndex.normalizeTitleId(titleId) ?: return
        val value = "$fingerprint|$serial|missing"
        val key = key(serial, style)
        if (read(key) != value) write(key, value)
    }

    private fun key(serial: String, style: VitaCoverStyle) = "asset:$serial:${style.name}"
}
