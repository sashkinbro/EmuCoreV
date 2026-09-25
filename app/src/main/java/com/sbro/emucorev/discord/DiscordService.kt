// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: MIT

package com.sbro.emucorev.discord

import android.app.Service
import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.util.Log

/** Bound-only helper service. Destroying the final binding unloads the Social SDK process. */
class DiscordService : Service() {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var messenger: Messenger
    private var loaded = false
    private var started = false

    private val pump = object : Runnable {
        override fun run() {
            if (loaded && started) {
                runCatching(DiscordNative::pump)
                    .onFailure { Log.w(TAG, "Discord callback pump failed", it) }
            }
            handler.postDelayed(this, PUMP_INTERVAL_MS)
        }
    }

    override fun onCreate() {
        super.onCreate()
        loaded = DiscordNative.load() && runCatching(DiscordNative::available).getOrDefault(false)
        Log.i(TAG, "Helper process ready (SDK loaded=$loaded)")
        messenger = Messenger(Handler(Looper.getMainLooper()) { message ->
            handleMessage(message)
            true
        })
        handler.post(pump)
    }

    override fun onBind(intent: Intent?): IBinder = messenger.binder

    override fun onDestroy() {
        handler.removeCallbacks(pump)
        if (loaded && started) runCatching(DiscordNative::stop)
        super.onDestroy()
    }

    private fun handleMessage(message: Message) {
        when (message.what) {
            DiscordIpc.MSG_START -> {
                if (!loaded) return
                val token = message.data?.getString(DiscordIpc.DATA_TOKEN).orEmpty()
                Log.i(TAG, "Starting Discord session (saved token=${token.isNotEmpty()})")
                runCatching {
                    if (token.isNotEmpty()) startActivity(DiscordAuthActivity.intent(this, token))
                    started = true
                }.onFailure { Log.w(TAG, "Discord start failed", it) }
            }

            DiscordIpc.MSG_AUTHORIZE -> {
                if (!loaded) return
                runCatching {
                    startActivity(DiscordAuthActivity.intent(this, authorize = true))
                    started = true
                }.onFailure { Log.w(TAG, "Discord authorization activity failed", it) }
            }

            DiscordIpc.MSG_SET_PRESENCE -> {
                if (!loaded || !started) return
                val data = message.data ?: Bundle.EMPTY
                runCatching {
                    DiscordNative.setPresence(
                        data.getString(DiscordIpc.DATA_DETAILS).orEmpty(),
                        data.getString(DiscordIpc.DATA_STATE).orEmpty(),
                        data.getString(DiscordIpc.DATA_COVER).orEmpty()
                    )
                }.onFailure { Log.w(TAG, "Discord presence update failed", it) }
            }

            DiscordIpc.MSG_QUERY -> reply(message.replyTo)

            DiscordIpc.MSG_STOP -> {
                if (loaded && started) runCatching(DiscordNative::stop)
                started = false
            }
        }
    }

    private fun reply(target: Messenger?) {
        target ?: return
        val data = Bundle().apply {
            putBoolean(DiscordIpc.DATA_AVAILABLE, loaded)
            if (loaded && started) {
                putInt(DiscordIpc.DATA_STATUS, runCatching(DiscordNative::status).getOrDefault(0))
                putString(DiscordIpc.DATA_SELF, runCatching(DiscordNative::self).getOrDefault(""))
                putString(DiscordIpc.DATA_FRIENDS, runCatching(DiscordNative::friends).getOrDefault(""))
                putString(DiscordIpc.DATA_ERROR, runCatching(DiscordNative::error).getOrNull())
                putString(DiscordIpc.DATA_FRESH_TOKEN, runCatching(DiscordNative::takeToken).getOrNull())
            } else if (loaded) {
                putInt(DiscordIpc.DATA_STATUS, 1)
            }
        }
        runCatching {
            target.send(Message.obtain(null, DiscordIpc.MSG_STATE).apply { this.data = data })
        }.onFailure { Log.w(TAG, "Unable to return Discord state", it) }
    }

    private companion object {
        const val TAG = "EmuCoreVDiscord"
        const val PUMP_INTERVAL_MS = 50L
    }
}
