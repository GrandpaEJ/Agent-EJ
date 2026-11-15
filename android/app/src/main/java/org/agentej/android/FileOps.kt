package org.agentej.android

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** Copy/cut buffer shared by every browser screen in the back stack. */
class FileClipboard {
    var paths by mutableStateOf<List<String>>(emptyList())
        private set
    var cut by mutableStateOf(false)
        private set

    val isEmpty get() = paths.isEmpty()

    fun set(list: List<String>, isCut: Boolean) {
        paths = list
        cut = isCut
    }

    fun clear() = set(emptyList(), false)
}

/** Blocking file operations; call from Dispatchers.IO. All I/O happens in C. */
object FileOps {
    fun exists(path: String) = !Native.statPath(path).startsWith("-1")

    private fun why(rc: Int) = when (rc) {
        -2 -> "can't put a folder inside itself"
        -3 -> "target exists"
        else -> "I/O error"
    }

    /** "name", "name (1)", "name (2)" … first free slot in [dir]. */
    fun freeName(dir: String, name: String): String {
        if (!exists(Util.join(dir, name))) return name
        val dot = name.lastIndexOf('.').takeIf { it > 0 } ?: name.length
        val stem = name.substring(0, dot)
        val ext = name.substring(dot)
        var i = 1
        while (exists(Util.join(dir, "$stem ($i)$ext"))) i++
        return "$stem ($i)$ext"
    }

    fun paste(sources: List<String>, destDir: String, cut: Boolean): Rc {
        var ok = 0
        val errors = ArrayList<String>()
        for (src in sources) {
            val name = src.trimEnd('/').substringAfterLast('/')
            val sameDir = Util.parent(src).trimEnd('/') == destDir.trimEnd('/')
            if (cut && sameDir) { ok++; continue }
            val target = Util.join(destDir, freeName(destDir, name))
            val rc = if (cut) Native.movePath(src, target) else Native.copyPath(src, target)
            if (rc == 0) ok++ else errors.add("$name: ${why(rc)}")
        }
        val verb = if (cut) "moved" else "copied"
        return if (errors.isEmpty()) Rc(0, "$verb $ok item${if (ok == 1) "" else "s"}")
        else Rc(-1, "$verb $ok, ${errors.size} failed — ${errors.first()}")
    }

    fun delete(paths: List<String>): Rc {
        val failed = paths.filter { Native.deletePath(it) != 0 }
        val ok = paths.size - failed.size
        return if (failed.isEmpty()) Rc(0, "deleted $ok item${if (ok == 1) "" else "s"}")
        else Rc(-1, "deleted $ok, could not delete ${failed.first().substringAfterLast('/')}")
    }

    fun rename(path: String, newName: String): Rc {
        val target = Util.join(Util.parent(path), newName)
        if (exists(target)) return Rc(-3, "$newName already exists")
        return if (Native.renamePath(path, target) == 0) Rc(0, "renamed to $newName")
        else Rc(-1, "rename failed")
    }

    fun create(dir: String, name: String, folder: Boolean): Rc {
        val target = Util.join(dir, name)
        if (exists(target)) return Rc(-3, "$name already exists")
        val rc = if (folder) Native.mkdirs(target) else Native.writeText(target, "")
        return if (rc == 0) Rc(0, "created $name") else Rc(-1, "could not create $name")
    }

    fun validName(name: String) = name.isNotBlank() && '/' !in name && name != "." && name != ".."
}
