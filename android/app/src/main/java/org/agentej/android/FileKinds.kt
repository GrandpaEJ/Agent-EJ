package org.agentej.android


enum class FileKind { DIR, TEXT, XML, IMAGE, APK, SPLITS, ARCHIVE, DEX, ARSC, AUDIO, VIDEO, PDF, UNKNOWN }

/** Everything the app can do to one file or folder; [label] is what the menus show. */
enum class Act(val label: String) {
    OPEN("Open"),
    TEXT("Text editor"),
    HEX("Hex viewer"),
    IMAGE("Image viewer"),
    ARCHIVE("Archive viewer"),
    APK_TOOLS("APK tools"),
    INSTALL("Install"),
    DECODE("Decode to project"),
    SIGN("Sign"),
    REPAIR_APK("Repair (anti-tamper)"),
    SIGKILL("Kill signature check"),
    MERGE("Merge splits → APK"),
    DEX_BROWSE("Dex editor"),
    DEX2SMALI("Dex2Smali"),
    DEX_REPAIR("Repair dex"),
    DEX_INFO("Dex property"),
    AXML_VIEW("Decode binary XML"),
    AXML_ENCODE("Encode to binary XML"),
    AXML_REPAIR("Repair binary XML"),
    ARSC_STRINGS("Resource strings"),
    ARSC_TRANSLATE("Translation mode"),
    ARSC_TOML("Dump resources (TOML)"),
    BUILD("Build APK"),
    ASSEMBLE("Smali → DEX"),
    DEOBF("Deobfuscate names"),
    COMPRESS("Compress to ZIP"),
    EXTERNAL("Open in another app"),
    OPEN_WITH("Open with…");
}

object FileKinds {
    private val text = setOf(
        "txt", "log", "md", "json", "smali", "java", "kt", "kts", "c", "h", "cpp", "hpp", "cc", "py", "sh",
        "js", "ts", "css", "html", "htm", "yml", "yaml", "toml", "ini", "cfg", "conf", "properties",
        "gradle", "pro", "csv", "mf", "sf", "rc", "bat", "lua", "go", "rs", "sql", "gitignore", "env"
    )
    private val images = setOf("png", "jpg", "jpeg", "webp", "gif", "bmp", "heic", "heif")
    private val audio = setOf("mp3", "m4a", "flac", "ogg", "opus", "aac", "wav", "amr")
    private val video = setOf("mp4", "mkv", "webm", "3gp", "mov", "avi", "m4v")

    fun of(name: String, isDir: Boolean): FileKind {
        if (isDir) return FileKind.DIR
        val ext = Util.ext(name)
        return when {
            ext in text -> FileKind.TEXT
            ext == "xml" -> FileKind.XML
            ext in images -> FileKind.IMAGE
            ext == "apk" -> FileKind.APK
            ext in setOf("apks", "xapk", "apkm") -> FileKind.SPLITS
            ext in setOf("zip", "jar", "aar") -> FileKind.ARCHIVE
            ext == "dex" -> FileKind.DEX
            ext == "arsc" -> FileKind.ARSC
            ext in audio -> FileKind.AUDIO
            ext in video -> FileKind.VIDEO
            ext == "pdf" -> FileKind.PDF
            else -> FileKind.UNKNOWN
        }
    }

