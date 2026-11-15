package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import org.agentej.android.Prefs
import org.agentej.android.Util

private data class Volume(val used: Long, val total: Long)

@Composable
fun DrawerContent(current: String, prefs: Prefs, onOpen: (String) -> Unit) {
    val context = LocalContext.current
    val home = remember { Native.homePath() }
    val appDir = remember { context.filesDir.absolutePath }
    var internal by remember { mutableStateOf<Volume?>(null) }
    LaunchedEffect(Unit) {
        withContext(Dispatchers.IO) {
            fun vol(p: String) = Native.diskStats(p).split('\t').let {
                Volume(it.getOrNull(0)?.toLongOrNull() ?: 0, it.getOrNull(1)?.toLongOrNull() ?: 0)
            }
            internal = vol(home)
        }
    }
    val cur = current.trimEnd('/').ifEmpty { "/" }

    ModalDrawerSheet(
        drawerContainerColor = MaterialTheme.colorScheme.surface,
        modifier = Modifier.width(300.dp)
    ) {
        Column(Modifier.fillMaxHeight().safeDrawingPadding().verticalScroll(rememberScrollState())) {
            Column(Modifier.padding(start = 20.dp, top = 20.dp, bottom = 14.dp)) {
                Text("EJ Manager", fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
                Text(
                    Native.version(), fontSize = 11.sp, fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis
                )
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

            Section("Local")
            DrawerRow(EjIcons.Phone, "Internal storage", internal, cur == home.trimEnd('/')) { onOpen(home) }
            DrawerRow(Icons.Default.Lock, "App files", null, cur == appDir, sub = "Private · keys, temp output") { onOpen(appDir) }
            DrawerRow(Icons.Default.Home, "Root  /", null, cur == "/") { onOpen("/") }

            HorizontalDivider(Modifier.padding(top = 6.dp), color = MaterialTheme.colorScheme.outlineVariant)
            Section("Bookmarks")
            val defaults = listOf("$home/Download", "$home/Documents", "$home/Android/data")
            (defaults + prefs.bookmarks.filter { it !in defaults }).forEach { p ->
                DrawerRow(
                    EjIcons.Bookmark, if (p.startsWith("$home/")) p.removePrefix("$home/") else Util.title(p), null, cur == p,
                    sub = p,
                    onRemove = if (p in prefs.bookmarks) ({ prefs.toggleBookmark(p) }) else null
                ) { onOpen(p) }
            }
            if (prefs.bookmarks.isEmpty()) Text(
                "Add your own from a folder's ⋮ menu.",
                fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp)
            )
        }
    }
}

@Composable
private fun Section(label: String) {
    Text(
        label, fontSize = 12.sp, fontWeight = FontWeight.Medium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(start = 20.dp, top = 14.dp, bottom = 4.dp)
    )
}

@Composable
private fun DrawerRow(
    icon: ImageVector,
    label: String,
    vol: Volume?,
    active: Boolean,
    sub: String = "",
    onRemove: (() -> Unit)? = null,
    onClick: () -> Unit
) {
    val accent = MaterialTheme.colorScheme.primary
    Row(
        Modifier.fillMaxWidth().heightIn(min = 56.dp)
            .background(if (active) Ej.colors.selection else MaterialTheme.colorScheme.surface)
            .clickable(onClick = onClick).padding(start = 16.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            Modifier.size(32.dp).clip(CircleShape).background(MaterialTheme.colorScheme.surfaceContainerHigh),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, null, tint = if (active) accent else MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(18.dp))
        }
        Column(Modifier.weight(1f).padding(start = 14.dp, end = 8.dp, top = 6.dp, bottom = 6.dp)) {
            Text(label, fontSize = 15.sp, color = if (active) accent else MaterialTheme.colorScheme.onSurface, maxLines = 1)
            if (vol != null && vol.total > 0) {
                LinearProgressIndicator(
                    progress = { (vol.used.toDouble() / vol.total).toFloat().coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                    trackColor = MaterialTheme.colorScheme.surfaceContainerHighest,
                    gapSize = 0.dp,
                    drawStopIndicator = {}
                )
                Text(
                    "${Util.compactSize(vol.used)} used, ${Util.compactSize(vol.total - vol.used)} free",
                    fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else if (sub.isNotEmpty()) Text(
                sub, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1, overflow = TextOverflow.Ellipsis
            )
        }
        if (onRemove != null) IconBtn(Icons.Default.Close, "Remove bookmark $label",
            tint = MaterialTheme.colorScheme.onSurfaceVariant, onClick = onRemove)
    }
}
