package org.agentej.android.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import org.agentej.android.ThumbSpec
import org.agentej.android.Thumbs

/**
 * The thumbnail for [spec] at [size], or null until (and unless) one exists.
 * Loading starts when the row composes and is cancelled when it scrolls away.
 */
@Composable
fun rememberThumb(spec: ThumbSpec?, size: Dp): ImageBitmap? {
    if (spec == null) return null
    val context = LocalContext.current
    val px = with(LocalDensity.current) { size.roundToPx() }
    var bmp by remember(spec, px) { mutableStateOf(Thumbs.peek(spec, px)) }
    if (bmp == null) LaunchedEffect(spec, px) {
        bmp = Thumbs.load(context.applicationContext, spec, px)
    }
    return bmp
}
