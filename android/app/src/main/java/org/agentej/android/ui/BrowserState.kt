package org.agentej.android.ui

import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.agentej.android.FsEntry
import org.agentej.android.Native
import org.agentej.android.ThumbSpec
import org.agentej.android.Thumbs
import org.agentej.android.Util

/** Navigation callbacks the browser needs from the app shell. */
class BrowserNav(
    val canBack: Boolean,
    val canForward: Boolean,
    val back: () -> Unit,
    val forward: () -> Unit,
    val open: (String) -> Unit,
    val apk: (String) -> Unit,
    val drawer: () -> Unit,
    /** Dual: swap the two panes. Single: switch to the other pane. */
    val swap: () -> Unit
)

sealed interface BrowserDialog {
    data class Rename(val entry: FsEntry) : BrowserDialog
    data class Delete(val paths: List<String>) : BrowserDialog
    data class Create(val folder: Boolean) : BrowserDialog
    data class Info(val entry: FsEntry) : BrowserDialog
}

/** Everything one directory listing shows; lives as long as its back-stack entry. */
@Stable
class BrowserState(val path: String) {
    val archive: Pair<String, String>? = Util.splitArchive(path)
    val inArchive get() = archive != null
    val isArchiveRoot get() = archive != null && archive.second.isEmpty()

    var raw by mutableStateOf<List<FsEntry>>(emptyList())
    var loading by mutableStateOf(true)
    var unreadable by mutableStateOf(false)
    var disk by mutableStateOf("")
    /** Bump to re-read the listing (after a file operation, pull-to-refresh). */
    var reload by mutableIntStateOf(0)

    var selected by mutableStateOf<Set<String>>(emptySet())
    val selecting get() = selected.isNotEmpty()

    var searching by mutableStateOf(false)
    var query by mutableStateOf("")
    /** Recursive hits; names are paths relative to [path]. Null = showing the folder itself. */
    var deepHits by mutableStateOf<List<FsEntry>?>(null)
    var deepBusy by mutableStateOf(false)

    var menuFor by mutableStateOf<FsEntry?>(null)
    var dialog by mutableStateOf<BrowserDialog?>(null)
    /** APK info dialog (and its signature sheet) for this path; kept here so it survives a hex view on top. */
    var apkInfo by mutableStateOf<String?>(null)
    var apkSig by mutableStateOf(false)

    fun full(e: FsEntry) = Util.join(path, e.name)

    /** The entry as something an action can work on. */
    fun target(e: FsEntry): org.agentej.android.Target {
        val split = archive
        return if (split != null) org.agentej.android.Target(full(e), e.isDir, split.first, split.second + e.name)
        else org.agentej.android.Target(full(e), e.isDir)
    }

    /** What to draw in this entry's tile, or null for the plain type tile. */
    fun thumbSpec(e: FsEntry): ThumbSpec? {
        if (e.isDir) return null
        val kind = Thumbs.kindOf(e.name, inArchive) ?: return null
        val split = archive
        return if (split != null) ThumbSpec(split.first, split.second + e.name, kind, "${e.size}")
        else ThumbSpec(full(e), null, kind, "${e.size}:${e.mtime}")
    }

    fun toggle(e: FsEntry) {
        selected = if (e.name in selected) selected - e.name else selected + e.name
    }

    fun selectedPaths() = selected.map { Util.join(path, it) }

    /** Blocking; call on Dispatchers.IO. */
    fun load() {
        val split = archive
        if (split != null) {
            val lines = Native.zipEntries(split.first)?.toList()
            unreadable = lines == null
            raw = Util.archiveChildren(lines ?: emptyList(), split.second)
            disk = ""
        } else {
            val list = Native.listDir(path)
            unreadable = list == null
            raw = list?.map(Util::parseEntry) ?: emptyList()
            val d = Native.diskStats(path).split('\t')
            val used = d.getOrNull(0)?.toLongOrNull() ?: 0L
            val total = d.getOrNull(1)?.toLongOrNull() ?: 0L
            disk = if (total > 0) "Disk: ${Util.compactSize(used)}/${Util.compactSize(total)}" else ""
        }
        selected = selected.filter { n -> raw.any { it.name == n } }.toSet()
    }

    fun deepSearch() {
        val q = query.trim()
        if (q.isEmpty() || inArchive) return
        val hits = Native.findFiles(path, q, 500)?.map(Util::parseEntry) ?: emptyList()
        deepHits = hits
    }
}
