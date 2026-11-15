package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.FileClipboard
import org.agentej.android.FsEntry
import org.agentej.android.Prefs
import org.agentej.android.SortKey
import org.agentej.android.Util

@Composable
fun BrowseBar(st: BrowserState, prefs: Prefs, nav: BrowserNav, shownCount: Int, onRefresh: () -> Unit) {
    var sortOpen by remember { mutableStateOf(false) }
    var moreOpen by remember { mutableStateOf(false) }
    val dirs = st.raw.count { it.isDir && (prefs.showHidden || !it.name.startsWith('.')) }
    val files = st.raw.count { !it.isDir && (prefs.showHidden || !it.name.startsWith('.')) }
    val sub = when {
        st.loading && st.raw.isEmpty() -> "Reading…"
        st.unreadable -> st.path
        else -> listOf("Folders: $dirs", "Files: $files", st.disk).filter { it.isNotEmpty() }.joinToString("   ")
    }
    EjTopBar(
        title = Util.title(st.path),
        subtitle = sub,
        leading = { IconBtn(Icons.Default.Menu, "Open drawer", onClick = nav.drawer) }
    ) {
        if (st.isArchiveRoot) TextButton(onClick = { nav.apk(st.archive!!.first) }) {
            Text("TOOLS", fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        }
        IconBtn(Icons.Default.Search, "Search") { st.searching = true }
        Box {
            IconBtn(EjIcons.Sort, "Sort") { sortOpen = true }
            DropdownMenu(sortOpen, onDismissRequest = { sortOpen = false }) {
                SortKey.entries.forEach { k ->
                    val active = prefs.sortKey == k
                    DropdownMenuItem(
                        text = {
                            Text(k.label + if (active) (if (prefs.sortDesc) "  ↓" else "  ↑") else "")
                        },
                        leadingIcon = {
                            Icon(Icons.Default.Check, null, tint = if (active) MaterialTheme.colorScheme.primary else Color.Transparent)
                        },
                        onClick = { prefs.setSort(k) }
                    )
                }
            }
        }
        Box {
            IconBtn(Icons.Default.MoreVert, "More") { moreOpen = true }
            DropdownMenu(moreOpen, onDismissRequest = { moreOpen = false }) {
                DropdownMenuItem(
                    text = { Text(if (prefs.showHidden) "Hide dot-files" else "Show hidden") },
                    onClick = { prefs.toggleHidden(); moreOpen = false }
                )
                if (!st.inArchive) DropdownMenuItem(
                    text = { Text(if (prefs.isBookmarked(st.path)) "Remove bookmark" else "Bookmark this folder") },
                    onClick = { prefs.toggleBookmark(st.path); moreOpen = false }
                )
                DropdownMenuItem(
                    text = { Text(if (prefs.grid) "List view" else "Grid view") },
                    onClick = { prefs.toggleGrid(); moreOpen = false }
                )
                DropdownMenuItem(
                    text = { Text(if (prefs.thumbs) "Hide thumbnails" else "Show thumbnails") },
                    onClick = { prefs.toggleThumbs(); moreOpen = false }
                )
                DropdownMenuItem(text = { Text("Refresh") }, onClick = { onRefresh(); moreOpen = false })
                DropdownMenuItem(
                    text = { Text("Select all") },
                    enabled = shownCount > 0,
                    onClick = { st.selected = st.raw.map { it.name }.toSet(); moreOpen = false }
                )
            }
        }
    }
}

@Composable
fun SelectionBar(
    st: BrowserState,
    shown: List<FsEntry>,
    clipboard: FileClipboard,
    onDelete: () -> Unit,
    onInfo: (FsEntry) -> Unit,
    onRename: (FsEntry) -> Unit,
    notify: (String) -> Unit
) {
    var moreOpen by remember { mutableStateOf(false) }
    val single = st.selected.singleOrNull()?.let { n -> st.raw.firstOrNull { it.name == n } }
    val writable = !st.inArchive
    EjTopBar(
        title = "${st.selected.size} selected",
        leading = { IconBtn(Icons.Default.Close, "Clear selection") { st.selected = emptySet() } }
    ) {
        IconBtn(EjIcons.SelectAll, "Select all") { st.selected = shown.map { it.name }.toSet() }
        if (writable) {
            IconBtn(EjIcons.Copy, "Copy") {
                clipboard.set(st.selectedPaths(), false)
                notify("${st.selected.size} ready to paste — open the target folder")
                st.selected = emptySet()
            }
            IconBtn(EjIcons.Cut, "Cut") {
                clipboard.set(st.selectedPaths(), true)
                notify("${st.selected.size} ready to move — open the target folder")
                st.selected = emptySet()
            }
            IconBtn(Icons.Default.Delete, "Delete", onClick = onDelete)
        }
        Box {
            IconBtn(Icons.Default.MoreVert, "More") { moreOpen = true }
            DropdownMenu(moreOpen, onDismissRequest = { moreOpen = false }) {
                DropdownMenuItem(
                    text = { Text("Rename") }, enabled = writable && single != null,
                    onClick = { moreOpen = false; single?.let(onRename) }
                )
                DropdownMenuItem(
                    text = { Text("Properties") }, enabled = single != null,
                    onClick = { moreOpen = false; single?.let(onInfo) }
                )
            }
        }
    }
}

@Composable
fun SearchBar(st: BrowserState, onDeep: () -> Unit) {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface).height(56.dp).padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Close search") {
            st.searching = false
            st.query = ""
            st.deepHits = null
        }
        TextField(
            value = st.query,
            onValueChange = { st.query = it; st.deepHits = null },
            placeholder = {
                Text(if (st.inArchive) "Filter this folder" else "Filter · search key looks in subfolders", fontSize = 14.sp)
            },
            singleLine = true,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = { onDeep() }),
            colors = TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent,
                unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent
            ),
            modifier = Modifier.weight(1f).focusRequester(focus)
        )
        if (st.query.isNotEmpty()) IconBtn(Icons.Default.Close, "Clear") { st.query = ""; st.deepHits = null }
    }
}

