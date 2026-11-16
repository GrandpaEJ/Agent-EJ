package org.agentej.android.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.SystemClock
import android.text.Editable
import android.text.InputType
import android.text.Spanned
import android.text.TextWatcher
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.widget.OverScroller
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView

/** Undo/redo as text diffs; quick typing or deleting runs merge into one step. */
class EditHistory {
    private class Op(var at: Int, var removed: String, var inserted: String, var time: Long)

    private val undo = ArrayDeque<Op>()
    private val redo = ArrayDeque<Op>()
    private var chars = 0L
    /** Bumped on every change so the undo/redo buttons recompose. */
    var version by mutableIntStateOf(0)
        private set
    val canUndo get() = version >= 0 && undo.isNotEmpty()
    val canRedo get() = version >= 0 && redo.isNotEmpty()

    fun record(at: Int, removed: String, inserted: String) {
        val now = SystemClock.uptimeMillis()
        val l = undo.lastOrNull()
        val recent = l != null && now - l.time < 1000 && !inserted.contains('\n')
        when {
            recent && removed.isEmpty() && l!!.removed.isEmpty() && inserted.length == 1 && l.at + l.inserted.length == at ->
                l.inserted += inserted
            recent && inserted.isEmpty() && l!!.inserted.isEmpty() && removed.length == 1 && at + 1 == l.at ->
                { l.removed = removed + l.removed; l.at = at }
            else -> undo.addLast(Op(at, removed, inserted, now))
        }
        undo.last().time = now
        chars += removed.length + inserted.length
        while (undo.size > 1 && (undo.size > 1000 || chars > 4_000_000))
            undo.removeFirst().let { chars -= it.removed.length + it.inserted.length }
        redo.clear()
        version++
    }

    /** Reverts the last step in [e]; returns where the cursor goes, or -1. */
    fun undo(e: Editable): Int {
        val op = undo.removeLastOrNull() ?: return -1
        e.replace(op.at, op.at + op.inserted.length, op.removed)
        redo.addLast(op)
        version++
        return op.at + op.removed.length
    }

    fun redo(e: Editable): Int {
        val op = redo.removeLastOrNull() ?: return -1
        e.replace(op.at, op.at + op.removed.length, op.inserted)
        op.time = 0
        undo.addLast(op)
        version++
        return op.at + op.inserted.length
    }
}

private class SynSpan(c: Int) : ForegroundColorSpan(c)
private class HitSpan(c: Int) : BackgroundColorSpan(c)

/**
 * The editing surface: a plain EditText, which scrolls inside itself and
 * relays out only the lines that change, so a file of thousands of lines
 * stays responsive. Only the lines on screen get syntax colours, and the
 * line-number gutter is drawn over the left padding, pinned while the text
 * scrolls sideways.
 */
@SuppressLint("ViewConstructor")
class CodeEditText(ctx: Context, val history: EditHistory) : EditText(ctx) {
    var lang = Lang.PLAIN
    var palette = EjPalette()
    var gutterBg = 0
    var accent = 0
    /** Called after every edit (not on undo-free span changes). */
    var onEdit: () -> Unit = {}

