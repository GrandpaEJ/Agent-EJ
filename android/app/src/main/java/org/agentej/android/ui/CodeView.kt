package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex

/** A search hit: [line] index, char range [start, end). */
data class Match(val line: Int, val start: Int, val end: Int)

/** Longest line we lay out unwrapped; past this the row clips instead of growing the canvas. */
private const val MAX_COLS = 2000

/**
 * Read-only code view. Lines are laid out lazily, coloured on first display,
 * and the line-number gutter stays pinned while the text scrolls sideways.
 */
@Composable
fun CodeView(
    lines: List<String>,
    lang: Lang,
    modifier: Modifier = Modifier,
    wrap: Boolean = false,
    fontSize: Float = 12f,
    matches: List<Match> = emptyList(),
    active: Int = -1,
    state: LazyListState = rememberLazyListState(),
    onLongPressLine: (Int) -> Unit = {}
) {
    val palette = Ej.colors
    val density = LocalDensity.current
    val style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = fontSize.sp, lineHeight = (fontSize * 1.5f).sp)
    val measurer = rememberTextMeasurer()
    val charPx = remember(fontSize) { measurer.measure("0", style).size.width.toFloat() }
    val digits = lines.size.toString().length
    val gutter = with(density) { (charPx * digits).toDp() } + 18.dp
    val longest = remember(lines) { lines.maxOfOrNull { it.length }?.coerceAtMost(MAX_COLS) ?: 0 }
    val textWidth = with(density) { (charPx * (longest + 2)).toDp() }
    val byLine = remember(matches) { matches.withIndex().groupBy { it.value.line } }
    val h = rememberScrollState()
    val bg = MaterialTheme.colorScheme.background
    val fg = MaterialTheme.colorScheme.onBackground

    Box(modifier.fillMaxSize().background(bg).then(if (wrap) Modifier else Modifier.horizontalScroll(h))) {
        LazyColumn(
            state = state,
            modifier = (if (wrap) Modifier.fillMaxWidth() else Modifier.width(gutter + textWidth + 12.dp)).fillMaxHeight()
        ) {
            items(lines.size, contentType = { 0 }) { i ->
                val hits = byLine[i]
                val text = remember(lines[i], lang, hits, active) {
                    val base = highlight(lines[i], lang, palette)
                    if (hits == null) base else buildAnnotatedString {
                        append(base)
                        hits.forEach { (idx, m) ->
                            val end = m.end.coerceAtMost(base.length)
                            if (m.start < end) addStyle(
                                SpanStyle(background = if (idx == active) palette.matchActive else palette.match),
                                m.start, end
                            )
                        }
                    }
                }
                Row(Modifier.pointerInput(i) { detectTapGestures(onLongPress = { onLongPressLine(i) }) }) {
                    Text(
                        (i + 1).toString(),
                        style = style,
                        color = palette.gutter,
                        textAlign = TextAlign.End,
                        modifier = Modifier.width(gutter)
                            .offset { IntOffset(if (wrap) 0 else h.value, 0) }
                            .zIndex(1f)
                            .background(MaterialTheme.colorScheme.surface)
                            .padding(end = 8.dp)
                    )
                    Text(
                        text,
                        style = style,
                        color = fg,
                        softWrap = wrap,
                        maxLines = if (wrap) Int.MAX_VALUE else 1,
                        modifier = Modifier.weight(1f).padding(start = 8.dp)
                    )
                }
            }
        }
    }
}

/** Case-insensitive hits across all lines, capped so a one-letter query stays cheap. */
fun findMatches(lines: List<String>, query: String, cap: Int = 5000): List<Match> {
    if (query.isEmpty()) return emptyList()
    val out = ArrayList<Match>()
    for ((i, l) in lines.withIndex()) {
        var from = 0
        while (true) {
            val at = l.indexOf(query, from, ignoreCase = true)
            if (at < 0) break
            out.add(Match(i, at, at + query.length))
            if (out.size >= cap) return out
            from = at + query.length
        }
    }
    return out
}
