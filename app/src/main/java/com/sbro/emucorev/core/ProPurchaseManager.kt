package com.sbro.emucorev.core

import android.app.Activity
import android.content.Context
import android.util.Log
import androidx.annotation.StringRes
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.ui.theme.ThemeMode
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

private const val TAG = "ProPurchaseManager"

enum class ProPurchaseTier(val productId: String) {
    BASE("emucorev_pro"),
    SUPPORTER("emucorev_pro_supporter"),
    PATRON("emucorev_pro_patron");

    companion object {
        val productIds: Set<String> = entries.mapTo(linkedSetOf(), ProPurchaseTier::productId)

        fun fromProductId(productId: String): ProPurchaseTier? =
            entries.firstOrNull { it.productId == productId }
    }
}

data class ProProductOffer(
    val tier: ProPurchaseTier,
    val title: String,
    val description: String,
    val formattedPrice: String
)

/**
 * Support tiers that the user can still buy.
 *
 * These are shown alongside the base Pro offer rather than only after Pro is
 * owned: a higher tier is purely a larger contribution, so hiding it behind a
 * purchase means most users never discover it. Tiers already owned are removed,
 * and owning any support tier removes the rest since it fully covers Pro.
 */
fun availableProSupportOffers(
    offers: List<ProProductOffer>,
    ownedProductIds: Set<String>
): List<ProProductOffer> {
    val ownsSupportTier = ProPurchaseTier.SUPPORTER.productId in ownedProductIds ||
        ProPurchaseTier.PATRON.productId in ownedProductIds
    if (ownsSupportTier) return emptyList()
    return offers.filter { offer ->
        offer.tier != ProPurchaseTier.BASE && offer.tier.productId !in ownedProductIds
    }
}

fun canPurchaseProTier(
    tier: ProPurchaseTier,
    isProUnlocked: Boolean,
    ownedProductIds: Set<String>
): Boolean {
    if (tier.productId in ownedProductIds) return false
    val ownsSupportTier = ProPurchaseTier.SUPPORTER.productId in ownedProductIds ||
        ProPurchaseTier.PATRON.productId in ownedProductIds
    if (ownsSupportTier) return false
    return tier != ProPurchaseTier.BASE || !isProUnlocked
}

data class ProPurchaseState(
    val isProUnlocked: Boolean = false,
    val isPurchaseStatusVerified: Boolean = false,
    val isBillingReady: Boolean = false,
    val isPurchaseInProgress: Boolean = false,
    val isProductLoading: Boolean = false,
    val isProductAvailable: Boolean = false,
    val productTitle: String? = null,
    val productPrice: String? = null,
    val products: List<ProProductOffer> = emptyList(),
    val ownedProductIds: Set<String> = emptySet(),
    val purchaseProductId: String? = null,
    @param:StringRes val messageResId: Int? = null
)

class ProPurchaseManager private constructor(context: Context) : PurchasesUpdatedListener {
    private val appContext = context.applicationContext
    private val preferences = AppPreferences(appContext)
    private var productDetailsById: Map<String, ProductDetails> = emptyMap()

    private val billingClient = BillingClient.newBuilder(appContext)
        .setListener(this)
        .enablePendingPurchases(
            PendingPurchasesParams.newBuilder()
                .enableOneTimeProducts()
                .build()
        )
        .enableAutoServiceReconnection()
        .build()

    private val _state = MutableStateFlow(
        ProPurchaseState(isProUnlocked = preferences.proUnlocked)
    )
    val state: StateFlow<ProPurchaseState> = _state.asStateFlow()

    init {
        connect()
    }

    fun connect() {
        if (billingClient.isReady) {
            _state.value = _state.value.copy(isBillingReady = true)
            queryProductDetails(showMessage = false)
            restorePurchases(showMessage = false)
            return
        }

        billingClient.startConnection(object : BillingClientStateListener {
            override fun onBillingSetupFinished(billingResult: BillingResult) {
                val ready = billingResult.responseCode == BillingClient.BillingResponseCode.OK
                _state.value = _state.value.copy(
                    isBillingReady = ready,
                    messageResId = if (ready) null else R.string.pro_message_unavailable
                )
                if (ready) {
                    queryProductDetails(showMessage = false)
                    restorePurchases(showMessage = false)
                }
            }

            override fun onBillingServiceDisconnected() {
                _state.value = _state.value.copy(isBillingReady = false)
            }
        })
    }

