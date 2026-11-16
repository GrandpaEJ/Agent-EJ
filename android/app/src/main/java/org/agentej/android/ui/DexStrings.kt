package org.agentej.android.ui

import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import java.util.Locale

/** One distinct string of the open dex pools; [dex] lists the dex indices that hold it. */
internal class PoolString(val value: String, val dex: List<Int>)

private const val KIND_STRING = 4
private const val USE_LIMIT = 50_000

/** Escapes like the C renderer (baksmali): 0x20..0x7e literal, the rest \\uXXXX. */
internal fun smaliEscape(raw: String): String = buildString(raw.length + 8) {
    for (ch in raw) when (ch) {
        '\b' -> append("\\b"); '\t' -> append("\\t"); '\n' -> append("\\n")
        '\u000c' -> append("\\f"); '\r' -> append("\\r")
        '"', '\'', '\\' -> append('\\').append(ch)
        else -> if (ch.code in 0x20..0x7e) append(ch) else append("\\u%04x".format(ch.code))
    }
}

/** Every distinct pool string over the editor's dex, sorted; blocking. */
internal fun loadPool(state: DexEditorState): List<PoolString> {
    val map = HashMap<String, MutableList<Int>>()
    state.src.entries.forEachIndexed { dex, entry ->
        Native.dexStrings(state.src.archive, entry)?.forEach { map.getOrPut(it) { ArrayList(1) }.add(dex) }
    }
    return map.entries.map { PoolString(it.key, it.value) }.sortedBy { it.value }
}

/** Lines that use [value] as a whole literal (not as part of a longer one); blocking. */
internal fun stringUses(state: DexEditorState, value: String, progress: (String) -> Unit): SearchResult {
    val spec = SearchSpec(value, KIND_STRING, case = true)
    val quoted = "\"" + smaliEscape(value) + "\""
    val byDex = state.classes.orEmpty().groupBy { it.dex }.mapValues { (_, l) -> l.associateBy { it.index } }
    val hits = ArrayList<SearchHit>()
    for ((dex, entry) in state.src.entries.withIndex()) {
        progress("Finding uses in ${state.src.label(dex)} (${dex + 1}/${state.src.entries.size})…")
        val rows = Native.dexFind(state.src.archive, entry, KIND_STRING, value, spec.flags, "", USE_LIMIT) ?: continue
        for (r in rows) {
            val p = r.split('\t', limit = 3)
            val text = p.getOrElse(2) { "" }
            if (quoted !in text) continue
            val c = byDex[dex]?.get(p[0].toInt()) ?: continue
            hits += SearchHit(c, p[1].toInt(), text.trim())
        }
    }
    return SearchResult(spec, hits, null)
}

/**
 * Replaces the literal [old] with [new] in every class that uses it; the rewritten classes join
 * the editor's edits (compiled by the hammer). Returns the number of classes changed; blocking.
 */
internal fun replaceString(state: DexEditorState, old: String, new: String, progress: (String) -> Unit): Int {
    val from = "\"" + smaliEscape(old) + "\""
    val to = "\"" + smaliEscape(new) + "\""
    val classes = stringUses(state, old, progress).hits.map { it.cls }.distinct()
    var changed = 0
    classes.forEachIndexed { i, c ->
        progress("Replacing in ${c.simple} (${i + 1}/${classes.size})…")
        val body = state.edits[c] ?: ("# ${state.src.label(c.dex)}\n\n" + state.renamed(state.smali(c)))
        val out = body.replace(from, to)
        if (out != body) {
            state.edits[c] = out
            changed++
        }
    }
    if (changed > 0) state.editGen++
    return changed
}

