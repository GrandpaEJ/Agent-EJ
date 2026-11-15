package org.agentej.android.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.FsEntry
import org.agentej.android.Util

/** Two-line MT row: tile, name, then "date   size" (or the parent folder for search hits). */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun FileRow(
    e: FsEntry,
    deep: Boolean,
    selected: Boolean,
    cutPending: Boolean,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    val haptic = LocalHapticFeedback.current
    val name = if (deep) e.name.substringAfterLast('/') else e.name
    val meta = buildString {
        if (deep) {
            append(e.name.substringBeforeLast('/', ".").let { if (it == ".") "./" else "$it/" })
            append("   ")
        }
        if (e.mtime > 0) append(Util.fmtTime(e.mtime))
        if (!e.isDir && (e.size > 0 || e.mtime > 0)) {
            if (isNotEmpty() && !endsWith(" ")) append("   ")
            append(Util.compactSize(e.size))
        }
    }
    Row(
        Modifier.fillMaxWidth().height(52.dp)
            .background(if (selected) Ej.colors.selection else Color.Transparent)
            .combinedClickable(
                onClick = onClick,
                onLongClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onLongClick()
                },
                onLongClickLabel = "Actions"
            )
            .padding(horizontal = 14.dp)
            .alpha(if (cutPending) 0.5f else 1f),
        verticalAlignment = Alignment.CenterVertically
    ) {
        FileTile(name, e.isDir, selected)
        Column(Modifier.weight(1f).padding(start = 12.dp)) {
            Text(
                name,
                fontSize = 15.sp,
                color = MaterialTheme.colorScheme.onBackground,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            if (meta.isNotEmpty()) Text(
                meta,
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}
