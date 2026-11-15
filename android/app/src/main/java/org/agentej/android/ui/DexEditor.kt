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
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import java.util.Locale

/** The dex files one editor works on: archive entries, or [""] for a plain .dex file. */
data class DexSource(val archive: String, val entries: List<String>) {
    fun label(dex: Int): String = entries[dex].ifEmpty { archive.substringAfterLast('/') }
}

object DexFiles {
    private val ROOT_DEX = Regex("""classes(\d*)\.dex""")

    /** Root classes*.dex of an APK/zip, in load order (classes, classes2, ...). */
    fun entries(archive: String): List<String> =
        (Native.zipEntries(archive) ?: emptyArray()).map { it.substringBefore('\t') }.filter { ROOT_DEX.matches(it) }
            .sortedBy { ROOT_DEX.matchEntire(it)!!.groupValues[1].ifEmpty { "1" }.toInt() }

    fun isRootDex(entry: String?) = entry != null && ROOT_DEX.matches(entry)
}

internal class DexCls(val dex: Int, val index: Int, val pkg: String, val simple: String) {
    val full get() = if (pkg.isEmpty()) simple else "$pkg.$simple"
}

/** Package node; [label] may span several segments once single-child chains are folded. */
private class PkgNode(val label: String, val path: String, val pkgs: List<PkgNode>, val classes: List<DexCls>)

private class MutPkg(val name: String) {
    val pkgs = sortedMapOf<String, MutPkg>()
    val classes = ArrayList<DexCls>()
}

private fun buildTree(all: List<DexCls>): PkgNode {
    val root = MutPkg("")
    for (c in all) {
        var n = root
        if (c.pkg.isNotEmpty()) for (seg in c.pkg.split('.')) n = n.pkgs.getOrPut(seg) { MutPkg(seg) }
        n.classes += c
    }
    fun freeze(m: MutPkg, parent: String): PkgNode {
        var label = m.name
        var cur = m
        /* "com" -> "google" -> "common" with nothing else inside reads as one row */
        while (cur.classes.isEmpty() && cur.pkgs.size == 1) {
            val only = cur.pkgs.values.first()
            label = "$label.${only.name}"
            cur = only
        }
        val path = if (parent.isEmpty()) label else "$parent.$label"
        return PkgNode(
            label, path,
            cur.pkgs.values.map { freeze(it, path) },
            cur.classes.sortedBy { it.simple.lowercase(Locale.US) }
        )
    }
    return PkgNode("", "", root.pkgs.values.map { freeze(it, "") }, root.classes.sortedBy { it.simple })
}

private sealed interface TreeRow {
    val depth: Int
    class Pkg(val node: PkgNode, override val depth: Int, val open: Boolean) : TreeRow
    class Cls(val c: DexCls, override val depth: Int) : TreeRow
}

private fun flatten(root: PkgNode, expanded: Set<String>): List<TreeRow> {
    val out = ArrayList<TreeRow>()
    fun walk(n: PkgNode, depth: Int) {
        for (p in n.pkgs) {
            val open = p.path in expanded
            out += TreeRow.Pkg(p, depth, open)
            if (open) walk(p, depth + 1)
        }
        for (c in n.classes) out += TreeRow.Cls(c, depth)
    }
    walk(root, 0)
    return out
}

/** MT-style "MultiDex" picker: every root dex of the archive, [checked] pre-ticked. */
@Composable
fun MultiDexDialog(entries: List<String>, checked: String?, onOpen: (List<String>) -> Unit, onDismiss: () -> Unit) {
    val picked = remember { mutableStateListOf<String>().apply { add(checked ?: entries.first()) } }
    androidx.compose.material3.AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("MultiDex") },
        text = {
            LazyColumn {
                items(entries) { e ->
                    Row(
                        Modifier.fillMaxWidth().clickable { if (e in picked) picked.remove(e) else picked.add(e) }
                            .padding(vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        androidx.compose.material3.Checkbox(checked = e in picked, onCheckedChange = null)
                        Spacer(Modifier.width(12.dp))
                        Text(e, fontSize = 15.sp)
                    }
                }
            }
        },
        confirmButton = {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                androidx.compose.material3.TextButton(onClick = { picked.clear(); picked.addAll(entries) }) { Text("Select all") }
                Row {
                    androidx.compose.material3.TextButton(onClick = onDismiss) { Text("Cancel") }
                    androidx.compose.material3.TextButton(
                        enabled = picked.isNotEmpty(),
                        onClick = { onOpen(entries.filter { it in picked }) }
                    ) { Text("OK") }
                }
            }
        }
    )
}

