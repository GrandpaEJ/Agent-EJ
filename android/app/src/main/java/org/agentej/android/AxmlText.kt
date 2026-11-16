package org.agentej.android

import android.content.pm.PackageManager
import android.content.res.Resources
import android.util.Xml
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.xmlpull.v1.XmlPullParser
import org.xmlpull.v1.XmlPullParserException
import java.io.StringReader

/** Decoded binary XML the way MT shows it: framework ids named, permissions labelled. */
object AxmlText {
    private val FRAMEWORK_ID = Regex("""([@?])(01[0-9a-f]{6})"""")
    private val PERM_NAME = Regex("""android:name="([^"]+)"""")
    private val ANDROID_REF = Regex("""([@?])android:([a-z]+)/([\w.]+)""")

    fun decode(pm: PackageManager?, archive: String, entry: String): String {
        val xml = Native.axmlDecode(archive, entry)
        return if (xml.isBlank()) xml else permissionLabels(pm, frameworkNames(xml))
    }

    /** `@0103022e` → `@android:style/Theme.Material.NoActionBar` (names the running system knows). */
    private fun frameworkNames(xml: String): String {
        val res = Resources.getSystem()
        return FRAMEWORK_ID.replace(xml) { m ->
            val name = runCatching { res.getResourceName(m.groupValues[2].toLong(16).toInt()) }.getOrNull()
            if (name == null) m.value else "${m.groupValues[1]}$name\""
        }
    }

    /** The inverse for the encoder: `@android:style/X` → `@0103xxxx`; unknown names are left as written. */
    fun frameworkIds(xml: String): String {
        val res = Resources.getSystem()
        return ANDROID_REF.replace(xml) { m ->
            val (sign, type, name) = m.destructured
            val id = res.getIdentifier(name, type, "android")
            if (id == 0) m.value else sign + "%08x".format(id)
        }
    }

    /** A `<!-- label -->` line above each uses-permission the system has a label for. */
    private fun permissionLabels(pm: PackageManager?, xml: String): String {
        if (pm == null) return xml
        val lines = xml.split('\n')
        val out = StringBuilder(xml.length + 1024)
        for ((i, line) in lines.withIndex()) {
            val t = line.trimStart()
            if (t.startsWith("<uses-permission")) {
                /* the name is on this line (one attribute) or on one of the attribute lines below it */
                val elem = buildString {
                    for (j in i until minOf(i + 8, lines.size)) {
                        append(lines[j]).append('\n')
                        if (lines[j].trimEnd().endsWith(">")) break
                    }
                }
                val name = PERM_NAME.find(elem)?.groupValues?.get(1)
                label(pm, name)?.let { out.append(line, 0, line.length - t.length).append("<!-- ").append(it).append(" -->\n") }
            }
            out.append(line)
            if (i < lines.size - 1) out.append('\n')
        }
        return out.toString()
    }

    private fun label(pm: PackageManager, name: String?): String? {
        if (name == null) return null
        val l = runCatching { pm.getPermissionInfo(name, 0).loadLabel(pm).toString() }.getOrNull()?.trim()
        if (l.isNullOrEmpty() || l == name) return null
        return l.replaceFirstChar { it.uppercaseChar() }.replace("--", "- -")
    }

    /** Where saving [entry] of [archive] writes: the APK's "_edited" copy, or the binary XML itself. */
    fun saveTarget(archive: String, entry: String): String = when {
        entry.isEmpty() -> archive
        Outputs.base(archive).endsWith("_edited") -> archive
        else -> Util.join(
            Util.parent(archive).takeIf { java.io.File(it).canWrite() } ?: Workspace.output,
            Outputs.base(archive) + "_edited.apk"
        )
    }

    /**
     * What a save starts from: the existing "_edited" copy when there is one, so
     * changes already made there (another XML file, translations) are kept
     * instead of being rebuilt from the original APK and lost.
     */
    fun saveBase(archive: String, entry: String): String {
        val t = saveTarget(archive, entry)
        return if (t != archive && java.io.File(t).isFile) t else archive
    }

    /** The viewer route for decoded XML that saves back through [save]. */
    fun route(title: String, body: String, archive: String, entry: String): Route {
        val target = saveTarget(archive, entry)
        val note = if (entry.isEmpty()) "Saved ${target.substringAfterLast('/')}"
        else "Saved into ${target.substringAfterLast('/')} · signed"
        return Route.Text(title, body, onSave = { save(archive, entry, it) }, savedNote = note)
    }

    /** Null when [xml] is well-formed, else the parser's complaint with its line. */
    fun check(xml: String): String? = try {
        val p = Xml.newPullParser().apply { setInput(StringReader(xml)) }
        while (p.next() != XmlPullParser.END_DOCUMENT) Unit
        null
    } catch (e: XmlPullParserException) {
        "Line ${e.lineNumber}: ${e.message?.substringBefore(" (position")?.substringBefore("@")?.trim()}"
    }

    /** Encodes the edited text and writes it back (see [saveTarget]); null when saved, else why not. */
    suspend fun save(archive: String, entry: String, xml: String): String? = withContext(Dispatchers.IO) {
        check(xml)?.let { return@withContext it }
        val target = saveTarget(archive, entry)
        /* never write over the file being read: build beside it, then swap */
        val tmp = "$target.tmp"
        val r = Util.parseRc(Native.axmlSave(saveBase(archive, entry), entry, frameworkIds(xml), tmp))
        if (r.code != 0) { java.io.File(tmp).delete(); return@withContext r.detail }
        if (!java.io.File(tmp).renameTo(java.io.File(target))) { java.io.File(tmp).delete(); "Cannot write $target" } else null
    }
}
