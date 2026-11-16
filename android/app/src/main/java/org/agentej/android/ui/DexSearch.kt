package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.Native
import java.util.Locale

/** Search types, in dex_find.h order (DEX_FIND_SMALI = 0 ...). */
val SEARCH_KINDS = listOf("Smali", "Class name", "Field name", "Method name", "String", "Integer")
private const val KIND_CLASS = 1
private const val KIND_INTEGER = 5
private const val HIT_LIMIT = 1000

data class SearchSpec(
    val query: String, val kind: Int = 0, val path: String = "/",
    val deep: Boolean = true, val case: Boolean = false, val regex: Boolean = false
) {
    /** "/" or "com.a" or "/com/a/" -> "com/a" for dex_find's scope. */
    val scope get() = path.trim().trim('/').replace('.', '/')
    val flags get() = (if (case) 1 else 0) or (if (regex) 2 else 0) or (if (deep) 0 else 4)
}

internal class SearchHit(val cls: DexCls, val line: Int, val text: String)

internal class SearchResult(val spec: SearchSpec, val hits: List<SearchHit>, val error: String?)

/** Runs [spec] over every dex of the editor, one dex at a time; blocking. */
internal fun runSearch(state: DexEditorState, spec: SearchSpec, progress: (String) -> Unit): SearchResult {
    val byDex = state.classes.orEmpty().groupBy { it.dex }.mapValues { (_, l) -> l.associateBy { it.index } }
    val hits = ArrayList<SearchHit>()
    for ((dex, entry) in state.src.entries.withIndex()) {
        if (hits.size >= HIT_LIMIT) break
        progress("Searching ${state.src.label(dex)} (${dex + 1}/${state.src.entries.size})…")
        val rows = Native.dexFind(state.src.archive, entry, spec.kind, spec.query, spec.flags, spec.scope, HIT_LIMIT - hits.size)
            ?: return SearchResult(spec, emptyList(),
                if (spec.kind == KIND_INTEGER) "Not a number: ${spec.query}" else "Invalid regular expression")
        for (r in rows) {
            val p = r.split('\t', limit = 3)
            val c = byDex[dex]?.get(p[0].toInt()) ?: continue
            hits += SearchHit(c, p[1].toInt(), p.getOrElse(2) { "" }.trim())
        }
    }
    return SearchResult(spec, hits, null)
}

/** MT-style search dialog: text, package path, type, subdirectories / case / regex. */
@Composable
fun DexSearchDialog(initial: SearchSpec, onSearch: (SearchSpec) -> Unit, onDismiss: () -> Unit) {
    var spec by remember { mutableStateOf(initial) }
    var kindOpen by remember { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Search") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                OutlinedTextField(spec.query, { spec = spec.copy(query = it) }, singleLine = true,
                    label = { Text("Text to find") }, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(spec.path, { spec = spec.copy(path = it) }, singleLine = true,
                    label = { Text("Path") }, placeholder = { Text("/ or com.example") }, modifier = Modifier.fillMaxWidth())
                Box {
                    Row(
                        Modifier.fillMaxWidth().heightIn(min = 44.dp).clickable { kindOpen = true },
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Search type", color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Spacer(Modifier.width(12.dp))
                        Text(SEARCH_KINDS[spec.kind], Modifier.weight(1f))
                        Icon(Icons.Filled.ArrowDropDown, null)
                    }
                    DropdownMenu(kindOpen, onDismissRequest = { kindOpen = false }) {
                        SEARCH_KINDS.forEachIndexed { i, k ->
                            DropdownMenuItem(text = { Text(k) }, onClick = { spec = spec.copy(kind = i); kindOpen = false })
                        }
                    }
                }
                Check("Search subdirectories", spec.deep) { spec = spec.copy(deep = it) }
                Check("Match case", spec.case, enabled = spec.kind != KIND_INTEGER) { spec = spec.copy(case = it) }
                Check("Regex", spec.regex, enabled = spec.kind != KIND_INTEGER) { spec = spec.copy(regex = it) }
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
        confirmButton = {
            TextButton(enabled = spec.query.isNotBlank(), onClick = { onSearch(spec.copy(query = spec.query.trim())) }) { Text("OK") }
        }
    )
}

