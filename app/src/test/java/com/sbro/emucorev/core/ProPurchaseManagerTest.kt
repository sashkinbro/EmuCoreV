package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ProPurchaseManagerTest {
    private val offers = ProPurchaseTier.entries.map { tier ->
        ProProductOffer(tier, tier.name, tier.name, "$1")
    }

    @Test
    fun basePurchaseIsBlockedAfterProIsUnlocked() {
        assertFalse(
            canPurchaseProTier(
                tier = ProPurchaseTier.BASE,
                isProUnlocked = true,
                ownedProductIds = setOf(ProPurchaseTier.BASE.productId)
            )
        )
    }

    @Test
    fun supportOffersExcludeBaseAndDisappearAfterSupportPurchase() {
        val beforePurchase = availableProSupportOffers(
            offers = offers,
            ownedProductIds = setOf(ProPurchaseTier.BASE.productId)
        )
        assertEquals(
            listOf(ProPurchaseTier.SUPPORTER, ProPurchaseTier.PATRON),
            beforePurchase.map(ProProductOffer::tier)
        )

        val afterPurchase = availableProSupportOffers(
            offers = offers,
            ownedProductIds = setOf(ProPurchaseTier.BASE.productId, ProPurchaseTier.SUPPORTER.productId)
        )
        assertTrue(afterPurchase.isEmpty())
    }

    @Test
    fun freshUserCanPurchaseBaseTier() {
        assertTrue(
            canPurchaseProTier(
                tier = ProPurchaseTier.BASE,
                isProUnlocked = false,
                ownedProductIds = emptySet()
            )
        )
    }
}
