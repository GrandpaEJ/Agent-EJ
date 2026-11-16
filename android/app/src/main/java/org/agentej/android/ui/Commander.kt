package org.agentej.android.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.SaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.Act
import org.agentej.android.External
import org.agentej.android.FileClipboard
import org.agentej.android.FileKind
import org.agentej.android.FileKinds
import org.agentej.android.FileOps
import org.agentej.android.FsEntry
import org.agentej.android.Prefs
import org.agentej.android.Rc
import org.agentej.android.AxmlText
import org.agentej.android.Route
import org.agentej.android.Target
import org.agentej.android.Tools
import org.agentej.android.Util
import org.agentej.android.dirHint
import org.agentej.android.extractTo

/** A dialog the commander is showing over the panes. */
private const val STORAGE_ROOT = "/storage/emulated/0"

private sealed interface Popup {
    data class Menu(val st: BrowserState, val e: FsEntry) : Popup
    data class Type(val t: Target, val acts: List<Act>) : Popup
    data class OpenWith(val t: Target) : Popup
    data class MultiDex(val archive: String, val entries: List<String>, val checked: String?) : Popup
}

/**
 * The file manager: one or two panes (MT's dual-pane commander), the bars for
 * whichever pane is active, and every dialog and tool those panes can start.
 */
