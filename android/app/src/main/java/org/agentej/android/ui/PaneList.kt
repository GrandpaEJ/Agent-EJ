package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.grid.items as gridItems
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.agentej.android.FileClipboard
import org.agentej.android.FsEntry
import org.agentej.android.Prefs
import org.agentej.android.Util
import java.util.Locale

/** What the pane currently shows: sorted, filtered, or the recursive search hits. */
fun shownEntries(st: BrowserState, prefs: Prefs): List<FsEntry> =
    st.deepHits ?: Util.sortEntries(st.raw, prefs.sortKey, prefs.sortDesc, prefs.showHidden).let { list ->
        val q = st.query.trim().lowercase(Locale.US)
        if (q.isEmpty()) list else list.filter { q in it.name.lowercase(Locale.US) }
    }

/** MT's ".." row at the top of every listing: tap to go up one level. */
@Composable
private fun UpRow(upTo: String?, narrow: Boolean, onUp: (String) -> Unit) {
    if (upTo == null) return
    FileRow(
        FsEntry("..", true, 0, 0), deep = false, selected = false, cutPending = false,
        thumb = null, tile = 26.dp, narrow = narrow,
        onClick = { onUp(upTo) }, onLongClick = {}
    )
}

/** The listing of one pane: loads its folder, then shows rows or grid cells. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PaneList(
    st: BrowserState,
    prefs: Prefs,
    clipboard: FileClipboard,
    narrow: Boolean,
    upTo: String?,
    onUp: (String) -> Unit,
    onTap: (FsEntry) -> Unit,
    onLong: (FsEntry) -> Unit
) {
    var refreshing by remember { mutableStateOf(false) }
    val listState = rememberLazyListState()
    val gridState = rememberLazyGridState()
    LaunchedEffect(st, st.reload) {
        st.loading = true
        withContext(Dispatchers.IO) { st.load() }
        st.loading = false
        refreshing = false
    }
    val shown by remember(st) { derivedStateOf { shownEntries(st, prefs) } }

    PullToRefreshBox(
        isRefreshing = refreshing,
        onRefresh = { refreshing = true; st.reload++ },
        modifier = Modifier.fillMaxSize()
    ) {
        when {
            st.loading && st.raw.isEmpty() -> Box(Modifier.fillMaxSize())
            st.unreadable -> Column { UpRow(upTo, narrow, onUp); EmptyState(
                "Can't read this folder",
                if (st.inArchive) "The archive is damaged or not a ZIP." else "It may be missing or protected by Android."
            ) }
            shown.isEmpty() && st.deepHits != null -> EmptyState("No matches", "Nothing below here is named like “${st.query}”.")
            shown.isEmpty() && st.query.isNotBlank() -> EmptyState("No matches here", "Press search on the keyboard to look in subfolders.")
            shown.isEmpty() -> Column { UpRow(upTo, narrow, onUp); EmptyState(
                "Empty folder",
                when {
                    st.inArchive -> ""
                    st.raw.isNotEmpty() -> "Only hidden files here. Turn on Show hidden in the menu."
                    else -> "Tap + to create a file or folder."
                }
            ) }
            prefs.grid -> LazyVerticalGrid(
                GridCells.Adaptive(if (narrow) 84.dp else GRID_CELL), Modifier.fillMaxSize(),
                state = gridState, contentPadding = PaddingValues(4.dp)
            ) {
                gridItems(shown, key = { it.name }) { e ->
                    FileCell(
                        e, selected = e.name in st.selected,
                        cutPending = clipboard.cut && st.full(e) in clipboard.paths,
                        thumb = if (prefs.thumbs) st.thumbSpec(e) else null,
                        onClick = { onTap(e) }, onLongClick = { onLong(e) }
                    )
                }
            }
            else -> LazyColumn(Modifier.fillMaxSize(), state = listState) {
                if (st.deepHits == null) item(key = "..") { UpRow(upTo, narrow, onUp) }
                items(shown, key = { it.name }) { e ->
                    FileRow(
                        e, deep = st.deepHits != null,
                        selected = e.name in st.selected,
                        cutPending = clipboard.cut && st.full(e) in clipboard.paths,
                        thumb = if (prefs.thumbs) st.thumbSpec(e) else null,
                        tile = if (prefs.thumbs) 30.dp else 26.dp,
                        narrow = narrow,
                        onClick = { onTap(e) }, onLongClick = { onLong(e) }
                    )
                }
            }
        }
    }
}
