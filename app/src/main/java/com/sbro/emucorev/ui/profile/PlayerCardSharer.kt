package com.sbro.emucorev.ui.profile

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import androidx.core.content.FileProvider
import androidx.core.graphics.createBitmap
import com.sbro.emucorev.R
import com.sbro.emucorev.data.PlayerProfile
import com.sbro.emucorev.data.PlayerRankInsights
import java.io.File
import java.io.FileOutputStream
import kotlin.math.roundToInt

object PlayerCardSharer {
    private const val METRIC_BASELINE_Y = 405f

    fun share(context: Context, profile: PlayerProfile, rank: PlayerRankInsights?) {
        val outputDirectory = File(context.cacheDir, "shared").apply { mkdirs() }
        val outputFile = File(outputDirectory, "emucorev-player-card.png")
        FileOutputStream(outputFile).use { stream ->
            createCard(context, profile, rank).compress(Bitmap.CompressFormat.PNG, 100, stream)
        }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", outputFile)
        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "image/png"
            putExtra(Intent.EXTRA_STREAM, uri)
            putExtra(Intent.EXTRA_TEXT, context.getString(R.string.profile_player_card_share_text, profile.playerTag))
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        context.startActivity(
            Intent.createChooser(shareIntent, context.getString(R.string.profile_player_card_share))
        )
    }

    private fun createCard(
        context: Context,
        profile: PlayerProfile,
        rank: PlayerRankInsights?
    ): Bitmap {
        val width = 1200
        val height = 630
        val bitmap = createBitmap(width, height)
        val canvas = Canvas(bitmap)
        val accent = accentColor(profile.profileAccent)
        val background = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            shader = LinearGradient(
                0f,
                0f,
                width.toFloat(),
                height.toFloat(),
                Color.rgb(8, 10, 17),
                blend(accent, Color.BLACK, 0.72f),
                Shader.TileMode.CLAMP
            )
        }
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), background)

        val panel = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.argb(210, 18, 22, 34) }
        canvas.drawRoundRect(RectF(60f, 58f, 1140f, 572f), 44f, 44f, panel)
        Paint(Paint.ANTI_ALIAS_FLAG).also { paint ->
            paint.color = accent
            canvas.drawRoundRect(RectF(60f, 58f, 78f, 572f), 10f, 10f, paint)
            canvas.drawCircle(190f, 210f, 86f, paint)
        }

        val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            typeface = android.graphics.Typeface.create("sans-serif", android.graphics.Typeface.BOLD)
        }
        textPaint.textSize = 84f
        val initial = profile.displayName.trim().firstOrNull()?.uppercaseChar()?.toString() ?: "E"
        canvas.drawText(initial, 160f, 240f, textPaint)

        textPaint.textSize = 33f
        textPaint.color = accent
        canvas.drawText(context.getString(R.string.profile_player_card_brand), 310f, 135f, textPaint)
        textPaint.textSize = 66f
        textPaint.color = Color.WHITE
        canvas.drawText(profile.displayName.take(24), 310f, 225f, textPaint)
        textPaint.textSize = 30f
        textPaint.color = Color.rgb(184, 192, 211)
        canvas.drawText(profile.playerTag, 310f, 275f, textPaint)

        drawMetric(canvas, context.getString(R.string.profile_total_time), formatDuration(profile.totalPlayTimeMs), 130f, accent)
        drawMetric(canvas, context.getString(R.string.profile_games_played), profile.gamesPlayed.toString(), 470f, accent)
        drawMetric(canvas, context.getString(R.string.profile_rank), rank?.let { "#${it.rank}" } ?: "—", 810f, accent)

        textPaint.textSize = 25f
        textPaint.color = Color.rgb(145, 154, 175)
        canvas.drawText(context.getString(R.string.profile_player_card_footer), 130f, 530f, textPaint)
        return bitmap
    }

    private fun drawMetric(canvas: Canvas, label: String, value: String, x: Float, accent: Int) {
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = android.graphics.Typeface.create("sans-serif", android.graphics.Typeface.BOLD)
            textSize = 44f
            color = Color.WHITE
        }
        canvas.drawText(value, x, METRIC_BASELINE_Y, paint)
        paint.typeface = android.graphics.Typeface.create("sans-serif", android.graphics.Typeface.NORMAL)
        paint.textSize = 24f
        paint.color = blend(accent, Color.WHITE, 0.45f)
        canvas.drawText(label, x, METRIC_BASELINE_Y + 38f, paint)
    }

    private fun formatDuration(durationMs: Long): String {
        val totalMinutes = durationMs.coerceAtLeast(0L) / 60_000L
        val hours = totalMinutes / 60L
        val minutes = totalMinutes % 60L
        return if (hours > 0L) "${hours}h ${minutes}m" else "${minutes}m"
    }

    private fun accentColor(accent: String): Int = when (accent) {
        "crimson" -> Color.rgb(220, 65, 79)
        "blue" -> Color.rgb(77, 158, 255)
        "violet" -> Color.rgb(157, 109, 255)
        "emerald" -> Color.rgb(54, 196, 142)
        else -> Color.rgb(216, 180, 95)
    }

    private fun blend(first: Int, second: Int, secondWeight: Float): Int {
        val weight = secondWeight.coerceIn(0f, 1f)
        return Color.rgb(
            (Color.red(first) * (1f - weight) + Color.red(second) * weight).roundToInt(),
            (Color.green(first) * (1f - weight) + Color.green(second) * weight).roundToInt(),
            (Color.blue(first) * (1f - weight) + Color.blue(second) * weight).roundToInt()
        )
    }
}