    fun purchase(
        activity: Activity,
        tier: ProPurchaseTier = ProPurchaseTier.BASE
    ) {
        val current = _state.value
        if (current.isPurchaseInProgress) return
        if (!canPurchaseProTier(tier, current.isProUnlocked, current.ownedProductIds)) {
            _state.value = current.copy(messageResId = R.string.pro_message_already_active)
            return
        }
        if (!billingClient.isReady) {
            connect()
            _state.value = current.copy(messageResId = R.string.pro_message_unavailable)
            return
        }

        val details = productDetailsById[tier.productId]
        if (details == null) {
            queryProductDetails(showMessage = true)
            return
        }

        val productParams = BillingFlowParams.ProductDetailsParams.newBuilder()
            .setProductDetails(details)
            .also { builder ->
                details.oneTimePurchaseOfferDetailsList
                    ?.firstOrNull()
                    ?.offerToken
                    ?.takeIf(String::isNotBlank)
                    ?.let(builder::setOfferToken)
            }
            .build()
        val flowParams = BillingFlowParams.newBuilder()
            .setProductDetailsParamsList(listOf(productParams))
            .build()

        _state.value = current.copy(
            isPurchaseInProgress = true,
            purchaseProductId = tier.productId,
            messageResId = null
        )
        val result = billingClient.launchBillingFlow(activity, flowParams)
        if (result.responseCode != BillingClient.BillingResponseCode.OK) {
            _state.value = _state.value.copy(
                isPurchaseInProgress = false,
                purchaseProductId = null,
                messageResId = R.string.pro_message_purchase_open_failed
            )
        }
    }

    fun restorePurchases(showMessage: Boolean = true) {
        if (!billingClient.isReady) {
            connect()
            if (showMessage) {
                _state.value = _state.value.copy(messageResId = R.string.pro_message_unavailable)
            }
            return
        }
        val params = QueryPurchasesParams.newBuilder()
            .setProductType(BillingClient.ProductType.INAPP)
            .build()
        billingClient.queryPurchasesAsync(params) { billingResult, purchases ->
            if (billingResult.responseCode == BillingClient.BillingResponseCode.OK) {
                val active = purchases.filter(::isActiveProPurchase)
                val owned = active
                    .flatMap(Purchase::getProducts)
                    .filterTo(linkedSetOf(), ProPurchaseTier.productIds::contains)
                _state.value = _state.value.copy(
                    isPurchaseStatusVerified = true,
                    ownedProductIds = owned
                )
                if (active.isNotEmpty()) {
                    handlePurchases(active, showUnlockMessage = showMessage)
                } else {
                    lockPro()
                    if (showMessage) {
                        _state.value = _state.value.copy(messageResId = R.string.pro_message_restore_missing)
                    }
                }
            } else if (showMessage) {
                _state.value = _state.value.copy(messageResId = R.string.pro_message_restore_failed)
            }
        }
    }

    fun clearMessage() {
        _state.value = _state.value.copy(messageResId = null)
    }

    override fun onPurchasesUpdated(billingResult: BillingResult, purchases: MutableList<Purchase>?) {
        when (billingResult.responseCode) {
            BillingClient.BillingResponseCode.OK -> {
                if (!purchases.isNullOrEmpty()) {
                    handlePurchases(purchases, showUnlockMessage = true)
                }
            }

            BillingClient.BillingResponseCode.USER_CANCELED -> {
                _state.value = _state.value.copy(
                    isPurchaseInProgress = false,
                    purchaseProductId = null
                )
            }

            else -> {
                _state.value = _state.value.copy(
                    isPurchaseInProgress = false,
                    purchaseProductId = null,
                    messageResId = R.string.pro_message_purchase_failed
                )
            }
        }
    }