@Composable
fun PasteStrip(clipboard: FileClipboard, onPaste: () -> Unit) {
    val n = clipboard.paths.size
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.primaryContainer).padding(start = 16.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            "$n item${if (n == 1) "" else "s"} to ${if (clipboard.cut) "move" else "copy"}",
            fontSize = 13.sp,
            color = MaterialTheme.colorScheme.onPrimaryContainer,
            modifier = Modifier.weight(1f)
        )
        TextButton(onClick = { clipboard.clear() }) { Text("CANCEL", fontSize = 12.sp) }
        TextButton(onClick = onPaste) { Text("PASTE HERE", fontSize = 12.sp, fontWeight = FontWeight.SemiBold) }
    }
}

/** MT's bottom bar: back, forward, new, up — icons only, evenly spaced. */
@Composable
fun BottomBar(nav: BrowserNav, upTo: String?, canCreate: Boolean, onCreate: (folder: Boolean) -> Unit) {
    var newOpen by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface).height(52.dp),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back", enabled = nav.canBack, onClick = nav.back)
        IconBtn(Icons.AutoMirrored.Filled.ArrowForward, "Forward", enabled = nav.canForward, onClick = nav.forward)
        Box {
            IconBtn(Icons.Default.Add, "New", enabled = canCreate) { newOpen = true }
            DropdownMenu(newOpen, onDismissRequest = { newOpen = false }) {
                DropdownMenuItem(
                    text = { Text("New folder") },
                    leadingIcon = { Icon(EjIcons.NewFolder, null) },
                    onClick = { newOpen = false; onCreate(true) }
                )
                DropdownMenuItem(
                    text = { Text("New file") },
                    leadingIcon = { Icon(Icons.Default.Add, null) },
                    onClick = { newOpen = false; onCreate(false) }
                )
            }
        }
        IconBtn(EjIcons.Up, "Up one level", enabled = upTo != null) { upTo?.let(nav.open) }
    }
}
