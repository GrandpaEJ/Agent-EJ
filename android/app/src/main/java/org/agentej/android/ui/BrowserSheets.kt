package org.agentej.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Create
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.List
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.FileClipboard
import org.agentej.android.FileOps
import org.agentej.android.FsEntry
import org.agentej.android.Native
import org.agentej.android.Rc
import org.agentej.android.Util

/** Long-press menu for one entry. Write actions are hidden inside archives (read-only). */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EntrySheet(
    st: BrowserState,
    clipboard: FileClipboard,
    nav: BrowserNav,
    onOpen: (FsEntry) -> Unit,
    onText: (FsEntry) -> Unit
) {
    val e = st.menuFor ?: return
    val full = st.full(e)
    val name = e.name.substringAfterLast('/')
    fun act(block: () -> Unit) { st.menuFor = null; block() }
    val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(onDismissRequest = { st.menuFor = null }, sheetState = sheet) {
        Column(Modifier.navigationBarsPadding().padding(bottom = 8.dp)) {
            Row(Modifier.padding(horizontal = 20.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                FileTile(name, e.isDir)
                Column(Modifier.padding(start = 12.dp)) {
                    Text(name, fontSize = 16.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(
                        if (e.isDir) "Folder" else Util.compactSize(e.size) + if (e.mtime > 0) "   " + Util.fmtTime(e.mtime) else "",
                        fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            HorizontalDivider(Modifier.padding(top = 8.dp), color = MaterialTheme.colorScheme.outlineVariant)
            SheetItem(Icons.Default.Check, "Select") { act { st.selected = setOf(e.name) } }
            if (Util.ext(name) in setOf("apk", "apks", "xapk") && !st.inArchive)
                SheetItem(Icons.Default.Build, "APK tools") { act { nav.apk(full) } }
            if (!e.isDir) SheetItem(Icons.Default.List, "Open as text") { act { onText(e) } }
            if (e.isDir || Util.isArchive(name)) SheetItem(EjIcons.Folder, "Open") { act { onOpen(e) } }
            if (!st.inArchive) {
                SheetItem(EjIcons.Copy, "Copy") { act { clipboard.set(listOf(full), false) } }
                SheetItem(EjIcons.Cut, "Cut") { act { clipboard.set(listOf(full), true) } }
                SheetItem(Icons.Default.Create, "Rename") { act { st.dialog = BrowserDialog.Rename(e) } }
                SheetItem(Icons.Default.Delete, "Delete", danger = true) {
                    act { st.dialog = BrowserDialog.Delete(listOf(full)) }
                }
            }
            SheetItem(Icons.Default.Info, "Properties") { act { st.dialog = BrowserDialog.Info(e) } }
        }
    }
}

@Composable
private fun SheetItem(icon: ImageVector, label: String, danger: Boolean = false, onClick: () -> Unit) {
    val tint = if (danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
    Row(
        Modifier.fillMaxWidth().height(48.dp).clickable(onClick = onClick).padding(horizontal = 20.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(icon, null, tint = if (danger) tint else MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(20.dp))
        Text(label, fontSize = 15.sp, color = tint, modifier = Modifier.padding(start = 20.dp))
    }
}

@Composable
fun BrowserDialogs(st: BrowserState, perform: (() -> Rc) -> Unit) {
    val close = { st.dialog = null }
    when (val d = st.dialog) {
        null -> Unit
        is BrowserDialog.Rename -> NameDialog("Rename", d.entry.name, "RENAME", close) { n ->
            close(); perform { FileOps.rename(st.full(d.entry), n) }
        }
        is BrowserDialog.Create -> NameDialog(if (d.folder) "New folder" else "New file", "", "CREATE", close) { n ->
            close(); perform { FileOps.create(st.path, n, d.folder) }
        }
        is BrowserDialog.Delete -> {
            val n = d.paths.size
            AlertDialog(
                onDismissRequest = close,
                title = { Text(if (n == 1) "Delete ${d.paths[0].trimEnd('/').substringAfterLast('/')}?" else "Delete $n items?") },
                text = { Text("Folders are deleted with everything inside. This can't be undone.") },
                confirmButton = {
                    TextButton(onClick = { close(); perform { FileOps.delete(d.paths) } }) {
                        Text("DELETE", color = MaterialTheme.colorScheme.error)
                    }
                },
                dismissButton = { TextButton(onClick = close) { Text("CANCEL") } }
            )
        }
        is BrowserDialog.Info -> InfoDialog(st, d.entry, close)
    }
}

@Composable
private fun NameDialog(title: String, initial: String, verb: String, onDismiss: () -> Unit, onOk: (String) -> Unit) {
    // Preselect the stem so "foo.apk" -> typing replaces "foo" only.
    val stemEnd = initial.lastIndexOf('.').takeIf { it > 0 } ?: initial.length
    var value by remember { mutableStateOf(TextFieldValue(initial, TextRange(0, stemEnd))) }
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    val name = value.text.trim()
    val valid = FileOps.validName(name) && name != initial
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = value,
                onValueChange = { value = it },
                singleLine = true,
                isError = name.isNotEmpty() && !FileOps.validName(name),
                supportingText = { if (name.isNotEmpty() && !FileOps.validName(name)) Text("Names can't contain /") },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { if (valid) onOk(name) }),
                modifier = Modifier.fillMaxWidth().focusRequester(focus)
            )
        },
        confirmButton = { TextButton(enabled = valid, onClick = { onOk(name) }) { Text(verb) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("CANCEL") } }
    )
}

@Composable
private fun InfoDialog(st: BrowserState, e: FsEntry, onDismiss: () -> Unit) {
    val full = st.full(e)
    val readable = remember(full) {
        if (st.inArchive) "inside archive" else if (Native.statPath(full).split('\t').getOrNull(3) == "1") "yes" else "no"
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(e.name.substringAfterLast('/'), maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column {
                FactRow("Path", full)
                FactRow("Type", if (e.isDir) "Folder" else Util.shortCode(e.name, false), mono = false)
                if (!e.isDir) FactRow("Size", "${Util.humanSize(e.size)} (${e.size} B)")
                if (e.mtime > 0) FactRow("Modified", Util.fmtTime(e.mtime))
                FactRow("Readable", readable, mono = false)
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("OK") } },
        containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
        iconContentColor = Color.Unspecified
    )
}
