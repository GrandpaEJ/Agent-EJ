package org.agentej.android.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.ArscText
import org.agentej.android.AxmlText
import org.agentej.android.TrRow
import java.util.Locale

private enum class Show(val label: String) { ALL("All"), MISSING("Untranslated"), EDITED("Edited") }

/** MT's Translation mode: every <string> of a resources.arsc, its default text and its text in one language. */
@Composable
fun TranslateScreen(archive: String, entry: String, onBack: () -> Unit) {
    val scope = rememberCoroutineScope()
    val snack = remember { SnackbarHostState() }
    /* after the first save into "<name>_edited.apk", later saves build on that copy */
    var src by remember { mutableStateOf(archive) }
    var qual by remember { mutableStateOf<String?>(null) }
    var locales by remember { mutableStateOf<List<String>>(emptyList()) }
    var rows by remember { mutableStateOf<List<TrRow>?>(null) }
    val edits = remember { mutableStateMapOf<Int, String>() }
    var show by remember { mutableStateOf(Show.ALL) }
    var query by remember { mutableStateOf("") }
    var editing by remember { mutableStateOf<TrRow?>(null) }
    var picking by remember { mutableStateOf(false) }
    var pendingQual by remember { mutableStateOf<String?>(null) }
    var confirmBack by remember { mutableStateOf(false) }
    var saving by remember { mutableStateOf(false) }

    /* first open: the device language when the table has it, else the first one */
    LaunchedEffect(src) {
        val l = withContext(Dispatchers.IO) { ArscText.locales(src, entry) }
        locales = l
        if (qual == null) {
            val dev = Locale.getDefault()
            qual = ArscText.qualOf(dev)?.takeIf { it in l } ?: "-${dev.language}".takeIf { it in l } ?: l.firstOrNull() ?: ""
        }
    }
    LaunchedEffect(src, qual) {
        val q = qual ?: return@LaunchedEffect
        rows = null
        rows = withContext(Dispatchers.IO) { ArscText.rows(src, entry, q) }
    }

    fun current(r: TrRow): String? = edits[r.id] ?: if (qual == "") r.def else r.value
    val all = rows.orEmpty()
    val shown = remember(rows, show, query, edits.size, qual) {
        val q = query.trim().lowercase(Locale.ROOT)
        all.filter { r ->
            when (show) {
                Show.ALL -> true
                Show.MISSING -> current(r) == null
                Show.EDITED -> r.id in edits
            } && (q.isEmpty() || q in r.key.lowercase(Locale.ROOT) || q in r.def.lowercase(Locale.ROOT) ||
                (current(r)?.lowercase(Locale.ROOT)?.contains(q) == true))
        }
    }
    val done = all.count { current(it) != null }

    fun save() = scope.launch {
        val q = qual ?: return@launch
        saving = true
        val target = AxmlText.saveTarget(src, entry)
        val err = withContext(Dispatchers.IO) { ArscText.save(src, entry, q, edits.toMap(), target) }
        saving = false
        if (err != null) { snack.showSnackbar(err); return@launch }
        val n = edits.size
        edits.clear()
        if (target != src) src = target
        else rows = withContext(Dispatchers.IO) { ArscText.rows(src, entry, q) }
        locales = withContext(Dispatchers.IO) { ArscText.locales(src, entry) }
        snack.showSnackbar("Saved $n string${if (n == 1) "" else "s"} into ${target.substringAfterLast('/')}${if (entry.isEmpty()) "" else " · signed"}")
    }

    BackHandler(enabled = edits.isNotEmpty()) { confirmBack = true }

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).safeDrawingPadding()) {
        Column(Modifier.fillMaxSize()) {
            val name = src.substringAfterLast('/')
            EjTopBar(
                "Translation · ${ArscText.label(qual ?: "")}",
                if (rows == null) name else "$name · $done/${all.size} translated" + if (edits.isEmpty()) "" else " · ${edits.size} unsaved",
                leading = { IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back") { if (edits.isEmpty()) onBack() else confirmBack = true } }
            ) {
                IconBtn(EjIcons.Translate, "Language") { picking = true }
                IconBtn(Icons.Default.Check, "Save", enabled = edits.isNotEmpty() && !saving, tint = MaterialTheme.colorScheme.primary) { save() }
            }
            TextField(
                query, { query = it }, singleLine = true,
                placeholder = { Text("Search key or text", fontSize = 14.sp) },
                colors = TextFieldDefaults.colors(
                    focusedContainerColor = MaterialTheme.colorScheme.surface, unfocusedContainerColor = MaterialTheme.colorScheme.surface,
                    focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent
                ),
                modifier = Modifier.fillMaxWidth()
            )
            Row(Modifier.padding(horizontal = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (s in Show.entries) {
                    val n = when (s) { Show.ALL -> all.size; Show.MISSING -> all.size - done; Show.EDITED -> edits.size }
                    FilterChip(show == s, { show = s }, label = { Text("${s.label} $n", fontSize = 13.sp) })
                }
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            when {
                rows == null -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
                all.isEmpty() -> EmptyState("No strings", "This resource table has no plain <string> resources.")
                shown.isEmpty() -> EmptyState("Nothing to show", if (query.isNotBlank()) "No string matches \"$query\"." else "No ${show.label.lowercase()} strings.")
                else -> LazyColumn(Modifier.fillMaxSize()) {
                    items(shown, key = { it.id }) { r -> TrRowItem(r, current(r), r.id in edits, qual == "") { editing = r } }
                }
            }
        }
        SnackbarHost(snack, Modifier.align(Alignment.BottomCenter))
        if (saving) Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.35f)).clickable(enabled = false) {}, contentAlignment = Alignment.Center) {
            CircularProgressIndicator()
        }
    }

    editing?.let { r ->
        EditDialog(r, current(r), qual ?: "", onDismiss = { editing = null }) { v ->
            val orig = if (qual == "") r.def else r.value
            if (v == orig) edits.remove(r.id) else edits[r.id] = v
            editing = null
        }
    }
    if (picking) LanguageDialog(locales, qual ?: "", onDismiss = { picking = false }) { q ->
        picking = false
        if (q == qual) return@LanguageDialog
        if (edits.isEmpty()) qual = q else pendingQual = q
    }
    pendingQual?.let { q ->
        AlertDialog(
            onDismissRequest = { pendingQual = null },
            title = { Text("Unsaved translations") },
            text = { Text("${edits.size} edit${if (edits.size == 1) "" else "s"} in ${ArscText.label(qual ?: "")} are not saved. Switching language drops them.") },
            confirmButton = { TextButton(onClick = { edits.clear(); qual = q; pendingQual = null }) { Text("Discard") } },
            dismissButton = { TextButton(onClick = { pendingQual = null }) { Text("Keep editing") } }
        )
    }
    if (confirmBack) AlertDialog(
        onDismissRequest = { confirmBack = false },
        title = { Text("Discard changes?") },
        text = { Text("${edits.size} unsaved edit${if (edits.size == 1) "" else "s"} will be lost.") },
        confirmButton = { TextButton(onClick = { confirmBack = false; onBack() }) { Text("Discard") } },
        dismissButton = { TextButton(onClick = { confirmBack = false }) { Text("Cancel") } }
    )
}

