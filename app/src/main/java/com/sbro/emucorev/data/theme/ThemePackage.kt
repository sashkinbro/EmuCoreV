package com.sbro.emucorev.data.theme

import com.sbro.emucorev.data.CustomThemeConfig
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** Parsed `theme.json` of an `.ecxtheme` package. See docs/themes/ecxtheme-format.md. */
@Serializable
data class ThemePackageSpec(
    val formatVersion: Int = 0,
    val id: String = "",
    val version: Int = 1,
    val minAppVersionCode: Int = 0,
    val requiresPro: Boolean = false,
    val author: String = "",
    val name: Map<String, String> = emptyMap(),
    val colors: Map<String, String> = emptyMap(),
    val dark: Boolean? = null,
    val accents: List<String> = emptyList(),
    val semantic: Map<String, String> = emptyMap(),
    val shape: ShapeSpec = ShapeSpec(),
    val typography: TypographySpec = TypographySpec(),
    val effects: EffectsSpec = EffectsSpec(),
    val background: BackgroundSpec = BackgroundSpec(),
    val components: ComponentsSpec = ComponentsSpec(),
    val borders: BordersSpec = BordersSpec(),
    val icons: IconsSpec = IconsSpec()
) {
    @Serializable
    data class ShapeSpec(val style: String = "rounded", val scale: Float = 1f)

    @Serializable
    data class TypographySpec(
        val font: String? = null,
        val titleCase: String = "none",
        val letterSpacing: Float = 0f
    )

    @Serializable
    data class EffectsSpec(
        val decor: String = "none",
        val style: String = "standard",
        val scanlines: Boolean = false
    )

    @Serializable
    data class BackgroundSpec(val dim: Float = 0.35f, val pattern: String = "none")

    /** Per-component structural variants; "default" keeps the UI style's own look. */
    @Serializable
    data class ComponentsSpec(
        val card: String = "default",
        val button: String = "default",
        val chip: String = "default",
        val switch: String = "default",
        val header: String = "default"
    )

    /** `width` is in dp (0..3); negative/absent keeps the style's own border width. */
    @Serializable
    data class BordersSpec(val width: Float = -1f, val accentEdge: String = "none")

    @Serializable
    data class IconsSpec(val container: String = "default")

    fun displayName(locale: String): String {
        val language = locale.substringBefore('-').substringBefore('_').lowercase()
        return (name[language] ?: name["en"] ?: name.values.firstOrNull() ?: id)
            .trim()
            .take(CustomThemeConfig.MAX_NAME_LENGTH)
    }

    companion object {
        const val SUPPORTED_FORMAT_VERSION = 1
        private val ID_PATTERN = Regex("[a-z0-9-]{3,48}")
        private val REQUIRED_COLORS = listOf(
            "primary", "onPrimary", "secondary", "tertiary", "background", "surface",
            "surfaceVariant", "onBackground", "onSurface", "onSurfaceVariant", "outline", "error"
        )

        private val JSON = Json {
            ignoreUnknownKeys = true
            isLenient = false
        }

        fun decode(raw: String): ThemePackageSpec {
            val spec = JSON.decodeFromString(serializer(), raw)
            require(spec.formatVersion >= 1) { "Missing theme formatVersion" }
            require(ID_PATTERN.matches(spec.id)) { "Invalid theme id" }
            require(spec.version >= 1) { "Invalid theme version" }
            require(!spec.name["en"].isNullOrBlank()) { "Theme name.en is required" }
            REQUIRED_COLORS.forEach { key ->
                require(parseHexColor(spec.colors[key]) != null) { "Theme color $key is missing or invalid" }
            }
            return spec
        }

        fun parseHexColor(value: String?): Int? {
            val hex = value?.trim()?.removePrefix("#") ?: return null
            if (hex.length != 6) return null
            return hex.toLongOrNull(16)?.let { (it or 0xFF000000L).toInt() }
        }
    }
}

enum class ThemeShapeStyle { ROUNDED, CUT, CUT_DIAGONAL, SQUARE }

enum class ThemeDecor { NONE, NEON }

/** Whole-UI drawing style (panels, buttons, headers). Each one is implemented in ui/theme/style. */
enum class UiStyle(val wire: String) {
    STANDARD("standard"), NEON("neon"), CLASSIC("classic"), LUNA("luna"), S60("s60"), AERO("aero"),
    METRO("metro"), OSD("osd"), PIXEL("pixel"), TERMINAL("terminal"), PAPER("paper"), BUBBLE("bubble");

