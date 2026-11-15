package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import org.agentej.android.Target
import java.util.Locale

private class DexClass(val index: Int, val name: String)

/** "Lcom/a/B;" -> "com.a.B". */
private fun dotted(desc: String) = desc.removePrefix("L").removeSuffix(";").replace('/', '.')

/** Dex editor for one DEX (plain file or archive entry): filterable class list, tap for smali. */
@Composable
fun DexScreen(t: Target, onBack: () -> Unit, onView: (String, String) -> Unit) {
    val scope = rememberCoroutineScope()
    val arc = t.archive ?: t.path
    val entry = t.entry ?: ""
    val classes by produceState<List<DexClass>?>(null, t) {
        value = withContext(Dispatchers.IO) {
            Native.dexClasses(arc, entry)?.mapIndexed { i, d -> DexClass(i, dotted(d)) } ?: emptyList()
        }
    }
    var query by rememberSaveable { mutableStateOf("") }
    val shown = remember(classes, query) {
        val q = query.trim().lowercase(Locale.US)
        classes?.let { all -> if (q.isEmpty()) all else all.filter { q in it.name.lowercase(Locale.US) } } ?: emptyList()
    }
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).safeDrawingPadding()) {
        EjTopBar(t.name, classes?.let { "${it.size} classes" } ?: "Reading…", leading = {
            IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back", onClick = onBack)
        })
        TextField(
            value = query, onValueChange = { query = it }, singleLine = true,
            placeholder = { Text("Filter classes", fontSize = 14.sp) },
            colors = TextFieldDefaults.colors(
                focusedContainerColor = MaterialTheme.colorScheme.surface,
                unfocusedContainerColor = MaterialTheme.colorScheme.surface,
                focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent
            ),
            modifier = Modifier.fillMaxWidth()
        )
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        when {
            classes == null -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
            classes!!.isEmpty() -> EmptyState("No classes", "This isn't a readable DEX file.")
            else -> LazyColumn(Modifier.fillMaxSize()) {
                items(shown, key = { it.index }) { c ->
                    Text(
                        c.name,
                        fontFamily = FontFamily.Monospace, fontSize = 12.5.sp, maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = MaterialTheme.colorScheme.onBackground,
                        modifier = Modifier.fillMaxWidth().height(36.dp).clickable {
                            scope.launch {
                                val smali = withContext(Dispatchers.IO) { Native.dexSmali(arc, entry, c.index) }
                                onView(c.name.substringAfterLast('.') + ".smali", smali)
                            }
                        }.padding(horizontal = 14.dp, vertical = 9.dp)
                    )
                }
            }
        }
    }
}