/**
 * Everything the dex editor keeps while it is on the route stack: survives opening a
 * class (which takes the editor out of composition) and is [close]d when popped.
 * Holds one native dex session at a time — the dex of the last opened class.
 */
class DexEditorState(val src: DexSource) {
    internal var classes by mutableStateOf<List<DexCls>?>(null)
    var expanded by mutableStateOf(setOf<String>())
    var query by mutableStateOf<String?>(null)
    val treeList = LazyListState()
    val findList = LazyListState()
    val searchList = LazyListState()
    /** 0 = Explorer, 1 = Search */
    var tab by mutableStateOf(0)
    internal var search by mutableStateOf<SearchResult?>(null)
    var lastSpec = SearchSpec("")
    private var handle = 0L
    private var handleDex = -1

    internal suspend fun load() {
        if (classes != null) return
        classes = withContext(Dispatchers.IO) {
            src.entries.flatMapIndexed { dex, entry ->
                (Native.dexClasses(src.archive, entry) ?: emptyArray()).mapIndexed { i, d ->
                    val dotted = d.removePrefix("L").removeSuffix(";").replace('/', '.')
                    DexCls(dex, i, dotted.substringBeforeLast('.', ""), dotted.substringAfterLast('.'))
                }
            }
        }
    }

    /** Blocking; call off the main thread. */
    @Synchronized internal fun smali(c: DexCls): String {
        if (handleDex != c.dex) {
            if (handle != 0L) Native.dexClose(handle)
            handle = Native.dexOpen(src.archive, src.entries[c.dex])
            handleDex = c.dex
        }
        return if (handle == 0L) "" else Native.dexClassSmali(handle, c.index)
    }

    @Synchronized fun close() {
        if (handle != 0L) Native.dexClose(handle)
        handle = 0L; handleDex = -1
    }
}

/**
 * Dex editor over several dex at once: one merged package tree (MT "Dex Editor plus"
 * explorer). Classes render on tap through a native session that keeps only the
 * dex of the last opened class parsed, so RAM stays at one dex.
 */
@Composable
fun DexEditorScreen(state: DexEditorState, onBack: () -> Unit, onView: (title: String, body: String, line: Int, needle: String?) -> Unit) {
    val src = state.src
    val scope = rememberCoroutineScope()
    LaunchedEffect(state) { state.load() }
    val classes = state.classes
    val tree = remember(classes) { classes?.let { buildTree(it) } }
    val rows = remember(tree, state.expanded) { tree?.let { flatten(it, state.expanded) } ?: emptyList() }
    val hits = remember(classes, state.query) {
        val q = state.query?.trim()?.lowercase(Locale.US).orEmpty()
        if (q.isEmpty()) null else classes.orEmpty().filter { q in it.full.lowercase(Locale.US) }.take(2000)
    }
    var busy by remember { mutableStateOf(false) }
    var searching by remember { mutableStateOf<String?>(null) }
    var askSearch by remember { mutableStateOf(false) }
    /** [line] is a 0-based line of the class smali (-1: top); [needle] is highlighted on it. */
    fun open(c: DexCls, line: Int = -1, needle: String? = null) {
        if (busy) return
        busy = true
        scope.launch {
            val text = withContext(Dispatchers.IO) { state.smali(c) }
            busy = false
            /* two header lines ("# classesN.dex", blank) sit above the smali */
            onView("${c.simple}.smali", "# ${src.label(c.dex)}\n\n$text", if (line < 0) -1 else line + 2, needle)
        }
    }
    fun runSearchNow(spec: SearchSpec) {
        state.lastSpec = spec
        askSearch = false
        searching = "Searching…"
        scope.launch {
            val r = withContext(Dispatchers.IO) { runSearch(state, spec) { msg -> searching = msg } }
            state.search = r
            searching = null
            state.searchList.scrollToItem(0)
        }
    }
    fun openHit(h: SearchHit) {
        if (h.text.isEmpty()) return open(h.cls)
        val needle = state.search?.spec?.let { sp -> hitRange(h.text, sp)?.let { h.text.substring(it) } }
        open(h.cls, h.line, needle)
    }
    var query by state::query
    var expanded by state::expanded
    /* Back closes the class finder before it leaves the editor */
    androidx.activity.compose.BackHandler(enabled = query != null) { query = null }
    /* ... and from the Search tab Back returns to the Explorer */
    androidx.activity.compose.BackHandler(enabled = query == null && state.tab == 1 && searching == null) { state.tab = 0 }
    if (askSearch) DexSearchDialog(state.lastSpec, onSearch = ::runSearchNow, onDismiss = { askSearch = false })

    val subtitle = classes?.let { "${it.size} classes · ${src.entries.size} dex" } ?: "Reading…"
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).safeDrawingPadding()) {
        EjTopBar("Dex Editor", subtitle, leading = {
            IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back", onClick = onBack)
        }) {
            if (state.tab == 0) {
                if (query == null) IconBtn(Icons.Filled.Search, "Find class") { query = "" }
                else IconBtn(Icons.Filled.Close, "Close find") { query = null }
            }
        }
        DexTabs(state.tab) { state.tab = it; if (it == 1) query = null }
        if (query != null) {
            val focus = remember { androidx.compose.ui.focus.FocusRequester() }
            LaunchedEffect(Unit) { focus.requestFocus() }
            TextField(
                value = query!!, onValueChange = { query = it }, singleLine = true,
                placeholder = { Text("Class name", fontSize = 14.sp) },
                colors = TextFieldDefaults.colors(
                    focusedContainerColor = MaterialTheme.colorScheme.surface,
                    unfocusedContainerColor = MaterialTheme.colorScheme.surface,
                    focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent
                ),
                modifier = Modifier.fillMaxWidth().focusRequester(focus)
            )
        }
        Box(Modifier.fillMaxWidth().height(2.dp)) { if (busy || searching != null) LinearProgressIndicator(Modifier.fillMaxSize()) }
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        when {
            classes == null -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
            classes!!.isEmpty() -> EmptyState("No classes", "None of the chosen files is a readable DEX.")
            state.tab == 1 -> DexSearchPane(state, searching, onNew = { askSearch = true }, onOpen = ::openHit)
            hits != null -> LazyColumn(Modifier.fillMaxSize(), state = state.findList) {
                items(hits) { c -> ClassRow(c.simple, c.pkg, 0) { open(c) } }
            }
            else -> LazyColumn(Modifier.fillMaxSize(), state = state.treeList) {
                items(rows, key = { r -> if (r is TreeRow.Pkg) "p:${r.node.path}" else (r as TreeRow.Cls).let { "c:${it.c.dex}:${it.c.index}" } }) { r ->
                    when (r) {
                        is TreeRow.Pkg -> PkgRow(r.node.label, r.depth, r.open) {
                            expanded = if (r.open) expanded - r.node.path else expanded + r.node.path
                        }
                        is TreeRow.Cls -> ClassRow(r.c.simple, null, r.depth) { open(r.c) }
                    }
                }
            }
        }
    }
}