@Composable
fun Commander(
    panes: List<Pane>,
    active: Int,
    onActivate: (Int) -> Unit,
    onSwap: () -> Unit,
    prefs: Prefs,
    clipboard: FileClipboard,
    saver: SaveableStateHolder,
    push: (Route) -> Unit,
    openDrawer: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snack = remember { SnackbarHostState() }
    var popup by remember { mutableStateOf<Popup?>(null) }
    var busy by remember { mutableStateOf<String?>(null) }
    val dual = prefs.dual
    val pane = panes[active]
    val other = panes[1 - active]
    val st = pane.st
    val shown = shownEntries(st, prefs)
    /** Folder name of the other pane when files can go straight there. */
    val target2 = if (dual && !other.st.inArchive) Util.title(other.path) else null

    fun note(msg: String) = scope.launch { snack.showSnackbar(msg) }
    fun reloadAll() = panes.forEach { it.reload() }

    /** Runs a blocking file op off the main thread, then refreshes both panes. */
    fun perform(op: () -> Rc) = scope.launch {
        val rc = withContext(Dispatchers.IO) { op() }
        panes.forEach { it.st.selected = emptySet() }
        reloadAll()
        note(rc.toString())
    }

    fun job(act: Act, t: Target) = scope.launch {
        busy = "${Tools.verb(act)} ${t.name}…"
        val r = withContext(Dispatchers.IO) { Tools.run(act, t) }
        busy = null
        reloadAll()
        val out = r.output
        if (r.rc.ok && out != null && act != Act.DEOBF) {
            val res = snack.showSnackbar("${r.rc.detail} → ${out.substringAfterLast('/')}", actionLabel = "SHOW")
            if (res == SnackbarResult.ActionPerformed) {
                val isDir = FileOps.exists("$out/.")
                (if (dual) other else pane).open(if (isDir) out else Util.parent(out))
                if (dual) onActivate(1 - active)
            }
        } else note(r.rc.toString())
    }

    fun viewText(act: Act, t: Target) = scope.launch {
        val v = withContext(Dispatchers.IO) { Tools.view(act, t, context.packageManager) }
        when {
            v != null && act == Act.AXML_VIEW -> push(AxmlText.route(v.first, v.second, t.archive ?: t.path, t.entry ?: ""))
            v != null -> push(Route.Text(v.first, v.second, if (act == Act.TEXT && !t.inArchive) t.path else null))
            act == Act.TEXT -> {
                val r = snack.showSnackbar("${t.name} looks binary", actionLabel = "HEX")
                if (r == SnackbarResult.ActionPerformed) push(Route.Hex(t))
            }
            else -> note("Can't decode ${t.name} as ${act.label.lowercase()}")
        }
    }

    fun external(t: Target, mime: String?) {
        if (t.inArchive) { note("Extract it first to open in another app"); return }
        External.view(context, t.path, mime)?.let { note(it) }
    }

    fun act(a: Act, t: Target) {
        when (a) {
            Act.OPEN -> pane.open(if (t.path.endsWith("/")) t.path else "${t.path}/")
            Act.ARCHIVE -> pane.open("${t.path.trimEnd('/')}/")
            Act.TEXT, Act.AXML_VIEW, Act.DEX_INFO, Act.ARSC_STRINGS -> viewText(a, t)
            Act.HEX -> push(Route.Hex(t))
            Act.IMAGE -> push(Route.Image(t))
            Act.DEX_BROWSE -> {
                val arc = t.archive ?: t.path
                if (t.kind == FileKind.APK && !t.inArchive || DexFiles.isRootDex(t.entry)) scope.launch {
                    val all = withContext(Dispatchers.IO) { DexFiles.entries(arc) }
                    when {
                        all.isEmpty() -> note("No classes.dex in ${t.name}")
                        all.size == 1 -> push(Route.Dex(DexSource(arc, all)))
                        else -> popup = Popup.MultiDex(arc, all, t.entry)
                    }
                } else push(Route.Dex(DexSource(arc, listOf(t.entry ?: ""))))
            }
            Act.APK_TOOLS -> push(Route.Apk(t.path))
            Act.INSTALL -> External.install(context, t.path)?.let { note(it) }
            Act.EXTERNAL -> external(t, null)
            Act.OPEN_WITH -> popup = Popup.OpenWith(t)
            else -> if (a in Tools.jobs) job(a, t)
        }
    }

    /** MT's tap rules: known types open directly or show their functions; unknown asks. */
    fun tap(p: Pane, e: FsEntry) {
        val s = p.st
        if (s.selecting) { s.toggle(e); return }
        val t = s.target(e)
        when (t.kind) {
            FileKind.DIR -> p.open("${t.path.trimEnd('/')}/")
            FileKind.ARCHIVE -> p.open("${t.path}/")
            FileKind.TEXT -> act(Act.TEXT, t)
            FileKind.XML -> if (t.inArchive) act(Act.AXML_VIEW, t) else scope.launch {
                val plain = withContext(Dispatchers.IO) { Tools.view(Act.TEXT, t) }
                if (plain != null) push(Route.Text(plain.first, plain.second, t.path)) else act(Act.AXML_VIEW, t)
            }
            FileKind.IMAGE -> act(Act.IMAGE, t)
            FileKind.AUDIO, FileKind.VIDEO, FileKind.PDF ->
                if (t.inArchive) popup = Popup.OpenWith(t) else act(Act.EXTERNAL, t)
            FileKind.APK -> if (t.inArchive) popup = Popup.Type(t, FileKinds.actions(t.kind, true)) else { s.apkSig = false; s.apkInfo = t.path }
            FileKind.SPLITS, FileKind.DEX, FileKind.ARSC ->
                popup = Popup.Type(t, FileKinds.actions(t.kind, t.inArchive))
            FileKind.UNKNOWN -> popup = Popup.OpenWith(t)
        }
    }

    fun long(p: Pane, e: FsEntry) {
        if (p.st.selecting) p.st.toggle(e) else popup = Popup.Menu(p.st, e)
    }

    BackHandler(enabled = st.selecting) { st.selected = emptySet() }
    BackHandler(enabled = !st.selecting && st.searching) {
        st.searching = false; st.query = ""; st.deepHits = null
    }
    /* Back: pane history first, then up a folder (not above internal storage),
     * and at the top a second press within 2 s closes the app, as in MT */
    var exitArmedAt by remember { mutableLongStateOf(0L) }
    BackHandler(enabled = !st.selecting && !st.searching) {
        val up = Util.up(pane.path)
        val atTop = pane.path.trimEnd('/').let { it == STORAGE_ROOT || it == "/sdcard" || it.isEmpty() }
        when {
            pane.canBack -> pane.back()
            up != null && !atTop -> pane.open(up)
            android.os.SystemClock.uptimeMillis() - exitArmedAt < 2000 -> (context as? android.app.Activity)?.finish()
            else -> {
                exitArmedAt = android.os.SystemClock.uptimeMillis()
                android.widget.Toast.makeText(context, "Press again to exit", android.widget.Toast.LENGTH_SHORT).show()
            }
        }
    }

    val nav = BrowserNav(
        canBack = pane.canBack, canForward = pane.canForward,
        back = { pane.back() }, forward = { pane.forward() },
        open = { pane.open(it) }, apk = { push(Route.Apk(it)) },
        drawer = openDrawer, swap = onSwap
    )

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surface).safeDrawingPadding()) {
        Column(Modifier.fillMaxSize()) {
            when {
                st.selecting -> SelectionBar(
                    st, shown, clipboard,
                    onDelete = { st.dialog = BrowserDialog.Delete(st.selectedPaths()) },
                    onInfo = { st.dialog = BrowserDialog.Info(it) },
                    onRename = { st.dialog = BrowserDialog.Rename(it) },
                    notify = { note(it) },
                    toOther = if (target2 != null) { cut ->
                        val src = st.selectedPaths()
                        perform { FileOps.paste(src, other.path, cut) }
                    } else null
                )
                st.searching -> SearchBar(st, onDeep = {
                    scope.launch {
                        st.deepBusy = true
                        withContext(Dispatchers.IO) { st.deepSearch() }
                        st.deepBusy = false
                    }
                })
                else -> BrowseBar(st, prefs, nav, shown.size, onRefresh = { st.reload++ })
            }
            Box(Modifier.fillMaxWidth().height(2.dp)) {
                if (st.loading || st.deepBusy) LinearProgressIndicator(Modifier.fillMaxSize())
                else HorizontalDivider(Modifier.align(Alignment.TopStart), color = MaterialTheme.colorScheme.outlineVariant)
            }
            Row(Modifier.weight(1f).fillMaxWidth()) {
                val visible = if (dual) listOf(0, 1) else listOf(active)
                visible.forEach { i ->
                    val p = panes[i]
                    val isActive = i == active
                    Column(
                        Modifier.weight(1f).fillMaxHeight()
                            .background(if (isActive || !dual) MaterialTheme.colorScheme.background else Ej.colors.paneIdle)
                    ) {
                        // MT: any touch in a pane makes it the active one (bars follow it).
                        Box(Modifier.weight(1f).pointerInput(i) {
                            awaitEachGesture { awaitFirstDown(requireUnconsumed = false); onActivate(i) }
                        }) {
                            saver.SaveableStateProvider(p.current.id) {
                                PaneList(
                                    p.st, prefs, clipboard, narrow = dual,
                                    upTo = Util.up(p.path), onUp = { up -> onActivate(i); p.open(up) },
                                    onTap = { e -> onActivate(i); tap(p, e) },
                                    onLong = { e -> onActivate(i); long(p, e) }
                                )
                            }
                        }
                    }
                    if (dual && i == 0) Box(Modifier.width(1.dp).fillMaxHeight().background(MaterialTheme.colorScheme.outlineVariant))
                }
            }
            if (!clipboard.isEmpty && !st.inArchive) PasteStrip(clipboard, onPaste = {
                val src = clipboard.paths
                val cut = clipboard.cut
                if (cut) clipboard.clear()
                perform { FileOps.paste(src, pane.path, cut) }
            })
            BottomBar(nav, upTo = Util.up(pane.path), canCreate = !st.inArchive, onCreate = {
                st.dialog = BrowserDialog.Create(it)
            })
        }
        SnackbarHost(snack, Modifier.align(Alignment.BottomCenter).padding(bottom = 60.dp))
    }

    when (val p = popup) {
        null -> Unit
        is Popup.Menu -> {
            val t = p.st.target(p.e)
            val writable = !t.inArchive
            val tools = if (t.kind == FileKind.DIR && writable) FileKinds.actions(FileKind.DIR, false, dirHint(t.path))
            else FileKinds.actions(t.kind, t.inArchive)
            EntryMenu(t, p.e, EntryActions(
                toOther = target2,
                select = { p.st.selected = setOf(p.e.name) },
                copy = {
                    if (target2 != null) perform { FileOps.paste(listOf(t.path), other.path, false) }
                    else { clipboard.set(listOf(t.path), false); note("Ready to paste — open the target folder") }
                },
                move = if (!writable) null else ({
                    if (target2 != null) perform { FileOps.paste(listOf(t.path), other.path, true) }
                    else { clipboard.set(listOf(t.path), true); note("Ready to move — open the target folder") }
                }),
                extract = if (!t.inArchive) null else ({
                    val dest = if (target2 != null) other.path else Util.parent(t.archive!!)
                    perform { extractTo(t, dest) }
                }),
                delete = if (!writable) null else ({ p.st.dialog = BrowserDialog.Delete(listOf(t.path)) }),
                rename = if (!writable) null else ({ p.st.dialog = BrowserDialog.Rename(p.e) }),
                tools = if (tools.isEmpty()) null else ({ popup = Popup.Type(t, tools) }),
                compress = if (writable && t.isDir) ({ act(Act.COMPRESS, t) }) else null,
                info = { p.st.dialog = BrowserDialog.Info(p.e) },
                share = if (writable && !t.isDir) ({ External.share(context, t.path)?.let { note(it) } }) else null,
                openWith = if (t.isDir) null else ({ popup = Popup.OpenWith(t) }),
                bookmark = if (writable && t.isDir) ({ prefs.toggleBookmark(t.path) }) else null,
                bookmarked = prefs.isBookmarked(t.path)
            ), onDismiss = { if (popup === p) popup = null })
        }
        is Popup.MultiDex -> MultiDexDialog(
            p.entries, p.checked,
            onOpen = { popup = null; push(Route.Dex(DexSource(p.archive, it))) },
            onDismiss = { if (popup === p) popup = null }
        )
        is Popup.Type -> TypeMenu(p.t, p.acts, onPick = { act(it, p.t) }, onDismiss = { if (popup === p) popup = null })
        is Popup.OpenWith -> OpenWithDialog(
            p.t, onPick = { act(it, p.t) }, onExternal = { external(p.t, it) },
            onDismiss = { if (popup === p) popup = null }
        )
    }
    panes.forEach { BrowserDialogs(it.st, perform = { op -> perform(op) }) }
    panes.forEach { pn ->
        val ps = pn.st
        val apk = ps.apkInfo ?: return@forEach
        val t = org.agentej.android.Target(apk, false)
        ApkInfoDialog(
            apk, sigOpen = ps.apkSig, onSig = { ps.apkSig = it },
            onFunction = { ps.apkInfo = null; popup = Popup.Type(t, FileKinds.actions(FileKind.APK, false)) },
            onView = { ps.apkInfo = null; act(Act.ARCHIVE, t) },
            onInstall = { ps.apkInfo = null; act(Act.INSTALL, t) },
            onOpenDir = { ps.apkInfo = null; pn.open("${it.trimEnd('/')}/") },
            onHex = { push(Route.Hex(org.agentej.android.Target(it, false))) },
            onDismiss = { ps.apkInfo = null }
        )
    }
    busy?.let { BusyDialog(it) }
}

@Composable
private fun BusyDialog(text: String) {
    Dialog(onDismissRequest = {}, properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false)) {
        Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Row(Modifier.padding(22.dp), verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.width(28.dp).height(28.dp), strokeWidth = 3.dp)
                Text(text, fontSize = 14.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 16.dp))
            }
        }
    }
}
