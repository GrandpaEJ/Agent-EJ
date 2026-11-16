package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale

/** One member of a smali class, as the Navigation outline lists it. */
data class OutlineItem(val kind: Char, val name: String, val desc: String, val line: Int, val col: Int)

/**
 * Class, fields and methods of a smali file in source order: `.class` gives the C row,
 * `.field name:Type` the F rows, `.method name(args)ret` the M rows. [OutlineItem.col]
 * is where the name starts on its line, so a jump can highlight it.
 */
fun smaliOutline(lines: List<String>): List<OutlineItem> {
    val out = ArrayList<OutlineItem>()
    for ((i, raw) in lines.withIndex()) {
        val l = raw.trimStart()
        when {
            l.startsWith(".class ") -> {
                val desc = l.substringAfterLast(' ')
                val simple = desc.removePrefix("L").removeSuffix(";").substringAfterLast('/')
                out += OutlineItem('C', simple, desc, i, raw.lastIndexOf(simple).coerceAtLeast(0))
            }
            l.startsWith(".field ") -> {
                /* ".field public static final x:I = 0x1" — the name:type token is the
                 * last one before an optional "= value" */
                val decl = l.substringBefore(" = ").trimEnd()
                val tok = decl.substringAfterLast(' ')
                val colon = tok.indexOf(':')
                if (colon > 0) {
                    val name = tok.substring(0, colon)
                    out += OutlineItem('F', name, tok.substring(colon + 1), i, raw.indexOf(tok).coerceAtLeast(0))
                }
            }
            l.startsWith(".method ") -> {
                val tok = l.substringAfterLast(' ')
                val paren = tok.indexOf('(')
                if (paren > 0) {
                    out += OutlineItem('M', tok.substring(0, paren), tok.substring(paren), i, raw.indexOf(tok).coerceAtLeast(0))
                }
            }
        }
    }
    return out
}

private val CONST_STRING = Regex("""^\s*const-string(?:/jumbo)?\s+[vp]\d+,\s*(".*")\s*$""")

/** String constants loaded by the class (`const-string`), as S rows; [OutlineItem.col] is the opening quote. */
fun smaliStrings(lines: List<String>): List<OutlineItem> {
    val out = ArrayList<OutlineItem>()
    for ((i, raw) in lines.withIndex()) {
        val m = CONST_STRING.find(raw) ?: continue
        val lit = m.groupValues[1]
        out += OutlineItem('S', lit.substring(1, lit.length - 1), "line ${i + 1}", i, m.groups[1]!!.range.first)
    }
    return out
}

/**
 * MT-style "Navigation": filterable outline of the class; tapping a row jumps to it.
 * Switch flips between the members and the string constants the code loads.
 */
@Composable
fun NavigationDialog(members: List<OutlineItem>, strings: List<OutlineItem>, onPick: (OutlineItem) -> Unit, onDismiss: () -> Unit) {
    var showStrings by remember { mutableStateOf(false) }
    val items = if (showStrings) strings else members
    var filter by remember { mutableStateOf<String?>(null) }
    val shown = remember(items, filter) {
        val q = filter?.trim()?.lowercase(Locale.US).orEmpty()
        if (q.isEmpty()) items else items.filter { q in it.name.lowercase(Locale.US) || q in it.desc.lowercase(Locale.US) }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Navigation", Modifier.weight(1f))
                if (filter == null) IconBtn(Icons.Filled.Search, "Filter members") { filter = "" }
                else IconBtn(Icons.Filled.Close, "Close filter") { filter = null }
            }
        },
        text = {
            Column {
                if (filter != null) {
                    val focus = remember { FocusRequester() }
                    LaunchedEffect(Unit) { focus.requestFocus() }
                    TextField(
                        value = filter!!, onValueChange = { filter = it }, singleLine = true,
                        placeholder = { Text("Name or descriptor", fontSize = 14.sp) },
                        colors = TextFieldDefaults.colors(
                            focusedContainerColor = Color.Transparent, unfocusedContainerColor = Color.Transparent
                        ),
                        modifier = Modifier.fillMaxWidth().focusRequester(focus)
                    )
                }
                if (shown.isEmpty()) Text(
                    when {
                        items.isNotEmpty() -> "Nothing matches."
                        showStrings -> "This class loads no string constants."
                        else -> "No class members found."
                    },
                    color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(vertical = 12.dp)
                )
                LazyColumn {
                    items(shown, key = { it.line }) { it -> OutlineRow(it) { onPick(it) } }
                }
            }
        },
        dismissButton = {
            TextButton(onClick = { showStrings = !showStrings }) { Text(if (showStrings) "Members" else "Strings") }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } }
    )
}

@Composable
private fun OutlineRow(item: OutlineItem, onClick: () -> Unit) {
    /* MT's colours: class blue, field orange, method red */
    val (bg, fg) = when (item.kind) {
        'C' -> Color(0xFF2E4F8F) to Color(0xFFD3E2FF)
        'F' -> Color(0xFF8A4B12) to Color(0xFFFFE0C2)
        'S' -> Color(0xFF2F6B3A) to Color(0xFFD2F5D9)
        else -> Color(0xFF8A2A2A) to Color(0xFFFFD6D6)
    }
    Row(
        Modifier.fillMaxWidth().heightIn(min = 48.dp).clickable(onClick = onClick).padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(Modifier.size(28.dp).clip(CircleShape).background(bg), contentAlignment = Alignment.Center) {
            Text(item.kind.toString(), fontSize = 12.sp, fontWeight = FontWeight.Bold, color = fg)
        }
        Spacer(Modifier.width(14.dp))
        Column {
            Text(item.name, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onSurface)
            Text(item.desc, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