/** MT "Strings" tab: the raw string pool with a filter; tap a string for its actions. */
@Composable
internal fun DexStringsPane(
    state: DexEditorState,
    busy: String?,
    onUses: (String) -> Unit,
    onReplace: (String, String) -> Unit,
    onCopy: (String) -> Unit
) {
    LaunchedEffect(state) {
        if (state.pool == null) state.pool = withContext(Dispatchers.IO) { loadPool(state) }
    }
    val pool = state.pool
    val filter = state.poolFilter
    val shown = remember(pool, filter) {
        val q = filter.lowercase(Locale.US)
        if (q.isEmpty()) pool else pool?.filter { q in it.value.lowercase(Locale.US) }
    }
    var picked by remember { mutableStateOf<PoolString?>(null) }
    var replacing by remember { mutableStateOf<PoolString?>(null) }

    Column(Modifier.fillMaxSize()) {
        TextField(
            value = filter, onValueChange = { state.poolFilter = it }, singleLine = true,
            placeholder = { Text("Filter strings", fontSize = 14.sp) },
            colors = TextFieldDefaults.colors(
                focusedContainerColor = MaterialTheme.colorScheme.surface,
                unfocusedContainerColor = MaterialTheme.colorScheme.surface,
                focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent
            ),
            modifier = Modifier.fillMaxWidth()
        )
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        when {
            pool == null -> EmptyState("Reading string pool…")
            shown.isNullOrEmpty() -> EmptyState(if (filter.isEmpty()) "No strings" else "No match", if (filter.isEmpty()) "" else "Nothing in the pool contains \"$filter\".")
            else -> {
                Text(
                    if (filter.isEmpty()) "${pool.size} strings" else "${shown.size} of ${pool.size} strings",
                    fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp)
                )
                LazyColumn(Modifier.fillMaxSize(), state = state.poolList) {
                    itemsIndexed(shown) { _, s -> StringRow(state, s, onClick = { if (busy == null) picked = s }, onLong = { onCopy(s.value) }) }
                }
            }
        }
    }

    picked?.let { s ->
        AlertDialog(
            onDismissRequest = { picked = null },
            title = { Text("String", fontSize = 16.sp) },
            text = {
                Text(smaliEscape(s.value).ifEmpty { "(empty)" }, fontFamily = FontFamily.Monospace, fontSize = 13.sp, maxLines = 8, overflow = TextOverflow.Ellipsis)
            },
            confirmButton = {
                Row(horizontalArrangement = Arrangement.spacedBy(0.dp)) {
                    TextButton(onClick = { picked = null; onCopy(s.value) }) { Text("Copy") }
                    TextButton(onClick = { picked = null; replacing = s }) { Text("Replace") }
                    TextButton(onClick = { picked = null; onUses(s.value) }) { Text("Find uses") }
                }
            }
        )
    }
    replacing?.let { s ->
        var to by remember(s) { mutableStateOf(s.value) }
        AlertDialog(
            onDismissRequest = { replacing = null },
            title = { Text("Replace string") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("Every class using this literal is rewritten and queued for the hammer.",
                        fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    OutlinedTextField(to, { to = it }, label = { Text("New value") }, modifier = Modifier.fillMaxWidth())
                }
            },
            confirmButton = {
                TextButton(enabled = to != s.value, onClick = { replacing = null; onReplace(s.value, to) }) { Text("Replace") }
            },
            dismissButton = { TextButton(onClick = { replacing = null }) { Text("Cancel") } }
        )
    }
}

@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
private fun StringRow(state: DexEditorState, s: PoolString, onClick: () -> Unit, onLong: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 36.dp).combinedClickable(onClick = onClick, onLongClick = onLong)
            .padding(horizontal = 16.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            smaliEscape(s.value).ifEmpty { "\"\"" }, fontFamily = FontFamily.Monospace, fontSize = 13.sp,
            color = MaterialTheme.colorScheme.onSurface, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f)
        )
        if (state.src.entries.size > 1) Text(
            s.dex.joinToString(",") { state.src.label(it).removePrefix("classes").removeSuffix(".dex").ifEmpty { "1" } },
            fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(start = 8.dp)
        )
    }
}