    /**
     * The type's own function list (MT's "Dex functions", "Apk functions"…), shown when
     * a file of that type is tapped and under Tools in the long-press menu.
     * Inside an archive only read-only views are offered.
     */
    fun actions(kind: FileKind, inArchive: Boolean, dirHint: DirHint = DirHint.PLAIN): List<Act> = when (kind) {
        FileKind.APK -> if (inArchive) listOf(Act.ARCHIVE, Act.HEX)
            else listOf(Act.ARCHIVE, Act.DEX_BROWSE, Act.INSTALL, Act.APK_TOOLS, Act.DECODE, Act.SIGN, Act.REPAIR_APK, Act.SIGKILL)
        FileKind.SPLITS -> if (inArchive) listOf(Act.ARCHIVE) else listOf(Act.MERGE, Act.ARCHIVE)
        FileKind.DEX -> if (inArchive) listOf(Act.DEX_BROWSE, Act.DEX2SMALI, Act.DEX_INFO, Act.HEX)
            else listOf(Act.DEX_BROWSE, Act.DEX2SMALI, Act.DEX_REPAIR, Act.DEX_INFO, Act.HEX)
        FileKind.ARSC -> if (inArchive) listOf(Act.ARSC_TRANSLATE, Act.ARSC_STRINGS, Act.HEX)
            else listOf(Act.ARSC_TRANSLATE, Act.ARSC_STRINGS, Act.ARSC_TOML, Act.HEX)
        FileKind.XML -> if (inArchive) listOf(Act.AXML_VIEW, Act.TEXT, Act.HEX)
            else listOf(Act.TEXT, Act.AXML_VIEW, Act.AXML_ENCODE, Act.AXML_REPAIR)
        FileKind.DIR -> when (dirHint) {
            DirHint.PROJECT -> listOf(Act.BUILD, Act.DEOBF, Act.COMPRESS)
            DirHint.SMALI -> listOf(Act.ASSEMBLE, Act.DEOBF, Act.COMPRESS)
            DirHint.PLAIN -> if (inArchive) emptyList() else listOf(Act.COMPRESS)
        }
        FileKind.ARCHIVE -> listOf(Act.ARCHIVE, Act.HEX)
        FileKind.IMAGE -> listOf(Act.IMAGE, Act.EXTERNAL, Act.HEX)
        FileKind.AUDIO, FileKind.VIDEO, FileKind.PDF -> listOf(Act.EXTERNAL, Act.HEX)
        FileKind.TEXT -> listOf(Act.TEXT, Act.HEX, Act.EXTERNAL)
        FileKind.UNKNOWN -> listOf(Act.TEXT, Act.HEX)
    }

    /** Built-in viewers offered by "Open with…" regardless of the extension. */
    fun builtIns(inArchive: Boolean): List<Act> = listOf(
        Act.TEXT, Act.HEX, Act.IMAGE, Act.ARCHIVE, Act.DEX_BROWSE, Act.AXML_VIEW, Act.ARSC_STRINGS
    ) + if (inArchive) emptyList() else listOf(Act.APK_TOOLS, Act.MERGE)
}

enum class DirHint { PLAIN, SMALI, PROJECT }

/** Where a tool writes its result: next to the input, never over an existing file. */
object Outputs {
    fun base(path: String) = path.trimEnd('/').substringAfterLast('/').substringBeforeLast('.')
        .ifEmpty { path.trimEnd('/').substringAfterLast('/') }

    /** Next to the source; in the workspace's output folder when that folder is read-only (e.g. /data/app). */
    fun beside(path: String, name: String): String {
        val dir = Util.parent(path)
        return free(Util.join(if (java.io.File(dir).canWrite()) dir else Workspace.output, name))
    }

    /** In one of the workspace folders, created on demand. */
    fun inWorkspace(dir: String, name: String) = java.io.File(dir).mkdirs().let { free(Util.join(dir, name)) }

    /** "x.apk" -> "x (2).apk" until the name is unused. */
    fun free(path: String): String {
        if (!FileOps.exists(path)) return path
        val dir = Util.parent(path)
        val file = path.substringAfterLast('/')
        val dot = file.lastIndexOf('.').takeIf { it > 0 } ?: file.length
        for (i in 2..999) {
            val p = Util.join(dir, file.substring(0, dot) + " ($i)" + file.substring(dot))
            if (!FileOps.exists(p)) return p
        }
        return path
    }

}

/** EJ's own folder on shared storage, like MT's /sdcard/MT2: decoded projects
 * live in projects/, and output whose source folder is read-only goes to output/. */
object Workspace {
    val root: String by lazy { Util.join(Native.homePath(), "EJ") }
    val projects: String get() = Util.join(root, "projects")
    val output: String get() = Util.join(root, "output")

    fun ensure() {
        listOf(projects, output).forEach { java.io.File(it).mkdirs() }
    }
}

/** What a folder holds: a decoded APK project, a smali tree, or anything else. */
fun dirHint(path: String): DirHint {
    val names = Native.listDir(path)?.map { it.substringBefore('\t') } ?: return DirHint.PLAIN
    if (names.any { it == "apktool.yml" } ||
        (names.contains("AndroidManifest.xml") && names.any { it.startsWith("smali") })) return DirHint.PROJECT
    if (path.trimEnd('/').substringAfterLast('/').startsWith("smali") || names.any { it.endsWith(".smali") })
        return DirHint.SMALI
    return DirHint.PLAIN
}
