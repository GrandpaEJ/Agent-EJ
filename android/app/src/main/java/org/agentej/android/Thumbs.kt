package org.agentej.android

import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.pdf.PdfRenderer
import android.media.ExifInterface
import android.media.MediaMetadataRetriever
import android.media.ThumbnailUtils
import android.os.Build
import android.os.ParcelFileDescriptor
import android.util.LruCache
import android.util.Size
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Collections
import kotlin.coroutines.coroutineContext

enum class ThumbKind { IMAGE, APK, VIDEO, AUDIO, PDF }

/** What to thumbnail. [entry] is set for an image inside an archive at [path]. */
data class ThumbSpec(val path: String, val entry: String?, val kind: ThumbKind, val stamp: String) {
    fun key(px: Int) = "$px|$path|${entry ?: ""}|$stamp"
}

/**
 * Thumbnails for the browser. Every source is downsampled to the tile size on
 * decode (never a full-resolution bitmap in memory), held in a byte-bounded
 * LRU, and the slow ones (APK icons, video frames, PDF pages) are also kept as
 * small WebP files in cacheDir so they don't need re-decoding next launch.
 */
object Thumbs {
    private const val MEM_BYTES = 8 * 1024 * 1024
    private const val DISK_BYTES = 24L * 1024 * 1024
    private const val MAX_ENTRY = 8 * 1024 * 1024

