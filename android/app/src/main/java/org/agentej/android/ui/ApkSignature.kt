package org.agentej.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.interfaces.ECPublicKey
import java.security.interfaces.RSAPublicKey
import java.text.SimpleDateFormat
import java.util.Locale
import java.util.zip.CRC32
import javax.security.auth.x500.X500Principal

/** The APK's first signer certificate (DER) as the package manager verified it; null when unsigned. */
internal class SigInfo(val file: String, val scheme: String, val cert: ByteArray?)

private fun x509(der: ByteArray): X509Certificate? = runCatching {
    CertificateFactory.getInstance("X.509").generateCertificate(der.inputStream()) as X509Certificate
}.getOrNull()

private fun hex(b: ByteArray, colon: Boolean, upper: Boolean): String =
    b.joinToString(if (colon) ":" else "") { (if (upper) "%02X" else "%02x").format(it) }

private fun digest(alg: String, b: ByteArray) = MessageDigest.getInstance(alg).digest(b)

private fun keyBits(c: X509Certificate): Int = when (val k = c.publicKey) {
    is RSAPublicKey -> k.modulus.bitLength()
    is ECPublicKey -> k.params.order.bitLength()
    else -> 0
}

private val STAMP = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)

/** MT's "Signature information": certificate summary, hashes with format switches, VIEW DATA / COMPARE. */
@Composable
internal fun SignatureDialog(sig: SigInfo, onHex: (String) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val der = sig.cert
    val cert = remember(der) { der?.let(::x509) }
    var colon by remember { mutableStateOf(true) }
    var upper by remember { mutableStateOf(true) }
    var showKey by remember { mutableStateOf(false) }
    var compare by remember { mutableStateOf(false) }
    /* Signature.hashCode() is Arrays.hashCode over the DER, the value MT calls HASH */
    val hashes = remember(der, colon, upper) {
        if (der == null) emptyList() else {
            val h = der.contentHashCode()
            val crc = CRC32().apply { update(der) }.value
            listOf(
                "HASH" to "0x%08X (%d)".format(h, h),
                "CRC32" to "0x%08X (%d)".format(crc, crc.toInt()),
                "MD5" to hex(digest("MD5", der), colon, upper),
                "SHA1" to hex(digest("SHA-1", der), colon, upper),
                "SHA256" to hex(digest("SHA-256", der), colon, upper)
            )
        }
    }
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = RoundedCornerShape(4.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Column(Modifier.padding(top = 18.dp, bottom = 2.dp)) {
                Text("Signature information", fontSize = 19.sp, modifier = Modifier.padding(horizontal = 22.dp))
                Column(
                    Modifier.heightIn(max = 520.dp).verticalScroll(rememberScrollState()).padding(vertical = 10.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp)
                ) {
                    KV("File", sig.file) { copyText(context, sig.file) }
                    KV("Scheme", sig.scheme) { copyText(context, sig.scheme) }
                    KV("Status", if (cert != null) "Verified successfully" else "No valid signature") {}
                    if (cert != null) {
                        KV("Algorithm", cert.sigAlgName) { copyText(context, cert.sigAlgName) }
                        KV("Public key", "${cert.publicKey.algorithm} · ${keyBits(cert)} bits ›") { showKey = true }
                        KV("Valid from", STAMP.format(cert.notBefore)) { copyText(context, STAMP.format(cert.notBefore)) }
                        KV("Valid until", STAMP.format(cert.notAfter)) { copyText(context, STAMP.format(cert.notAfter)) }
                        val owner = cert.subjectX500Principal.getName(X500Principal.RFC1779)
                        KV("Owner", owner) { copyText(context, owner) }
                        for ((k, v) in hashes) KV(k, v) { copyText(context, v) }
                        Toggle("Format", "Add colon", colon) { colon = it }
                        Toggle("", "Upper case", upper) { upper = it }
                    }
                }
                Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    TextButton(enabled = der != null, onClick = {
                        val f = java.io.File(context.cacheDir, "signature.der")
                        if (der != null && runCatching { f.writeBytes(der) }.isSuccess) onHex(f.path)
                    }) { Text("VIEW DATA") }
                    Row {
                        TextButton(enabled = der != null, onClick = { compare = true }) { Text("COMPARE") }
                        TextButton(onClick = onDismiss) { Text("CLOSE") }
                    }
                }
            }
        }
    }
    if (showKey && cert != null) PublicKeyDialog(cert) { showKey = false }
    if (compare && der != null) CompareDialog(der) { compare = false }
}

