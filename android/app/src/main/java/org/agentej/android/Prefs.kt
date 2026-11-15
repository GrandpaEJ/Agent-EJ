package org.agentej.android

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

enum class SortKey(val label: String) { NAME("Name"), DATE("Date"), SIZE("Size"), TYPE("Type") }

/** Browser preferences, observable by Compose and written through to disk. */
class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("ej", Context.MODE_PRIVATE)

    var sortKey by mutableStateOf(
        runCatching { SortKey.valueOf(sp.getString("sort", "NAME")!!) }.getOrDefault(SortKey.NAME)
    )
        private set
    var sortDesc by mutableStateOf(sp.getBoolean("desc", false))
        private set
    var showHidden by mutableStateOf(sp.getBoolean("hidden", false))
        private set
    var thumbs by mutableStateOf(sp.getBoolean("thumbs", true))
        private set
    var grid by mutableStateOf(sp.getBoolean("grid", false))
        private set
    var bookmarks by mutableStateOf(
        sp.getString("bookmarks", null)?.split('\n')?.filter { it.isNotBlank() } ?: emptyList()
    )
        private set

    fun setSort(key: SortKey) {
        sortDesc = if (key == sortKey) !sortDesc else key == SortKey.DATE || key == SortKey.SIZE
        sortKey = key
        sp.edit().putString("sort", key.name).putBoolean("desc", sortDesc).apply()
    }

    fun toggleHidden() {
        showHidden = !showHidden
        sp.edit().putBoolean("hidden", showHidden).apply()
    }

    fun toggleThumbs() {
        thumbs = !thumbs
        sp.edit().putBoolean("thumbs", thumbs).apply()
    }

    fun toggleGrid() {
        grid = !grid
        sp.edit().putBoolean("grid", grid).apply()
    }

    fun toggleBookmark(path: String) {
        val p = path.trimEnd('/').ifEmpty { "/" }
        bookmarks = if (p in bookmarks) bookmarks - p else bookmarks + p
        sp.edit().putString("bookmarks", bookmarks.joinToString("\n")).apply()
    }

    fun isBookmarked(path: String) = path.trimEnd('/').ifEmpty { "/" } in bookmarks
}