    private fun queryProductDetails(showMessage: Boolean) {
        productDetailsById = emptyMap()
        _state.value = _state.value.copy(
            isProductLoading = true,
            isProductAvailable = false,
            productTitle = null,
            productPrice = null,
            products = emptyList(),
            messageResId = null
        )
        val products = ProPurchaseTier.entries.map { tier ->
            QueryProductDetailsParams.Product.newBuilder()
                .setProductId(tier.productId)
                .setProductType(BillingClient.ProductType.INAPP)
                .build()
        }
        val params = QueryProductDetailsParams.newBuilder()
            .setProductList(products)
            .build()

        billingClient.queryProductDetailsAsync(params) { billingResult, result ->
            if (billingResult.responseCode != BillingClient.BillingResponseCode.OK) {
                Log.w(TAG, "Product query failed: ${billingResult.responseCode} ${billingResult.debugMessage}")
                _state.value = _state.value.copy(
                    isProductLoading = false,
                    messageResId = if (showMessage) R.string.pro_message_unavailable else null
                )
                return@queryProductDetailsAsync
            }
            val detailsById = result.productDetailsList
                .filter { it.productId in ProPurchaseTier.productIds }
                .associateBy(ProductDetails::getProductId)
            val offers = ProPurchaseTier.entries.mapNotNull { tier ->
                val details = detailsById[tier.productId] ?: return@mapNotNull null
                val price = details.oneTimePurchaseOfferDetailsList
                    ?.firstOrNull()
                    ?.formattedPrice
                    ?: details.oneTimePurchaseOfferDetails?.formattedPrice
                    ?: return@mapNotNull null
                ProProductOffer(tier, details.title, details.description, price)
            }
            val baseOffer = offers.firstOrNull { it.tier == ProPurchaseTier.BASE }
            productDetailsById = detailsById
            _state.value = _state.value.copy(
                isProductLoading = false,
                isProductAvailable = baseOffer != null,
                productTitle = baseOffer?.title,
                productPrice = baseOffer?.formattedPrice,
                products = offers,
                messageResId = if (showMessage && baseOffer == null) R.string.pro_message_unavailable else null
            )
            if (BuildConfig.DEBUG) {
                Log.i(TAG, "Fetched Pro products=${detailsById.keys}")
            }
        }
    }

    private fun handlePurchases(purchases: List<Purchase>, showUnlockMessage: Boolean) {
        val active = purchases.filter(::isActiveProPurchase)
        val owned = active
            .flatMap(Purchase::getProducts)
            .filterTo(linkedSetOf(), ProPurchaseTier.productIds::contains)
        _state.value = _state.value.copy(ownedProductIds = owned)
        active.forEach { purchase ->
            if (purchase.isAcknowledged) {
                unlockPro(showUnlockMessage)
            } else {
                val params = AcknowledgePurchaseParams.newBuilder()
                    .setPurchaseToken(purchase.purchaseToken)
                    .build()
                billingClient.acknowledgePurchase(params) { result ->
                    if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                        unlockPro(showUnlockMessage)
                    } else {
                        _state.value = _state.value.copy(
                            isPurchaseInProgress = false,
                            purchaseProductId = null,
                            messageResId = R.string.pro_message_pending_confirmation
                        )
                    }
                }
            }
        }
    }

    private fun unlockPro(showMessage: Boolean) {
        val wasUnlocked = preferences.proUnlocked
        preferences.proUnlocked = true
        if (!wasUnlocked) {
            preferences.themeMode = ThemeMode.PRO
        }
        AppIconManager.applyProIcon(appContext, enabled = true)
        _state.value = _state.value.copy(
            isProUnlocked = true,
            isPurchaseInProgress = false,
            purchaseProductId = null,
            messageResId = if (showMessage) R.string.pro_message_active else null
        )
    }

    private fun lockPro() {
        preferences.proUnlocked = false
        AppIconManager.applyProIcon(appContext, enabled = false)
        _state.value = _state.value.copy(
            isProUnlocked = false,
            isPurchaseInProgress = false,
            purchaseProductId = null,
            ownedProductIds = emptySet()
        )
    }

    private fun isActiveProPurchase(purchase: Purchase): Boolean =
        purchase.purchaseState == Purchase.PurchaseState.PURCHASED &&
            purchase.products.any(ProPurchaseTier.productIds::contains)

    companion object {
        @Volatile
        private var instance: ProPurchaseManager? = null

        fun getInstance(context: Context): ProPurchaseManager =
            instance ?: synchronized(this) {
                instance ?: ProPurchaseManager(context).also { instance = it }
            }
    }
}