@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
private fun DexTabs(selected: Int, onSelect: (Int) -> Unit) {
    androidx.compose.material3.SecondaryTabRow(
        selectedTabIndex = selected, containerColor = MaterialTheme.colorScheme.surface
    ) {
        listOf("EXPLORER", "SEARCH").forEachIndexed { i, label ->
            androidx.compose.material3.Tab(
                selected = selected == i, onClick = { onSelect(i) },
                text = { Text(label, fontSize = 13.sp, fontWeight = FontWeight.Medium) },
                unselectedContentColor = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

private val ROW_INDENT = 18.dp

@Composable
private fun PkgRow(label: String, depth: Int, open: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(40.dp).clickable(onClick = onClick).padding(start = 4.dp + ROW_INDENT * depth),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            if (open) Icons.Filled.KeyboardArrowDown else Icons.AutoMirrored.Filled.KeyboardArrowRight, null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(18.dp)
        )
        Spacer(Modifier.width(4.dp))
        Box(
            Modifier.size(24.dp).clip(RoundedCorner6).background(Ej.colors.tileDir),
            contentAlignment = Alignment.Center
        ) { Icon(EjIcons.Folder, null, tint = Color.White, modifier = Modifier.size(15.dp)) }
        Spacer(Modifier.width(10.dp))
        Text(label, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onBackground)
    }
}

@Composable
private fun ClassRow(name: String, pkg: String?, depth: Int, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = if (pkg != null) 46.dp else 40.dp).clickable(onClick = onClick)
            .padding(start = 26.dp + ROW_INDENT * depth, end = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            Modifier.size(22.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primaryContainer),
            contentAlignment = Alignment.Center
        ) { Text("C", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary) }
        Spacer(Modifier.width(10.dp))
        Column {
            /* long synthetic names ($lambda-1$1) wrap to a second line, as in MT */
            Text(name, fontSize = 14.sp, lineHeight = 17.sp, maxLines = 2, overflow = TextOverflow.Ellipsis,
                color = MaterialTheme.colorScheme.onBackground, modifier = Modifier.padding(vertical = 4.dp))
            if (!pkg.isNullOrEmpty()) Text(pkg, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

private val RoundedCorner6 = androidx.compose.foundation.shape.RoundedCornerShape(6.dp)