    private val mem = object : LruCache<String, ImageBitmap>(MEM_BYTES) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }
    private val failed = Collections.synchronizedSet(HashSet<String>())
    private val gate = Semaphore(2)
    private var diskDir: File? = null

    private val images = setOf("png", "jpg", "jpeg", "webp", "gif", "bmp", "heic", "heif")
    private val videos = setOf("mp4", "mkv", "webm", "3gp", "mov", "avi", "m4v")
    private val audio = setOf("mp3", "m4a", "flac", "ogg", "opus", "aac", "wav")

    fun kindOf(name: String, inArchive: Boolean): ThumbKind? {
        val ext = Util.ext(name)
        return when {
            ext in images -> ThumbKind.IMAGE
            inArchive -> null
            ext == "apk" -> ThumbKind.APK
            ext in videos -> ThumbKind.VIDEO
            ext in audio -> ThumbKind.AUDIO
            ext == "pdf" -> ThumbKind.PDF
            else -> null
        }
    }

    fun peek(spec: ThumbSpec, px: Int): ImageBitmap? = mem.get(spec.key(px))

    /** Null when the file has no picture to show; never throws. */
    suspend fun load(context: Context, spec: ThumbSpec, px: Int): ImageBitmap? {
        val key = spec.key(px)
        mem.get(key)?.let { return it }
        if (key in failed) return null
        return gate.withPermit {
            coroutineContext.ensureActive()
            withContext(Dispatchers.IO) {
                mem.get(key) ?: runCatching { produce(context, spec, px, key) }.getOrNull()
                    ?.asImageBitmap()?.also { mem.put(key, it) }
                    ?: run { failed.add(key); null }
            }
        }
    }

    private fun produce(context: Context, spec: ThumbSpec, px: Int, key: String): Bitmap? {
        val slow = spec.kind != ThumbKind.IMAGE || spec.entry != null
        val file = if (slow) diskFile(context, key) else null
        if (file != null && file.exists()) BitmapFactory.decodeFile(file.path)?.let { return it }
        val bmp = when (spec.kind) {
            ThumbKind.IMAGE -> image(spec, px)
            ThumbKind.APK -> apkIcon(context, spec.path, px)
            ThumbKind.VIDEO -> video(spec.path, px)
            ThumbKind.AUDIO -> cover(spec.path, px)
            ThumbKind.PDF -> pdf(spec.path, px)
        } ?: return null
        if (file != null) runCatching {
            file.outputStream().use { bmp.compress(webp(), 80, it) }
        }
        return bmp
    }

    @Suppress("DEPRECATION")
    private fun webp() = if (Build.VERSION.SDK_INT >= 30) Bitmap.CompressFormat.WEBP_LOSSY else Bitmap.CompressFormat.WEBP

    private fun diskFile(context: Context, key: String): File {
        val dir = diskDir ?: File(context.cacheDir, "thumbs").also { it.mkdirs(); diskDir = it; trim(it) }
        val name = java.lang.Long.toHexString(key.hashCode().toLong() and 0xffffffffL) +
            java.lang.Long.toHexString(key.length.toLong())
        return File(dir, "$name.webp")
    }

    /** Oldest-first eviction down to half the budget, once per process. */
    private fun trim(dir: File) {
        val files = dir.listFiles()?.sortedBy { it.lastModified() } ?: return
        var total = files.sumOf { it.length() }
        if (total <= DISK_BYTES) return
        for (f in files) {
            if (total <= DISK_BYTES / 2) break
            total -= f.length()
            f.delete()
        }
    }

    private fun sample(w: Int, h: Int, px: Int): Int {
        var s = 1
        while (w / (s * 2) >= px && h / (s * 2) >= px) s *= 2
        return s
    }

    /** Center-crop to a px×px square; tiles and grid cells are square. */
    private fun square(src: Bitmap, px: Int, rotate: Int = 0): Bitmap {
        val side = minOf(src.width, src.height)
        val m = Matrix().apply {
            val s = px.toFloat() / side
            postScale(s, s)
            if (rotate != 0) postRotate(rotate.toFloat())
        }
        val out = Bitmap.createBitmap(src, (src.width - side) / 2, (src.height - side) / 2, side, side, m, true)
        if (out !== src) src.recycle()
        return out
    }

    private fun image(spec: ThumbSpec, px: Int): Bitmap? {
        val bytes = spec.entry?.let { e ->
            Native.readEntry(spec.path, e)?.takeIf { it.size <= MAX_ENTRY } ?: return null
        }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        if (bytes != null) BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        else BitmapFactory.decodeFile(spec.path, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        val opts = BitmapFactory.Options().apply { inSampleSize = sample(bounds.outWidth, bounds.outHeight, px) }
        val bmp = (if (bytes != null) BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)
        else BitmapFactory.decodeFile(spec.path, opts)) ?: return null
        val rot = if (bytes == null && Util.ext(spec.path) in setOf("jpg", "jpeg")) exifRotation(spec.path) else 0
        return square(bmp, px, rot)
    }

    private fun exifRotation(path: String): Int = runCatching {
        when (ExifInterface(path).getAttributeInt(ExifInterface.TAG_ORIENTATION, 1)) {
            ExifInterface.ORIENTATION_ROTATE_90 -> 90
            ExifInterface.ORIENTATION_ROTATE_180 -> 180
            ExifInterface.ORIENTATION_ROTATE_270 -> 270
            else -> 0
        }
    }.getOrDefault(0)

    /** PackageManager resolves adaptive icons and density buckets for us. */
    @Suppress("DEPRECATION")
    private fun apkIcon(context: Context, path: String, px: Int): Bitmap? {
        val pm = context.packageManager
        val info = if (Build.VERSION.SDK_INT >= 33) pm.getPackageArchiveInfo(path, PackageManager.PackageInfoFlags.of(0))
        else pm.getPackageArchiveInfo(path, 0)
        val app = info?.applicationInfo ?: return null
        app.sourceDir = path
        app.publicSourceDir = path
        return app.loadIcon(pm).toBitmap(px, px, Bitmap.Config.ARGB_8888)
    }

    private fun video(path: String, px: Int): Bitmap? {
        if (Build.VERSION.SDK_INT >= 29) {
            return square(ThumbnailUtils.createVideoThumbnail(File(path), Size(px, px), null), px)
        }
        return retriever(path) { r -> r.getFrameAtTime(1_000_000)?.let { square(it, px) } }
    }

    private fun cover(path: String, px: Int): Bitmap? = retriever(path) { r ->
        r.embeddedPicture?.let { b ->
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(b, 0, b.size, bounds)
            val opts = BitmapFactory.Options().apply { inSampleSize = sample(bounds.outWidth, bounds.outHeight, px) }
            BitmapFactory.decodeByteArray(b, 0, b.size, opts)?.let { square(it, px) }
        }
    }

    private inline fun <T> retriever(path: String, block: (MediaMetadataRetriever) -> T): T {
        val r = MediaMetadataRetriever()
        try {
            r.setDataSource(path)
            return block(r)
        } finally {
            r.release()
        }
    }

    private fun pdf(path: String, px: Int): Bitmap? =
        ParcelFileDescriptor.open(File(path), ParcelFileDescriptor.MODE_READ_ONLY).use { fd ->
            PdfRenderer(fd).use { doc ->
                if (doc.pageCount == 0) return null
                doc.openPage(0).use { page ->
                    val s = px.toFloat() / maxOf(page.width, page.height)
                    val bmp = Bitmap.createBitmap(
                        (page.width * s).toInt().coerceAtLeast(1),
                        (page.height * s).toInt().coerceAtLeast(1),
                        Bitmap.Config.ARGB_8888
                    )
                    bmp.eraseColor(android.graphics.Color.WHITE)
                    page.render(bmp, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
                    bmp
                }
            }
        }
}