@Composable
private fun TrRowItem(r: TrRow, value: String?, edited: Boolean, isDefault: Boolean, onClick: () -> Unit) {
    val cs = MaterialTheme.colorScheme
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick)) {
        Box(Modifier.width(3.dp).heightIn(min = 56.dp).fillMaxHeight().background(if (edited) cs.primary else Color.Transparent))
        Column(Modifier.weight(1f).padding(start = 13.dp, end = 16.dp, top = 8.dp, bottom = 8.dp)) {
            Text(r.key, fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = cs.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (!isDefault) Text(r.def, fontSize = 13.sp, color = cs.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
            if (value == null) Text("Not translated", fontSize = 13.sp, fontStyle = FontStyle.Italic, color = Ej.colors.warning)
            else Text(value.ifEmpty { "(empty)" }, fontSize = 15.sp, color = cs.onSurface, maxLines = 3, overflow = TextOverflow.Ellipsis)
        }
    }
    HorizontalDivider(color = cs.outlineVariant.copy(alpha = 0.5f))
}

@Composable
private fun EditDialog(r: TrRow, value: String?, qual: String, onDismiss: () -> Unit, onOk: (String) -> Unit) {
    var v by remember(r.id) { mutableStateOf(value ?: "") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(r.key, fontSize = 15.sp, fontFamily = FontFamily.Monospace, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (qual.isNotEmpty()) {
                    Text("Default", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    SelectionContainer { Text(r.def.ifEmpty { "(empty)" }, fontSize = 14.sp, maxLines = 6, overflow = TextOverflow.Ellipsis) }
                }
                OutlinedTextField(
                    v, { v = it }, label = { Text(ArscText.label(qual)) }, minLines = 2, maxLines = 8,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = { TextButton(onClick = { onOk(v) }) { Text("OK") } },
        dismissButton = {
            Row {
                if (qual.isNotEmpty()) TextButton(onClick = { v = r.def }) { Text("Use default") }
                TextButton(onClick = onDismiss) { Text("Cancel") }
            }
        }
    )
}

/** The table's languages, then every other language the device knows ("Add language"). */
@Composable
private fun LanguageDialog(have: List<String>, current: String, onDismiss: () -> Unit, onPick: (String) -> Unit) {
    var adding by remember { mutableStateOf(false) }
    var filter by remember { mutableStateOf("") }
    val options = remember(have, adding) {
        if (adding) ArscText.addable(have)
        else (listOf("") + have).map { it to ArscText.label(it) }.sortedWith(compareBy({ it.first.isNotEmpty() }, { it.second.lowercase() }))
    }
    val f = filter.trim().lowercase(Locale.ROOT)
    val shown = options.filter { f.isEmpty() || f in it.second.lowercase(Locale.ROOT) || f in it.first.lowercase(Locale.ROOT) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (adding) "Add language" else "Language") },
        text = {
            Column {
                OutlinedTextField(filter, { filter = it }, singleLine = true, placeholder = { Text("Filter") }, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.heightIn(min = 6.dp))
                LazyColumn(Modifier.heightIn(max = 380.dp)) {
                    items(shown, key = { it.first }) { (q, label) ->
                        Row(
                            Modifier.fillMaxWidth().clickable { onPick(q) }.padding(vertical = 11.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(label, fontSize = 15.sp, modifier = Modifier.weight(1f),
                                color = if (q == current && !adding) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface)
                            Text(q.removePrefix("-").ifEmpty { "values" }, fontSize = 12.sp, fontFamily = FontFamily.Monospace,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
        dismissButton = { if (!adding) TextButton(onClick = { adding = true; filter = "" }) { Text("Add language") } }
    )
}
