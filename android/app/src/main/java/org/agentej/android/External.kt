package org.agentej.android

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.webkit.MimeTypeMap
import androidx.core.content.FileProvider
import java.io.File

/** Hands a file to other apps: view (with a chosen type), share, install. */
object External {
    /** "Open with…" → External tab: the type the user picks decides which apps match. */
    enum class As(val label: String, val mime: String?) {
        AUTO("Detect from name", null), TEXT("Text", "text/plain"), IMAGE("Image", "image/*"),
        AUDIO("Audio", "audio/*"), VIDEO("Video", "video/*"), ANY("Any app", "*/*")
    }

    fun mimeOf(path: String): String =
        MimeTypeMap.getSingleton().getMimeTypeFromExtension(Util.ext(path)) ?: "*/*"

    private fun uri(context: Context, path: String) =
        FileProvider.getUriForFile(context, context.packageName + ".files", File(path))

    /** Returns null on success, or why it could not start. */
    fun view(context: Context, path: String, mime: String? = null): String? = start(context) {
        Intent(Intent.ACTION_VIEW).setDataAndType(uri(context, path), mime ?: mimeOf(path))
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }

    fun share(context: Context, path: String): String? = start(context) {
        val send = Intent(Intent.ACTION_SEND).setType(mimeOf(path))
            .putExtra(Intent.EXTRA_STREAM, uri(context, path))
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        Intent.createChooser(send, "Share ${path.substringAfterLast('/')}")
    }

    fun install(context: Context, path: String): String? = view(context, path, "application/vnd.android.package-archive")

    private inline fun start(context: Context, build: () -> Intent): String? = try {
        val i = build()
        context.startActivity(if (i.action == Intent.ACTION_CHOOSER) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        else Intent.createChooser(i, null).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        null
    } catch (e: ActivityNotFoundException) {
        "No app can open this"
    } catch (e: IllegalArgumentException) {
        "This location can't be shared"
    }
}
