package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import org.agentej.android.Outputs
import org.agentej.android.Util
import org.agentej.android.Workspace

private data class ZipRow(val name: String, val size: Long, val compressed: Boolean)

private val TABS = listOf("Info", "ZIP", "Manifest", "Res", "Dex")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ApkScreen(apkPath: String, onBack: () -> Unit, onView: (String, String) -> Unit) {
    val scope = rememberCoroutineScope()
    val fileName = apkPath.substringAfterLast('/')
    // next to the APK, or the workspace output folder when that is read-only (installed apps)
    val baseDir = remember(apkPath) {
        apkPath.substringBeforeLast('/', "/sdcard").takeIf { java.io.File(it).canWrite() } ?: Workspace.output
    }
    val baseName = fileName.substringBeforeLast('.')

    var tab by rememberSaveable { mutableIntStateOf(0) }
    val rows = remember { mutableStateListOf<ZipRow>() }
    val info = remember { mutableStateListOf<Pair<String, String>>() }
    val resStrings = remember { mutableStateListOf<String>() }
    val dexNames = remember { mutableStateListOf<String>() }
    val classes = remember { mutableStateListOf<String>() }
    var dexSel by remember { mutableStateOf("classes.dex") }
    var manifest by remember { mutableStateOf<String?>(null) }
    var status by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }

    fun task(block: suspend () -> String) {
        scope.launch {
            busy = true
            status = withContext(Dispatchers.IO) { block() }
            busy = false
        }
    }

    fun openEntry(name: String) {
        task {
            when {
                name.endsWith(".xml", true) -> {
                    val xml = Native.axmlDecode(apkPath, name)
                    if (xml.isNotBlank()) {
                        onView(name, xml)
                        "decoded $name"
                    } else {
                        "cannot decode $name"
                    }
                }
                else -> {
                    val text = Native.readEntry(apkPath, name)?.let { Util.decodeText(it) }
                    if (text != null) {
                        onView(name, text)
                        "opened $name"
                    } else {
                        "binary entry: $name"
                    }
                }
            }
        }
    }

    LaunchedEffect(apkPath) {
        busy = true
        val (raw, facts) = withContext(Dispatchers.IO) {
            (Native.zipEntries(apkPath)?.toList() ?: emptyList()) to
                (Native.apkInfo(apkPath)?.toList() ?: emptyList())
        }
        rows.clear()
        raw.forEach { line ->
            val p = line.split('\t')
            if (p.size >= 3) rows.add(ZipRow(p[0], p[1].toLongOrNull() ?: 0L, p[2] == "1"))
        }
        info.clear()
        facts.forEach { line ->
            val parts = line.split('\t', limit = 2)
            if (parts.size == 2) info.add(parts[0] to parts[1])
        }
        dexNames.clear()
        dexNames.addAll(Util.dexNames(raw))
        dexNames.firstOrNull()?.let { dexSel = it }
        status = if (info.isNotEmpty()) "pick a tab" else "${rows.size} entries"
        busy = false
    }

    LaunchedEffect(tab) {
        when (tab) {
            2 -> if (manifest == null) {
                busy = true
                manifest = withContext(Dispatchers.IO) { Native.axmlDecode(apkPath, "AndroidManifest.xml") }
                busy = false
            }
            3 -> if (resStrings.isEmpty()) {
                busy = true
                val list = withContext(Dispatchers.IO) { Native.arscStrings(apkPath, 8000)?.toList() ?: emptyList() }
                resStrings.addAll(list)
                busy = false
            }
        }
    }

    LaunchedEffect(dexSel, tab) {
        if (tab == 4 && classes.isEmpty()) {
            busy = true
            val list = withContext(Dispatchers.IO) { Native.dexClasses(apkPath, dexSel)?.toList() ?: emptyList() }
            classes.addAll(list)
            busy = false
        }
    }

    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surface).safeDrawingPadding()) {
        Surface(color = MaterialTheme.colorScheme.surface) {
            Column(Modifier.fillMaxWidth()) {
                EjTopBar(
                    title = fileName,
                    subtitle = status,
                    leading = { IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back", onClick = onBack) }
                ) {
                    TextButton(enabled = !busy, onClick = { task { Native.apkAnalyze(apkPath) } }) {
                        Text("ANALYZE", fontSize = 12.sp)
                    }
                }
                Row(
                    Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    TextButton(enabled = !busy, onClick = {
                        task {
                            val dir = Util.join(baseDir, "${baseName}_unpacked")
                            val rc = Native.extractAll(apkPath, dir)
                            if (rc >= 0) "extracted $rc entries → ${baseName}_unpacked" else "extract failed rc=$rc"
                        }
                    }) { Text("Extract", fontSize = 11.sp) }
                    TextButton(enabled = !busy, onClick = {
                        task {
                            val out = Outputs.beside(apkPath, "${baseName}_signed.apk")
                            Util.parseRc(Native.signApk(apkPath, out)).toString()
                        }
                    }) { Text("Sign", fontSize = 11.sp) }
                    TextButton(enabled = !busy, onClick = {
                        task {
                            val dir = Util.join(baseDir, "${baseName}_unpacked")
                            val out = Outputs.beside(apkPath, "${baseName}_repacked.apk")
                            val rc = Native.packDir(dir, out, true)
                            if (rc == 0) "repacked + signed → ${baseName}_repacked.apk" else "repack failed rc=$rc"
                        }
                    }) { Text("Repack", fontSize = 11.sp) }
                    TextButton(enabled = !busy, onClick = {
                        task {
                            val toml = Util.join(baseDir, "$baseName.res.toml")
                            Util.parseRc(Native.arscDumpToml(apkPath, toml)).toString()
                        }
                    }) { Text("arsc.toml", fontSize = 11.sp) }
                }
                TabRow(selectedTabIndex = tab, containerColor = MaterialTheme.colorScheme.surface) {
                    TABS.forEachIndexed { i, label ->
                        Tab(selected = tab == i, onClick = { tab = i }, text = { Text(label, fontSize = 11.sp) })
                    }
                }
            }
        }

        if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
        else HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

        Column(Modifier.weight(1f).fillMaxWidth()) {
            when (tab) {
                0 -> LazyColumn(Modifier.fillMaxSize()) {
                    items(info) { (k, v) ->
                        FactRow(k, if (k == "Size") Util.compactSize(v.toLongOrNull() ?: 0L) else v)
                    }
                    if (info.isEmpty()) item {
                        Text(
                            "reading…",
                            fontSize = 11.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(16.dp)
                        )
                    }
                }

                1 -> LazyColumn(Modifier.fillMaxSize()) {
                    items(rows, key = { it.name }) { row ->
                        Row(
                            Modifier.fillMaxWidth().height(38.dp)
                                .clickable { openEntry(row.name) }
                                .padding(horizontal = 12.dp),
                            verticalAlignment = androidx.compose.ui.Alignment.CenterVertically
                        ) {
                            Text(
                                row.name,
                                fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f)
                            )
                            Text(
                                (if (row.compressed) "z " else "") + Util.compactSize(row.size),
                                fontSize = 9.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(start = 8.dp)
                            )
                        }
                    }
                }

                2 -> manifest.let { m ->
                    if (m == null) EmptyState("Decoding manifest…")
                    else if (m.isBlank()) EmptyState("No readable AndroidManifest.xml", "The entry is missing or not binary XML.")
                    else CodeView(remember(m) { m.split('\n') }, Lang.XML, Modifier.fillMaxSize())
                }

                3 -> LazyColumn(Modifier.fillMaxSize()) {
                    itemsIndexed(resStrings) { i, s ->
                        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)) {
                            Text(
                                i.toString(),
                                fontSize = 9.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(end = 10.dp)
                            )
                            Text(s, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                        }
                    }
                }

                4 -> Column(Modifier.fillMaxSize()) {
                    Row(
                        Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        dexNames.forEach { d ->
                            TextButton(onClick = {
                                dexSel = d
                                classes.clear()
                            }) {
                                Text(
                                    d,
                                    fontSize = 10.sp,
                                    color = if (d == dexSel) MaterialTheme.colorScheme.primary
                                    else MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    LazyColumn(Modifier.fillMaxSize()) {
                        itemsIndexed(classes) { i, cls ->
                            Text(
                                cls,
                                fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.fillMaxWidth().clickable {
                                    scope.launch {
                                        val smali = withContext(Dispatchers.IO) { Native.dexSmali(apkPath, dexSel, i) }
                                        onView(cls, smali)
                                    }
                                }.padding(horizontal = 12.dp, vertical = 6.dp)
                            )
                        }
                    }
                }
            }
        }
    }
}
