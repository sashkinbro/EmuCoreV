package com.sbro.emucorev.data

/** Outgoing game metadata may use custom web art or an exact identity; never a title guess. */
internal suspend fun shareableGameCoverPath(
    coverArtPath: String?,
    titleId: String?,
    resolveExact: suspend (titleId: String, candidatePath: String?) -> String?
): String? {
    val candidate = coverArtPath?.trim()?.takeIf { it.isNotEmpty() }
    if (candidate != null &&
        (candidate.startsWith("http://") || candidate.startsWith("https://")) &&
        !VitaCoverIndex.isManagedRemoteUrl(candidate)) return candidate
    val serial = titleId?.trim()?.takeIf { it.isNotEmpty() } ?: return null
    return resolveExact(serial, candidate)
}
