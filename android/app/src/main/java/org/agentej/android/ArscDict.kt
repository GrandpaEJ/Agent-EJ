package org.agentej.android

import android.util.Xml
import org.xmlpull.v1.XmlPullParser
import java.io.File

/**
 * Translation dictionary as a plain Android strings.xml, so a language can be
 * translated in any editor or tool and brought back. Keys are resource names;
 * texts use aapt's escaping (\n, \', \", \\, leading \@ and \?).
 */
object ArscDict {
    /** Where a language's dictionary goes by default: beside the APK. */
    fun pathFor(archive: String, qual: String): String {
        val f = File(archive)
        val tag = qual.removePrefix("-").ifEmpty { "default" }
        return File(f.parentFile, "${f.nameWithoutExtension}_strings-$tag.xml").path
    }

    private fun esc(s: String): String {
        val b = StringBuilder(s.length + 8)
        s.forEachIndexed { i, c ->
            when (c) {
                '&' -> b.append("&amp;")
                '<' -> b.append("&lt;")
                '>' -> b.append("&gt;")
                '\\' -> b.append("\\\\")
                '\'' -> b.append("\\'")
                '"' -> b.append("\\\"")
                '\n' -> b.append("\\n")
                '\t' -> b.append("\\t")
                '@', '?' -> { if (i == 0) b.append('\\'); b.append(c) }
                else -> b.append(c)
            }
        }
        return b.toString()
    }

    private fun unesc(raw: String): String {
        var s = raw
        if (s.length >= 2 && s.startsWith('"') && s.endsWith('"')) s = s.substring(1, s.length - 1)
        else s = s.trim().replace(Regex("\\s+"), " ")           /* aapt folds unquoted whitespace */
        if ('\\' !in s) return s
        val b = StringBuilder(s.length)
        var i = 0
        while (i < s.length) {
            val c = s[i]
            if (c == '\\' && i + 1 < s.length) {
                b.append(when (val n = s[i + 1]) { 'n' -> '\n'; 't' -> '\t'; else -> n })
                i += 2
            } else { b.append(c); i++ }
        }
        return b.toString()
    }

    /** Writes [rows] (text in the language, or untranslated as empty comments). */
    fun export(path: String, rows: List<Pair<TrRow, String?>>): String? = runCatching {
        val b = StringBuilder("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n")
        for ((r, v) in rows) {
            if (v == null) b.append("    <!-- ").append(r.key).append(": ").append(esc(r.def).replace("--", "- -")).append(" -->\n")
            else b.append("    <string name=\"").append(r.key).append("\">").append(esc(v)).append("</string>\n")
        }
        b.append("</resources>\n")
        File(path).writeText(b.toString())
        null
    }.getOrElse { "Cannot write $path: ${it.message}" }

    /** Reads name → text from a strings.xml. */
    fun read(path: String): Result<Map<String, String>> = runCatching {
        val out = LinkedHashMap<String, String>()
        File(path).inputStream().use { input ->
            val p = Xml.newPullParser()
            p.setInput(input, "UTF-8")
            var name: String? = null
            val text = StringBuilder()
            while (true) {
                when (p.next()) {
                    XmlPullParser.START_TAG -> if (p.name == "string") { name = p.getAttributeValue(null, "name"); text.setLength(0) }
                    XmlPullParser.TEXT -> if (name != null) text.append(p.text)
                    XmlPullParser.END_TAG -> if (p.name == "string") { name?.let { out[it] = unesc(text.toString()) }; name = null }
                    XmlPullParser.END_DOCUMENT -> break
                }
            }
        }
        out
    }
}