@Composable
private fun Check(label: String, on: Boolean, enabled: Boolean = true, set: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable(enabled = enabled) { set(!on) },
        verticalAlignment = Alignment.CenterVertically
    ) {
        Checkbox(checked = on && enabled, onCheckedChange = null, enabled = enabled)
        Spacer(Modifier.width(10.dp))
        Text(label, color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

private sealed interface SearchRow {
    class Pkg(val name: String) : SearchRow
    class Cls(val c: DexCls) : SearchRow
    class Hit(val h: SearchHit) : SearchRow
}

/** Search tab: functions on top, then hits grouped package -> class -> line. */
@Composable
internal fun DexSearchPane(state: DexEditorState, busy: String?, onNew: () -> Unit, onOpen: (SearchHit) -> Unit) {
    val res = state.search
    val rows = remember(res) {
        val out = ArrayList<SearchRow>()
        res?.hits?.groupBy { it.cls.pkg }?.toSortedMap()?.forEach { (pkg, inPkg) ->
            out += SearchRow.Pkg(pkg.ifEmpty { "(default package)" })
            inPkg.groupBy { it.cls }.forEach { (c, hs) ->
                out += SearchRow.Cls(c)
                if (res.spec.kind != KIND_CLASS) hs.forEach { out += SearchRow.Hit(it) }
            }
        }
        out
    }
    LazyColumn(Modifier.fillMaxSize(), state = state.searchList) {
        item { SectionLabel("Function") }
        item { FnRow(Icons.Filled.Search, "Start a new search", enabled = busy == null, onClick = onNew) }
        if (res != null) item { FnRow(Icons.Filled.Close, "Clear search results", enabled = busy == null) { state.search = null } }
        if (busy != null) item {
            Text(busy, fontSize = 13.sp, color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(16.dp))
        }
        if (res != null && busy == null) {
            item {
                val more = if (res.hits.size >= HIT_LIMIT) "+" else ""
                SectionLabel(res.error ?: "search results (${res.hits.size}$more) - ${res.spec.query}")
            }
            if (res.error == null && res.hits.isEmpty()) item {
                Text("Nothing found.", color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(16.dp))
            }
            items(rows) { r ->
                when (r) {
                    is SearchRow.Pkg -> Row(Modifier.fillMaxWidth().height(40.dp).padding(start = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(24.dp).clip(androidx.compose.foundation.shape.RoundedCornerShape(6.dp)).background(Ej.colors.tileDir), contentAlignment = Alignment.Center) {
                            Icon(EjIcons.Folder, null, tint = androidx.compose.ui.graphics.Color.White, modifier = Modifier.size(15.dp))
                        }
                        Spacer(Modifier.width(10.dp))
                        Text(r.name, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onBackground)
                    }
                    is SearchRow.Cls -> Row(
                        Modifier.fillMaxWidth().heightIn(min = 40.dp)
                            .clickable { onOpen(SearchHit(r.c, 0, "")) }.padding(start = 28.dp, end = 12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(Modifier.size(22.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primaryContainer), contentAlignment = Alignment.Center) {
                            Text("C", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary)
                        }
                        Spacer(Modifier.width(10.dp))
                        Text(r.c.simple, fontSize = 14.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onBackground)
                    }
                    is SearchRow.Hit -> Text(
                        highlight(r.h.text, res.spec),
                        fontFamily = FontFamily.Monospace, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.fillMaxWidth().clickable { onOpen(r.h) }
                            .padding(start = 60.dp, end = 12.dp, top = 8.dp, bottom = 8.dp)
                    )
                }
            }
        }
    }
}

/** Where the query sits in a hit line, for highlighting (null for regex/integer). */
fun hitRange(text: String, spec: SearchSpec): IntRange? {
    if (spec.regex || spec.kind == KIND_INTEGER || spec.query.isEmpty()) return null
    val i = if (spec.case) text.indexOf(spec.query) else text.lowercase(Locale.US).indexOf(spec.query.lowercase(Locale.US))
    return if (i < 0) null else i until i + spec.query.length
}

@Composable
private fun highlight(text: String, spec: SearchSpec): AnnotatedString {
    val r = hitRange(text, spec) ?: return AnnotatedString(text)
    val hl = SpanStyle(background = MaterialTheme.colorScheme.primary, color = MaterialTheme.colorScheme.onPrimary)
    /* keep the hit visible when the line is long: start a little before it */
    val from = (r.first - 24).coerceAtLeast(0)
    return buildAnnotatedString {
        if (from > 0) append("…")
        append(text.substring(from, r.first))
        pushStyle(hl); append(text.substring(r.first, r.last + 1)); pop()
        append(text.substring(r.last + 1))
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis,
        modifier = Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surfaceContainer).padding(horizontal = 12.dp, vertical = 8.dp)
    )
}

@Composable
private fun FnRow(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, enabled: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(48.dp).clickable(enabled = enabled, onClick = onClick).padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(icon, null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(22.dp))
        Spacer(Modifier.width(14.dp))
        Text(label, fontSize = 15.sp, color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
