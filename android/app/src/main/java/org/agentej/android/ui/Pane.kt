package org.agentej.android.ui

import androidx.compose.runtime.Stable
import androidx.compose.runtime.mutableStateListOf

/** One open folder in a pane's history; [id] keys its saved scroll state. */
class PaneEntry(val id: Long, val state: BrowserState)

/**
 * One file pane with its own back/forward history, like each window in MT.
 * Each history entry keeps its [BrowserState] so going back restores the
 * listing, selection and search as they were.
 */
@Stable
class Pane(start: String, private val nextId: () -> Long) {
    val stack = mutableStateListOf(PaneEntry(nextId(), BrowserState(start)))
    private val fwd = mutableStateListOf<PaneEntry>()

    val current: PaneEntry get() = stack.last()
    val st: BrowserState get() = current.state
    val path: String get() = st.path
    val canBack get() = stack.size > 1
    val canForward get() = fwd.isNotEmpty()

    /** Ids dropped from history, so the caller can forget their saved UI state. */
    var dropped: (Long) -> Unit = {}

    fun open(path: String) {
        if (path == this.path) { st.reload++; return }
        fwd.forEach { dropped(it.id) }
        fwd.clear()
        stack.add(PaneEntry(nextId(), BrowserState(path)))
    }

    fun back(): Boolean {
        if (stack.size <= 1) return false
        fwd.add(stack.removeAt(stack.lastIndex))
        return true
    }

    fun forward() {
        if (fwd.isNotEmpty()) stack.add(fwd.removeAt(fwd.lastIndex))
    }

    fun reload() { st.reload++ }
}
