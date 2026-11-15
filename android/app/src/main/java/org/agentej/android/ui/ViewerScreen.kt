package org.agentej.android.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.agentej.android.Native
import org.agentej.android.Util

/** Files above this open read-only: a single BasicTextField over megabytes stalls the UI thread. */
private const val EDIT_LIMIT = 256 * 1024

@Composable
fun ViewerScreen(
    title: String, body: String, source: String?, onBack: () -> Unit,
    focusLine: Int = -1, focusText: String? = null
) {
    val scope = rememberCoroutineScope()
    val clip = LocalClipboardManager.current
    val snack = remember { SnackbarHostState() }
    var text by remember { mutableStateOf(body) }
    val lines = remember(text) { text.split('\n').map { it.removeSuffix("\r") } }
    val lang = remember(title) { detectLang(title, body) }

    var wrap by rememberSaveable { mutableStateOf(lang == Lang.PLAIN) }
    var font by rememberSaveable { mutableFloatStateOf(12f) }
    var searching by remember { mutableStateOf(false) }
    var query by remember { mutableStateOf("") }
    var matches by remember { mutableStateOf<List<Match>>(emptyList()) }
    var active by remember { mutableIntStateOf(-1) }
    var editing by remember { mutableStateOf(false) }
    var draft by remember { mutableStateOf("") }
    var confirmDiscard by remember { mutableStateOf(false) }
    var moreOpen by remember { mutableStateOf(false) }
    var navOpen by remember { mutableStateOf(false) }
    val outline = remember(lines, lang) { if (lang == Lang.SMALI) smaliOutline(lines) else emptyList() }
    val strings = remember(lines, lang) { if (lang == Lang.SMALI) smaliStrings(lines) else emptyList() }
    val list = rememberLazyListState()
    val dirty = editing && draft != text
    val canEdit = source != null && text.length <= EDIT_LIMIT

    /* a search-hit highlight stays until the user's own find replaces it */
    var focusPinned by remember { mutableStateOf(focusLine >= 0) }
    LaunchedEffect(query, lines) {
        if (query.isEmpty() && focusPinned) return@LaunchedEffect
        focusPinned = false
        delay(150)
        matches = withContext(Dispatchers.Default) { findMatches(lines, query) }
        active = if (matches.isEmpty()) -1 else 0
    }
    LaunchedEffect(active) {
        matches.getOrNull(active)?.let { list.scrollToItem((it.line - 4).coerceAtLeast(0)) }
    }

    /* opened on a search hit: bring its line up and mark the hit (or the whole line) */
    LaunchedEffect(Unit) {
        if (focusLine !in lines.indices) return@LaunchedEffect
        val l = lines[focusLine]
        val col = focusText?.let { l.indexOf(it, ignoreCase = true) } ?: -1
        matches = listOf(if (col >= 0) Match(focusLine, col, col + focusText!!.length) else Match(focusLine, l.length - l.trimStart().length, l.length))
        active = 0
        list.scrollToItem((focusLine - 4).coerceAtLeast(0))
    }

    fun step(d: Int) {
        if (matches.isNotEmpty()) active = (active + d).mod(matches.size)
    }

    fun save() = scope.launch {
        val rc = withContext(Dispatchers.IO) { Native.writeText(source!!, draft) }
        if (rc == 0) {
            text = draft
            editing = false
            snack.showSnackbar("Saved ${Util.compactSize(draft.toByteArray().size.toLong())}")
        } else snack.showSnackbar("Save failed — the folder may be read-only")
    }

    BackHandler(enabled = dirty) { confirmDiscard = true }
    BackHandler(enabled = !dirty && editing) { editing = false }
    BackHandler(enabled = !editing && searching) { searching = false; query = "" }

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.surface).safeDrawingPadding()) {
        Column(Modifier.fillMaxSize()) {
            EjTopBar(
                title = title,
                subtitle = if (editing) "Editing" + if (dirty) " · unsaved" else ""
                else "${lines.size} lines · ${lang.label} · ${Util.compactSize(text.length.toLong())}",
                leading = {
                    IconBtn(Icons.AutoMirrored.Filled.ArrowBack, "Back") {
                        if (dirty) confirmDiscard = true else if (editing) editing = false else onBack()
                    }
                }
            ) {
                if (editing) {
                    IconBtn(EjIcons.Save, "Save", enabled = dirty) { save() }
                } else {
                    if (outline.isNotEmpty()) IconBtn(EjIcons.Compass, "Navigation") { navOpen = true }
                    IconBtn(Icons.Default.Search, "Find") { searching = !searching; if (!searching) query = "" }
                    IconBtn(EjIcons.Wrap, if (wrap) "Turn off wrap" else "Wrap lines",
                        tint = if (wrap) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface) { wrap = !wrap }
                    Box {
                        IconBtn(Icons.Default.MoreVert, "More") { moreOpen = true }
                        DropdownMenu(moreOpen, onDismissRequest = { moreOpen = false }) {
                            if (canEdit) DropdownMenuItem(text = { Text("Edit") }, onClick = {
                                moreOpen = false; draft = text; editing = true; searching = false
                            })
                            DropdownMenuItem(text = { Text("Copy all") }, onClick = {
                                moreOpen = false; clip.setText(AnnotatedString(text))
                                scope.launch { snack.showSnackbar("Copied ${lines.size} lines") }
                            })
                            DropdownMenuItem(text = { Text("Larger text") }, enabled = font < 20f, onClick = { font += 1f })
                            DropdownMenuItem(text = { Text("Smaller text") }, enabled = font > 8f, onClick = { font -= 1f })
                            if (source == null) DropdownMenuItem(
                                text = { Text("Read-only (decoded)", color = MaterialTheme.colorScheme.onSurfaceVariant) },
                                enabled = false, onClick = {}
                            )
                        }
                    }
                }
            }
            if (searching && !editing) FindBar(query, { query = it }, matches.size, active, ::step) {
                searching = false; query = ""
            }
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

            if (editing) {
                BasicTextField(
                    value = draft,
                    onValueChange = { draft = it },
                    textStyle = TextStyle(
                        fontFamily = FontFamily.Monospace, fontSize = font.sp, lineHeight = (font * 1.5f).sp,
                        color = MaterialTheme.colorScheme.onBackground
                    ),
                    cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                    modifier = Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).padding(12.dp)
                )
            } else if (text.isBlank()) {
                EmptyState("Empty file", if (canEdit) "Use ⋮ → Edit to add content." else "")
            } else {
                CodeView(
                    lines, lang,
                    modifier = Modifier.weight(1f),
                    wrap = wrap, fontSize = font, matches = matches, active = active, state = list,
                    onLongPressLine = { i ->
                        clip.setText(AnnotatedString(lines[i]))
                        scope.launch { snack.showSnackbar("Copied line ${i + 1}") }
                    }
                )
            }
        }
        SnackbarHost(snack, Modifier.align(Alignment.BottomCenter))
    }

    if (navOpen) NavigationDialog(outline, strings, onDismiss = { navOpen = false }, onPick = { it ->
        navOpen = false
        /* highlight the member name like a find hit and bring its line near the top */
        val len = if (it.kind == 'S') it.name.length + 2 else it.name.length
        matches = listOf(Match(it.line, it.col, it.col + len))
        active = 0
        scope.launch { list.scrollToItem((it.line - 4).coerceAtLeast(0)) }
    })

    if (confirmDiscard) AlertDialog(
        onDismissRequest = { confirmDiscard = false },
        title = { Text("Discard changes?") },
        text = { Text("Your edits to $title haven't been saved.") },
        confirmButton = {
            TextButton(onClick = { confirmDiscard = false; editing = false }) {
                Text("DISCARD", color = MaterialTheme.colorScheme.error)
            }
        },
        dismissButton = { TextButton(onClick = { confirmDiscard = false; save() }) { Text("SAVE") } }
    )
}

@Composable
private fun FindBar(
    query: String,
    onQuery: (String) -> Unit,
    count: Int,
    active: Int,
    step: (Int) -> Unit,
    onClose: () -> Unit
) {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    Row(
        Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface).height(52.dp).padding(start = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        TextField(
            value = query,
            onValueChange = onQuery,
            placeholder = { Text("Find in file", fontSize = 14.sp) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = { step(1) }),
            colors = TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent, unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent
            ),
            modifier = Modifier.weight(1f).focusRequester(focus)
        )
        Text(
            when {
                query.isEmpty() -> ""
                count == 0 -> "no match"
                else -> "${active + 1}/$count${if (count >= 5000) "+" else ""}"
            },
            fontSize = 12.sp,
            color = if (query.isNotEmpty() && count == 0) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
        IconBtn(Icons.Default.KeyboardArrowUp, "Previous match", enabled = count > 0) { step(-1) }
        IconBtn(Icons.Default.KeyboardArrowDown, "Next match", enabled = count > 0) { step(1) }
        IconBtn(Icons.Default.Close, "Close find", onClick = onClose)
    }
}
