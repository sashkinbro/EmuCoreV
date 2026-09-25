// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: MIT

package com.sbro.emucorev.discord

/** Plain Bundle protocol between the GPL app and the isolated proprietary-SDK helper process. */
internal object DiscordIpc {
    const val MSG_START = 1
    const val MSG_AUTHORIZE = 2
    const val MSG_SET_PRESENCE = 3
    const val MSG_QUERY = 4
    const val MSG_STOP = 5
    const val MSG_STATE = 100

    const val DATA_TOKEN = "token"
    const val DATA_DETAILS = "details"
    const val DATA_STATE = "state"
    const val DATA_COVER = "cover"
    const val DATA_STATUS = "status"
    const val DATA_SELF = "self"
    const val DATA_FRIENDS = "friends"
    const val DATA_ERROR = "error"
    const val DATA_FRESH_TOKEN = "freshToken"
    const val DATA_AVAILABLE = "available"

    const val FIELD_SEPARATOR = '\u001f'
    const val RECORD_SEPARATOR = '\u001e'
}