@Composable
private fun KV(k: String, v: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(horizontal = 22.dp, vertical = 5.dp)) {
        Text(k, fontSize = 14.sp, color = MaterialTheme.colorScheme.onSurface, modifier = Modifier.width(88.dp))
        Text(v, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
    }
}

@Composable
private fun Toggle(k: String, label: String, on: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable { onChange(!on) }.padding(horizontal = 22.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(k, fontSize = 14.sp, color = MaterialTheme.colorScheme.onSurface, modifier = Modifier.width(88.dp))
        Text(label, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
        Switch(on, onChange)
    }
}

@Composable
private fun PublicKeyDialog(cert: X509Certificate, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val key = cert.publicKey
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = RoundedCornerShape(4.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Column(Modifier.padding(top = 18.dp, bottom = 2.dp)) {
                Text("Public key", fontSize = 19.sp, modifier = Modifier.padding(horizontal = 22.dp))
                Column(Modifier.heightIn(max = 520.dp).verticalScroll(rememberScrollState()).padding(vertical = 10.dp)) {
                    KV("Algorithm", key.algorithm) { copyText(context, key.algorithm) }
                    KV("Key size", "${keyBits(cert)} bits") {}
                    when (key) {
                        is RSAPublicKey -> {
                            val e = key.publicExponent
                            KV("Exponent", "$e (0x${e.toString(16)})") { copyText(context, e.toString()) }
                            KV("Modulus", key.modulus.toString()) { copyText(context, key.modulus.toString()) }
                        }
                        is ECPublicKey -> {
                            KV("X", key.w.affineX.toString(16)) { copyText(context, key.w.affineX.toString(16)) }
                            KV("Y", key.w.affineY.toString(16)) { copyText(context, key.w.affineY.toString(16)) }
                        }
                        else -> hex(key.encoded, false, true).let { KV("Encoded", it) { copyText(context, it) } }
                    }
                }
                TextButton(onClick = onDismiss, modifier = Modifier.align(Alignment.End).padding(end = 8.dp)) { Text("CLOSE") }
            }
        }
    }
}

/** MT's "Paste and Verify": a pasted hash is checked against every hash of the certificate. */
@Composable
private fun CompareDialog(der: ByteArray, onDismiss: () -> Unit) {
    val context = LocalContext.current
    var text by remember { mutableStateOf("") }
    val known = remember(der) {
        val crc = CRC32().apply { update(der) }.value
        listOf(
            "MD5" to hex(digest("MD5", der), false, false), "SHA1" to hex(digest("SHA-1", der), false, false),
            "SHA256" to hex(digest("SHA-256", der), false, false), "CRC32" to "%08x".format(crc),
            "HASH" to "%08x".format(der.contentHashCode())
        )
    }
    val norm = text.trim().lowercase(Locale.US).removePrefix("0x").filter { it.isLetterOrDigit() }
    val hit = known.firstOrNull { it.second == norm }?.first
        ?: if (norm.isNotEmpty() && norm.trimStart('-').all(Char::isDigit) && text.trim().toIntOrNull() == der.contentHashCode()) "HASH" else null
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = RoundedCornerShape(4.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            Column(Modifier.padding(start = 22.dp, end = 16.dp, top = 18.dp, bottom = 14.dp)) {
                Text("Paste and Verify", fontSize = 19.sp)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextField(
                        text, { text = it }, singleLine = true, modifier = Modifier.weight(1f),
                        colors = TextFieldDefaults.colors(focusedContainerColor = Color.Transparent, unfocusedContainerColor = Color.Transparent)
                    )
                    Spacer(Modifier.width(8.dp))
                    OutlinedButton(onClick = {
                        val cm = context.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                        text = cm.primaryClip?.getItemAt(0)?.coerceToText(context)?.toString().orEmpty()
                    }) { Text("PASTE") }
                }
                if (norm.isNotEmpty()) Text(
                    if (hit != null) "Matches the $hit of this signature" else "Does not match this signature",
                    fontSize = 13.sp, modifier = Modifier.padding(top = 8.dp),
                    color = if (hit != null) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
                )
            }
        }
    }
}
