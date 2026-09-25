// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: MIT

package com.sbro.emucorev.discord

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.util.Log

class DiscordAuthActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!DiscordNative.load()) {
            finish()
            return
        }

        runCatching {
            Class.forName("com.discord.socialsdk.DiscordSocialSdkInit")
                .getMethod("setEngineActivity", Activity::class.java)
                .invoke(null, this)
            DiscordNative.start(intent.getStringExtra(EXTRA_TOKEN).orEmpty())
            if (intent.getBooleanExtra(EXTRA_AUTHORIZE, false)) {
                DiscordNative.authorize()
            }
        }.onFailure { error ->
            Log.w(TAG, "Unable to initialize Discord", error)
        }
        finish()
        @Suppress("DEPRECATION")
        overridePendingTransition(0, 0)
    }

    companion object {
        private const val TAG = "EmuCoreVDiscord"
        private const val EXTRA_TOKEN = "token"
        private const val EXTRA_AUTHORIZE = "authorize"

        internal fun intent(context: Context, token: String = "", authorize: Boolean = false): Intent =
            Intent(context, DiscordAuthActivity::class.java)
                .putExtra(EXTRA_TOKEN, token)
                .putExtra(EXTRA_AUTHORIZE, authorize)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_ANIMATION)
    }
}
