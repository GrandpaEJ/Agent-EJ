package org.agentej.android

import android.content.pm.PackageManager
import android.content.res.Resources

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
}