    companion object {
        fun fromWire(value: String?): UiStyle = entries.firstOrNull { it.wire.equals(value, ignoreCase = true) } ?: STANDARD
    }
}

enum class ThemeFont { SYSTEM, RUBIK, EXO2, MONOSPACE, SERIF, PACKAGE, PIXEL, SPACE_MONO, LORA, OSWALD }

/* Structural variants (see THEME_FORMAT.md). DEFAULT always means "what the UI style does today". */
enum class CardVariant { DEFAULT, ELEVATED, OUTLINED, FLAT, GLASS, INSET, NOTCHED, DOUBLE }
enum class ButtonVariant { DEFAULT, FILLED, OUTLINE, SOFT, GEL, RAISED3D, UNDERLINE, BRACKET }
enum class ChipVariant { DEFAULT, PILL, SQUARE, TAG }
enum class SwitchVariant { DEFAULT, TRACK, CHECKBOX, LED }
enum class HeaderVariant { DEFAULT, SOLID, GRADIENT, UNDERLINE, OUTLINED, GLASS, LARGE_TITLE }
enum class AccentEdge { NONE, LEFT, TOP, BOTTOM }
enum class BackgroundPattern { NONE, DOTS, GRID, DIAGONAL, WAVES, NOISE, CIRCUIT, HEX }
enum class IconContainer { DEFAULT, NONE, CIRCLE, SQUIRCLE, SQUARE }
enum class TitleCase { NONE, UPPER }

/** Case/dash-insensitive enum lookup; unknown or missing values give [default]. */
internal fun <E : Enum<E>> Array<E>.byWire(value: String?, default: E): E {
    val key = value?.trim()?.replace("-", "")?.replace("_", "")?.lowercase() ?: return default
    return firstOrNull { it.name.replace("_", "").lowercase() == key } ?: default
}

/** Theme properties beyond the Material color scheme, as plain values (no Compose types). */
data class ThemeStyleSpec(
    val shapeStyle: ThemeShapeStyle = ThemeShapeStyle.ROUNDED,
    val shapeScale: Float = 1f,
    val decor: ThemeDecor = ThemeDecor.NONE,
    val uiStyle: UiStyle = UiStyle.STANDARD,
    val accents: List<Int> = emptyList(),
    val success: Int? = null,
    val warning: Int? = null,
    val info: Int? = null,
    val gold: Int? = null,
    val font: ThemeFont? = null,
    val scanlines: Boolean = false,
    val backgroundDim: Float = 0.35f,
    val card: CardVariant = CardVariant.DEFAULT,
    val button: ButtonVariant = ButtonVariant.DEFAULT,
    val chip: ChipVariant = ChipVariant.DEFAULT,
    val switch: SwitchVariant = SwitchVariant.DEFAULT,
    val header: HeaderVariant = HeaderVariant.DEFAULT,
    /** Border width override in dp; null keeps each style's own width. */
    val borderWidthDp: Float? = null,
    val accentEdge: AccentEdge = AccentEdge.NONE,
    val pattern: BackgroundPattern = BackgroundPattern.NONE,
    val titleCase: TitleCase = TitleCase.NONE,
    val titleLetterSpacingSp: Float = 0f,
    val iconContainer: IconContainer = IconContainer.DEFAULT
)

fun ThemePackageSpec.toCustomThemeConfig(locale: String): CustomThemeConfig {
    fun color(key: String): Int? = ThemePackageSpec.parseHexColor(colors[key])
    val base = CustomThemeConfig(
        name = displayName(locale),
        dark = dark ?: isDarkColor(color("background")!!),
        primary = color("primary")!!,
        onPrimary = color("onPrimary")!!,
        secondary = color("secondary")!!,
        tertiary = color("tertiary")!!,
        background = color("background")!!,
        surface = color("surface")!!,
        surfaceVariant = color("surfaceVariant")!!,
        onBackground = color("onBackground")!!,
        onSurface = color("onSurface")!!,
        onSurfaceVariant = color("onSurfaceVariant")!!,
        outline = color("outline")!!,
        error = color("error")!!
    )
    return base.copy(
        primaryContainer = color("primaryContainer") ?: base.primaryContainer,
        onPrimaryContainer = color("onPrimaryContainer") ?: base.onPrimaryContainer,
        onSecondary = color("onSecondary") ?: base.onSecondary,
        secondaryContainer = color("secondaryContainer") ?: base.secondaryContainer,
        onSecondaryContainer = color("onSecondaryContainer") ?: base.onSecondaryContainer,
        onTertiary = color("onTertiary") ?: base.onTertiary,
        tertiaryContainer = color("tertiaryContainer") ?: base.tertiaryContainer,
        onTertiaryContainer = color("onTertiaryContainer") ?: base.onTertiaryContainer,
        outlineVariant = color("outlineVariant") ?: base.outlineVariant,
        onError = color("onError") ?: base.onError,
        errorContainer = color("errorContainer") ?: base.errorContainer,
        onErrorContainer = color("onErrorContainer") ?: base.onErrorContainer
    ).sanitized()
}

