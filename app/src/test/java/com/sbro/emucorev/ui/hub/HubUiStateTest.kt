package com.sbro.emucorev.ui.hub

import com.sbro.emucorev.data.hub.HubItem
import com.sbro.emucorev.data.hub.HubKind
import org.junit.Assert.assertEquals
import org.junit.Test

class HubUiStateTest {
    @Test
    fun defaultOrderIsNewestFirstAndStable() {
        val older = item(id = "older", publishedAt = "2025-03-10T09:00:00Z")
        val newerB = item(id = "newer-b", title = "Beta", publishedAt = "2026-06-01T09:00:00Z")
        val newerA = item(id = "newer-a", title = "Alpha", publishedAt = "2026-06-01T09:00:00Z")

        val visible = HubUiState(items = listOf(older, newerB, newerA)).visibleItems

        assertEquals(listOf("newer-a", "newer-b", "older"), visible.map(HubItem::id))
    }

    @Test
    fun historyOrderUsesEventDateInsteadOfCatalogPublicationDate() {
        val launch = item(
            id = "launch",
            kind = HubKind.HISTORY,
            publishedAt = "2026-08-01T09:00:00Z",
            eventDate = "2000-03-04"
        )
        val slim = item(
            id = "slim",
            kind = HubKind.HISTORY,
            publishedAt = "2026-07-01T09:00:00Z",
            eventDate = "2004-11"
        )

        val visible = HubUiState(
            items = listOf(launch, slim),
            section = HubSection.HISTORY
        ).visibleItems

        assertEquals(listOf("slim", "launch"), visible.map(HubItem::id))
    }

    @Test
    fun productAndFeaturedFiltersComposeBeforeSorting() {
        val coreXFeatured = item(
            id = "corex-featured",
            products = listOf("emucorex", "ps2"),
            featured = true
        )
        val coreXRegular = item(id = "corex-regular", products = listOf("emucorex"))
        val coreVFeatured = item(
            id = "corev-featured",
            products = listOf("emucorev", "playstation-vita"),
            featured = true
        )

        val visible = HubUiState(
            items = listOf(coreXRegular, coreVFeatured, coreXFeatured),
            productFilter = HubProductFilter.EMUCOREX,
            featuredOnly = true
        ).visibleItems

        assertEquals(listOf("corex-featured"), visible.map(HubItem::id))
    }

    @Test
    fun titleOrderIgnoresInputOrder() {
        val visible = HubUiState(
            items = listOf(item("z", "Zulu"), item("a", "Alpha")),
            sortOrder = HubSortOrder.TITLE_ASCENDING
        ).visibleItems

        assertEquals(listOf("a", "z"), visible.map(HubItem::id))
    }

    private fun item(
        id: String,
        title: String = id,
        kind: HubKind = HubKind.NEWS,
        publishedAt: String = "2026-01-01T00:00:00Z",
        eventDate: String? = null,
        products: List<String> = emptyList(),
        featured: Boolean = false
    ) = HubItem(
        id = id,
        kind = kind,
        title = title,
        summary = "Summary for $title",
        publishedAt = publishedAt,
        updatedAt = publishedAt,
        eventDate = eventDate,
        datePrecision = null,
        year = eventDate?.take(4)?.toIntOrNull(),
        categories = emptyList(),
        tags = emptyList(),
        relatedProductIds = products,
        featured = featured,
        priority = 0,
        canonicalUrl = "https://example.invalid/$id",
        sources = emptyList(),
        heroThumbnailUrl = null,
        heroThumbnailSha256 = null,
        heroThumbnailBytes = null,
        heroDisplayUrl = null,
        heroDisplaySha256 = null,
        heroDisplayBytes = null,
        heroOriginalUrl = null,
        heroOriginalSha256 = null,
        heroOriginalBytes = null,
        imageDownloadAllowed = false,
        heroAttribution = null,
        heroSourceUrl = null,
        heroLicenseId = null,
        heroLicenseUrl = null,
        providerId = null,
        channelTitle = null,
        sourceLanguage = null,
        isFavorite = false
    )
}
