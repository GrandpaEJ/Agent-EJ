package org.agentej.android.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

/*
 * Neutral-grey chrome, measured against MT Manager (docs/mt-ui-analysis.md).
 * Blue is the only chrome accent and means "actionable" (links, selection,
 * focus); green is reserved for success, amber/red for warnings/errors.
 */
private val EjDark = darkColorScheme(
    primary = Color(0xFF5B9BFF),
    onPrimary = Color(0xFF06142B),
    primaryContainer = Color(0xFF1B2D4A),
    onPrimaryContainer = Color(0xFFD3E2FF),
    secondary = Color(0xFF8B9098),
    onSecondary = Color(0xFF101215),
    tertiary = Color(0xFF3DDC84),
    onTertiary = Color(0xFF04120A),
    background = Color(0xFF16191E),
    onBackground = Color(0xFFDDE1E6),
    surface = Color(0xFF101215),
    onSurface = Color(0xFFDDE1E6),
    surfaceVariant = Color(0xFF1D2127),
    onSurfaceVariant = Color(0xFF8E949D),
    surfaceContainerLowest = Color(0xFF0C0E10),
    surfaceContainerLow = Color(0xFF181B20),
    surfaceContainer = Color(0xFF1D2127),
    surfaceContainerHigh = Color(0xFF22262D),
    surfaceContainerHighest = Color(0xFF2A2F37),
    inverseSurface = Color(0xFFDDE1E6),
    inverseOnSurface = Color(0xFF16191E),
    outline = Color(0xFF3A404A),
    outlineVariant = Color(0xFF24282F),
    error = Color(0xFFFF7A7A),
    onError = Color(0xFF2B0606),
    scrim = Color(0xFF000000)
)

/** Roles Material has no slot for: file-type tiles, syntax, state. */
@Immutable
data class EjPalette(
    val tileDir: Color = Color(0xFF4A4F57),
    val tileApk: Color = Color(0xFF2F7D4F),
    val tileDex: Color = Color(0xFF357476),
    val tileArsc: Color = Color(0xFF985E00),
    val tileXml: Color = Color(0xFF314D9D),
    val tileZip: Color = Color(0xFF6B5410),
    val tileImg: Color = Color(0xFF3E6B3E),
    val tileSo: Color = Color(0xFF5A3E6B),
    val tileOther: Color = Color(0xFF3A3F46),
    val tileGlyph: Color = Color(0xFFF2F4F7),
    val tileFolder: Color = Color(0xFFD7DBE0),
    val success: Color = Color(0xFF3DDC84),
    val warning: Color = Color(0xFFE5B454),
    val selection: Color = Color(0x2E5B9BFF),
    val match: Color = Color(0x40E5B454),
    val matchActive: Color = Color(0xE0B07A1C),
    val gutter: Color = Color(0xFF767C86),
    // syntax
    val synKeyword: Color = Color(0xFFC792EA),
    val synDirective: Color = Color(0xFF82AAFF),
    val synString: Color = Color(0xFFC3E88D),
    val synNumber: Color = Color(0xFFF78C6C),
    val synComment: Color = Color(0xFF808892),
    val synType: Color = Color(0xFFFFCB6B),
    val synRegister: Color = Color(0xFF89DDFF),
    val synTag: Color = Color(0xFF7FB0FF),
    val synAttr: Color = Color(0xFFE5B454)
)

val LocalEj = staticCompositionLocalOf { EjPalette() }

object Ej {
    val colors: EjPalette
        @Composable get() = LocalEj.current
}

@Composable
fun AgentXTheme(content: @Composable () -> Unit) {
    androidx.compose.runtime.CompositionLocalProvider(LocalEj provides EjPalette()) {
        MaterialTheme(colorScheme = EjDark, typography = Typography(), content = content)
    }
}
