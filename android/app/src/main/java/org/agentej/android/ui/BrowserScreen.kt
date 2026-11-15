package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
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

@Composable
fun BrowserScreen(
    path: String,
    canGoBack: Boolean,
    onBack: () -> Unit,
    onEnterDir: (String) -> Unit,
    onOpenArchive: (String) -> Unit,
    onOpenText: (String, String) -> Unit,
    onShortcut: (String) -> Unit
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    var refresh by remember { mutableIntStateOf(0) }
    var entries by remember { mutableStateOf<List<FsEntry>>(emptyList()) }
    var status by remember { mutableStateOf("") }

    LaunchedEffect(path, refresh) {
        val list = withContext(Dispatchers.IO) {
            Native.listDir(path)?.map(Util::parseEntry) ?: emptyList()
        }
        entries = list
        status = if (list.isEmpty()) "empty or unreadable (no permission?)" else "${list.size} items"
    }

    fun open(e: FsEntry) {
        val full = Util.join(path, e.name)
        when {
            e.isDir -> onEnterDir(full)
            Util.isArchive(e.name) -> onOpenArchive(full)
            else -> scope.launch {
                val text = withContext(Dispatchers.IO) { Native.readText(full, 4 * 1024 * 1024) }
                if (text != null) onOpenText(e.name, text) else status = "binary or too large: ${e.name}"
            }
        }
    }

    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
        Surface(color = MaterialTheme.colorScheme.surface, tonalElevation = 2.dp) {
            Column(Modifier.padding(horizontal = 8.dp, vertical = 4.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { if (canGoBack) onBack() else onShortcut("/") }) {
                        Text(if (canGoBack) "\u2191" else "\u2302", fontSize = 16.sp)
                    }
                    Text(
                        path,
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.weight(1f)
                    )
                    TextButton(onClick = { refresh++ }) { Text("\u21BB", fontSize = 15.sp) }
                }
                Row(
                    Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    TextButton(onClick = { onShortcut("/sdcard/Download") }) { Text("Download", fontSize = 11.sp) }
                    TextButton(onClick = { onShortcut("/sdcard/Android/data") }) { Text("Android/data", fontSize = 11.sp) }
                    TextButton(onClick = { onShortcut("/sdcard/Documents") }) { Text("Documents", fontSize = 11.sp) }
                    TextButton(onClick = { onShortcut(context.filesDir.absolutePath) }) { Text("App", fontSize = 11.sp) }
                }
                Text(status, fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

        LazyColumn(Modifier.fillMaxSize()) {
            items(entries, key = { "${path}/${it.name}" }) { e ->
                Row(
                    Modifier.fillMaxWidth().clickable { open(e) }.padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(Util.glyph(e.name, e.isDir), fontSize = 14.sp)
                    Column(Modifier.weight(1f).padding(start = 8.dp)) {
                        Text(
                            e.name,
                            fontSize = 13.sp,
                            fontWeight = if (e.isDir) FontWeight.SemiBold else FontWeight.Normal,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        if (!e.isDir) Text(
                            Util.humanSize(e.size),
                            fontSize = 9.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    if (e.isDir) Text("\u203A", fontSize = 15.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f))
            }
        }
    }
}
