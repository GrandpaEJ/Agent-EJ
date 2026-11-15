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
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
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
import org.agentej.android.Util

private data class ZipRow(val name: String, val size: Long, val compressed: Boolean)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ApkScreen(apkPath: String, onBack: () -> Unit, onView: (String, String) -> Unit) {
    val scope = rememberCoroutineScope()
    val apk = remember(apkPath) { apkPath }
    val fileName = apk.substringAfterLast('/')
    val baseDir = apk.substringBeforeLast('/', "/sdcard")
    val baseName = fileName.substringBeforeLast('.')

    var tab by remember { mutableIntStateOf(0) }
    val rows = remember { mutableStateListOf<ZipRow>() }
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

    LaunchedEffect(apkPath) {
        val raw = withContext(Dispatchers.IO) { Native.zipEntries(apkPath)?.toList() ?: emptyList() }
        rows.clear()
        raw.forEach { line ->
            val p = line.split('\t')
            if (p.size >= 3) rows.add(ZipRow(p[0], p[1].toLongOrNull() ?: 0L, p[2] == "1"))
        }
        dexNames.clear()
        dexNames.addAll(Util.dexNames(raw))
        dexNames.firstOrNull()?.let { dexSel = it }
        status = "${rows.size} entries"
    }

    LaunchedEffect(tab) {
        when (tab) {
            1 -> if (manifest == null) {
                busy = true
                manifest = withContext(Dispatchers.IO) { Native.axmlDecode(apkPath, "AndroidManifest.xml") }
                busy = false
            }
            2 -> if (resStrings.isEmpty()) {
                busy = true
                val list = withContext(Dispatchers.IO) { Native.arscStrings(apkPath, 8000)?.toList() ?: emptyList() }
                resStrings.addAll(list)
                busy = false
            }
        }
    }

    LaunchedEffect(dexSel, tab) {
        if (tab == 3 && classes.isEmpty()) {
            busy = true
            val list = withContext(Dispatchers.IO) { Native.dexClasses(apkPath, dexSel)?.toList() ?: emptyList() }
            classes.addAll(list)
            busy = false
        }
    }

    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
        Surface(color = MaterialTheme.colorScheme.surface, tonalElevation = 2.dp) {
            Column(Modifier.fillMaxWidth()) {
                Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 2.dp)) {
                    TextButton(onClick = onBack) { Text("\u2190", fontSize = 16.sp) }
                    Column(Modifier.weight(1f).padding(top = 8.dp)) {
                        Text(fileName, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                            maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(status, fontSize = 9.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    TextButton(onClick = { task { Native.apkAnalyze(apkPath) } }) {
                        Text("analyze", fontSize = 11.sp)
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
                            val out = Util.join(baseDir, "${baseName}_signed.apk")
                            Util.parseRc(Native.signApk(apkPath, out)).toString()
                        }
                    }) { Text("Sign", fontSize = 11.sp) }
                    TextButton(enabled = !busy, onClick = {
                        task {
                            val dir = Util.join(baseDir, "${baseName}_unpacked")
                            val out = Util.join(baseDir, "${baseName}_repacked.apk")
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
                    TextButton(onClick = {
                        scope.launch {
                            val dump = withContext(Dispatchers.IO) { Native.dexDump(apkPath, dexSel) }
                            onView("$dexSel header", dump)
                        }
                    }) { Text("dex info", fontSize = 11.sp) }
                }
                TabRow(selectedTabIndex = tab, containerColor = MaterialTheme.colorScheme.surface) {
                    listOf("ZIP", "Manifest", "Res", "Dex").forEachIndexed { i, label ->
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
                    items(rows, key = { it.name }) { row ->
                        Row(
                            Modifier.fillMaxWidth().clickable {
                                task {
                                    when {
                                        row.name.endsWith(".xml", true) -> {
                                            val xml = Native.axmlDecode(apkPath, row.name)
                                            if (xml.isNotBlank()) {
                                                onView(row.name, xml)
                                                "decoded ${row.name}"
                                            } else "cannot decode ${row.name}"
                                        }
                                        else -> {
                                            val bytes = Native.readEntry(apkPath, row.name)
                                            val text = bytes?.let { Util.decodeText(it) }
                                            if (text != null) {
                                                onView(row.name, text)
                                                "opened ${row.name}"
                                            } else "binary entry: ${row.name}"
                                        }
                                    }
                                }
                            }.padding(horizontal = 12.dp, vertical = 6.dp)
                        ) {
                            Text(row.name, fontSize = 11.sp, fontFamily = FontFamily.Monospace,
                                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                            Text((if (row.compressed) "z " else "") + Util.humanSize(row.size),
                                fontSize = 9.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(start = 8.dp))
                        }
                    }
                }

                1 -> Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp)) {
                    Text(manifest ?: "decoding…", fontFamily = FontFamily.Monospace, fontSize = 11.sp,
                        lineHeight = 15.sp)
                }

                2 -> LazyColumn(Modifier.fillMaxSize()) {
                    itemsIndexed(resStrings) { i, s ->
                        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)) {
                            Text(i.toString(), fontSize = 9.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(end = 10.dp))
                            Text(s, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                        }
                    }
                }

                3 -> Column(Modifier.fillMaxSize()) {
                    Row(Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        dexNames.forEach { d ->
                            TextButton(onClick = { dexSel = d; classes.clear() }) {
                                Text(d, fontSize = 10.sp,
                                    color = if (d == dexSel) MaterialTheme.colorScheme.primary
                                    else MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    LazyColumn(Modifier.fillMaxSize()) {
                        itemsIndexed(classes) { i, cls ->
                            Text(cls, fontSize = 11.sp, fontFamily = FontFamily.Monospace,
                                maxLines = 1, overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.fillMaxWidth().clickable {
                                    scope.launch {
                                        val smali = withContext(Dispatchers.IO) { Native.dexSmali(apkPath, dexSel, i) }
                                        onView(cls, smali)
                                    }
                                }.padding(horizontal = 12.dp, vertical = 6.dp))
                        }
                    }
                }
            }
        }
    }
}
