package com.sbro.emucorev.ui.pro

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AccountCircle
import androidx.compose.material.icons.rounded.Apps
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProProductOffer
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.core.ProPurchaseTier
import com.sbro.emucorev.core.availableProSupportOffers

private val ProGold = Color(0xFFFFC857)

@Composable
fun ProPurchasePanel(
    modifier: Modifier = Modifier,
    manager: ProPurchaseManager = ProPurchaseManager.getInstance(LocalContext.current)
) {
    val context = LocalContext.current
    val state by manager.state.collectAsState()
    var supportDialogVisible by remember { mutableStateOf(false) }
    val activity = context.findActivity()
    val baseOffer = state.products.firstOrNull { it.tier == ProPurchaseTier.BASE }
    val supportOffers = availableProSupportOffers(state.products, state.ownedProductIds)

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(26.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        border = BorderStroke(
            1.dp,
            if (state.isProUnlocked) ProGold.copy(alpha = 0.65f) else MaterialTheme.colorScheme.outlineVariant
        )
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Icon(Icons.Rounded.Star, contentDescription = null, tint = ProGold, modifier = Modifier.size(32.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        stringResource(R.string.pro_title),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        stringResource(
                            if (state.isProUnlocked) R.string.settings_pro_active else R.string.onboarding_pro_subtitle
                        ),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                if (state.isProUnlocked) {
                    Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = ProGold)
                }
            }

            ProFeatureRow(Icons.Rounded.Palette, R.string.settings_pro_feature_crimson_title, R.string.settings_pro_feature_crimson_desc)
            ProFeatureRow(Icons.Rounded.Apps, R.string.settings_pro_feature_icon_title, R.string.settings_pro_feature_icon_desc)
            ProFeatureRow(Icons.Rounded.AccountCircle, R.string.settings_pro_feature_profile_title, R.string.settings_pro_feature_badge_desc)

            if (!state.isProUnlocked) {
                Button(
                    onClick = {
                        if (activity != null) manager.purchase(activity, ProPurchaseTier.BASE)
                    },
                    enabled = activity != null &&
                        state.isBillingReady &&
                        state.isProductAvailable &&
                        !state.isPurchaseInProgress,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    if (state.isPurchaseInProgress) {
                        CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                    } else {
                        Icon(Icons.Rounded.Star, contentDescription = null, modifier = Modifier.size(18.dp))
                    }
                    Text(
                        text = if (state.isProductLoading) {
                            stringResource(R.string.pro_price_loading)
                        } else {
                            "${stringResource(R.string.settings_pro_buy)} · " +
                                (baseOffer?.formattedPrice ?: stringResource(R.string.pro_price_unavailable))
                        },
                        modifier = Modifier.padding(start = 8.dp)
                    )
                }
            } else if (supportOffers.isNotEmpty()) {
                OutlinedButton(
                    onClick = { supportDialogVisible = true },
                    enabled = !state.isPurchaseInProgress,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.Star, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(stringResource(R.string.settings_pro_support_more), modifier = Modifier.padding(start = 8.dp))
                }
            }

            TextButton(
                onClick = { manager.restorePurchases(showMessage = true) },
                enabled = !state.isPurchaseInProgress,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(stringResource(R.string.settings_pro_restore))
            }

            state.messageResId?.let { messageRes ->
                Text(
                    stringResource(messageRes),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
    }

    if (supportDialogVisible) {
        ProSupportDialog(
            offers = supportOffers,
            purchaseInProgress = state.isPurchaseInProgress,
            onPurchase = { tier ->
                if (activity != null) manager.purchase(activity, tier)
            },
            onDismiss = { supportDialogVisible = false }
        )
    }
}

@Composable
private fun ProFeatureRow(icon: ImageVector, titleRes: Int, descriptionRes: Int) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.Top,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Icon(icon, contentDescription = null, tint = ProGold, modifier = Modifier.size(22.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(stringResource(titleRes), fontWeight = FontWeight.SemiBold)
            Text(
                stringResource(descriptionRes),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ProSupportDialog(
    offers: List<ProProductOffer>,
    purchaseInProgress: Boolean,
    onPurchase: (ProPurchaseTier) -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Rounded.Star, contentDescription = null, tint = ProGold) },
        title = { Text(stringResource(R.string.settings_pro_feature_support_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.settings_pro_feature_support_desc))
                offers.forEach { offer ->
                    OutlinedButton(
                        onClick = { onPurchase(offer.tier) },
                        enabled = !purchaseInProgress,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("${offer.title} · ${offer.formattedPrice}")
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.common_close)) }
        }
    )
}

@Composable
fun ProWelcomeDialog(
    onContinue: () -> Unit,
    manager: ProPurchaseManager = ProPurchaseManager.getInstance(LocalContext.current)
) {
    val context = LocalContext.current
    val activity = context.findActivity()
    val state by manager.state.collectAsState()
    AlertDialog(
        onDismissRequest = onContinue,
        icon = { Icon(Icons.Rounded.Star, contentDescription = null, tint = ProGold) },
        title = { Text(stringResource(R.string.welcome_title)) },
        text = { Text(stringResource(R.string.welcome_body)) },
        confirmButton = {
            Button(
                onClick = {
                    if (state.isProUnlocked) {
                        onContinue()
                    } else if (activity != null) {
                        manager.purchase(activity, ProPurchaseTier.BASE)
                    }
                },
                enabled = state.isProUnlocked ||
                    (activity != null && state.isBillingReady && state.isProductAvailable && !state.isPurchaseInProgress)
            ) {
                Text(
                    stringResource(
                        if (state.isProUnlocked) R.string.welcome_primary else R.string.settings_pro_buy
                    )
                )
            }
        },
        dismissButton = {
            TextButton(onClick = onContinue) { Text(stringResource(R.string.welcome_secondary)) }
        }
    )
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}
