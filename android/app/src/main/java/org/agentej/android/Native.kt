package org.agentej.android

import android.content.res.AssetManager

/**
 * Thin Kotlin shell over the C core. Every filesystem, archive, AXML, ARSC,
 * DEX, Smali and signing operation happens in `libejcore.so`; Kotlin only
 * renders and routes taps.
 *
 * Calls that report an outcome return "rc<TAB>detail".
 */
object Native {
    init {
        System.loadLibrary("ejcore")
    }

    external fun init(assets: AssetManager, dataDir: String)
    external fun version(): String
    external fun homePath(): String
    external fun storageOk(): Boolean

    // filesystem
    external fun listDir(path: String): Array<String>?
    external fun statPath(path: String): String
    external fun diskStats(path: String): String
    external fun readText(path: String, max: Int): String?
    external fun writeText(path: String, text: String): Int
    external fun mkdirs(path: String): Int
    external fun deletePath(path: String): Int
    external fun renamePath(from: String, to: String): Int
    external fun copyPath(from: String, to: String): Int

    // archives
    external fun zipEntries(archive: String): Array<String>?
    external fun readEntry(archive: String, name: String): ByteArray?
    external fun extractAll(archive: String, outDir: String): Int
    external fun packDir(dir: String, output: String, sign: Boolean): Int
    external fun zipalign(input: String, output: String, align: Int): Int

    // binary resources
    external fun axmlDecode(archive: String, entry: String): String
    external fun axmlEncode(xml: String, output: String): Int
    external fun arscStrings(archive: String, limit: Int): Array<String>?
    external fun arscPatch(archive: String, index: Int, value: String): String
    external fun arscDumpToml(archive: String, output: String): String
    external fun repairFile(input: String, output: String): String

    // dex / smali
    external fun dexClasses(archive: String, entry: String): Array<String>?
    external fun dexSmali(archive: String, entry: String, index: Int): String
    external fun dexDump(archive: String, entry: String): String
    external fun smaliAssemble(srcDir: String, outDex: String): String
    external fun dexRepair(input: String, output: String): String

    // apk
    external fun apkAnalyze(path: String): String
    external fun apkInfo(path: String): Array<String>?
    external fun signApk(input: String, output: String): String
}
