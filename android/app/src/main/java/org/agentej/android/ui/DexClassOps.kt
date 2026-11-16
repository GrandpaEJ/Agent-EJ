package org.agentej.android.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import org.agentej.android.Native

/** What a long press in the dex editor acted on. */
internal sealed interface DexOpsTarget {
    class Cls(val c: DexCls) : DexOpsTarget
    class Pkg(val path: String) : DexOpsTarget
}

private val NAME = Regex("""[A-Za-z_$][\w$\-]*(\.[A-Za-z_$][\w$\-]*)*""")
private val CLASS_LINE = Regex("""^\.class\b[^\n]*?\s(L[^;\s]+;)""", RegexOption.MULTILINE)

internal fun classDesc(full: String) = "L" + full.replace('.', '/') + ";"
private fun pkgDesc(pkg: String) = "L" + pkg.replace('.', '/') + "/"
private fun inPkg(c: DexCls, pkg: String) = c.pkg == pkg || c.pkg.startsWith("$pkg.")

/** A class text with the editor's renames applied (how the compiled dex will read). */
internal fun DexEditorState.renamed(text: String): String =
    if (renames.isEmpty()) text
    else Native.smaliRename(text, renames.map { it.first }.toTypedArray(), renames.map { it.second }.toTypedArray())

private fun DexEditorState.exists(full: String) = classes.orEmpty().any { it.full == full }

/** A name renamed away earlier would be renamed again by the compile, so it stays taken. */
private fun DexEditorState.renamedAway(desc: String) = renames.any { (f, _) ->
    if (desc.endsWith(";")) f == desc || (f.endsWith("/") && desc.startsWith(f))
    else desc.startsWith(f) || f.startsWith(desc)
}

private fun DexEditorState.header(dex: Int) = "# ${src.label(dex)}\n\n"

/** Records one rename and renames the classes [map] picks; edits and history follow. */
private fun DexEditorState.rename(from: String, to: String, map: (DexCls) -> String?) {
    renames += from to to
    val renamedOne = { t: String -> Native.smaliRename(t, arrayOf(from), arrayOf(to)) }
    val moved = HashMap<DexCls, DexCls>()
    classes = classes.orEmpty().map { c -> map(c)?.let { c.named(it) }?.also { moved[c] = it } ?: c }
    for ((c, t) in edits.toList()) {
        edits.remove(c)
        edits[moved[c] ?: c] = renamedOne(t)
    }
    for (i in history.indices) moved[history[i]]?.let { history[i] = it }
    editGen++
}

/** Null when done, else why not. */
internal fun DexEditorState.renameClass(c: DexCls, to: String): String? {
    if (!NAME.matches(to)) return "Not a valid class name"
    if (to == c.full) return null
    if (exists(to)) return "$to already exists"
    if (renamedAway(classDesc(to))) return "$to was renamed earlier in this session"
    rename(classDesc(c.full), classDesc(to)) { if (it == c) to else null }
    return null
}

internal fun DexEditorState.renamePackage(pkg: String, to: String): String? {
    if (!NAME.matches(to)) return "Not a valid package name"
    if (to == pkg) return null
    val names = classes.orEmpty().map { it.full }.toHashSet()
    val clash = classes.orEmpty().filter { inPkg(it, pkg) }.firstOrNull { (to + it.full.removePrefix(pkg)) in names }
    if (clash != null) return "${to + clash.full.removePrefix(pkg)} already exists"
    if (renamedAway(pkgDesc(to))) return "$to was renamed earlier in this session"
    rename(pkgDesc(pkg), pkgDesc(to)) { if (inPkg(it, pkg)) to + it.full.removePrefix(pkg) else null }
    return null
}

internal fun DexEditorState.deleteClasses(cs: List<DexCls>) {
    if (cs.isEmpty()) return
    val gone = cs.toHashSet()
    for (c in cs) {
        edits.remove(c)
        if (!c.isNew) deleted += c
    }
    classes = classes.orEmpty().filter { it !in gone }
    history.removeAll { it in gone }
    editGen++
}

/** Adds a class to [dex] with [text] (smali), or a bare public class when null. */
internal fun DexEditorState.addClass(full: String, dex: Int, text: String? = null): String? {
    if (!NAME.matches(full)) return "Not a valid class name"
    if (exists(full)) return "$full already exists"
    if (renamedAway(classDesc(full))) return "$full was renamed earlier in this session"
    val c = DexCls(dex, nextNew++, "", "").named(full)
    edits[c] = header(dex) + (text ?: ".class public ${classDesc(full)}\n.super Ljava/lang/Object;\n.source \"${c.simple}.java\"\n\n\n" +
        "# direct methods\n.method public constructor <init>()V\n    .registers 1\n\n" +
        "    invoke-direct {p0}, Ljava/lang/Object;-><init>()V\n\n    return-void\n.end method\n")
    classes = classes.orEmpty() + c
    editGen++
    return null
}

