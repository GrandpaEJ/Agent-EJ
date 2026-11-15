package org.agentej.android.ui

import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.os.Build
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** What the MT-style APK info dialog shows: header, then label/value rows. */
internal class ApkInfo(val label: String, val version: String, val icon: Bitmap?, val rows: List<InfoRow>, val sig: SigInfo)

/** One table row; a tap copies [v], opens [dir] in the pane, or (for [sig]) the signature dialog. */
internal class InfoRow(val k: String, val v: String, val dir: String? = null, val sig: Boolean = false)

/** Packer/protector markers seen in entry names (libs and assets). */
private val PROTECTORS = listOf(
    "libjiagu" to "360 Jiagu", "libshella" to "Tencent Legu", "libshellx" to "Tencent Legu",
    "libsecexe" to "Bangcle", "libsecmain" to "Bangcle", "libDexHelper" to "SecNeo",
    "libexecmain" to "Ijiami", "ijiami" to "Ijiami", "libbaiduprotect" to "Baidu",
    "libmobisec" to "Alibaba", "aliprotector" to "Alibaba", "libnesec" to "NetEase Yidun",
    "libkwscmm" to "Kiwisec", "libAPKProtect" to "APKProtect", "libpairipcore" to "Google PairIP",
    "dexprotector" to "DexProtector", "libdexprotector" to "DexProtector", "libapssec" to "Virbox"
)

private fun schemes(bits: Int): String = when {
    bits < 0 -> "Unreadable"
    bits == 0 -> "Unsigned"
    else -> listOf(1 to "V1", 2 to "V2", 4 to "V3", 8 to "V3.1", 16 to "Stamp")
        .filter { bits and it.first != 0 }.joinToString(" + ") { it.second }
}

private fun protection(entries: List<String>): String {
    val hits = PROTECTORS.filter { (k, _) -> entries.any { k in it } }.map { it.second }.distinct()
    return if (hits.isEmpty()) "No Detected" else hits.joinToString(", ")
}

private val ANDROID = mapOf(
    21 to "5", 22 to "5.1", 23 to "6", 24 to "7", 25 to "7.1", 26 to "8", 27 to "8.1", 28 to "9", 29 to "10",
    30 to "11", 31 to "12", 32 to "12L", 33 to "13", 34 to "14", 35 to "15", 36 to "16", 37 to "17"
)

private fun sdk(api: Int) = ANDROID[api]?.let { "Android $it (API $api)" } ?: "API $api"

/** MT's size style: 29.49M. */
private fun mtSize(b: Long): String = when {
    b >= 1L shl 30 -> String.format(Locale.US, "%.2fG", b / 1073741824.0)
    b >= 1L shl 20 -> String.format(Locale.US, "%.2fM", b / 1048576.0)
    b >= 1L shl 10 -> String.format(Locale.US, "%.2fK", b / 1024.0)
    else -> "${b}B"
}

private fun date(ms: Long) = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(ms))

