package org.agentej.android.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.FsEntry
import org.agentej.android.ThumbSpec
import org.agentej.android.Util

/** Grid cell edge; thumbnails for the grid are decoded at this size. */
val GRID_CELL = 104.dp

/** "date   size", or the parent folder first for recursive-search hits. */
private fun metaLine(e: FsEntry, deep: Boolean) = buildString {
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

@OptIn(ExperimentalFoundationApi::class)
private fun Modifier.entryClicks(
    haptic: androidx.compose.ui.hapticfeedback.HapticFeedback,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) = combinedClickable(
    onClick = onClick,
    onLongClick = {
        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
        onLongClick()
    },
    onLongClickLabel = "Actions"
)

/** Two-line MT row: tile (or thumbnail), name, then the meta line. */
@Composable
fun FileRow(
    e: FsEntry,
    deep: Boolean,
    selected: Boolean,
    cutPending: Boolean,
    thumb: ThumbSpec?,
    tile: Dp,
    narrow: Boolean = false,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    val haptic = LocalHapticFeedback.current
    val name = if (deep) e.name.substringAfterLast('/') else e.name
    val meta = metaLine(e, deep)
    val bmp = rememberThumb(thumb, tile)
    // MT density: ~40-44dp rows, small tile, one meta line.
    Row(
        Modifier.fillMaxWidth().height(tile + 10.dp)
            .background(if (selected) Ej.colors.selection else Color.Transparent)
            .entryClicks(haptic, onClick, onLongClick)
            .padding(horizontal = if (narrow) 8.dp else 12.dp)
            .alpha(if (cutPending) 0.5f else 1f),
        verticalAlignment = Alignment.CenterVertically
    ) {
        FileTile(name, e.isDir, selected, bmp, tile)
        Column(Modifier.weight(1f).padding(start = if (narrow) 8.dp else 12.dp)) {
            Text(
                name,
                fontSize = 14.sp,
                lineHeight = 17.sp,
                color = MaterialTheme.colorScheme.onBackground,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            if (meta.isNotEmpty()) Text(
                meta,
                fontSize = 10.5.sp,
                lineHeight = 13.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

/** Gallery cell: square thumbnail (or big type tile), two-line name, size. */
@Composable
fun FileCell(
    e: FsEntry,
    selected: Boolean,
    cutPending: Boolean,
    thumb: ThumbSpec?,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    val haptic = LocalHapticFeedback.current
    val name = e.name.substringAfterLast('/')
    val bmp = rememberThumb(thumb, GRID_CELL)
    Column(
        Modifier.padding(4.dp).clip(RoundedCornerShape(10.dp))
            .background(if (selected) Ej.colors.selection else Color.Transparent)
            .entryClicks(haptic, onClick, onLongClick)
            .padding(6.dp)
            .alpha(if (cutPending) 0.5f else 1f),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        androidx.compose.foundation.layout.BoxWithConstraints(Modifier.fillMaxWidth().aspectRatio(1f)) {
            FileTile(name, e.isDir, selected, bmp, maxWidth)
        }
        Text(
            name,
            fontSize = 12.sp,
            lineHeight = 15.sp,
            color = MaterialTheme.colorScheme.onBackground,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(top = 6.dp)
        )
        Text(
            if (e.isDir) " " else Util.compactSize(e.size),
            fontSize = 10.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1
        )
    }
}