    private val numPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.MONOSPACE; textAlign = Paint.Align.RIGHT }
    private val bgPaint = Paint()
    private var newlines = IntArray(0)
    private var newlineCount = 0
    private var newlinesStale = true
    private var removed = ""
    private var applying = false
    private var hits: List<Match> = emptyList()
    private var activeHit = -1
    private val rehighlight = Runnable { colorVisible() }
    /* Span changes relayout lines, and TextView then scrolls back to the cursor.
     * Only the user's own typing or cursor moves may do that. */
    private var holdScroll = false
    /* TextView drags but never flings; long files need it */
    private val scroller = OverScroller(ctx)
    private val flinger = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean { scroller.forceFinished(true); return false }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            val lay = layout ?: return false
            val maxY = (lay.height + extendedPaddingTop + extendedPaddingBottom - height).coerceAtLeast(0)
            var maxX = 0
            if (horizontallyScrolling() && kotlin.math.abs(vx) > kotlin.math.abs(vy)) {
                var w = 0f
                for (r in lay.getLineForVertical(scrollY)..lay.getLineForVertical(scrollY + height)) w = maxOf(w, lay.getLineMax(r))
                maxX = (w + totalPaddingLeft + totalPaddingRight - width).toInt().coerceAtLeast(0)
            }
            scroller.fling(scrollX, scrollY, if (maxX > 0) -vx.toInt() else 0, -vy.toInt(), 0, maxX, 0, maxY)
            postInvalidateOnAnimation()
            return true
        }
    })

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        flinger.onTouchEvent(event)
        return super.onTouchEvent(event)
    }

    override fun computeScroll() {
        super.computeScroll()
        if (scroller.computeScrollOffset()) {
            scrollTo(scroller.currX, scroller.currY)
            postInvalidateOnAnimation()
        }
    }

    init {
        background = null
        typeface = Typeface.MONOSPACE
        gravity = Gravity.TOP or Gravity.START
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI
        isSaveEnabled = false            /* a big file in the saved state overflows the binder */
        isVerticalScrollBarEnabled = true
        setLineSpacing(0f, 1.25f)
        addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence, start: Int, count: Int, after: Int) {
                removed = if (applying) "" else s.subSequence(start, start + count).toString()
            }
            override fun onTextChanged(s: CharSequence, start: Int, before: Int, count: Int) {
                if (!applying) history.record(start, removed, s.subSequence(start, start + count).toString())
                holdScroll = false
                newlinesStale = true
            }
            override fun afterTextChanged(s: Editable) {
                scheduleColor()
                onEdit()
            }
        })
    }

    override fun bringPointIntoView(offset: Int): Boolean = if (holdScroll) false else super.bringPointIntoView(offset)

    override fun onSelectionChanged(selStart: Int, selEnd: Int) {
        holdScroll = false
        super.onSelectionChanged(selStart, selEnd)
    }

    fun style(lang: Lang, p: EjPalette, fg: Int, gutter: Int, accent: Int, sizeSp: Float, wrap: Boolean) {
        val restyle = lang != this.lang || p != palette
        this.lang = lang; palette = p; gutterBg = gutter; this.accent = accent
        setTextColor(fg)
        highlightColor = p.selection.toArgb()
        if (Build.VERSION.SDK_INT >= 29) textCursorDrawable = GradientDrawable().apply {
            setColor(accent); setSize((2 * resources.displayMetrics.density).toInt(), 1)
        }
        if (textSize != sp(sizeSp)) setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        numPaint.textSize = textSize
        setHorizontallyScrolling(!wrap)
        updateGutter()
        if (restyle) { text.getSpans(0, text.length, SynSpan::class.java).forEach { text.removeSpan(it) } }
        scheduleColor()
    }

    private fun sp(v: Float) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, v, resources.displayMetrics)

    fun load(s: String) {
        applying = true
        setText(s, BufferType.EDITABLE)
        applying = false
        newlinesStale = true
        updateGutter()
    }

    fun undo() { applyHistory { history.undo(it) } }
    fun redo() { applyHistory { history.redo(it) } }

    private fun applyHistory(op: (Editable) -> Int) {
        applying = true
        val at = op(text)
        applying = false
        if (at >= 0) setSelection(at.coerceIn(0, text.length))
    }

    /** Types [s] over the selection, as the keyboard would. */
    fun insert(s: String) {
        val a = selectionStart.coerceAtLeast(0)
        val b = selectionEnd.coerceAtLeast(0)
        text.replace(minOf(a, b), maxOf(a, b), s)
    }

    private fun index() {
        if (!newlinesStale) return
        val t = text
        var n = 0
        var arr = if (newlines.size < 64) IntArray(1024) else newlines
        for (i in 0 until t.length) if (t[i] == '\n') {
            if (n == arr.size) arr = arr.copyOf(arr.size * 2)
            arr[n++] = i
        }
        newlines = arr
        newlineCount = n
        newlinesStale = false
    }

    /** Offset where logical [line] begins. */
    fun lineStart(line: Int): Int {
        index()
        return if (line <= 0) 0 else if (line > newlineCount) text.length else newlines[line - 1] + 1
    }

    private fun lineOf(offset: Int): Int {
        index()
        var lo = 0
        var hi = newlineCount
        while (lo < hi) { val m = (lo + hi) ushr 1; if (newlines[m] < offset) lo = m + 1 else hi = m }
        return lo
    }

    private fun updateGutter() {
        index()
        val digits = maxOf(2, (newlineCount + 1).toString().length)
        val d = resources.displayMetrics.density
        val w = (numPaint.measureText("0") * digits + 18 * d).toInt()
        val pad = (8 * d).toInt()
        if (paddingLeft != w + pad) setPadding(w + pad, (6 * d).toInt(), (24 * d).toInt(), (6 * d).toInt())
    }

    fun setHits(list: List<Match>, active: Int) {
        if (list == hits && active == activeHit) return
        hits = list; activeHit = active
        holdScroll = true
        text.getSpans(0, text.length, HitSpan::class.java).forEach { text.removeSpan(it) }
        list.forEachIndexed { i, m ->
            val base = lineStart(m.line)
            val a = (base + m.start).coerceAtMost(text.length)
            val e = (base + m.end).coerceAtMost(text.length)
            if (a < e) text.setSpan(HitSpan((if (i == active) palette.matchActive else palette.match).toArgb()), a, e, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        redrawText()
    }

    /** Scrolls logical [line]:[col] to about the fifth row. */
    fun reveal(line: Int, col: Int) {
        val lay = layout ?: run { post { reveal(line, col) }; return }
        val off = (lineStart(line) + col).coerceIn(0, text.length)
        val row = lay.getLineForOffset(off)
        val y = (lay.getLineTop(row) - 4 * lineHeight).coerceAtLeast(0)
        /* sideways only when the spot is off screen */
        val px = lay.getPrimaryHorizontal(off).toInt()
        val shown = width - totalPaddingLeft - totalPaddingRight
        val x = when {
            !horizontallyScrolling() || px < shown - width / 6 -> 0
            px in scrollX until scrollX + shown - width / 6 -> scrollX
            else -> (px - shown / 2).coerceAtLeast(0)
        }
        holdScroll = true
        scrollTo(x, y)
        scheduleColor()
    }

    private fun horizontallyScrolling() = layout?.let { it.width > width } ?: false

    private fun scheduleColor() {
        removeCallbacks(rehighlight)
        postDelayed(rehighlight, 30)
    }

    override fun onScrollChanged(horiz: Int, vert: Int, oldHoriz: Int, oldVert: Int) {
        super.onScrollChanged(horiz, vert, oldHoriz, oldVert)
        if (vert != oldVert) scheduleColor()
    }

    /** Colours the lines on screen plus a screenful either side. */
    private fun colorVisible() {
        val lay = layout ?: return
        if (lang == Lang.PLAIN || text.isEmpty()) return
        val span = height
        val first = lineOf(lay.getLineStart(lay.getLineForVertical((scrollY - span).coerceAtLeast(0))))
        val last = lineOf(lay.getLineEnd(lay.getLineForVertical(scrollY + 2 * span)))
        val t = text
        holdScroll = true
        var changed = 0
        for (ln in first..last) {
            val a = lineStart(ln)
            val b = if (ln < newlineCount) newlines[ln] else t.length
            if (a > b) continue
            val h = highlight(t.substring(a, b), lang, palette)
            if (h.length != b - a) continue
            val want = h.spanStyles.filter { it.item.color.isSpecified() && it.start < it.end }
            val have = t.getSpans(a, b, SynSpan::class.java).filter { t.getSpanStart(it) >= a && t.getSpanEnd(it) <= b }
            if (have.size == want.size && want.all { w ->
                    have.any { t.getSpanStart(it) == a + w.start && t.getSpanEnd(it) == a + w.end && it.foregroundColor == w.item.color.toArgb() }
                }) continue                                   /* already right: no relayout */
            have.forEach { t.removeSpan(it) }
            changed++
            for (r in want) t.setSpan(SynSpan(r.item.color.toArgb()), a + r.start, a + r.end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        if (changed > 0) redrawText()
    }

    /* Span edits don't always reach TextView's cached per-block drawing;
     * re-setting the (unused) shadow drops that cache. */
    private fun redrawText() = setShadowLayer(0f, 0f, 0f, 0)

    private fun androidx.compose.ui.graphics.Color.isSpecified() = this != androidx.compose.ui.graphics.Color.Unspecified

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val lay = layout ?: return
        index()
        val d = resources.displayMetrics.density
        val gw = paddingLeft - 8 * d
        bgPaint.color = gutterBg
        canvas.drawRect(scrollX.toFloat(), scrollY.toFloat(), scrollX + gw, (scrollY + height).toFloat(), bgPaint)
        val top = (scrollY - extendedPaddingTop).coerceAtLeast(0)
        val first = lay.getLineForVertical(top)
        val last = lay.getLineForVertical(scrollY + height)
        val cursorLine = lineOf(selectionStart.coerceAtLeast(0))
        val t = text
        for (row in first..last) {
            val start = lay.getLineStart(row)
            if (start != 0 && t[start - 1] != '\n') continue      /* wrapped continuation */
            val n = lineOf(start)
            numPaint.color = if (n == cursorLine) accent else palette.gutter.toArgb()
            canvas.drawText((n + 1).toString(), scrollX + gw - 8 * d, (lay.getLineBaseline(row) + extendedPaddingTop).toFloat(), numPaint)
        }
    }
}

/** Holds the editor view so the screen can save, undo, insert and scroll. */
class CodeEditorState {
    val history = EditHistory()
    var view: CodeEditText? = null
    /** Bumped on each edit; the screen derives "unsaved" from it. */
    var edits by mutableIntStateOf(0)
    fun text(): String? = view?.text?.toString()
    fun reveal(line: Int, col: Int = 0) { view?.reveal(line, col) }
}

/** MT-style text editor: opens straight into editing, coloured, with line numbers. */
@Composable
fun CodeEditor(
    initial: String,
    lang: Lang,
    state: CodeEditorState,
    modifier: Modifier = Modifier,
    wrap: Boolean = false,
    fontSize: Float = 12f,
    matches: List<Match> = emptyList(),
    active: Int = -1
) {
    val p = Ej.colors
    val fg = MaterialTheme.colorScheme.onBackground.toArgb()
    val gutter = MaterialTheme.colorScheme.surface.toArgb()
    val accent = MaterialTheme.colorScheme.primary.toArgb()
    val bg = MaterialTheme.colorScheme.background
    AndroidView(
        factory = { ctx ->
            CodeEditText(ctx, state.history).also { v ->
                v.style(lang, p, fg, gutter, accent, fontSize, wrap)
                v.load(initial)
                v.onEdit = { state.edits++ }
                state.view = v
            }
        },
        update = { v ->
            v.style(lang, p, fg, gutter, accent, fontSize, wrap)
            v.setHits(matches, active)
        },
        onRelease = { if (state.view === it) state.view = null },
        modifier = modifier.background(bg)
    )
}

private val SYMBOLS = listOf("⇥", "<", ">", "/", "=", "\"", ":", ";", ".", ",", "{", "}", "(", ")", "[", "]", "-", "_", "#", "@", "$", "\\")

/** MT-style key row under the editor: undo, redo and the symbols a phone keyboard hides. */
@Composable
fun SymbolBar(state: CodeEditorState) {
    val c = MaterialTheme.colorScheme
    val h = state.history
    Row(
        Modifier.fillMaxWidth().height(44.dp).background(c.surface),
        verticalAlignment = Alignment.CenterVertically
    ) {
        IconBtn(EjIcons.Undo, "Undo", enabled = h.canUndo) { state.view?.undo() }
        IconBtn(EjIcons.Redo, "Redo", enabled = h.canRedo) { state.view?.redo() }
        Row(Modifier.weight(1f).fillMaxHeight().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
            SYMBOLS.forEach { s ->
                Box(
                    Modifier.fillMaxHeight().widthIn(min = 38.dp).clickable { state.view?.insert(if (s == "⇥") "    " else s) },
                    contentAlignment = Alignment.Center
                ) {
                    Text(s, fontSize = 16.sp, fontFamily = FontFamily.Monospace, color = c.onSurface)
                }
            }
        }
    }
}
