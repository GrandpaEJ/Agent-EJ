package org.agentej.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Create
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import org.agentej.android.FsEntry
import org.agentej.android.Target
import org.agentej.android.Util

/** One cell of the long-press grid; [onClick] null = shown but unavailable here. */
class MenuItem(val icon: ImageVector, val label: String, val danger: Boolean = false, val onClick: (() -> Unit)?)

/** What the long-press menu can do for this entry, decided by [Commander]. */
class EntryActions(
    val toOther: String?,        // other pane's folder name when "Copy →" applies, else null
    val select: () -> Unit,
    val copy: () -> Unit,
    val move: (() -> Unit)?,
    val extract: (() -> Unit)?,
    val delete: (() -> Unit)?,
    val rename: (() -> Unit)?,
    val tools: (() -> Unit)?,
    val compress: (() -> Unit)?,
    val info: () -> Unit,
    val share: (() -> Unit)?,
    val openWith: (() -> Unit)?,
    val bookmark: (() -> Unit)?,
    val bookmarked: Boolean
)

/** MT's long-press menu: the entry, then actions in a two-column grid. */
@Composable
fun EntryMenu(t: Target, e: FsEntry, a: EntryActions, onDismiss: () -> Unit) {
    val arrow = if (a.toOther != null) " →" else ""
    val items = buildList {
        if (t.inArchive) add(MenuItem(EjIcons.Extract, "Extract$arrow", onClick = a.extract))
        else add(MenuItem(EjIcons.Copy, "Copy$arrow", onClick = a.copy))
        add(MenuItem(EjIcons.Cut, "Move$arrow", onClick = a.move))
        add(MenuItem(Icons.Default.Delete, "Delete", danger = true, onClick = a.delete))
        add(MenuItem(Icons.Default.Create, "Rename", onClick = a.rename))
        add(MenuItem(EjIcons.Tools, "Tools", onClick = a.tools))
        add(MenuItem(EjIcons.Zip, "Compress", onClick = a.compress))
        add(MenuItem(Icons.Default.Info, "Properties", onClick = a.info))
        add(MenuItem(EjIcons.Share, "Share", onClick = a.share))
        add(MenuItem(EjIcons.OpenWith, "Open with…", onClick = a.openWith))
        if (e.isDir) add(MenuItem(EjIcons.Bookmark, if (a.bookmarked) "Unbookmark" else "Bookmark", onClick = a.bookmark))
        else add(MenuItem(Icons.Default.Check, "Select", onClick = a.select))
    }
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = RoundedCornerShape(10.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Column(Modifier.padding(vertical = 8.dp)) {
                Row(Modifier.padding(horizontal = 16.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    FileTile(t.name, e.isDir, size = 32.dp)
                    Column(Modifier.padding(start = 12.dp)) {
                        Text(t.name, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(
                            if (e.isDir) "Folder" else Util.compactSize(e.size) + if (e.mtime > 0) "   " + Util.fmtTime(e.mtime) else "",
                            fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                HorizontalDivider(Modifier.padding(vertical = 4.dp), color = MaterialTheme.colorScheme.outlineVariant)
                items.chunked(2).forEach { row ->
                    Row(Modifier.fillMaxWidth().padding(horizontal = 6.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        row.forEach { GridCell(it, onDismiss, Modifier.weight(1f)) }
                        if (row.size == 1) Row(Modifier.weight(1f)) {}
                    }
                }
            }
        }
    }
}

@Composable
private fun GridCell(item: MenuItem, dismiss: () -> Unit, modifier: Modifier) {
    val enabled = item.onClick != null
    val tint = if (item.danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
    Row(
        modifier.height(46.dp).clip(RoundedCornerShape(6.dp))
            .clickable(enabled = enabled) { dismiss(); item.onClick?.invoke() }
            .alpha(if (enabled) 1f else 0.38f)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(item.icon, null, tint = if (item.danger) tint else MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(20.dp))
        Text(item.label, fontSize = 14.sp, color = tint, maxLines = 1, modifier = Modifier.padding(start = 12.dp))
    }
}
