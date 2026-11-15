package org.agentej.android.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.grid.items as gridItems
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.FileClipboard
import org.agentej.android.FsEntry
import org.agentej.android.Native
import org.agentej.android.Prefs
import org.agentej.android.Rc
import org.agentej.android.Util
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BrowserScreen(path: String, prefs: Prefs, clipboard: FileClipboard, nav: BrowserNav) {
    val scope = rememberCoroutineScope()
    val st = remember(path) { BrowserState(path) }
    val snack = remember { SnackbarHostState() }
    var reload by remember { mutableIntStateOf(0) }
    var refreshing by remember { mutableStateOf(false) }
    val listState = rememberLazyListState()
    val gridState = rememberLazyGridState()

    LaunchedEffect(path, reload) {
        st.loading = true
        withContext(Dispatchers.IO) { st.load() }
        st.loading = false
        refreshing = false
    }

    val shown by remember(st) {
        derivedStateOf {
            st.deepHits ?: Util.sortEntries(st.raw, prefs.sortKey, prefs.sortDesc, prefs.showHidden).let { list ->
                val q = st.query.trim().lowercase(Locale.US)
                if (q.isEmpty()) list else list.filter { q in it.name.lowercase(Locale.US) }
            }
        }
    }

    fun say(rc: Rc) = scope.launch { snack.showSnackbar(rc.detail) }

    /** Runs a blocking op off the main thread, reports it, and reloads the listing. */
    fun perform(op: () -> Rc) = scope.launch {
        val rc = withContext(Dispatchers.IO) { op() }
        st.selected = emptySet()
        reload++
        say(rc)
    }

    fun openText(e: FsEntry) = scope.launch {
        val full = st.full(e)
        val text = withContext(Dispatchers.IO) {
            val split = st.archive
            if (split != null) Native.readEntry(split.first, split.second + e.name)?.let { Util.decodeText(it) }
            else Native.readText(full, 4 * 1024 * 1024)
        }
        if (text != null) nav.text(e.name.substringAfterLast('/'), text, if (st.inArchive) null else full)
        else snack.showSnackbar("${e.name.substringAfterLast('/')} is binary or larger than 4 MB")
    }

    fun open(e: FsEntry) {
        val full = st.full(e)
        when {
            st.selecting -> st.toggle(e)
            e.isDir -> nav.open(if (full.endsWith("/")) full else "$full/")
            Util.isArchive(e.name) -> nav.open("$full/")
            else -> openText(e)
        }
    }

    BackHandler(enabled = st.selecting) { st.selected = emptySet() }
    BackHandler(enabled = !st.selecting && st.searching) {
        st.searching = false
        st.query = ""
        st.deepHits = null
    }

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surface).safeDrawingPadding()) {
        Column(Modifier.fillMaxSize()) {
            when {
                st.selecting -> SelectionBar(st, shown, clipboard, onDelete = {
                    st.dialog = BrowserDialog.Delete(st.selectedPaths())
                }, onInfo = { st.dialog = BrowserDialog.Info(it) }, onRename = {
                    st.dialog = BrowserDialog.Rename(it)
                }, notify = { scope.launch { snack.showSnackbar(it) } })
                st.searching -> SearchBar(st, onDeep = {
                    scope.launch {
                        st.deepBusy = true
                        withContext(Dispatchers.IO) { st.deepSearch() }
                        st.deepBusy = false
                    }
                })
                else -> BrowseBar(st, prefs, nav, shown.size, onRefresh = { reload++ })
            }
            Box(Modifier.fillMaxWidth().height(2.dp)) {
                if (st.loading || st.deepBusy) LinearProgressIndicator(Modifier.fillMaxSize())
                else HorizontalDivider(Modifier.align(Alignment.TopStart), color = MaterialTheme.colorScheme.outlineVariant)
            }

            PullToRefreshBox(
                isRefreshing = refreshing,
                onRefresh = { refreshing = true; reload++ },
                modifier = Modifier.weight(1f).fillMaxWidth().background(MaterialTheme.colorScheme.background)
            ) {
                when {
                    st.loading && st.raw.isEmpty() -> Unit
                    st.unreadable -> EmptyState(
                        "Can't read this folder",
                        if (st.inArchive) "The archive is damaged or not a ZIP." else "It may be missing or protected by Android."
                    )
                    shown.isEmpty() && st.deepHits != null -> EmptyState("No matches", "Nothing under this folder is named like “${st.query}”.")
                    shown.isEmpty() && st.query.isNotBlank() -> EmptyState("No matches here", "Press search on the keyboard to look in subfolders too.")
                    shown.isEmpty() -> EmptyState(
                        "Empty folder",
                        when {
                            st.inArchive -> ""
                            st.raw.isNotEmpty() -> "Only hidden files here. Turn on Show hidden in the menu."
                            else -> "Tap + to create a file or folder."
                        }
                    )
                    prefs.grid -> LazyVerticalGrid(
                        GridCells.Adaptive(GRID_CELL),
                        Modifier.fillMaxSize(),
                        state = gridState,
                        contentPadding = PaddingValues(6.dp)
                    ) {
                        gridItems(shown, key = { it.name }) { e ->
                            FileCell(
                                e,
                                selected = e.name in st.selected,
                                cutPending = clipboard.cut && st.full(e) in clipboard.paths,
                                thumb = if (prefs.thumbs) st.thumbSpec(e) else null,
                                onClick = { open(e) },
                                onLongClick = { if (st.selecting) st.toggle(e) else st.menuFor = e }
                            )
                        }
                    }
                    else -> LazyColumn(Modifier.fillMaxSize(), state = listState) {
                        items(shown, key = { it.name }) { e ->
                            FileRow(
                                e,
                                deep = st.deepHits != null,
                                selected = e.name in st.selected,
                                cutPending = clipboard.cut && st.full(e) in clipboard.paths,
                                thumb = if (prefs.thumbs) st.thumbSpec(e) else null,
                                tile = if (prefs.thumbs) 40.dp else 28.dp,
                                onClick = { open(e) },
                                onLongClick = { if (st.selecting) st.toggle(e) else st.menuFor = e }
                            )
                        }
                    }
                }
            }

            if (!clipboard.isEmpty && !st.inArchive) PasteStrip(clipboard, onPaste = {
                val src = clipboard.paths
                val cut = clipboard.cut
                if (cut) clipboard.clear()
                perform { org.agentej.android.FileOps.paste(src, path, cut) }
            })
            BottomBar(
                nav = nav,
                upTo = Util.up(path),
                canCreate = !st.inArchive,
                onCreate = { st.dialog = BrowserDialog.Create(it) }
            )
        }
        SnackbarHost(snack, Modifier.align(Alignment.BottomCenter).padding(bottom = 64.dp))
    }

    EntrySheet(st, clipboard, nav, onOpen = ::open, onText = { openText(it) })
    BrowserDialogs(st, perform = ::perform)
}
