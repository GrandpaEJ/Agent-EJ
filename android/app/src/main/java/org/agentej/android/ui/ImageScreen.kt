package org.agentej.android.ui

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.rememberTransformableState
import androidx.compose.foundation.gestures.transformable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import org.agentej.android.Target

/** Decoded at most at twice the screen size: big photos never land in memory at full resolution. */
private fun decode(t: Target, maxPx: Int): Pair<ImageBitmap, String>? {
    val bytes = t.archive?.let { Native.readEntry(it, t.entry ?: "") ?: return null }
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    if (bytes != null) BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds) else BitmapFactory.decodeFile(t.path, bounds)
    if (bounds.outWidth <= 0) return null
    var s = 1
    while (bounds.outWidth / (s * 2) >= maxPx || bounds.outHeight / (s * 2) >= maxPx) s *= 2
    val o = BitmapFactory.Options().apply { inSampleSize = s }
    val bmp = (if (bytes != null) BitmapFactory.decodeByteArray(bytes, 0, bytes.size, o) else BitmapFactory.decodeFile(t.path, o))
        ?: return null
    return bmp.asImageBitmap() to "${bounds.outWidth}×${bounds.outHeight}"
}

@Composable
fun ImageScreen(t: Target, onBack: () -> Unit) {
    val dm = LocalContext.current.resources.displayMetrics
    val maxPx = maxOf(dm.widthPixels, dm.heightPixels)
    /** Unit while decoding, null when it can't be decoded. */
    val state by produceState<Any?>(Unit, t) { value = withContext(Dispatchers.IO) { decode(t, maxPx) } }
    @Suppress("UNCHECKED_CAST") val img = state as? Pair<ImageBitmap, String>
    var scale by remember { mutableFloatStateOf(1f) }
    var pan by remember { mutableStateOf(Offset.Zero) }
    val zoom = rememberTransformableState { z, p, _ ->
        scale = (scale * z).coerceIn(1f, 8f)
        pan = if (scale == 1f) Offset.Zero else pan + p
    }
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).safeDrawingPadding()) {
        EjTopBar(t.name, img?.second ?: "", leading = {
            IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back", onClick = onBack)
        })
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            val pic = img
            when {
                pic != null -> {
                    Image(
                        pic.first, t.name,
                        Modifier.fillMaxSize()
                            .pointerInput(Unit) { detectTapGestures(onDoubleTap = { scale = if (scale > 1f) 1f else 2.5f; pan = Offset.Zero }) }
                            .transformable(zoom)
                            .graphicsLayer(scaleX = scale, scaleY = scale, translationX = pan.x, translationY = pan.y)
                    )
                }
                state == Unit -> CircularProgressIndicator()
                else -> EmptyState("Can't show this image", "The format isn't supported or the file is damaged.")
            }
        }
    }
}
