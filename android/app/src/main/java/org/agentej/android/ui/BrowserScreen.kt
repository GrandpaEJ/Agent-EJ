package org.agentej.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.FsEntry
import org.agentej.android.Native
import org.agentej.android.Util

/** MT-style type tiles: folders grey, apk green, dex teal, arsc amber, xml blue. */
private fun tileColor(name: String, isDir: Boolean): Color = when {
    isDir -> Color(0xFF4A4F57)
    Util.ext(name) in setOf("apk", "apks", "xapk") -> Color(0xFF2F7D4F)
    Util.ext(name) == "dex" -> Color(0xFF357476)
    Util.ext(name) == "arsc" -> Color(0xFF985E00)
    Util.ext(name) == "xml" -> Color(0xFF314D9D)
    Util.ext(name) == "zip" || Util.ext(name) == "jar" -> Color(0xFF6B5410)
    Util.ext(name) in setOf("png", "jpg", "jpeg", "webp") -> Color(0xFF3E6B3E)
    Util.ext(name) == "so" -> Color(0xFF5A3E6B)
    else -> Color(0xFF3A3F46)
}

@Composable
fun BrowserScreen(
    path: String,
    canGoBack: Boolean,
    onBack: () -> Unit,
    onNavigate: (String) -> Unit,
    onOpenText: (String, String) -> Unit,
    onOpenApkTools: (String) -> Unit,
    onShortcut: (String) -> Unit
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val archive = remember(path) { Util.splitArchive(path) }
    val displayPath = remember(path) {
        if (path.length <= 30) path
        else {
            val parts = path.trimEnd('/').split('/')
            if (parts.size <= 2) path else ".../" + parts.takeLast(2).joinToString("/") + (if (path.endsWith("/")) "/" else "")
        }
    }
    var refresh by remember { mutableIntStateOf(0) }
    var entries by remember { mutableStateOf<List<FsEntry>>(emptyList()) }
    var counts by remember { mutableStateOf("") }
    var status by remember { mutableStateOf("") }

    LaunchedEffect(path, refresh) {
        val split = Util.splitArchive(path)
        val loaded = withContext(Dispatchers.IO) {
            if (split != null) {
                val lines = Native.zipEntries(split.first)?.toList() ?: emptyList()
                val children = Util.archiveChildren(lines, split.second)
                val dirs = children.count { it.isDir }
                val files = children.size - dirs
                Triple(children, "Folders: $dirs   Files: $files", "archive")
            } else {
                val list = Native.listDir(path)?.map(Util::parseEntry) ?: emptyList()
                val dirs = list.count { it.isDir }
                val files = list.size - dirs
                val disk = Native.diskStats(path).split('\t')
                val diskText = if (disk.size == 2)
                    "Disk: ${Util.compactSize(disk[0].toLongOrNull() ?: 0L)}/${Util.compactSize(disk[1].toLongOrNull() ?: 0L)}"
                else ""
                Triple(list, "Folders: $dirs   Files: $files   $diskText", "fs")
            }
        }
        entries = loaded.first
        counts = loaded.second
        status = if (loaded.third == "fs" && entries.isEmpty()) "empty or unreadable" else ""
    }

    fun open(e: FsEntry) {
        val full = Util.join(path, e.name)
        when {
            e.isDir -> onNavigate(if (path.endsWith("/")) full else "$full/")
            Util.isArchive(e.name) -> onNavigate("$full/")
            else -> scope.launch {
                val text = withContext(Dispatchers.IO) {
                    val split = Util.splitArchive(path)
                    if (split != null) {
                        val bytes = Native.readEntry(split.first, split.second + e.name)
                        bytes?.let { Util.decodeText(it) }
                    } else {
                        Native.readText(full, 4 * 1024 * 1024)
                    }
                }
                if (text != null) onOpenText(e.name, text) else status = "binary or too large: ${e.name}"
            }
        }
    }

    Column(Modifier.fillMaxSize().safeDrawingPadding().background(MaterialTheme.colorScheme.background)) {
        Surface(color = MaterialTheme.colorScheme.surface, tonalElevation = 2.dp) {
            Column(Modifier.fillMaxWidth().padding(horizontal = 6.dp, vertical = 2.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { if (canGoBack) onBack() else onShortcut("/") }) {
                        Text("\u2190", fontSize = 16.sp)
                    }
                    Column(Modifier.weight(1f)) {
                        Text(
                            displayPath,
                            fontSize = 16.sp,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            counts,
                            fontSize = 9.sp,
                            fontFamily = FontFamily.Monospace,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    if (archive != null && archive.second.isEmpty()) {
                        TextButton(onClick = { onOpenApkTools(archive.first) }) {
                            Text("TOOLS", fontSize = 11.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                    TextButton(onClick = { refresh++ }) { Text("\u21BB", fontSize = 15.sp) }
                }
                Row(
                    Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    TextButton(onClick = { onShortcut("/sdcard/Download") }) { Text("Download", fontSize = 10.sp) }
                    TextButton(onClick = { onShortcut("/sdcard/Android/data") }) { Text("Android/data", fontSize = 10.sp) }
                    TextButton(onClick = { onShortcut("/sdcard/Documents") }) { Text("Documents", fontSize = 10.sp) }
                    TextButton(onClick = { onShortcut(context.filesDir.absolutePath) }) { Text("App", fontSize = 10.sp) }
                }
                if (status.isNotEmpty()) {
                    Text(status, fontSize = 9.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

        LazyColumn(Modifier.fillMaxSize()) {
            items(entries, key = { "${path}/${it.name}" }) { e ->
                Row(
                    Modifier.fillMaxWidth().height(44.dp).clickable { open(e) }
                        .padding(horizontal = 10.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Tile(e.name, e.isDir)
                    Column(Modifier.weight(1f).padding(start = 10.dp)) {
                        Text(
                            e.name,
                            fontSize = 14.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            buildString {
                                if (e.mtime > 0) append(Util.fmtTime(e.mtime))
                                if (!e.isDir && e.size > 0) {
                                    if (isNotEmpty()) append("   ")
                                    append(Util.compactSize(e.size))
                                }
                            },
                            fontSize = 10.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun Tile(name: String, isDir: Boolean) {
    val color = tileColor(name, isDir)
    Box(
        Modifier.size(26.dp).clip(RoundedCornerShape(6.dp)).background(color),
        contentAlignment = Alignment.Center
    ) {
        if (isDir) {
            Canvas(Modifier.size(16.dp)) {
                val w = size.width
                val h = size.height
                drawRoundRect(
                    Color(0xFFD7DBE0),
                    topLeft = Offset(0f, h * 0.22f),
                    size = Size(w, h * 0.68f),
                    cornerRadius = CornerRadius(w * 0.12f)
                )
                drawRoundRect(
                    Color(0xFFD7DBE0),
                    topLeft = Offset(0f, h * 0.08f),
                    size = Size(w * 0.5f, h * 0.24f),
                    cornerRadius = CornerRadius(w * 0.1f)
                )
            }
        } else {
            Text(
                Util.shortCode(name, false),
                fontSize = 9.sp,
                fontWeight = FontWeight.Bold,
                color = Color(0xFFF2F4F7)
            )
        }
    }
}
