package org.agentej.android

import java.util.Locale

data class FsEntry(val name: String, val isDir: Boolean, val size: Long, val mtime: Long)

/** Outcome of a C call that reports "rc<TAB>detail". */
data class Rc(val code: Int, val detail: String) {
    val ok: Boolean get() = code == 0
    override fun toString(): String = if (ok) detail else "failed: $detail"
}

object Util {
    val archives = setOf("apk", "zip", "jar", "aar", "xapk", "apks")

    fun parseRc(text: String): Rc {
        val tab = text.indexOf('\t')
        if (tab < 0) return Rc(text.toIntOrNull() ?: -1, "")
        return Rc(text.substring(0, tab).toIntOrNull() ?: -1, text.substring(tab + 1))
    }

    fun parseEntry(line: String): FsEntry {
        val p = line.split('\t')
        if (p.size < 4) return FsEntry(line, false, 0, 0)
        return FsEntry(p[0], p[1] == "1", p[2].toLongOrNull() ?: 0L, p[3].toLongOrNull() ?: 0L)
    }

    fun ext(name: String) = name.substringAfterLast('.', "").lowercase(Locale.US)

    fun isArchive(name: String) = ext(name) in archives

    fun humanSize(bytes: Long): String {
        if (bytes < 1024) return "$bytes B"
        var v = bytes.toDouble() / 1024.0
        var u = 0
        val units = arrayOf("KB", "MB", "GB", "TB")
        while (v >= 1024 && u < units.size - 1) {
            v /= 1024
            u++
        }
        return String.format(Locale.US, if (v >= 100) "%.0f %s" else "%.1f %s", v, units[u])
    }

    fun glyph(name: String, isDir: Boolean): String = when {
        isDir -> "\uD83D\uDCC1"
        isArchive(name) -> "\uD83D\uDCE6"
        ext(name) == "dex" || ext(name) == "smali" -> "\u2699"
        ext(name) == "xml" -> "\uD83D\uDCC4"
        else -> "\uD83D\uDCC4"
    }

    fun dexNames(entries: List<String>): List<String> = entries
        .map { it.substringBefore('\t') }
        .filter { it.matches(Regex("classes\\d*\\.dex")) }
        .sortedBy { if (it == "classes.dex") 0 else it.filter(Char::isDigit).toIntOrNull() ?: 99 }

    fun parent(path: String): String {
        val trimmed = path.trimEnd('/')
        val i = trimmed.lastIndexOf('/')
        return if (i <= 0) "/" else trimmed.substring(0, i)
    }

    fun join(dir: String, name: String) = if (dir.endsWith("/")) "$dir$name" else "$dir/$name"

    /** Text view of an entry, or null when it looks binary. */
    fun decodeText(bytes: ByteArray, max: Int = 2_000_000): String? {
        val slice = if (bytes.size > max) bytes.copyOf(max) else bytes
        if (slice.any { it == 0.toByte() }) return null
        return String(slice, Charsets.UTF_8)
    }
}
