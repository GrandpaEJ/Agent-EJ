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

    /** Editable value types: resource type name → what the screen calls them. */
    val TYPES = listOf("string" to "Strings", "color" to "Colors", "dimen" to "Dimens", "bool" to "Bools", "integer" to "Integers")

    /** Qualifiers of [type] besides the default: "-fr", "-zh-rCN", "-night". */
    fun locales(archive: String, entry: String, type: String = "string"): List<String> =
        Native.arscLocales(archive, entry, type).split('\n').filter { it.isNotEmpty() }

    fun rows(archive: String, entry: String, qual: String, type: String = "string"): List<TrRow> =
        Native.arscStringsIn(archive, entry, type, qual).split('\n').mapNotNull { line ->
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

    private val COLOR = Regex("#([0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})")
    private val DIMEN = Regex("-?(\\d+(\\.\\d*)?|\\.\\d+)(px|dp|dip|sp|pt|in|mm)")
    private val INT = Regex("-?\\d+|0[xX][0-9a-fA-F]{1,8}")

    /** Why [text] can't be a [type] value, or null when it can. */
    fun invalid(type: String, text: String): String? = when (type) {
        "color" -> if (COLOR.matches(text)) null else "Use #rgb, #argb, #rrggbb or #aarrggbb"
        "dimen" -> if (DIMEN.matches(text)) null else "Use a number and a unit: 16dp, 14sp, 1.5px"
        "bool" -> if (text == "true" || text == "false") null else "Use true or false"
        "integer" -> if (INT.matches(text)) null else "Use a whole number or 0x hex"
        else -> null
    }

    /** ARGB of a colour value as the table prints it ("#rrggbb" / "#aarrggbb"). */
    fun argb(v: String?): Long? {
        val h = v?.removePrefix("#") ?: return null
        val n = h.toLongOrNull(16) ?: return null
        return when (h.length) { 6 -> 0xFF000000L or n; 8 -> n; else -> null }
    }

    /** Writes [edits] (id → text) for [qual]; null when saved, else why not. Blocking. */
    fun save(archive: String, entry: String, qual: String, edits: Map<Int, String>, target: String): String? {
        val tmp = "$target.tmp"
        val ids = edits.keys.toIntArray()
        val r = Util.parseRc(Native.arscTranslate(archive, entry, qual, ids, ids.map { edits.getValue(it) }.toTypedArray(), tmp))
        if (r.code != 0) { java.io.File(tmp).delete(); return r.detail }
        return if (java.io.File(tmp).renameTo(java.io.File(target))) null else { java.io.File(tmp).delete(); "Cannot write $target" }
    }
}
