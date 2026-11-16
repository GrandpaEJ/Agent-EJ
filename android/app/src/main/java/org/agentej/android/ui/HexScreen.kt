package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.Native
import org.agentej.android.Target
import org.agentej.android.Util

private const val ROW = 16
private const val CHUNK = 4096
/** Entries inside an archive are read whole; cap them so the viewer stays small. */
private const val ENTRY_MAX = 16 * 1024 * 1024

/** Byte source for the viewer: pages of a plain file, or one archive entry in memory. */
private class HexSource(t: Target) {
    private val entry: ByteArray? = t.archive?.let { a ->
        Native.readEntry(a, t.entry ?: "")?.let { if (it.size > ENTRY_MAX) it.copyOf(ENTRY_MAX) else it }
    }
    private val path = t.path
    val size: Long = entry?.size?.toLong() ?: Native.statPath(path).split('\t').getOrNull(1)?.toLongOrNull() ?: 0L
    private val pages = object : android.util.LruCache<Long, ByteArray>(32) {}

    fun row(index: Long): ByteArray {
        val off = index * ROW
        entry?.let { return it.copyOfRange(off.toInt(), minOf(it.size, (off + ROW).toInt())) }
        val page = off / CHUNK
        val bytes = pages.get(page) ?: (Native.readBytes(path, page * CHUNK, CHUNK) ?: ByteArray(0)).also { pages.put(page, it) }
        val from = (off - page * CHUNK).toInt()
        return if (from >= bytes.size) ByteArray(0) else bytes.copyOfRange(from, minOf(bytes.size, from + ROW))
    }
}

@Composable
fun HexScreen(t: Target, onBack: () -> Unit) {
    val src = remember(t) { HexSource(t) }
    val rows = (src.size + ROW - 1) / ROW
    val dim = MaterialTheme.colorScheme.onSurfaceVariant
    val accent = Ej.colors.tileFolder
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).safeDrawingPadding()) {
        EjTopBar(t.name, "${Util.humanSize(src.size)} · ${src.size} bytes", leading = {
            IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back", onClick = onBack)
        })
        if (src.size == 0L) { EmptyState("Nothing to show", "The file is empty or can't be read."); return }
        SelectionContainer {
            LazyColumn(Modifier.fillMaxSize().padding(horizontal = 8.dp)) {
                items(rows.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()) { i ->
                    val b = src.row(i.toLong())
                    Text(
                        buildAnnotatedString {
                            withStyle(SpanStyle(color = accent)) { append("%08X  ".format(i.toLong() * ROW)) }
                            for (k in 0 until ROW) {
                                append(if (k < b.size) "%02X".format(b[k].toInt() and 0xff) else "  ")
                                append(if (k == 7) "  " else " ")
                            }
                            withStyle(SpanStyle(color = dim)) {
                                append(' ')
                                b.forEach { val c = it.toInt() and 0xff; append(if (c in 0x20..0x7e) c.toChar() else '.') }
                            }
                        },
                        fontFamily = FontFamily.Monospace,
                        fontSize = 10.5.sp,
                        lineHeight = 15.sp,
                        maxLines = 1,
                        color = MaterialTheme.colorScheme.onBackground
                    )
                }
            }
        }
    }
}
