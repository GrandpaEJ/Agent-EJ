package org.agentej.android.ui

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import org.agentej.android.Outputs
import org.agentej.android.Util
import org.agentej.android.Workspace

/** Outcome of compiling the dex editor's edits (MT's hammer). */
internal data class DexSaveResult(val ok: Boolean, val out: String, val detail: String)

/** "# classesN.dex" and the blank line the editor puts above each class. */
private fun stripHeader(text: String): String =
    if (text.startsWith("# ")) text.substringAfter('\n').removePrefix("\n") else text

/**
 * Next to the source as "<name>_edited.apk" (or .dex), or in the workspace when that folder is
 * read-only. The name is fixed, so compiling again replaces the last build: edits stay in the
 * editor and every build starts from the untouched original.
 */
internal fun dexSaveTarget(src: DexSource): String {
    val bare = src.entries.size == 1 && src.entries[0].isEmpty()
    val dir = Util.parent(src.archive).takeIf { java.io.File(it).canWrite() } ?: Workspace.output
    return Util.join(dir, Outputs.base(src.archive) + if (bare) "_edited.dex" else "_edited.apk")
}

/** Rebuilds every dex that holds an edited class; blocking work runs on IO. */
internal suspend fun compileEdits(state: DexEditorState): DexSaveResult = withContext(Dispatchers.IO) {
    val src = state.src
    val edits = state.edits.toList()
    val out = dexSaveTarget(src)
    java.io.File(out).parentFile?.mkdirs()
    val res = Native.dexSave(
        src.archive,
        edits.map { src.entries[it.first.dex] }.toTypedArray(),
        edits.map { it.first.index }.toIntArray(),
        edits.map { stripHeader(it.second) }.toTypedArray(),
        out
    )
    val rc = Util.parseRc(res)
    DexSaveResult(rc.code == 0, out, rc.detail)
}

@Composable
internal fun DexSaveDialog(r: DexSaveResult, onInstall: (() -> Unit)?, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (r.ok) "Compiled" else "Compile failed") },
        text = { Text(if (r.ok) "${r.detail}\n\n${r.out}" else r.detail) },
        confirmButton = {
            if (r.ok && onInstall != null) TextButton(onClick = onInstall) { Text("Install") }
            else TextButton(onClick = onDismiss) { Text("OK") }
        },
        dismissButton = { if (r.ok && onInstall != null) TextButton(onClick = onDismiss) { Text("Close") } }
    )
}

@Composable
internal fun DexDiscardDialog(count: Int, onCompile: () -> Unit, onDiscard: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Uncompiled edits") },
        text = { Text("$count edited class${if (count == 1) "" else "es"} not compiled into an APK yet.") },
        confirmButton = { TextButton(onClick = onCompile) { Text("Compile") } },
        dismissButton = { TextButton(onClick = onDiscard) { Text("Discard") } }
    )
}