/** Blocking: PackageManager on the APK file plus the core's signing-block scan. */
@Suppress("DEPRECATION")
internal fun loadApkInfo(context: Context, path: String): ApkInfo {
    val pm = context.packageManager
    val flags = if (Build.VERSION.SDK_INT >= 28) PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES
    val pi = pm.getPackageArchiveInfo(path, flags)
    val ai = pi?.applicationInfo?.apply { sourceDir = path; publicSourceDir = path }
    val rows = ArrayList<InfoRow>()
    operator fun ArrayList<InfoRow>.plusAssign(kv: Pair<String, String>) { add(InfoRow(kv.first, kv.second)) }
    val scheme = schemes(Native.apkSigSchemes(path))
    val signers = if (Build.VERSION.SDK_INT >= 28) pi?.signingInfo?.apkContentsSigners else pi?.signatures
    val code = pi?.let { if (Build.VERSION.SDK_INT >= 28) it.longVersionCode else it.versionCode.toLong() }
    rows += "Package name" to (pi?.packageName ?: "Unreadable manifest")
    rows += "Version code" to (code?.toString() ?: "-")
    rows += "File size" to mtSize(java.io.File(path).length())
    rows.add(InfoRow("Signature", scheme, sig = true))
    rows += "Protection" to protection((Native.zipEntries(path) ?: emptyArray()).map { it.substringBefore('\t') })
    if (ai != null) {
        rows += "Target SDK" to sdk(ai.targetSdkVersion)
        if (Build.VERSION.SDK_INT >= 24) rows += "Minimum SDK" to sdk(ai.minSdkVersion)
    }
    val inst = pi?.packageName?.let { runCatching { pm.getPackageInfo(it, 0) }.getOrNull() }
    if (inst == null) {
        rows += "Installed" to "Not installed"
        rows.add(InfoRow("APK path", path, dir = java.io.File(path).parent))
    } else {
        val ia = inst.applicationInfo
        val icode = if (Build.VERSION.SDK_INT >= 28) inst.longVersionCode else inst.versionCode.toLong()
        rows += "Installed" to "${inst.versionName ?: "-"} ($icode)"
        ia?.dataDir?.let { rows.add(InfoRow("Data directory 1", it, dir = it)) }
        val ext = "${android.os.Environment.getExternalStorageDirectory().path}/Android/data/${inst.packageName}"
        rows.add(InfoRow("Data directory 2", ext, dir = ext))
        val apk = ia?.sourceDir ?: path
        rows.add(InfoRow("APK path", apk, dir = java.io.File(apk).parent))
        rows += "First install" to date(inst.firstInstallTime)
        rows += "Last update" to date(inst.lastUpdateTime)
        ia?.uid?.let { rows += "UID" to it.toString() }
    }
    val icon = runCatching { ai?.loadIcon(pm)?.toBitmap(144, 144) }.getOrNull()
    val sig = SigInfo(java.io.File(path).name, scheme, signers?.firstOrNull()?.toByteArray())
    return ApkInfo(ai?.loadLabel(pm)?.toString() ?: java.io.File(path).name, pi?.versionName ?: "", icon, rows, sig)
}

internal fun copyText(context: Context, v: String) {
    (context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager)
        .setPrimaryClip(android.content.ClipData.newPlainText("apk info", v))
    android.widget.Toast.makeText(context, "Copied to the clipboard", android.widget.Toast.LENGTH_SHORT).show()
}

/** MT's APK tap dialog: what the APK is, then FUNCTION (all tools) · VIEW (as a folder) · INSTALL. */
@Composable
internal fun ApkInfoDialog(
    path: String, onFunction: () -> Unit, onView: () -> Unit, onInstall: () -> Unit,
    sigOpen: Boolean, onSig: (Boolean) -> Unit,
    onOpenDir: (String) -> Unit, onHex: (String) -> Unit, onDismiss: () -> Unit
) {
    val context = LocalContext.current
    var info by remember(path) { mutableStateOf<ApkInfo?>(null) }
    LaunchedEffect(path) { info = withContext(Dispatchers.IO) { loadApkInfo(context, path) } }
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = RoundedCornerShape(4.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Column(Modifier.padding(top = 18.dp, bottom = 2.dp)) {
                val i = info
                if (i == null) Box(Modifier.fillMaxWidth().padding(40.dp), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
                else {
                    Row(Modifier.padding(horizontal = 18.dp), verticalAlignment = Alignment.CenterVertically) {
                        i.icon?.let { Image(it.asImageBitmap(), null, Modifier.size(52.dp)) }
                        Spacer(Modifier.width(14.dp))
                        Column {
                            Text(i.label, fontSize = 19.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            if (i.version.isNotEmpty()) Text(i.version, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    /* MT's two-column table: label left, value right on one line; tap a row to copy its value */
                    Column(Modifier.heightIn(max = 460.dp).verticalScroll(rememberScrollState()).padding(top = 10.dp, bottom = 4.dp)) {
                        for (r in i.rows) Row(
                            Modifier.fillMaxWidth().clickable {
                                when {
                                    r.sig -> onSig(true)
                                    r.dir != null -> onOpenDir(r.dir)
                                    else -> copyText(context, r.v)
                                }
                            }.padding(horizontal = 20.dp, vertical = 2.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(r.k, fontSize = 14.sp, maxLines = 1, color = MaterialTheme.colorScheme.onSurface, modifier = Modifier.width(124.dp))
                            Text(r.v, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                                color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
                        }
                    }
                }
                Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    TextButton(onClick = onFunction) { Text("FUNCTION") }
                    Row {
                        TextButton(onClick = onView) { Text("VIEW") }
                        TextButton(onClick = onInstall) { Text("INSTALL") }
                    }
                }
            }
        }
    }
    val i = info
    if (sigOpen && i != null) SignatureDialog(i.sig, onHex = onHex, onDismiss = { onSig(false) })
}
