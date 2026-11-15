package org.agentej.android

/** What an action works on: a plain file or folder, or an entry inside an archive. */
data class Target(val path: String, val isDir: Boolean, val archive: String? = null, val entry: String? = null) {
    val name get() = path.trimEnd('/').substringAfterLast('/')
    val inArchive get() = archive != null
    val kind get() = FileKinds.of(name, isDir)
}

/** A finished job: its outcome and the file or folder it produced. */
data class JobResult(val rc: Rc, val output: String?)

/**
 * The work behind each [Act]. Everything here blocks on the C core, so call it
 * on Dispatchers.IO. Results land next to the input (MT's convention) under a
 * name that never overwrites an existing file.
 */
object Tools {
    val jobs = setOf(
        Act.DECODE, Act.SIGN, Act.REPAIR_APK, Act.SIGKILL, Act.MERGE, Act.DEX2SMALI, Act.DEX_REPAIR,
        Act.AXML_ENCODE, Act.AXML_REPAIR, Act.ARSC_TOML, Act.BUILD, Act.ASSEMBLE, Act.DEOBF, Act.COMPRESS
    )

    /** Short progress line shown while a job runs. */
    fun verb(act: Act) = when (act) {
        Act.DECODE -> "Decoding"
        Act.SIGN -> "Signing"
        Act.REPAIR_APK -> "Repairing"
        Act.SIGKILL -> "Patching signature checks"
        Act.MERGE -> "Merging splits"
        Act.DEX2SMALI -> "Disassembling"
        Act.BUILD -> "Building"
        Act.ASSEMBLE -> "Assembling"
        Act.DEOBF -> "Renaming"
        Act.COMPRESS -> "Compressing"
        else -> "Working"
    }

    fun run(act: Act, t: Target): JobResult {
        val p = t.path.trimEnd('/')
        val b = Outputs.base(p)
        fun rc(text: String) = Util.parseRc(text)
        fun int(code: Int, ok: String, bad: String) = Rc(code, if (code == 0) ok else bad)
        return when (act) {
            Act.DECODE -> Outputs.inWorkspace(Workspace.projects, "${b}_src").let { JobResult(rc(Native.apkDecode(p, it)), it) }
            Act.SIGN -> Outputs.beside(p, "${b}_signed.apk").let { JobResult(rc(Native.signApk(p, it)), it) }
            Act.REPAIR_APK -> Outputs.beside(p, "${b}_repaired.apk").let { JobResult(rc(Native.apkRepair(p, it)), it) }
            Act.SIGKILL -> Outputs.beside(p, "${b}_kill.apk").let { JobResult(rc(Native.sigKill(p, it)), it) }
            Act.MERGE -> Outputs.beside(p, "${b}_merged.apk").let { JobResult(rc(Native.apkMerge(p, it)), it) }
            Act.DEX2SMALI -> {
                val out = if (t.archive != null) Outputs.beside(t.archive, "${Outputs.base(t.archive)}_${b}_smali")
                else Outputs.beside(p, "${b}_smali")
                JobResult(rc(Native.dexToSmaliDir(t.archive ?: p, t.entry, out)), out)
            }
            Act.DEX_REPAIR -> Outputs.beside(p, "${b}_repaired.dex").let { JobResult(rc(Native.dexRepair(p, it)), it) }
            Act.AXML_REPAIR -> Outputs.beside(p, "${b}_repaired.xml").let { JobResult(rc(Native.axmlRepair(p, it)), it) }
            Act.AXML_ENCODE -> {
                val xml = Native.readText(p, 8 * 1024 * 1024)
                    ?: return JobResult(Rc(-1, "not a text XML file"), null)
                val out = Outputs.beside(p, "${b}_binary.xml")
                JobResult(rc(Native.axmlEncode(AxmlText.frameworkIds(xml), out)), out)
            }
            Act.ARSC_TOML -> Outputs.beside(p, "$b.toml").let { JobResult(rc(Native.arscDumpToml(p, it)), it) }
            Act.BUILD -> Outputs.beside(p, "${b}_build.apk").let { JobResult(rc(Native.apkBuild(p, it)), it) }
            Act.ASSEMBLE -> Outputs.beside(p, "$b.dex").let { JobResult(rc(Native.smaliAssemble(p, it)), it) }
            Act.DEOBF -> JobResult(rc(Native.deobfRename(p)), p)
            Act.COMPRESS -> Outputs.beside(p, "$b.zip").let {
                JobResult(int(Native.packDir(p, it, false), "compressed", "compress failed"), it)
            }
            else -> JobResult(Rc(-1, "${act.label} is not a background job"), null)
        }
    }

    /** Text produced by a viewing action: (title, body), or null when it has none. */
    fun view(act: Act, t: Target, pm: android.content.pm.PackageManager? = null): Pair<String, String>? {
        val arc = t.archive ?: t.path
        val entry = t.entry ?: ""
        return when (act) {
            Act.TEXT -> {
                val text = if (t.archive != null) Native.readEntry(t.archive, entry)?.let { Util.decodeText(it) }
                else Native.readText(t.path, 4 * 1024 * 1024)
                text?.let { t.name to it }
            }
            Act.AXML_VIEW -> AxmlText.decode(pm, arc, entry).takeIf { it.isNotBlank() }?.let { t.name to it }
            Act.DEX_INFO -> Native.dexDump(arc, entry).takeIf { it.isNotBlank() }?.let { "${t.name} · property" to it }
            Act.ARSC_STRINGS -> Native.arscStrings(arc, 20000)?.let { list ->
                "${t.name} · strings" to list.mapIndexed { i, s -> "$i\t$s" }.joinToString("\n")
            }
            else -> null
        }
    }
}

/** Copies a file or folder out of an archive into [destDir] (never overwriting). */
fun extractTo(t: Target, destDir: String): Rc {
    val arc = t.archive ?: return Rc(-1, "not inside an archive")
    val inner = (t.entry ?: "").trimEnd('/')
    if (!t.isDir) {
        val out = Outputs.free(Util.join(destDir, t.name))
        return if (Native.extractEntry(arc, inner, out) == 0) Rc(0, "extracted ${t.name}") else Rc(-1, "extract failed")
    }
    val prefix = "$inner/"
    val names = Native.zipEntries(arc)?.map { it.substringBefore('\t') }
        ?.filter { it.startsWith(prefix) && !it.endsWith("/") } ?: return Rc(-1, "cannot read archive")
    val root = Outputs.free(Util.join(destDir, t.name))
    var bad = 0
    names.forEach { if (Native.extractEntry(arc, it, Util.join(root, it.removePrefix(prefix))) != 0) bad++ }
    return if (bad == 0) Rc(0, "extracted ${names.size} file${if (names.size == 1) "" else "s"}")
    else Rc(-1, "extracted ${names.size - bad}, $bad failed")
}
