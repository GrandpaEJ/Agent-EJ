package org.agentej.android

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class FsEntry(
    val name: String,
    val isDir: Boolean,
    val size: Long,
    val mtime: Long
)

/** Outcome of a C call that reports "rc<TAB>detail". */
data class Rc(val code: Int, val detail: String) {
    val ok: Boolean get() = code == 0
    override fun toString(): String = if (ok) detail else "failed: $detail"
}

object Util {
    val archives = setOf("apk", "zip", "jar", "aar", "xapk", "apks")
    private val stamp = SimpleDateFormat("yy-MM-dd HH:mm", Locale.US)

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

    /** MT-style compact meta: 501B / 56.07K / 10.22M, no space. */
    fun compactSize(bytes: Long): String {
        if (bytes < 1024) return "${bytes}B"
        var v = bytes.toDouble() / 1024.0
        var u = 0
        val units = arrayOf("K", "M", "G", "T")
        while (v >= 1024 && u < units.size - 1) {
            v /= 1024
            u++
        }
        return String.format(Locale.US, "%.2f%s", v, units[u])
    }

    fun fmtTime(seconds: Long): String =
        if (seconds <= 0) "-" else stamp.format(Date(seconds * 1000))

    fun parent(path: String): String {
        val trimmed = path.trimEnd('/')
        val i = trimmed.lastIndexOf('/')
        return if (i <= 0) "/" else trimmed.substring(0, i)
    }

    fun join(dir: String, name: String) = if (dir.endsWith("/")) "$dir$name" else "$dir/$name"

    /**
     * "/sdcard/Download/x.apk/res/" -> ("/sdcard/Download/x.apk", "res/"); null when
     * the path does not point inside an archive.
     */
    fun splitArchive(path: String): Pair<String, String>? {
        val lower = path.lowercase(Locale.US)
        for (ext in archives) {
            val needle = ".$ext/"
            val i = lower.indexOf(needle)
            if (i >= 0) {
                val archive = path.substring(0, i + ext.length + 1)
                val inner = path.substring(i + ext.length + 2)
                return archive to inner.trimStart('/')
            }
        }
        return null
    }

    /** Text view of an entry, or null when it looks binary. */
    fun decodeText(bytes: ByteArray, max: Int = 2_000_000): String? {
        val slice = if (bytes.size > max) bytes.copyOf(max) else bytes
        if (slice.any { it == 0.toByte() }) return null
        return String(slice, Charsets.UTF_8)
    }

    /**
     * Immediate children of an archive prefix, in MT's shape: directories first,
     * then files, both alphabetical.
     */
    fun archiveChildren(lines: List<String>, prefix: String): List<FsEntry> {
        val dirs = LinkedHashSet<String>()
        val files = ArrayList<FsEntry>()
        for (line in lines) {
            val p = line.split('\t')
            val full = p.getOrNull(0) ?: continue
            if (!full.startsWith(prefix)) continue
            val rest = full.substring(prefix.length)
            if (rest.isEmpty()) continue
            val slash = rest.indexOf('/')
            if (slash >= 0) {
                dirs.add(rest.substring(0, slash))
            } else {
                files.add(FsEntry(rest, false, p.getOrNull(1)?.toLongOrNull() ?: 0L, 0L))
            }
        }
        val out = ArrayList<FsEntry>(dirs.size + files.size)
        dirs.sorted().forEach { out.add(FsEntry(it, true, 0L, 0L)) }
        files.sortedBy { it.name.lowercase(Locale.US) }.forEach { out.add(it) }
        return out
    }

    /** classes.dex plus classes2.dex … classesN.dex, in load order. */
    fun dexNames(entries: List<String>): List<String> = entries
        .map { it.substringBefore('\t') }
        .filter { it.matches(Regex("classes\\d*\\.dex")) }
        .sortedBy { if (it == "classes.dex") 0 else it.filter(Char::isDigit).toIntOrNull() ?: 99 }

    fun shortCode(name: String, isDir: Boolean): String {
        if (isDir) return "DIR"
        return when (ext(name)) {
            "apk", "apks", "xapk" -> "APK"
            "zip" -> "ZIP"
            "jar", "aar" -> "JAR"
            "dex" -> "DEX"
            "arsc" -> "ARS"
            "xml" -> "XML"
            "png", "jpg", "jpeg", "webp" -> "IMG"
            "so" -> "SO"
            "txt", "log", "md" -> "TXT"
            "json" -> "JSN"
            "smali" -> "SML"
            "mp4", "mkv" -> "VID"
            "" -> "FILE"
            else -> ext(name).take(3).uppercase(Locale.US)
        }
    }
}
