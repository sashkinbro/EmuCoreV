package com.sbro.emucorev.ui.library

import android.annotation.SuppressLint
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PageSize
import androidx.compose.foundation.pager.PagerDefaults
import androidx.compose.foundation.pager.PagerSnapDistance
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ViewCarousel
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.InputMode
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.ui.common.RequestFocusOnResume
import com.sbro.emucorev.ui.common.GameCoverArt
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlin.math.absoluteValue
import com.sbro.emucorev.ui.theme.neon.neonShape

@OptIn(ExperimentalComposeUiApi::class)
@Composable
internal fun LibraryShelfMode(
    games: List<InstalledVitaGame>,
    artworkVersion: Long,
    initialTitleId: String? = null,
    topInset: Dp,
    bottomInset: Dp,
    horizontalInset: Dp,
    modifier: Modifier = Modifier,
    onExitShelfMode: () -> Unit,
    onGameClick: (InstalledVitaGame) -> Unit,
    contextMenu: @Composable (InstalledVitaGame, Boolean, () -> Unit) -> Unit
) {
    if (games.isEmpty()) return
    val startIndex = remember(games, initialTitleId) {
        games.indexOfFirst { it.titleId.equals(initialTitleId, ignoreCase = true) }.takeIf { it >= 0 } ?: 0
    }
    val pagerState = rememberPagerState(
        initialPage = startIndex.coerceIn(0, games.lastIndex),
        pageCount = { games.size }
    )
    val scope = rememberCoroutineScope()
    // Always own focus on the shelf: key events (D-pad, keyboard, gamepad connected later) only
    // reach the card handler while a card is focused. The focus border stays gamepad-only.
    val shouldRequestGamepadFocus = true
    val cardFocusRequesters = remember(games) { List(games.size) { FocusRequester() } }
    val currentPage = pagerState.currentPage.coerceIn(0, games.lastIndex)
    val activeGame = games[currentPage]
    val pagerFlingBehavior = PagerDefaults.flingBehavior(
        state = pagerState,
        pagerSnapDistance = PagerSnapDistance.atMost(4)
    )

    LaunchedEffect(games.size) {
        val lastIndex = games.lastIndex
        if (pagerState.currentPage > lastIndex) {
            pagerState.scrollToPage(lastIndex)
        }
    }
    // Button navigation keeps an explicit target index (wraps around) instead of deriving it
    // from the page the pager happens to be on mid-animation.
    val navTarget = remember(games) { intArrayOf(-1) }
    val navJob = remember(games) { arrayOfNulls<Job>(1) }
    DisposableEffect(games) {
        onDispose { navJob[0]?.cancel() }
    }
    fun navigateBy(delta: Int) {
        val count = games.size
        if (count <= 1) return
        val active = navJob[0]?.isActive == true && navTarget[0] in 0 until count
        val base = if (active) navTarget[0] else pagerState.currentPage.coerceIn(0, count - 1)
        val target = ((base + delta) % count + count) % count
        navTarget[0] = target
        navJob[0]?.cancel()
        navJob[0] = scope.launch {
            val wrapped = (delta > 0 && target < base) || (delta < 0 && target > base)
            if (wrapped) pagerState.scrollToPage(target) else pagerState.animateScrollToPage(target)
        }
    }
    // Focus follows the settled page; the target card may not be composed yet (e.g. after a
    // wrap-around jump), so retry for a few frames instead of failing silently.
    val inputModeManager = LocalInputModeManager.current
    val keyDriven = com.sbro.emucorev.ui.common.rememberControllerConnected()
    LaunchedEffect(pagerState.settledPage, shouldRequestGamepadFocus, cardFocusRequesters, keyDriven, inputModeManager.inputMode) {
        if (!shouldRequestGamepadFocus) return@LaunchedEffect
        // Clickable cards are not focusable in touch input mode (cold start before any key press).
        if (keyDriven) inputModeManager.requestInputMode(InputMode.Keyboard)
        val requester = cardFocusRequesters[pagerState.settledPage.coerceIn(0, cardFocusRequesters.lastIndex)]
        repeat(8) {
            if (runCatching { requester.requestFocus() }.getOrDefault(false)) return@LaunchedEffect
            withFrameNanos { }
        }
    }
    RequestFocusOnResume(
        focusRequester = cardFocusRequesters[pagerState.currentPage.coerceIn(0, cardFocusRequesters.lastIndex)],
        enabled = shouldRequestGamepadFocus
    )

    BoxWithConstraints(
        modifier = modifier.fillMaxSize()
    ) {
        val isLandscape = maxWidth > maxHeight
        val baseCardWidth = when {
            maxWidth >= 1100.dp -> 254.dp
            maxWidth >= 840.dp -> 228.dp
            maxWidth >= 620.dp -> 204.dp
            else -> 182.dp
        }
        val coverPromptHeightAllowance = 0.dp
        val reservedHeight = (if (isLandscape) 124.dp else 164.dp) + coverPromptHeightAllowance
        val maxCardWidthFromHeight =
            ((maxHeight - topInset - bottomInset - reservedHeight).coerceAtLeast(210.dp)) * (2f / 3f)
        val cardWidth = (if (isLandscape) baseCardWidth * 0.84f else baseCardWidth)
            .coerceAtMost(maxCardWidthFromHeight)
        val horizontalPadding = ((maxWidth - cardWidth) / 2).coerceAtLeast(0.dp)

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        ) {
            val activeShelfCoverPath = activeGame.iconPath
            ShelfBackdrop(
                titleId = activeGame.titleId,
                title = activeGame.title,
                coverPath = activeShelfCoverPath,
                artworkVersion = artworkVersion,
                modifier = Modifier.fillMaxSize()
            )

            Column(
                modifier = Modifier.fillMaxSize()
            ) {
                ShelfTopBar(
                    title = activeGame.title,
                    topInset = topInset,
                    horizontalInset = horizontalInset,
                    onExitShelfMode = onExitShelfMode
                )
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth()
                        .padding(top = 8.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(bottom = bottomInset + 46.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        HorizontalPager(
                            state = pagerState,
                            key = { games[it].titleId },
                            pageSize = PageSize.Fixed(cardWidth),
                            contentPadding = PaddingValues(horizontal = horizontalPadding),
                            pageSpacing = 8.dp,
                            beyondViewportPageCount = 3,
                            flingBehavior = pagerFlingBehavior,
                            modifier = Modifier.fillMaxSize()
                        ) { page ->
                            val pageOffset = pagerState.getOffsetDistanceInPages(page)
                            val distanceFromCenter = pageOffset.absoluteValue.coerceIn(0f, 1.25f)
                            val focusProgress = 1f - distanceFromCenter.coerceIn(0f, 1f)
                            val scale = if (isLandscape) {
                                0.82f + (0.22f * focusProgress)
                            } else {
                                0.88f + (0.16f * focusProgress)
                            }
                            val cardAlpha = 0.42f + (0.58f * focusProgress)
                            val cardShiftY = if (isLandscape) {
                                4.dp * distanceFromCenter.coerceIn(0f, 1f)
                            } else {
                                8.dp * distanceFromCenter.coerceIn(0f, 1f)
                            }
                            val tilt = -pageOffset.coerceIn(-1f, 1f) * 15f
                            val game = games[page]

                            ShelfCoverCard(
                                game = game,
                                isActive = page == pagerState.currentPage,
                                artworkVersion = artworkVersion,
                                contextMenu = contextMenu,
                                focusRequester = cardFocusRequesters[page],
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .graphicsLayer {
                                        scaleX = scale
                                        scaleY = scale
                                        alpha = cardAlpha
                                        translationY = cardShiftY.value * density
                                        rotationY = tilt
                                        cameraDistance = 18f * density
                                    },
                                onClick = {
                                    if (page == pagerState.currentPage) {
                                        onGameClick(game)
                                    } else {
                                        scope.launch { pagerState.animateScrollToPage(page) }
                                    }
                                },
                                onNavigateLeft = { navigateBy(-1) },
                                onNavigateRight = { navigateBy(1) }
                            )
                        }
                    }

                    AnimatedContent(
                        targetState = activeGame.titleId,
                        transitionSpec = {
                            fadeIn(animationSpec = tween(220)) togetherWith
                                fadeOut(animationSpec = tween(140))
                        },
                        label = "shelf-details",
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .fillMaxWidth()
                            .padding(horizontal = horizontalInset)
                            .padding(bottom = bottomInset + 8.dp)
                    ) { activePath ->
                        // AnimatedContent keeps rendering the previous path through the exit fade
                        // while games may already have changed (hide/restore, rescan). A throwing
                        // first {} here crashed recomposition (Vitals NoSuchElementException).
                        val selectedGame = games.firstOrNull { it.titleId == activePath } ?: return@AnimatedContent
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(
                                    Brush.verticalGradient(
                                        listOf(
                                            Color.Black.copy(alpha = 0f),
                                            Color.Black.copy(alpha = 0.12f),
                                            Color.Black.copy(alpha = 0.34f)
                                        )
                                    )
                                )
                                .padding(horizontal = 24.dp, vertical = 12.dp)
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.Bottom
                            ) {
                                Text(
                                    modifier = Modifier
                                        .weight(1f)
                                        .padding(end = 12.dp),
                                    text = selectedGame.title,
                                    style = MaterialTheme.typography.bodyMedium.copy(
                                        fontWeight = FontWeight.Medium,
                                        shadow = Shadow(
                                            color = Color.Black.copy(alpha = 0.72f),
                                            blurRadius = 8f
                                        )
                                    ),
                                    color = Color.White.copy(alpha = 0.94f),
                                    textAlign = TextAlign.Start,
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )

                                Text(
                                    text = androidx.compose.ui.res.stringResource(
                                        R.string.library_shelf_games_counter,
                                        currentPage + 1,
                                        games.size
                                    ),
                                    modifier = Modifier.padding(start = 12.dp),
                                    style = MaterialTheme.typography.labelMedium.copy(
                                        fontWeight = FontWeight.SemiBold,
                                        shadow = Shadow(
                                            color = Color.Black.copy(alpha = 0.72f),
                                            blurRadius = 8f
                                        )
                                    ),
                                    color = Color.White.copy(alpha = 0.88f),
                                    maxLines = 1
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ShelfTopBar(
    title: String,
    topInset: Dp,
    horizontalInset: Dp,
    onExitShelfMode: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = horizontalInset,
                end = horizontalInset,
                top = topInset + 30.dp,
                bottom = 6.dp
            ),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = title,
            modifier = Modifier
                .weight(1f)
                .padding(end = 12.dp),
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
            color = Color.White.copy(alpha = 0.94f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )

        IconButton(onClick = onExitShelfMode) {
            Icon(
                imageVector = Icons.Rounded.ViewCarousel,
                contentDescription = androidx.compose.ui.res.stringResource(R.string.library_exit_shelf),
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun ShelfBackdrop(
    titleId: String,
    title: String,
    coverPath: String?,
    artworkVersion: Long,
    modifier: Modifier = Modifier
) {
    val baseGradient = listOf(
        Color(0xFF11141A),
        Color(0xFF18202A),
        Color(0xFF101318)
    )
    val stageGlow = listOf(
        Color(0xFF344153).copy(alpha = 0.42f),
        Color(0xFF263240).copy(alpha = 0.22f),
        Color.Transparent
    )
    val coverAlpha = 0.16f
    val verticalVeil = listOf(
        Color(0xFF0F1217).copy(alpha = 0.08f),
        Color.Transparent,
        Color(0xFF0F1217).copy(alpha = 0.24f)
    )
    val sideVeil = listOf(
        Color.Black.copy(alpha = 0.18f),
        Color.Transparent,
        Color.Black.copy(alpha = 0.18f)
    )

    Box(
        modifier = modifier.background(Brush.verticalGradient(baseGradient))
    ) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Brush.radialGradient(stageGlow))
        )

            GameCoverArt(
                titleId = titleId,
                fallbackIconPath = coverPath,
                artworkVersion = artworkVersion,
                contentDescription = "",
                fallbackTitle = title,
                modifier = Modifier
                    .fillMaxSize()
                    .graphicsLayer { alpha = coverAlpha }
            )

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.verticalGradient(verticalVeil)
                )
        )
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.horizontalGradient(sideVeil)
                )
        )
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun ShelfCoverCard(
    game: InstalledVitaGame,
    isActive: Boolean,
    artworkVersion: Long,
    contextMenu: @Composable (InstalledVitaGame, Boolean, () -> Unit) -> Unit,
    focusRequester: FocusRequester,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
    onNavigateLeft: () -> Unit,
    onNavigateRight: () -> Unit
) {
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val showMenu = remember(game.titleId) { mutableStateOf(false) }
    var is3dCover by remember(game.titleId, artworkVersion) { mutableStateOf(false) }
    val coverAspectRatio = 2f / 3f
    val horizontalCoverPadding = if (isActive) 6.dp else 8.dp
    val verticalCoverPadding = if (isActive) 6.dp else 4.dp
    val shelfCoverPath = game.iconPath
    // Flat covers are plain rectangles so their corners are never clipped.
    val shape = RectangleShape

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .aspectRatio(coverAspectRatio)
            .focusRequester(focusRequester)
            .onPreviewKeyEvent { keyEvent ->
                if (keyEvent.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val isShelfNavigationKey = when (keyEvent.key) {
                    Key.DirectionLeft,
                    Key.DirectionRight,
                    Key.DirectionCenter,
                    Key.Enter,
                    Key.NumPadEnter,
                    Key.ButtonA -> true
                    else -> false
                }
                val isSelectKey = isShelfNavigationKey &&
                    keyEvent.key != Key.DirectionLeft && keyEvent.key != Key.DirectionRight
                if (keyEvent.nativeKeyEvent.repeatCount != 0 && isSelectKey) {
                    return@onPreviewKeyEvent true
                }
                when (keyEvent.key) {
                    Key.DirectionLeft -> {
                        onNavigateLeft()
                        true
                    }
                    Key.DirectionRight -> {
                        onNavigateRight()
                        true
                    }
                    Key.DirectionCenter,
                    Key.Enter,
                    Key.NumPadEnter,
                    Key.ButtonA -> {
                        debouncedClick()
                        true
                    }
                    else -> false
                }
            }
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = debouncedClick,
                onLongClick = { showMenu.value = true }
            )
            .gamepadFocusableCard(
                showIdleBorder = false,
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false,
                focusBorderVerticalInsetFraction = if (is3dCover) 0.07f else 0f
            ),
        shape = shape,
        color = Color.Transparent,
        tonalElevation = 0.dp,
        shadowElevation = 0.dp
    ) {
            GameCoverArt(
                titleId = game.titleId,
                fallbackIconPath = shelfCoverPath,
                artworkVersion = artworkVersion,
                contentDescription = game.title,
                fallbackTitle = game.title,
                onPresentationChanged = { is3dCover = it },
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = horizontalCoverPadding, vertical = verticalCoverPadding)
            )
    }

    contextMenu(game, showMenu.value) {
        showMenu.value = false
        runCatching { focusRequester.requestFocus() }
    }
}

@Composable
private fun ShelfMissingCoverCard(
    title: String,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .background(
                Brush.verticalGradient(
                    listOf(
                        Color(0xFF2C333D).copy(alpha = 0.92f),
                        Color(0xFF1C222B).copy(alpha = 0.96f)
                    )
                )
            )
            .padding(18.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
            color = Color.White.copy(alpha = 0.86f),
            textAlign = TextAlign.Center,
            maxLines = 5,
            overflow = TextOverflow.Ellipsis
        )
    }
}