/** Imports one smali file: replaces the class it declares, or adds it to [dex]. */
internal fun DexEditorState.importSmali(text: String, dex: Int): String {
    val desc = CLASS_LINE.find(text)?.groupValues?.get(1) ?: return "No .class line in that file"
    val full = desc.removePrefix("L").removeSuffix(";").replace('/', '.')
    val old = classes.orEmpty().firstOrNull { it.full == full }
    if (old != null) {
        edits[old] = header(old.dex) + text
        editGen++
        return "Replaced ${old.simple}"
    }
    return addClass(full, dex, text) ?: "Added ${full.substringAfterLast('.')}"
}

private fun DexEditorState.dexOf(t: DexOpsTarget) = when (t) {
    is DexOpsTarget.Cls -> t.c.dex
    is DexOpsTarget.Pkg -> classes.orEmpty().firstOrNull { inPkg(it, t.path) }?.dex ?: 0
}

private enum class Ask { MENU, COPY, ADD, RENAME, DELETE }

/** MT's long-press menu on a class or package and the dialogs behind it. */
@Composable
internal fun DexOpsDialogs(
    state: DexEditorState, t: DexOpsTarget, onCopy: (String) -> Unit, onToast: (String) -> Unit,
    onOpen: (DexCls) -> Unit, onDone: () -> Unit
) {
    var ask by remember(t) { mutableStateOf(Ask.MENU) }
    val context = LocalContext.current
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) {
            val text = runCatching { context.contentResolver.openInputStream(uri)?.use { it.readBytes().decodeToString() } }.getOrNull()
            onToast(if (text == null) "Cannot read that file" else state.importSmali(text, state.dexOf(t)))
        }
        onDone()
    }
    val cls = (t as? DexOpsTarget.Cls)?.c
    val name = cls?.full ?: (t as DexOpsTarget.Pkg).path
    val pkg = cls?.pkg ?: name
    when (ask) {
        Ask.MENU -> MenuDialog(if (cls != null) cls.simple else name, onDone, buildList {
            add("Copy" to { ask = Ask.COPY })
            add("Add class" to { ask = Ask.ADD })
            add("Import smali" to { pick.launch("*/*") })
            add((if (cls != null) "Rename" else "Rename package") to { ask = Ask.RENAME })
            add((if (cls != null) "Delete" else "Delete package") to { ask = Ask.DELETE })
        })
        Ask.COPY -> MenuDialog("Copy", onDone, buildList {
            if (cls != null) add("Class name  ${cls.simple}" to { onCopy(cls.simple); onDone() })
            add("Full name  $name" to { onCopy(name); onDone() })
            add("Descriptor  ${if (cls != null) classDesc(name) else pkgDesc(name)}" to {
                onCopy(if (cls != null) classDesc(name) else pkgDesc(name)); onDone()
            })
        })
        Ask.ADD -> NamePrompt("Add class", if (pkg.isEmpty()) "" else "$pkg.", "Class name (with package)", "Add", onDone) { n ->
            state.addClass(n.trim(), state.dexOf(t)).also { if (it == null) state.classes?.firstOrNull { c -> c.full == n.trim() }?.let(onOpen) }
        }
        Ask.RENAME -> NamePrompt(
            if (cls != null) "Rename class" else "Rename package", name, if (cls != null) "Class name (with package)" else "Package",
            "Rename", onDone,
            note = "References in the open dex are updated. The manifest and other dex are not."
        ) { n -> if (cls != null) state.renameClass(cls, n.trim()) else state.renamePackage(name, n.trim()) }
        Ask.DELETE -> {
            val victims = if (cls != null) listOf(cls) else state.classes.orEmpty().filter { inPkg(it, name) }
            AlertDialog(
                onDismissRequest = onDone,
                title = { Text(if (cls != null) "Delete class" else "Delete package") },
                text = { Text(if (cls != null) "Delete ${cls.full}?" else "Delete $name and its ${victims.size} class${if (victims.size == 1) "" else "es"}?") },
                confirmButton = { TextButton(onClick = { state.deleteClasses(victims); onDone() }) { Text("Delete") } },
                dismissButton = { TextButton(onClick = onDone) { Text("Cancel") } }
            )
        }
    }
}

@Composable
private fun MenuDialog(title: String, onDismiss: () -> Unit, items: List<Pair<String, () -> Unit>>) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title, fontSize = 16.sp, maxLines = 2) },
        text = {
            Column {
                for ((label, act) in items) Text(
                    label, fontSize = 15.sp, maxLines = 2, color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.fillMaxWidth().clickable(onClick = act).padding(vertical = 12.dp)
                )
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } }
    )
}

/** Text prompt; [onOk] returns null to close or an error to show under the field. */
@Composable
private fun NamePrompt(
    title: String, initial: String, label: String, ok: String, onDismiss: () -> Unit,
    note: String? = null, onOk: (String) -> String?
) {
    var v by remember { mutableStateOf(initial) }
    var err by remember { mutableStateOf<String?>(null) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (note != null) Text(note, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                OutlinedTextField(
                    v, { v = it; err = null }, label = { Text(label) }, singleLine = true, isError = err != null,
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    supportingText = err?.let { { Text(it) } }, modifier = Modifier.fillMaxWidth()
                )
            }
        },
        confirmButton = { TextButton(onClick = { err = onOk(v); if (err == null) onDismiss() }) { Text(ok) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
