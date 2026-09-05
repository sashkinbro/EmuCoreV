package com.sbro.emucorev.ui.pro

import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorev.ui.theme.neon.neonAccentColor
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AccountCircle
import androidx.compose.material.icons.rounded.Apps
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProProductOffer
import com.sbro.emucorev.core.ProPurchaseManager
import com.sbro.emucorev.core.ProPurchaseTier
import com.sbro.emucorev.core.availableProSupportOffers

private val ProGold = Color(0xFFFFC857)

@Composable
fun ProPurchasePanel(
    modifier: Modifier = Modifier,
    showFeatures: Boolean = true,
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
        shape = neonShape(26.dp),
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

            if (showFeatures) {
                ProFeatureRow(Icons.Rounded.Palette, R.string.settings_pro_feature_crimson_title, R.string.settings_pro_feature_crimson_desc)
                ProFeatureRow(Icons.Rounded.Apps, R.string.settings_pro_feature_icon_title, R.string.settings_pro_feature_icon_desc)
                ProFeatureRow(Icons.Rounded.AccountCircle, R.string.settings_pro_feature_profile_title, R.string.settings_pro_feature_badge_desc)
            }

            if (!state.isProUnlocked) {
                Button(
                    onClick = {
                        if (activity != null) manager.purchase(activity, ProPurchaseTier.BASE)
                    },
                    enabled = activity != null &&
                        state.isBillingReady &&
                        state.isProductAvailable &&
                        !state.isPurchaseInProgress,
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 56.dp),
                    shape = neonShape(18.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = ProGold,
                        contentColor = Color(0xFF291D00),
                        disabledContainerColor = ProGold.copy(alpha = 0.42f),
                        disabledContentColor = Color(0xFF291D00).copy(alpha = 0.72f)
                    ),
                    elevation = ButtonDefaults.buttonElevation(
                        defaultElevation = 4.dp,
                        pressedElevation = 1.dp,
                        disabledElevation = 0.dp
                    )
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
            }

            if (supportOffers.isNotEmpty()) {
                OutlinedButton(
                    shape = neonButtonShape(),
                    onClick = { supportDialogVisible = true },
                    enabled = !state.isPurchaseInProgress,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.Star, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = stringResource(R.string.settings_pro_support_more_short),
                        maxLines = 1,
                        modifier = Modifier.padding(start = 8.dp)
                    )
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
fun ProBenefitCards(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        ProBenefitCard(
            icon = Icons.Rounded.Palette,
            titleRes = R.string.settings_pro_feature_crimson_title,
            descriptionRes = R.string.settings_pro_feature_crimson_desc
        )
        ProBenefitCard(
            icon = Icons.Rounded.Apps,
            titleRes = R.string.settings_pro_feature_icon_title,
            descriptionRes = R.string.settings_pro_feature_icon_desc
        )
        ProBenefitCard(
            icon = Icons.Rounded.AccountCircle,
            titleRes = R.string.settings_pro_feature_profile_title,
            descriptionRes = R.string.settings_pro_feature_badge_desc
        )
    }
}

@Composable
private fun ProBenefitCard(
    icon: ImageVector,
    titleRes: Int,
    descriptionRes: Int
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
        tonalElevation = 3.dp,
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f)
        )
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Surface(
                shape = neonShape(14.dp),
                color = ProGold.copy(alpha = 0.14f)
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = ProGold,
                    modifier = Modifier
                        .padding(10.dp)
                        .size(26.dp)
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(3.dp)
            ) {
                Text(
                    text = stringResource(titleRes),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = stringResource(descriptionRes),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
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
    if (LocalNeonTheme.current) {
        NeonProSupportDialog(
            offers = offers,
            purchaseInProgress = purchaseInProgress,
            onPurchase = onPurchase,
            onDismiss = onDismiss
        )
        return
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Rounded.Star, contentDescription = null, tint = ProGold) },
        title = { Text(stringResource(R.string.settings_pro_feature_support_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.settings_pro_feature_support_desc))
                offers.forEach { offer ->
                    OutlinedButton(
                        shape = neonButtonShape(),
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
private fun NeonProSupportDialog(
    offers: List<ProProductOffer>,
    purchaseInProgress: Boolean,
    onPurchase: (ProPurchaseTier) -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 560.dp)
                    .heightIn(max = maxHeight),
                shape = neonShape(28.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.primary.copy(alpha = 0.42f)
                ),
                tonalElevation = 0.dp,
                shadowElevation = 18.dp
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .verticalScroll(rememberScrollState())
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(52.dp)
                                .clip(neonShape(16.dp))
                                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.Star,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(28.dp)
                            )
                        }
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(2.dp)
                        ) {
                            Text(
                                text = stringResource(R.string.settings_pro_support_dialog_eyebrow),
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = stringResource(R.string.settings_pro_support_dialog_title),
                                style = MaterialTheme.typography.headlineSmall,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                    }

                    Text(
                        text = stringResource(R.string.settings_pro_support_dialog_desc),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    offers.forEachIndexed { index, offer ->
                        NeonSupportOfferCard(
                            offer = offer,
                            accent = neonAccentColor(index),
                            enabled = !purchaseInProgress,
                            onClick = { onPurchase(offer.tier) }
                        )
                    }

                    Text(
                        text = stringResource(R.string.settings_pro_support_same_features),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    Button(
                        shape = neonButtonShape(),
                        onClick = onDismiss,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = stringResource(R.string.common_close),
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.padding(vertical = 4.dp)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun NeonSupportOfferCard(
    offer: ProProductOffer,
    accent: Color,
    enabled: Boolean,
    onClick: () -> Unit
) {
    val titleRes = when (offer.tier) {
        ProPurchaseTier.SUPPORTER -> R.string.settings_pro_supporter_title
        ProPurchaseTier.PATRON -> R.string.settings_pro_patron_title
        ProPurchaseTier.BASE -> R.string.pro_title
    }
    val descriptionRes = when (offer.tier) {
        ProPurchaseTier.SUPPORTER -> R.string.settings_pro_supporter_desc
        ProPurchaseTier.PATRON -> R.string.settings_pro_patron_desc
        ProPurchaseTier.BASE -> R.string.settings_pro_feature_support_desc
    }
    Surface(
        modifier = Modifier.fillMaxWidth(),
        enabled = enabled,
        onClick = onClick,
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f),
        border = BorderStroke(1.dp, accent.copy(alpha = 0.48f))
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(42.dp)
                        .clip(neonShape(14.dp))
                        .background(accent.copy(alpha = 0.14f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Star,
                        contentDescription = null,
                        tint = accent,
                        modifier = Modifier.size(22.dp)
                    )
                }
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    Text(
                        text = stringResource(titleRes),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = stringResource(descriptionRes),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Surface(
                modifier = Modifier.align(Alignment.End),
                shape = neonShape(10.dp),
                color = accent.copy(alpha = 0.12f),
                border = BorderStroke(1.dp, accent.copy(alpha = 0.34f))
            ) {
                Text(
                    text = offer.formattedPrice,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = accent,
                    maxLines = 1
                )
            }
        }
    }
}

@Composable
fun ProWelcomeDialog(
    onContinue: () -> Unit,
    manager: ProPurchaseManager = ProPurchaseManager.getInstance(LocalContext.current)
) {
    val context = LocalContext.current
    val activity = context.findActivity()
    val state by manager.state.collectAsState()
    var supportDialogVisible by remember { mutableStateOf(false) }
    val supportOffers = availableProSupportOffers(state.products, state.ownedProductIds)
    AlertDialog(
        onDismissRequest = onContinue,
        icon = {
            Image(
                painter = painterResource(
                    if (state.isProUnlocked) {
                        R.drawable.ic_drawer_app_pro
                    } else {
                        R.drawable.ic_drawer_app
                    }
                ),
                contentDescription = null,
                modifier = Modifier
                    .size(72.dp)
                    .clip(CircleShape)
            )
        },
        title = { Text(stringResource(R.string.welcome_title)) },
        text = { Text(stringResource(R.string.welcome_body)) },
        confirmButton = {
            // Laid out as a single full-width column so "support more" sits
            // directly under the primary action instead of beside it.
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    shape = neonButtonShape(),
                    onClick = {
                        if (state.isProUnlocked) {
                            onContinue()
                        } else if (activity != null) {
                            manager.purchase(activity, ProPurchaseTier.BASE)
                        }
                    },
                    enabled = state.isProUnlocked ||
                        (activity != null && state.isBillingReady && state.isProductAvailable && !state.isPurchaseInProgress),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        stringResource(
                            if (state.isProUnlocked) R.string.welcome_primary else R.string.settings_pro_buy
                        )
                    )
                }
                if (!state.isProUnlocked && supportOffers.isNotEmpty()) {
                    OutlinedButton(
                        shape = neonButtonShape(),
                        onClick = { supportDialogVisible = true },
                        enabled = !state.isPurchaseInProgress,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.Star, contentDescription = null, modifier = Modifier.size(18.dp))
                        Text(
                            text = stringResource(R.string.settings_pro_support_more_short),
                            maxLines = 1,
                            modifier = Modifier.padding(start = 8.dp)
                        )
                    }
                }
                TextButton(
                    onClick = onContinue,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(stringResource(R.string.welcome_secondary))
                }
            }
        }
    )

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

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}
