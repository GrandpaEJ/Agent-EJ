package org.agentej.android.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import org.agentej.android.Util

enum class Lang(val label: String) { SMALI("smali"), XML("xml"), JSON("json"), CONF("conf"), PLAIN("text") }

fun detectLang(title: String, body: String): Lang {
    when (Util.ext(title)) {
        "smali" -> return Lang.SMALI
        "xml", "html", "svg" -> return Lang.XML
        "json" -> return Lang.JSON
        "toml", "properties", "ini", "cfg", "conf", "mf", "sf" -> return Lang.CONF
    }
    val head = body.trimStart().take(16)
    return when {
        head.startsWith(".class") -> Lang.SMALI
        head.startsWith("<") -> Lang.XML
        head.startsWith("{") || head.startsWith("[{") -> Lang.JSON
        else -> Lang.PLAIN
    }
}

/**
 * One-line tokenizers. Each is a single left-to-right scan with no regex and
 * no cross-line state, so a line can be coloured the moment it scrolls in.
 */
fun highlight(line: String, lang: Lang, p: EjPalette): AnnotatedString = when (lang) {
    Lang.SMALI -> smali(line, p)
    Lang.XML -> xml(line, p)
    Lang.JSON -> json(line, p)
    Lang.CONF -> conf(line, p)
    Lang.PLAIN -> AnnotatedString(line)
}

private fun AnnotatedString.Builder.span(s: String, c: Color) {
    val start = length
    append(s)
    addStyle(SpanStyle(color = c), start, length)
}

private fun isWord(c: Char) = c.isLetterOrDigit() || c == '_' || c == '$' || c == '-' || c == '/'

private fun quoted(line: String, from: Int): Int {
    var i = from + 1
    while (i < line.length) {
        if (line[i] == '\\') i += 2
        else if (line[i] == '"') return i + 1
        else i++
    }
    return line.length
}

private fun smali(line: String, p: EjPalette) = buildAnnotatedString {
    var i = 0
    var first = true
    while (i < line.length) {
        val c = line[i]
        when {
            c == ' ' || c == '\t' -> { append(c); i++; continue }
            c == '#' -> { span(line.substring(i), p.synComment); i = line.length }
            c == '"' -> { val e = quoted(line, i); span(line.substring(i, e), p.synString); i = e }
            first && c == '.' -> {
                var e = i + 1
                while (e < line.length && isWord(line[e])) e++
                span(line.substring(i, e), p.synDirective); i = e
            }
            // Labels (":cond_0") start a token; "->f:I" is a field type, not a label.
            c == ':' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '{' || line[i - 1] == '\t') -> {
                var e = i + 1
                while (e < line.length && isWord(line[e])) e++
                span(line.substring(i, e), p.synAttr); i = e
            }
            first && c.isLetter() -> {
                var e = i
                while (e < line.length && isWord(line[e])) e++
                span(line.substring(i, e), p.synKeyword); i = e
            }
            (c == 'v' || c == 'p') && i + 1 < line.length && line[i + 1].isDigit() &&
                (i == 0 || !isWord(line[i - 1])) -> {
                var e = i + 1
                while (e < line.length && line[e].isDigit()) e++
                span(line.substring(i, e), p.synRegister); i = e
            }
            c == 'L' || (c == '[' && line.indexOf(';', i) > 0) -> {
                val semi = line.indexOf(';', i)
                val stop = line.indexOfAny(charArrayOf(' ', ',', ')', '"'), i).let { if (it < 0) line.length else it }
                if (semi in i until stop) { span(line.substring(i, semi + 1), p.synType); i = semi + 1 }
                else { append(c); i++ }
            }
            (c.isDigit() || (c == '-' && i + 1 < line.length && line[i + 1].isDigit())) &&
                (i == 0 || !isWord(line[i - 1])) -> {
                var e = i + 1
                while (e < line.length && (line[e].isLetterOrDigit() || line[e] == '.')) e++
                span(line.substring(i, e), p.synNumber); i = e
            }
            else -> {
                var e = i
                if (isWord(c)) while (e < line.length && isWord(line[e]) && line[e] != 'L') e++
                if (e == i) e = i + 1
                append(line.substring(i, e)); i = e
            }
        }
        first = false
    }
}

private fun xml(line: String, p: EjPalette) = buildAnnotatedString {
    var i = 0
    while (i < line.length) {
        val c = line[i]
        when {
            line.startsWith("<!--", i) -> {
                val e = line.indexOf("-->", i).let { if (it < 0) line.length else it + 3 }
                span(line.substring(i, e), p.synComment); i = e
            }
            c == '<' -> {
                var e = i + 1
                if (e < line.length && (line[e] == '/' || line[e] == '?' || line[e] == '!')) e++
                while (e < line.length && (isWord(line[e]) || line[e] == ':' || line[e] == '.')) e++
                span(line.substring(i, e), p.synTag); i = e
            }
            c == '>' || line.startsWith("/>", i) || line.startsWith("?>", i) -> {
                val e = if (c == '>') i + 1 else i + 2
                span(line.substring(i, e), p.synTag); i = e
            }
            c == '"' -> { val e = quoted(line, i); span(line.substring(i, e), p.synString); i = e }
            isWord(c) -> {
                var e = i
                while (e < line.length && (isWord(line[e]) || line[e] == ':' || line[e] == '.')) e++
                val word = line.substring(i, e)
                if (e < line.length && line[e] == '=') span(word, p.synAttr) else append(word)
                i = e
            }
            else -> { append(c); i++ }
        }
    }
}

private fun json(line: String, p: EjPalette) = buildAnnotatedString {
    var i = 0
    while (i < line.length) {
        val c = line[i]
        when {
            c == '"' -> {
                val e = quoted(line, i)
                var k = e
                while (k < line.length && line[k] == ' ') k++
                span(line.substring(i, e), if (k < line.length && line[k] == ':') p.synAttr else p.synString)
                i = e
            }
            c.isDigit() || c == '-' -> {
                var e = i + 1
                while (e < line.length && (line[e].isDigit() || line[e] in ".eE+-")) e++
                span(line.substring(i, e), p.synNumber); i = e
            }
            line.startsWith("true", i) || line.startsWith("null", i) -> { span(line.substring(i, i + 4), p.synKeyword); i += 4 }
            line.startsWith("false", i) -> { span("false", p.synKeyword); i += 5 }
            else -> { append(c); i++ }
        }
    }
}

private fun conf(line: String, p: EjPalette) = buildAnnotatedString {
    val t = line.trimStart()
    when {
        t.startsWith("#") || t.startsWith(";") -> span(line, p.synComment)
        t.startsWith("[") -> span(line, p.synDirective)
        else -> {
            val eq = line.indexOfAny(charArrayOf('=', ':'))
            if (eq <= 0) { append(line); return@buildAnnotatedString }
            span(line.substring(0, eq), p.synAttr)
            append(line[eq])
            val rest = line.substring(eq + 1)
            val trimmed = rest.trim()
            val c = when {
                trimmed.startsWith("\"") -> p.synString
                trimmed.toDoubleOrNull() != null -> p.synNumber
                trimmed == "true" || trimmed == "false" -> p.synKeyword
                else -> null
            }
            if (c != null) span(rest, c) else append(rest)
        }
    }
}
