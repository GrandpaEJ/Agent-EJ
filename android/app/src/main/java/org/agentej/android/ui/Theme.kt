package org.agentej.android.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val EjDark = darkColorScheme(
    primary = Color(0xFF3DDC84),
    onPrimary = Color(0xFF04120A),
    primaryContainer = Color(0xFF14361F),
    onPrimaryContainer = Color(0xFFB9F5CE),
    secondary = Color(0xFF6BA8FF),
    onSecondary = Color(0xFF04101F),
    background = Color(0xFF0E1013),
    onBackground = Color(0xFFE6E9EE),
    surface = Color(0xFF14171C),
    onSurface = Color(0xFFE6E9EE),
    surfaceVariant = Color(0xFF1C2027),
    onSurfaceVariant = Color(0xFFA9B2BF),
    outline = Color(0xFF2A303A),
    outlineVariant = Color(0xFF20252D),
    error = Color(0xFFFF6B6B)
)

@Composable
fun AgentXTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = EjDark, typography = Typography(), content = content)
}
