package com.sbro.emucorev.ui.common

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Insights
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import android.os.SystemClock
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.core.AppAnalytics
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.core.ProPurchaseTier
import com.sbro.emucorev.core.review.AndroidInAppReviewAttemptStore
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import kotlinx.coroutines.delay

/** Timing rules for the recurring Pro reminder dialog. Pure so it can be unit tested. */
object ProReminderPolicy {
    const val REMINDER_INTERVAL_MS = 3L * 60 * 60 * 1000
    const val BACKOFF_INTERVAL_MS = 12L * 60 * 60 * 1000
    const val BACKOFF_AFTER_DISMISSALS = 5
    const val REVIEW_COOLDOWN_MS = 5L * 60 * 1000

    fun intervalMs(dismissStreak: Int): Long =
        if (dismissStreak > 0 && dismissStreak % BACKOFF_AFTER_DISMISSALS == 0) BACKOFF_INTERVAL_MS else REMINDER_INTERVAL_MS

    fun shouldShow(
        nowMs: Long,
        lastShownAtMs: Long,
        dismissStreak: Int,
        isProUnlocked: Boolean,
        canPurchase: Boolean,
        reviewAttemptAtMs: Long
    ): Boolean {
        if (isProUnlocked || !canPurchase) return false
        if (lastShownAtMs == 0L) return false
        if (reviewAttemptAtMs > 0L && nowMs - reviewAttemptAtMs < REVIEW_COOLDOWN_MS) return false
        return nowMs - lastShownAtMs >= intervalMs(dismissStreak)
    }
}

/** Limits impression events to one per placement per [intervalMs]. Pure so it can be unit tested. */
internal class ProPromoImpressionThrottle(private val intervalMs: Long = DEFAULT_INTERVAL_MS) {
    private val lastLoggedAtMs = HashMap<String, Long>()

    @Synchronized
    fun shouldLog(placement: String, nowMs: Long): Boolean {
        val last = lastLoggedAtMs[placement]
        if (last != null && nowMs - last in 0 until intervalMs) return false
        lastLoggedAtMs[placement] = nowMs
        return true
    }

    companion object {
        const val DEFAULT_INTERVAL_MS = 10L * 60 * 1000
        val shared = ProPromoImpressionThrottle()
    }
}

enum class ProPromoVariant(
    val id: String,
    val icon: ImageVector,
    @StringRes val title: Int,
    @StringRes val body: Int,
    /** Optional image shown instead of [icon] (the library card shows the gold Pro app icon). */
    @DrawableRes val image: Int? = null
) {
    LIBRARY("library", Icons.Rounded.Star, R.string.pro_promo_library_title, R.string.pro_promo_library_body, R.drawable.ic_drawer_app_pro),
    THEMES("themes", Icons.Rounded.Palette, R.string.pro_promo_themes_title, R.string.pro_promo_themes_body),
    PROFILE("profile", Icons.Rounded.Insights, R.string.pro_promo_profile_title, R.string.pro_promo_profile_body)
}

private fun Context.findActivity(): Activity? {
    var current: Context? = this
    while (current is ContextWrapper) {
        if (current is Activity) return current
        current = current.baseContext
    }
    return null
}

