package org.agentej.android

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import org.agentej.android.ui.AgentXTheme
import org.agentej.android.ui.ApkScreen
import org.agentej.android.ui.BrowserScreen
import org.agentej.android.ui.ViewerScreen

sealed class Route {
    data class Browse(val path: String) : Route()
    data class Apk(val path: String) : Route()
    data class Text(val title: String, val body: String) : Route()
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Native.init(assets, filesDir.absolutePath)
        setContent { AgentXTheme { AppRoot() } }
    }
}

private fun storageGranted(): Boolean = Native.storageOk()

private fun openStorageSettings(context: Context) {
    val intents = buildList {
        if (Build.VERSION.SDK_INT >= 30) {
            add(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:${context.packageName}")))
            add(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
        }
        add(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}")))
    }
    intents.forEach { i ->
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (runCatching { context.startActivity(i) }.isSuccess) return
    }
}

@Composable
fun AppRoot() {
    val context = LocalContext.current
    var granted by remember { mutableStateOf(storageGranted()) }
    val lifecycleOwner = LocalLifecycleOwner.current
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { granted = storageGranted() }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) granted = storageGranted()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    if (!granted) {
        PermissionGate(
            onRequest = {
                if (Build.VERSION.SDK_INT >= 30) openStorageSettings(context)
                else permissionLauncher.launch(
                    arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                )
            },
            onRecheck = { granted = storageGranted() }
        )
        return
    }

    val start = remember { Native.homePath() }
    val stack = remember { mutableStateListOf<Route>(Route.Browse(start)) }
    val push: (Route) -> Unit = { stack.add(it) }
    val pop: () -> Unit = { if (stack.size > 1) stack.removeAt(stack.lastIndex) }

    BackHandler(enabled = stack.size > 1) { pop() }

    when (val route = stack.last()) {
        is Route.Browse -> BrowserScreen(
            path = route.path,
            canGoBack = stack.size > 1,
            onBack = pop,
            onNavigate = { push(Route.Browse(it)) },
            onOpenText = { title, body -> push(Route.Text(title, body)) },
            onOpenApkTools = { push(Route.Apk(it)) },
            onShortcut = { push(Route.Browse(it)) }
        )

        is Route.Apk -> ApkScreen(
            apkPath = route.path,
            onBack = pop,
            onView = { title, body -> push(Route.Text(title, body)) }
        )

        is Route.Text -> ViewerScreen(route.title, route.body, onBack = pop)
    }
}

@Composable
private fun PermissionGate(onRequest: () -> Unit, onRecheck: () -> Unit) {
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).padding(24.dp)) {
        Spacer(Modifier.height(48.dp))
        Text("EJ Manager", fontSize = 26.sp, color = MaterialTheme.colorScheme.primary)
        Text(
            "Native APK toolkit: browse, unpack, decode manifest/resources, disassemble dex to smali, repack and sign.\n\nAll I/O and every tool runs in C; this screen is only UI.",
            fontSize = 12.sp,
            lineHeight = 18.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 8.dp)
        )
        Spacer(Modifier.height(24.dp))
        Button(onClick = onRequest, modifier = Modifier.fillMaxWidth()) { Text("Grant all-files access") }
        TextButton(onClick = onRecheck, modifier = Modifier.fillMaxWidth()) { Text("Recheck") }
        Spacer(Modifier.height(24.dp))
        Text(Native.version(), fontSize = 10.sp, fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
