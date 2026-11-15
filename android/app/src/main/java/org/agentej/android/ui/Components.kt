package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.Image
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.Util

/** One header shape for every screen: leading action, title + subtitle, trailing actions. */
@Composable
fun EjTopBar(
    title: String,
    subtitle: String = "",
    leading: (@Composable () -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {}
) {
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface)
            .heightIn(min = 56.dp).padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        leading?.invoke()
        Column(Modifier.weight(1f).padding(start = if (leading == null) 12.dp else 2.dp)) {
            Text(
                title,
                fontSize = 17.sp,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            if (subtitle.isNotEmpty()) Text(
                subtitle,
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        actions()
    }
}

@Composable
fun IconBtn(
    icon: ImageVector,
    label: String,
    enabled: Boolean = true,
    tint: Color = MaterialTheme.colorScheme.onSurface,
    onClick: () -> Unit
) {
    IconButton(onClick = onClick, enabled = enabled) {
        Icon(
            icon,
            contentDescription = label,
            tint = if (enabled) tint else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f),
            modifier = Modifier.size(22.dp)
        )
    }
}

@Composable
fun tileColor(name: String, isDir: Boolean): Color {
    val c = Ej.colors
    if (isDir) return c.tileDir
    return when (Util.ext(name)) {
        "apk", "apks", "xapk" -> c.tileApk
        "dex" -> c.tileDex
        "arsc" -> c.tileArsc
        "xml" -> c.tileXml
        "zip", "jar", "aar" -> c.tileZip
        "png", "jpg", "jpeg", "webp", "gif" -> c.tileImg
        "so" -> c.tileSo
        else -> c.tileOther
    }
}

/**
 * MT-style type square: a real thumbnail when [thumb] is loaded, otherwise the
 * folder glyph or a 3-letter extension code on the type colour.
 */
@Composable
fun FileTile(
    name: String,
    isDir: Boolean,
    selected: Boolean = false,
    thumb: ImageBitmap? = null,
    size: Dp = 28.dp
) {
    val bg = if (selected) MaterialTheme.colorScheme.primary
    else if (thumb != null) MaterialTheme.colorScheme.surfaceContainerHigh else tileColor(name, isDir)
    val glyph = (size * 0.62f).coerceAtMost(40.dp)
    Box(
        Modifier.size(size).clip(RoundedCornerShape(if (size > 40.dp) 8.dp else 6.dp)).background(bg),
        contentAlignment = Alignment.Center
    ) {
        when {
            selected -> Icon(
                Icons.Default.Check, null,
                tint = MaterialTheme.colorScheme.onPrimary, modifier = Modifier.size(glyph)
            )
            thumb != null -> Image(
                thumb, contentDescription = null, contentScale = ContentScale.Crop,
                filterQuality = FilterQuality.Medium, modifier = Modifier.fillMaxSize()
            )
            isDir -> Icon(EjIcons.Folder, null, tint = Ej.colors.tileFolder, modifier = Modifier.size(glyph))
            else -> Text(
                Util.shortCode(name, false),
                fontSize = if (size > 40.dp) 14.sp else if (size > 30.dp) 11.sp else 9.sp,
                fontWeight = FontWeight.Bold,
                color = Ej.colors.tileGlyph
            )
        }
    }
}

/** Empty / error placeholder: says what happened and what to do next. */
@Composable
fun EmptyState(title: String, hint: String = "", modifier: Modifier = Modifier) {
    Column(
        modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(title, fontSize = 15.sp, color = MaterialTheme.colorScheme.onSurface, textAlign = TextAlign.Center)
        if (hint.isNotEmpty()) Text(
            hint,
            fontSize = 12.sp,
            lineHeight = 17.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(top = 6.dp)
        )
    }
}

/** Left label / right value row, the MT info-sheet table. */
@Composable
fun FactRow(key: String, value: String, mono: Boolean = true) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 7.dp)) {
        Text(
            key, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(0.36f), maxLines = 1
        )
        Text(
            value, fontSize = 12.sp,
            fontFamily = if (mono) FontFamily.Monospace else FontFamily.Default,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.weight(0.64f), maxLines = 3, overflow = TextOverflow.Ellipsis
        )
    }
}
