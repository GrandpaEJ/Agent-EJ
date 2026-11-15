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
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.rememberDrawerState
import kotlinx.coroutines.launch
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
import org.agentej.android.ui.BrowserNav
import org.agentej.android.ui.BrowserScreen
import org.agentej.android.ui.DrawerContent
import org.agentej.android.ui.ViewerScreen

sealed class Route {
    data class Browse(val path: String) : Route()
    data class Apk(val path: String) : Route()
    /** [source] is the on-disk file when the text is editable, null for decoded/derived text. */
    data class Text(val title: String, val body: String, val source: String? = null) : Route()
}

/** A back-stack slot; [id] keys its saved UI state (scroll position, tabs). */
data class Entry(val id: Long, val route: Route)

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

    Shell()
}

@Composable
private fun Shell() {
    val context = LocalContext.current
    val prefs = remember { Prefs(context.applicationContext) }
    val clipboard = remember { FileClipboard() }
    val nextId = remember { longArrayOf(1) }
    fun entry(r: Route) = Entry(nextId[0]++, r)

    val start = remember { Native.homePath() }
    val stack = remember { mutableStateListOf(entry(Route.Browse(start))) }
    val forward = remember { mutableStateListOf<Entry>() }
    val saver = rememberSaveableStateHolder()
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()

    val push: (Route) -> Unit = { r ->
        forward.forEach { saver.removeState(it.id) }
        forward.clear()
        stack.add(entry(r))
    }
    val pop: () -> Unit = {
        if (stack.size > 1) forward.add(stack.removeAt(stack.lastIndex))
    }
    val goForward: () -> Unit = {
        if (forward.isNotEmpty()) stack.add(forward.removeAt(forward.lastIndex))
    }
    val jump: (String) -> Unit = { path ->
        scope.launch { drawer.close() }
        push(Route.Browse(path))
    }

    BackHandler(enabled = drawer.isOpen) { scope.launch { drawer.close() } }
    BackHandler(enabled = !drawer.isOpen && stack.size > 1) { pop() }

    val top = stack.last()
    ModalNavigationDrawer(
        drawerState = drawer,
        gesturesEnabled = top.route is Route.Browse || drawer.isOpen,
        scrimColor = androidx.compose.ui.graphics.Color.Black.copy(alpha = 0.5f),
        drawerContent = {
            DrawerContent(
                current = (top.route as? Route.Browse)?.path ?: "",
                prefs = prefs,
                onOpen = jump
            )
        }
    ) {
        saver.SaveableStateProvider(top.id) {
            when (val route = top.route) {
                is Route.Browse -> BrowserScreen(
                    path = route.path,
                    prefs = prefs,
                    clipboard = clipboard,
                    nav = BrowserNav(
                        canBack = stack.size > 1,
                        canForward = forward.isNotEmpty(),
                        back = pop,
                        forward = goForward,
                        open = { push(Route.Browse(it)) },
                        text = { t, b, src -> push(Route.Text(t, b, src)) },
                        apk = { push(Route.Apk(it)) },
                        drawer = { scope.launch { drawer.open() } }
                    )
                )

                is Route.Apk -> ApkScreen(
                    apkPath = route.path,
                    onBack = pop,
                    onView = { title, body -> push(Route.Text(title, body)) }
                )

                is Route.Text -> ViewerScreen(route.title, route.body, route.source, onBack = pop)
            }
        }
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
