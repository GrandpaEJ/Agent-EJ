package org.agentej.android.ui

import android.graphics.Canvas
import android.graphics.Paint
import android.view.MotionEvent
import android.widget.TextView

/**
 * A draggable thumb on the right edge, shown once a file is a few screens
 * long: drag it to cross thousands of lines at once.
 */
internal class FastScroll(private val v: TextView) {
    private val d = v.resources.displayMetrics.density
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val thumbH = 52 * d
    private val thumbW = 6 * d
    private val grabW = 40 * d
    private var dragging = false
    private var grab = 0f
    var color = 0
    var activeColor = 0

    private fun range(): Int = (v.layout?.height ?: 0) + v.extendedPaddingTop + v.extendedPaddingBottom - v.height
    private fun shown() = range() > v.height * 3
    private fun top(): Float {
        val r = range()
        return if (r <= 0) 0f else v.scrollY.toFloat() / r * (v.height - thumbH)
    }

    fun draw(c: Canvas) {
        if (!shown()) return
        val w = if (dragging) thumbW * 1.6f else thumbW
        val x = v.scrollX + v.width - w - 3 * d
        val y = v.scrollY + top()
        paint.color = if (dragging) activeColor else color
        c.drawRoundRect(x, y, x + w, y + thumbH, w / 2, w / 2, paint)
    }

    /** True when the event belongs to the thumb. */
    fun touch(e: MotionEvent): Boolean {
        if (!shown() && !dragging) return false
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val t = top()
                if (e.x >= v.width - grabW && e.y >= t - 16 * d && e.y <= t + thumbH + 16 * d) {
                    dragging = true
                    grab = e.y - t
                    v.parent?.requestDisallowInterceptTouchEvent(true)
                    v.invalidate()
                    return true
                }
            }
            MotionEvent.ACTION_MOVE -> if (dragging) {
                val f = ((e.y - grab) / (v.height - thumbH)).coerceIn(0f, 1f)
                v.scrollTo(v.scrollX, (f * range()).toInt())
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (dragging) {
                dragging = false
                v.invalidate()
                return true
            }
        }
        return false
    }
}
