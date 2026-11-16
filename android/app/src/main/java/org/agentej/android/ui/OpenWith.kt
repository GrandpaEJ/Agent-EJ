package org.agentej.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SecondaryTabRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import org.agentej.android.Act
import org.agentej.android.External
import org.agentej.android.FileKind
import org.agentej.android.FileKinds
import org.agentej.android.Target

/** Tile colour/glyph for a built-in viewer, reusing the file-type tiles. */
private fun sample(a: Act) = when (a) {
    Act.TEXT -> "a.txt"; Act.HEX -> "a.bin"; Act.IMAGE -> "a.png"; Act.ARCHIVE -> "a.zip"
    Act.DEX_BROWSE -> "a.dex"; Act.AXML_VIEW -> "a.xml"; Act.ARSC_STRINGS -> "a.arsc"
    Act.APK_TOOLS, Act.MERGE -> "a.apk"; else -> "a"
}

@Composable
private fun DialogFrame(title: String, onDismiss: () -> Unit, content: @Composable () -> Unit) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = RoundedCornerShape(10.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Column(Modifier.padding(top = 18.dp, bottom = 4.dp)) {
                Text(title, fontSize = 18.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.padding(horizontal = 22.dp))
                content()
                TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End).padding(end = 8.dp)) { Text("CLOSE") }
            }
        }
    }
}

/** MT's per-type function list ("Dex functions", "Apk functions"…), shown on tap. */
@Composable
fun TypeMenu(t: Target, acts: List<Act>, onPick: (Act) -> Unit, onDismiss: () -> Unit) {
    DialogFrame(t.name, onDismiss) {
        LazyColumn(Modifier.padding(top = 8.dp).heightIn(max = 460.dp)) {
            items(if (t.isDir) acts else acts + Act.OPEN_WITH) { a ->
                Text(
                    a.label, fontSize = 15.sp,
                    modifier = Modifier.fillMaxWidth().clickable { onDismiss(); onPick(a) }
                        .padding(horizontal = 22.dp, vertical = 13.dp)
                )
            }
        }
    }
}

@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
/** "Open with…": built-in viewers as a grid, or another app for a chosen type. */
@Composable
fun OpenWithDialog(t: Target, onPick: (Act) -> Unit, onExternal: (String?) -> Unit, onDismiss: () -> Unit) {
    var tab by remember { mutableIntStateOf(0) }
    val external = !t.inArchive && t.kind != FileKind.DIR
    DialogFrame("Open with…", onDismiss) {
        if (external) SecondaryTabRow(selectedTabIndex = tab, containerColor = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Tab(tab == 0, { tab = 0 }, text = { Text("Built-in") })
            Tab(tab == 1, { tab = 1 }, text = { Text("Other apps") })
        }
        if (tab == 0) LazyVerticalGrid(GridCells.Fixed(3), Modifier.padding(8.dp).heightIn(max = 420.dp)) {
            items(FileKinds.builtIns(t.inArchive)) { a ->
                Column(
                    Modifier.clickable { onDismiss(); onPick(a) }.padding(vertical = 12.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    FileTile(sample(a), false, size = 44.dp)
                    Text(a.label, fontSize = 12.sp, textAlign = TextAlign.Center, maxLines = 2,
                        lineHeight = 14.sp, modifier = Modifier.padding(top = 6.dp, start = 4.dp, end = 4.dp))
                }
            }
        } else LazyColumn(Modifier.padding(top = 4.dp)) {
            items(External.As.entries) { k ->
                Row(
                    Modifier.fillMaxWidth().height(48.dp).clickable { onDismiss(); onExternal(k.mime) }.padding(horizontal = 22.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) { Text(k.label, fontSize = 15.sp) }
            }
        }
    }
}
