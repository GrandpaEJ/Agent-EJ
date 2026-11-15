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
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Dispatchers
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
import org.agentej.android.ui.Commander
import org.agentej.android.ui.DexEditorScreen
import org.agentej.android.ui.DexEditorState
import org.agentej.android.ui.DexSource
import org.agentej.android.ui.DrawerContent
import org.agentej.android.ui.HexScreen
import org.agentej.android.ui.ImageScreen
import org.agentej.android.ui.Pane
import org.agentej.android.ui.ViewerScreen

/** Screens opened over the file panes. */
sealed class Route {
    data class Apk(val path: String) : Route()
    /** [source] is the on-disk file when the text is editable, null for decoded/derived text. */
    data class Text(
        val title: String, val body: String, val source: String? = null, val line: Int = -1, val needle: String? = null,
        val onSave: (suspend (String) -> String?)? = null, val savedNote: String? = null
    ) : Route()
    data class Hex(val target: Target) : Route()
    data class Image(val target: Target) : Route()
    class Dex(src: DexSource) : Route() { val state = DexEditorState(src) }
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
    val newId = { nextId[0]++ }
    val saver = rememberSaveableStateHolder()
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()

    val panes = remember {
        val home = Native.homePath()
        val last = prefs.lastPaths
        val start = { i: Int -> last.getOrNull(i)?.takeIf { FileOps.exists(it) } ?: home }
        mutableStateListOf(Pane(start(0), newId), Pane(start(1), newId)).onEach { p ->
            p.dropped = { saver.removeState(it) }
        }
    }
    var active by remember { mutableIntStateOf(0) }
    /** Viewer/tool screens stacked over the panes; empty = the panes are showing. */
    val overlays = remember { mutableStateListOf<Entry>() }
    val push: (Route) -> Unit = { overlays.add(Entry(newId(), it)) }
    val pop: () -> Unit = {
        if (overlays.isNotEmpty()) overlays.removeAt(overlays.lastIndex).let { e ->
            (e.route as? Route.Dex)?.state?.close()
            saver.removeState(e.id)
        }
    }

    LaunchedEffect(Unit) { withContext(Dispatchers.IO) { Workspace.ensure() } }
    LaunchedEffect(panes[0].path, panes[1].path) { prefs.savePanes(panes[0].path, panes[1].path) }

    BackHandler(enabled = drawer.isOpen) { scope.launch { drawer.close() } }
    BackHandler(enabled = !drawer.isOpen && overlays.isNotEmpty()) { pop() }

    val top = overlays.lastOrNull()
    ModalNavigationDrawer(
        drawerState = drawer,
        gesturesEnabled = top == null || drawer.isOpen,
        scrimColor = androidx.compose.ui.graphics.Color.Black.copy(alpha = 0.5f),
        drawerContent = {
            DrawerContent(current = panes[active].path, prefs = prefs, onOpen = { path ->
                scope.launch { drawer.close() }
                overlays.forEach { (it.route as? Route.Dex)?.state?.close(); saver.removeState(it.id) }
                overlays.clear()
                panes[active].open(path)
            })
        }
    ) {
        if (top == null) {
            Commander(
                panes = panes, active = active, onActivate = { active = it },
                onSwap = {
                    if (prefs.dual) { val a = panes[0]; panes[0] = panes[1]; panes[1] = a } else active = 1 - active
                },
                prefs = prefs, clipboard = clipboard, saver = saver, push = push,
                openDrawer = { scope.launch { drawer.open() } }
            )
        } else saver.SaveableStateProvider(top.id) {
            when (val route = top.route) {
                is Route.Apk -> ApkScreen(apkPath = route.path, onBack = pop, onView = { t, b, e ->
                    push(if (e == null) Route.Text(t, b) else AxmlText.route(t, b, route.path, e))
                })
                is Route.Text -> ViewerScreen(route.title, route.body, route.source, onBack = pop, focusLine = route.line, focusText = route.needle, onSave = route.onSave, savedNote = route.savedNote)
                is Route.Hex -> HexScreen(route.target, onBack = pop)
                is Route.Image -> ImageScreen(route.target, onBack = pop)
                is Route.Dex -> DexEditorScreen(route.state, onBack = pop, onView = { t, b, l, n, save -> push(Route.Text(t, b, line = l, needle = n, onSave = save)) })
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
