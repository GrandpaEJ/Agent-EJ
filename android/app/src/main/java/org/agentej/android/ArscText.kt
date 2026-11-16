package org.agentej.android

import java.util.Locale

/** One <string>: its default text and its text in the chosen language (null when missing). */
class TrRow(val id: Int, val key: String, val def: String, val value: String?)

/** Translation mode over resources.arsc through the C core (arsc_tr_*). */
object ArscText {
    private fun unesc(s: String): String {
        if ('\\' !in s) return s
        val b = StringBuilder(s.length)
        var i = 0
        while (i < s.length) {
            val c = s[i]
            if (c == '\\' && i + 1 < s.length) {
                b.append(when (s[i + 1]) { 'n' -> '\n'; 't' -> '\t'; 'r' -> '\r'; else -> s[i + 1] })
                i += 2
            } else { b.append(c); i++ }
        }
        return b.toString()
    }

    /** Qualifiers of the string type besides the default: "-fr", "-zh-rCN", "-b+sr+Latn". */
    fun locales(archive: String, entry: String): List<String> =
        Native.arscLocales(archive, entry).split('\n').filter { it.isNotEmpty() }

    fun rows(archive: String, entry: String, qual: String): List<TrRow> =
        Native.arscStringsIn(archive, entry, qual).split('\n').mapNotNull { line ->
            val p = line.split('\t')
            if (p.size < 4) return@mapNotNull null
            val id = p[0].toLongOrNull(16)?.toInt() ?: return@mapNotNull null
            TrRow(id, unesc(p[1]), unesc(p[2]), if (p[3].startsWith("=")) unesc(p[3].substring(1)) else null)
        }

    /** "-zh-rCN" → "zh-CN", "-b+sr+Latn" → "sr-Latn"; null for non-locale qualifiers ("-land"). */
    fun tag(qual: String): String? {
        val q = qual.removePrefix("-").substringBefore("-v")
        if (q.startsWith("b+")) return q.removePrefix("b+").replace('+', '-')
        val m = Regex("""^([a-z]{2,3})(?:-r([A-Z]{2}|[0-9]{3}))?$""").find(q) ?: return null
        return m.groupValues[1] + (m.groupValues[2].takeIf { it.isNotEmpty() }?.let { "-$it" } ?: "")
    }

    /** "French (Canada)" for "-fr-rCA", "Default" for "", the raw qualifier otherwise. */
    fun label(qual: String): String {
        if (qual.isEmpty()) return "Default"
        val t = tag(qual) ?: return qual.removePrefix("-")
        val name = Locale.forLanguageTag(t).getDisplayName(Locale.getDefault())
        return if (name.isBlank() || name == t) qual.removePrefix("-") else name
    }

    /** Qualifier a new language gets: "-bn", "-zh-rCN" (what arsc_tr_set can create). */
    fun qualOf(l: Locale): String? {
        val lang = l.language.takeIf { it.length == 2 } ?: return null
        val c = l.country
        return if (c.length == 2) "-$lang-r$c" else if (c.isEmpty()) "-$lang" else null
    }

    /** Languages "Add language" offers: device locales the table does not have yet. */
    fun addable(have: Collection<String>): List<Pair<String, String>> =
        Locale.getAvailableLocales().asSequence()
            .filter { it.script.isEmpty() && it.variant.isEmpty() }
            .mapNotNull { l -> qualOf(l)?.let { it to l.getDisplayName(Locale.getDefault()) } }
            .filter { it.first !in have }
            .distinctBy { it.first }
            .sortedBy { it.second.lowercase() }
            .toList()

    /** Writes [edits] (id → text) for [qual]; null when saved, else why not. Blocking. */
    fun save(archive: String, entry: String, qual: String, edits: Map<Int, String>, target: String): String? {
        val tmp = "$target.tmp"
        val ids = edits.keys.toIntArray()
        val r = Util.parseRc(Native.arscTranslate(archive, entry, qual, ids, ids.map { edits.getValue(it) }.toTypedArray(), tmp))
        if (r.code != 0) { java.io.File(tmp).delete(); return r.detail }
        return if (java.io.File(tmp).renameTo(java.io.File(target))) null else { java.io.File(tmp).delete(); "Cannot write $target" }
    }
}
