package org.agentej.android.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
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
import org.agentej.android.ArscDict
import org.agentej.android.ArscText
import org.agentej.android.AxmlText
import org.agentej.android.TrRow
import java.util.Locale

private enum class Show(val label: String) { ALL("All"), MISSING("Untranslated"), EDITED("Edited") }

private fun missingLabel(type: String) = if (type == "string") "Untranslated" else "Not set"

/** MT's Translation mode: every <string> of a resources.arsc, its default text and its text in one language. */
@Composable
fun TranslateScreen(archive: String, entry: String, onBack: () -> Unit) {
    val scope = rememberCoroutineScope()
    val snack = remember { SnackbarHostState() }
    /* saves go into "<name>_edited.apk" and build on it when it already exists */
    var src by remember { mutableStateOf(AxmlText.saveBase(archive, entry)) }
    var type by remember { mutableStateOf("string") }
    var pendingType by remember { mutableStateOf<String?>(null) }
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
    val selected = remember { mutableStateMapOf<Int, Boolean>() }
    var menuOpen by remember { mutableStateOf(false) }
    var importPath by remember { mutableStateOf<String?>(null) }

    /* first open: the device language when the table has it, else the first one */
    /* other value types open on their default values: that is what one usually changes */
    LaunchedEffect(src, type) {
        val l = withContext(Dispatchers.IO) { ArscText.locales(src, entry, type) }
        locales = l
        if (qual == null) {
            val dev = Locale.getDefault()
            qual = if (type != "string") ""
            else ArscText.qualOf(dev)?.takeIf { it in l } ?: "-${dev.language}".takeIf { it in l } ?: l.firstOrNull() ?: ""
        }
    }
    LaunchedEffect(src, qual, type) {
        val q = qual ?: return@LaunchedEffect
        rows = null
        rows = withContext(Dispatchers.IO) { ArscText.rows(src, entry, q, type) }
    }

    fun switchType(t: String) {
        if (t == type) return
        rows = null; qual = null; type = t; selected.clear()
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
        else rows = withContext(Dispatchers.IO) { ArscText.rows(src, entry, q, type) }
        locales = withContext(Dispatchers.IO) { ArscText.locales(src, entry, type) }
        snack.showSnackbar("Saved $n value${if (n == 1) "" else "s"} into ${target.substringAfterLast('/')}${if (entry.isEmpty()) "" else " · signed"}")
    }

    /** Sets [r]'s text, or drops the edit when it is back to the stored text. */
    fun setText(r: TrRow, v: String) {
        val orig = if (qual == "") r.def else r.value
        if (v == orig) edits.remove(r.id) else edits[r.id] = v
    }

    fun fillMissing() {
        val missing = all.filter { current(it) == null }
        missing.forEach { setText(it, it.def) }
        scope.launch { snack.showSnackbar(if (missing.isEmpty()) "Nothing untranslated" else "Filled ${missing.size} with the default text") }
    }

    fun export() = scope.launch {
        val q = qual ?: return@launch
        val path = ArscDict.pathFor(archive, q, type)
        val err = withContext(Dispatchers.IO) { ArscDict.export(path, all.map { it to current(it) }, type) }
        snack.showSnackbar(err ?: "Exported ${all.size} strings to ${path.substringAfterLast('/')}")
    }

    fun import(path: String) = scope.launch {
        val map = withContext(Dispatchers.IO) { ArscDict.read(path, type) }.getOrElse {
            snack.showSnackbar("Cannot read ${path.substringAfterLast('/')}: ${it.message}"); return@launch
        }
        val byKey = all.associateBy { it.key }
        var n = 0
        var bad = 0
        for ((k, v) in map) byKey[k]?.let { r ->
            if (ArscText.invalid(type, v) != null) bad++
            else if (current(r) != v) { setText(r, v); n++ }
        }
        val unknown = map.keys.count { it !in byKey }
        snack.showSnackbar("Imported $n change${if (n == 1) "" else "s"}" +
            (if (unknown > 0) " · $unknown unknown keys skipped" else "") + (if (bad > 0) " · $bad invalid values skipped" else ""))
    }

    BackHandler(enabled = selected.isNotEmpty()) { selected.clear() }
    BackHandler(enabled = selected.isEmpty() && edits.isNotEmpty()) { confirmBack = true }

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).safeDrawingPadding()) {
        Column(Modifier.fillMaxSize()) {
            val name = src.substringAfterLast('/')
            if (selected.isNotEmpty()) EjTopBar(
                "${selected.size} selected", "",
                leading = { IconBtn(Icons.Default.Close, "Clear selection") { selected.clear() } }
            ) {
                IconBtn(EjIcons.SelectAll, "Select all shown") { shown.forEach { selected[it.id] = true } }
                if (qual != "") TextButton(onClick = {
                    all.filter { it.id in selected }.forEach { setText(it, it.def) }; selected.clear()
                }) { Text("Use default") }
                TextButton(onClick = { selected.keys.forEach { edits.remove(it) }; selected.clear() }) { Text("Revert") }
            } else EjTopBar(
                (if (type == "string") "Translation" else ArscText.TYPES.first { it.first == type }.second) + " · ${ArscText.label(qual ?: "")}",
                if (rows == null) name else "$name · $done/${all.size} ${if (type == "string") "translated" else "set"}" +
                    if (edits.isEmpty()) "" else " · ${edits.size} unsaved",
                leading = { IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back") { if (edits.isEmpty()) onBack() else confirmBack = true } }
            ) {
                IconBtn(EjIcons.Translate, "Language") { picking = true }
                IconBtn(Icons.Default.Check, "Save", enabled = edits.isNotEmpty() && !saving, tint = MaterialTheme.colorScheme.primary) { save() }
                Box {
                    IconBtn(Icons.Default.MoreVert, "More") { menuOpen = true }
                    DropdownMenu(menuOpen, onDismissRequest = { menuOpen = false }) {
                        if (qual != "") DropdownMenuItem(text = { Text("Fill ${missingLabel(type).lowercase()} with default") }, enabled = rows != null,
                            onClick = { menuOpen = false; fillMissing() })
                        DropdownMenuItem(text = { Text("Export ${type}s.xml") }, enabled = rows != null, onClick = { menuOpen = false; export() })
                        DropdownMenuItem(text = { Text("Import ${type}s.xml") }, enabled = rows != null, onClick = {
                            menuOpen = false; importPath = ArscDict.pathFor(archive, qual ?: "", type)
                        })
                    }
                }
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
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((t, label) in ArscText.TYPES)
                    FilterChip(type == t, { if (edits.isEmpty()) switchType(t) else if (t != type) pendingType = t }, label = { Text(label, fontSize = 13.sp) })
            }
            Row(Modifier.padding(horizontal = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (s in Show.entries) {
                    val n = when (s) { Show.ALL -> all.size; Show.MISSING -> all.size - done; Show.EDITED -> edits.size }
                    val l = if (s == Show.MISSING) missingLabel(type) else s.label
                    FilterChip(show == s, { show = s }, label = { Text("$l $n", fontSize = 13.sp) })
                }
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            when {
                rows == null -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
                all.isEmpty() -> EmptyState("Nothing here", "This resource table has no plain <$type> values.")
                shown.isEmpty() -> EmptyState("Nothing to show", if (query.isNotBlank()) "No string matches \"$query\"." else "No ${show.label.lowercase()} strings.")
                else -> LazyColumn(Modifier.fillMaxSize()) {
                    items(shown, key = { it.id }) { r ->
                        TrRowItem(r, current(r), r.id in edits, qual == "", r.id in selected, type,
                            onClick = {
                                if (selected.isEmpty()) editing = r
                                else if (r.id in selected) selected.remove(r.id) else selected[r.id] = true
                            },
                            onLongClick = { selected[r.id] = true })
                    }
                }
            }
        }
        SnackbarHost(snack, Modifier.align(Alignment.BottomCenter))
        if (saving) Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.35f)).clickable(enabled = false) {}, contentAlignment = Alignment.Center) {
            CircularProgressIndicator()
        }
    }

    editing?.let { r ->
        EditDialog(r, current(r), qual ?: "", type, onDismiss = { editing = null }) { v ->
            setText(r, v)
            editing = null
        }
    }
    if (picking) LanguageDialog(locales, qual ?: "", onDismiss = { picking = false }) { q ->
        picking = false
        if (q == qual) return@LanguageDialog
        if (edits.isEmpty()) qual = q else pendingQual = q
    }
    pendingType?.let { t ->
        AlertDialog(
            onDismissRequest = { pendingType = null },
            title = { Text("Unsaved changes") },
            text = { Text("${edits.size} unsaved edit${if (edits.size == 1) "" else "s"} will be dropped.") },
            confirmButton = { TextButton(onClick = { edits.clear(); pendingType = null; switchType(t) }) { Text("Discard") } },
            dismissButton = { TextButton(onClick = { pendingType = null }) { Text("Keep editing") } }
        )
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
    importPath?.let { start ->
        var path by remember(start) { mutableStateOf(start) }
        AlertDialog(
            onDismissRequest = { importPath = null },
            title = { Text("Import strings.xml") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Texts are matched by string name and become unsaved edits in ${ArscText.label(qual ?: "")}.",
                        fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    OutlinedTextField(path, { path = it }, label = { Text("File") }, maxLines = 3, modifier = Modifier.fillMaxWidth())
                }
            },
            confirmButton = { TextButton(onClick = { importPath = null; import(path.trim()) }, enabled = path.isNotBlank()) { Text("Import") } },
            dismissButton = { TextButton(onClick = { importPath = null }) { Text("Cancel") } }
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

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun TrRowItem(
    r: TrRow, value: String?, edited: Boolean, isDefault: Boolean, picked: Boolean, type: String,
    onClick: () -> Unit, onLongClick: () -> Unit
) {
    val cs = MaterialTheme.colorScheme
    Row(Modifier.fillMaxWidth().background(if (picked) Ej.colors.selection else Color.Transparent)
        .combinedClickable(onClick = onClick, onLongClick = onLongClick)) {
        Box(Modifier.width(3.dp).heightIn(min = 56.dp).fillMaxHeight().background(if (edited) cs.primary else Color.Transparent))
        if (type == "color") ArscText.argb(value ?: r.def)?.let { c ->
            Box(Modifier.padding(start = 13.dp, top = 12.dp).size(28.dp)
                .background(Color(c), RoundedCornerShape(6.dp))
                .border(1.dp, cs.outlineVariant, RoundedCornerShape(6.dp)))
        }
        Column(Modifier.weight(1f).padding(start = 13.dp, end = 16.dp, top = 8.dp, bottom = 8.dp)) {
            Text(r.key, fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = cs.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (!isDefault) Text(r.def, fontSize = 13.sp, color = cs.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
            if (value == null) Text(if (type == "string") "Not translated" else "Not set", fontSize = 13.sp, fontStyle = FontStyle.Italic, color = Ej.colors.warning)
            else Text(value.ifEmpty { "(empty)" }, fontSize = 15.sp, color = cs.onSurface, maxLines = 3, overflow = TextOverflow.Ellipsis)
        }
    }
    HorizontalDivider(color = cs.outlineVariant.copy(alpha = 0.5f))
}

@Composable
private fun EditDialog(r: TrRow, value: String?, qual: String, type: String, onDismiss: () -> Unit, onOk: (String) -> Unit) {
    var v by remember(r.id) { mutableStateOf(value ?: "") }
    val err = ArscText.invalid(type, v.trim())
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
                    v, { v = it }, label = { Text(ArscText.label(qual)) },
                    minLines = if (type == "string") 2 else 1, maxLines = if (type == "string") 8 else 1,
                    isError = err != null && v.isNotEmpty(),
                    supportingText = if (err != null && v.isNotEmpty()) ({ Text(err) }) else null,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = { TextButton(onClick = { onOk(if (type == "string") v else v.trim()) }, enabled = err == null) { Text("OK") } },
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