private fun isDarkColor(argb: Int): Boolean {
    val r = (argb ushr 16) and 0xFF
    val g = (argb ushr 8) and 0xFF
    val b = argb and 0xFF
    return (0.299 * r + 0.587 * g + 0.114 * b) < 128
}

private val SQUARE_NATIVE_STYLES = setOf(UiStyle.CLASSIC, UiStyle.PIXEL, UiStyle.TERMINAL)

fun ThemePackageSpec.toStyleSpec(): ThemeStyleSpec = ThemeStyleSpec(
    shapeStyle = when (shape.style.lowercase()) {
        "cut" -> ThemeShapeStyle.CUT
        "cutdiagonal" -> ThemeShapeStyle.CUT_DIAGONAL
        "square" -> ThemeShapeStyle.SQUARE
        // Square-native looks (9x bevels, pixel art, terminals) never get round corners: a package left on
        // the default "rounded" would otherwise keep pill badges, chips and sliders inside a square UI.
        else -> if (UiStyle.fromWire(effects.style) in SQUARE_NATIVE_STYLES) ThemeShapeStyle.SQUARE else ThemeShapeStyle.ROUNDED
    },
    shapeScale = shape.scale.takeIf { it.isFinite() }?.coerceIn(0f, 2f) ?: 1f,
    decor = if (effects.decor.equals("neon", ignoreCase = true)) ThemeDecor.NEON else ThemeDecor.NONE,
    uiStyle = UiStyle.fromWire(effects.style),
    accents = accents.mapNotNull(ThemePackageSpec::parseHexColor).take(3),
    success = ThemePackageSpec.parseHexColor(semantic["success"]),
    warning = ThemePackageSpec.parseHexColor(semantic["warning"]),
    info = ThemePackageSpec.parseHexColor(semantic["info"]),
    gold = ThemePackageSpec.parseHexColor(semantic["gold"]),
    font = when (typography.font?.lowercase()) {
        "system" -> ThemeFont.SYSTEM
        "rubik" -> ThemeFont.RUBIK
        "exo2" -> ThemeFont.EXO2
        "monospace" -> ThemeFont.MONOSPACE
        "serif" -> ThemeFont.SERIF
        "package" -> ThemeFont.PACKAGE
        "pixel" -> ThemeFont.PIXEL
        "spacemono" -> ThemeFont.SPACE_MONO
        "lora" -> ThemeFont.LORA
        "oswald" -> ThemeFont.OSWALD
        else -> null
    },
    scanlines = effects.scanlines,
    backgroundDim = background.dim.takeIf { it.isFinite() }?.coerceIn(0f, 0.9f) ?: 0.35f,
    card = CardVariant.entries.toTypedArray().byWire(components.card.removeSuffix("-border").removeSuffix("_border"), CardVariant.DEFAULT),
    button = ButtonVariant.entries.toTypedArray().byWire(components.button, ButtonVariant.DEFAULT),
    chip = ChipVariant.entries.toTypedArray().byWire(components.chip, ChipVariant.DEFAULT),
    switch = SwitchVariant.entries.toTypedArray().byWire(components.switch, SwitchVariant.DEFAULT),
    header = HeaderVariant.entries.toTypedArray().byWire(components.header, HeaderVariant.DEFAULT),
    borderWidthDp = borders.width.takeIf { it.isFinite() && it >= 0f }?.coerceIn(0f, 3f),
    accentEdge = AccentEdge.entries.toTypedArray().byWire(borders.accentEdge, AccentEdge.NONE),
    pattern = BackgroundPattern.entries.toTypedArray().byWire(background.pattern, BackgroundPattern.NONE),
    titleCase = TitleCase.entries.toTypedArray().byWire(typography.titleCase, TitleCase.NONE),
    titleLetterSpacingSp = typography.letterSpacing.takeIf { it.isFinite() }?.coerceIn(-1f, 6f) ?: 0f,
    iconContainer = IconContainer.entries.toTypedArray().byWire(icons.container, IconContainer.DEFAULT)
)