@Composable
fun ProPromoCard(
    variant: ProPromoVariant,
    modifier: Modifier = Modifier,
    onLearnMore: (() -> Unit)? = null
) {
    val context = LocalContext.current
    val manager = remember(context) { ProPurchaseManager.getInstance(context.applicationContext) }
    val preferences = remember(context) { AppPreferences(context.applicationContext) }
    val state by manager.state.collectAsState()
    val hiddenUntil by remember(preferences, variant) { preferences.proPromoHiddenUntilFlow(variant.id) }
        .collectAsState(initial = Long.MAX_VALUE)

    if (state.isProUnlocked) return
    if (!(state.isProductAvailable || state.isProductLoading)) return
    if (hiddenUntil > System.currentTimeMillis()) return

    LaunchedEffect(variant) {
        if (ProPromoImpressionThrottle.shared.shouldLog(variant.id, SystemClock.elapsedRealtime())) {
            AppAnalytics.logProPromoImpression(variant.id)
        }
    }

    val price = state.productPrice
    val colors = MaterialTheme.colorScheme
    val primary = colors.primary
    val gold = Color(0xFFE9B949)
    val shape = neonShape(20.dp)
    val backgroundBrush = Brush.linearGradient(
        colors = listOf(
            primary.copy(alpha = 0.20f),
            colors.surfaceVariant.copy(alpha = 0.34f),
            colors.surfaceVariant.copy(alpha = 0.22f)
        )
    )
    val borderBrush = Brush.linearGradient(listOf(gold.copy(alpha = 0.60f), primary.copy(alpha = 0.35f)))

    val proPill: @Composable () -> Unit = {
        Row(
            modifier = Modifier
                .height(22.dp)
                .clip(RoundedCornerShape(50))
                .background(Brush.horizontalGradient(listOf(Color(0xFFF6D06B), Color(0xFFD99A2B))))
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = Icons.Rounded.Star,
                contentDescription = null,
                modifier = Modifier.size(12.dp),
                tint = Color(0xFF2A1A00)
            )
            Spacer(Modifier.width(4.dp))
            Text(
                // Brand label; the price rides along so the button can stay short.
                text = if (price != null) "PRO · $price" else "PRO",
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.sp,
                color = Color(0xFF2A1A00),
                maxLines = 1,
                softWrap = false
            )
        }
    }
    val iconBox: @Composable (Dp, Dp) -> Unit = { boxSize, iconSize ->
        Box(
            modifier = Modifier
                .size(boxSize)
                // The gold app icon brings its own round badge, so it gets no backing tile.
                .then(
                    if (variant.image == null) {
                        Modifier.clip(neonShape(12.dp)).background(Brush.linearGradient(listOf(primary.copy(alpha = 0.38f), primary.copy(alpha = 0.10f))))
                    } else {
                        Modifier
                    }
                ),
            contentAlignment = Alignment.Center
        ) {
            val image = variant.image
            if (image != null) {
                // The gold Pro launcher icon: exactly what the purchase turns the app icon into.
                Image(
                    painter = painterResource(image),
                    contentDescription = null,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Fit
                )
            } else {
                Icon(
                    imageVector = variant.icon,
                    contentDescription = null,
                    modifier = Modifier.size(iconSize),
                    tint = primary
                )
            }
        }
    }
    val buyButton: @Composable () -> Unit = {
        Button(
            modifier = Modifier.heightIn(min = 38.dp),
            shape = neonShape(16.dp),
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp),
            enabled = !state.isPurchaseInProgress,
            onClick = {
                AppAnalytics.logProPromoAction(variant.id, "get_pro", preferences.proPromoDismissStreak(variant.id))
                preferences.resetProPromoDismissStreak(variant.id)
                context.findActivity()?.let { manager.purchase(it, ProPurchaseTier.BASE, "promo_" + variant.id) }
            }
        ) {
            Icon(
                imageVector = Icons.Rounded.Star,
                contentDescription = null,
                modifier = Modifier.size(16.dp)
            )
            Spacer(Modifier.width(6.dp))
            Text(
                text = stringResource(R.string.settings_pro_buy),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
    val closeButton: @Composable () -> Unit = {
        IconButton(
            onClick = {
                val streak = preferences.proPromoDismissStreak(variant.id) + 1
                AppAnalytics.logProPromoAction(variant.id, "dismiss", streak)
                preferences.hideProPromo(
                    variant.id,
                    streak,
                    System.currentTimeMillis() + ProReminderPolicy.intervalMs(streak)
                )
            },
            modifier = Modifier.size(28.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.Close,
                contentDescription = stringResource(R.string.pro_promo_hide),
                modifier = Modifier.size(18.dp),
                tint = colors.onSurfaceVariant
            )
        }
    }

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = shape,
        color = Color.Transparent,
        border = BorderStroke(1.dp, borderBrush)
    ) {
        Box(
            modifier = Modifier
                .clip(shape)
                .then(
                    if (onLearnMore != null) {
                        Modifier.clickable(role = Role.Button) {
                            AppAnalytics.logProPromoAction(variant.id, "learn_more", preferences.proPromoDismissStreak(variant.id))
                            onLearnMore()
                        }
                    }
                    else Modifier
                )
                .background(backgroundBrush, shape)
        ) {
            BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
                if (maxWidth >= 640.dp) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        iconBox(44.dp, 24.dp)
                        Spacer(Modifier.width(14.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                proPill()
                                Spacer(Modifier.width(10.dp))
                                Text(
                                    text = stringResource(variant.title),
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.Bold,
                                    color = colors.onSurface,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                            Spacer(Modifier.height(2.dp))
                            Text(
                                text = stringResource(variant.body),
                                style = MaterialTheme.typography.bodyMedium,
                                color = colors.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        Spacer(Modifier.width(16.dp))
                        buyButton()
                        Spacer(Modifier.width(4.dp))
                        closeButton()
                    }
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 14.dp, vertical = 12.dp)
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            iconBox(40.dp, 22.dp)
                            Spacer(Modifier.width(12.dp))
                            Text(
                                text = stringResource(variant.title),
                                modifier = Modifier.weight(1f),
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.Bold,
                                color = colors.onSurface,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                            Spacer(Modifier.width(4.dp))
                            closeButton()
                        }
                        Spacer(Modifier.height(10.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            proPill()
                            Spacer(Modifier.weight(1f))
                            buyButton()
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun ProReminderDialog(
    price: String?,
    isProductLoading: Boolean,
    purchaseInProgress: Boolean,
    onGetPro: () -> Unit,
    onLater: () -> Unit
) {
    AppAlertDialog(
        onDismissRequest = onLater,
        showEyebrow = false,
        showIconContainer = false,
        icon = {
            Image(
                painter = painterResource(R.drawable.ic_drawer_app_pro),
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Fit
            )
        },
        title = { Text(text = stringResource(R.string.pro_reminder_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(
                    text = stringResource(R.string.pro_reminder_body),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = price ?: if (isProductLoading) stringResource(R.string.pro_price_loading) else "",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        },
        confirmButton = {
            Button(
                shape = neonButtonShape(),
                onClick = onGetPro,
                enabled = !purchaseInProgress && !isProductLoading
            ) {
                Text(
                    text = stringResource(R.string.settings_pro_buy)
                )
            }
        },
        dismissButton = {
            TextButton(onClick = onLater) {
                Text(text = stringResource(R.string.pro_reminder_later))
            }
        }
    )
}

@Composable
fun ProReminderHost(isHomeVisible: Boolean) {
    val context = LocalContext.current
    val manager = remember(context) { ProPurchaseManager.getInstance(context.applicationContext) }
    val preferences = remember(context) { AppPreferences(context.applicationContext) }
    val state by manager.state.collectAsState()
    var showDialog by remember { mutableStateOf(false) }

    LaunchedEffect(isHomeVisible) {
        if (!isHomeVisible) return@LaunchedEffect
        delay(1500)
        // The one-time welcome dialog owns the first Pro pitch.
        if (!preferences.welcomeDialogShown) return@LaunchedEffect
        val current = manager.state.value
        val now = System.currentTimeMillis()
        val lastShownAt = preferences.proReminderLastShownAtMs
        if (lastShownAt == 0L) {
            preferences.proReminderLastShownAtMs = now
            return@LaunchedEffect
        }
        val show = ProReminderPolicy.shouldShow(
            nowMs = now,
            lastShownAtMs = lastShownAt,
            dismissStreak = preferences.proReminderDismissStreak,
            isProUnlocked = current.isProUnlocked,
            canPurchase = current.isProductAvailable && current.productPrice != null,
            reviewAttemptAtMs = AndroidInAppReviewAttemptStore(context.applicationContext).readAttemptState().lastAttemptAtMs
        )
        if (show) {
            preferences.proReminderLastShownAtMs = now
            AppAnalytics.logProReminderShown(preferences.proReminderDismissStreak)
            showDialog = true
        }
    }

    if (showDialog && isHomeVisible && !state.isProUnlocked) {
        ProReminderDialog(
            price = state.productPrice,
            isProductLoading = state.isProductLoading,
            purchaseInProgress = state.isPurchaseInProgress,
            onGetPro = {
                showDialog = false
                AppAnalytics.logProReminderAction("get_pro", preferences.proReminderDismissStreak)
                preferences.proReminderDismissStreak = 0
                context.findActivity()?.let { manager.purchase(it, ProPurchaseTier.BASE, "reminder") }
            },
            onLater = {
                showDialog = false
                preferences.proReminderDismissStreak = preferences.proReminderDismissStreak + 1
                AppAnalytics.logProReminderAction("later", preferences.proReminderDismissStreak)
            }
        )
    }
}
