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
    /** 0 ok, -1 io error, -2 target inside source, -3 target exists. Streams; folders recurse. */
    external fun copyPath(from: String, to: String): Int
    /** rename(2), falling back to copy + delete across filesystems. Same codes as copyPath. */
    external fun movePath(from: String, to: String): Int
    /** Case-insensitive name search under root; lines are "rel/path\tisDir\tsize\tmtime". */
    external fun findFiles(root: String, query: String, limit: Int): Array<String>?

    // archives
    external fun zipEntries(archive: String): Array<String>?
    external fun readEntry(archive: String, name: String): ByteArray?
    external fun extractAll(archive: String, outDir: String): Int
    external fun extractEntry(archive: String, name: String, outPath: String): Int
    external fun packDir(dir: String, output: String, sign: Boolean): Int
    external fun zipalign(input: String, output: String, align: Int): Int

    // binary resources
    external fun axmlDecode(archive: String, entry: String): String
    external fun axmlEncode(xml: String, output: String): String
    /** Translation mode: qualifiers of the <string> type, one per line ([entry] "" = a bare .arsc). */
    external fun arscLocales(archive: String, entry: String, type: String): String
    /** "id\tkey\tdefault\t[=text in qual]" per string, escaped. */
    external fun arscStringsIn(archive: String, entry: String, type: String, qual: String): String
    /** Writes texts for ids in qual: signed APK copy, or the table, at [output]. */
    external fun arscTranslate(archive: String, entry: String, qual: String, ids: IntArray, texts: Array<String>, output: String): String
    /** Encodes edited XML back into [entry] of [archive] (signed copy at [output]), or to [output] when entry is "". */
    external fun axmlSave(archive: String, entry: String, xml: String, output: String): String
    external fun arscStrings(archive: String, limit: Int): Array<String>?
    external fun arscPatch(archive: String, index: Int, value: String): String
    external fun arscDumpToml(archive: String, output: String): String
    external fun repairFile(input: String, output: String): String
    external fun axmlRepair(input: String, output: String): String

    // dex / smali
    external fun dexClasses(archive: String, entry: String): Array<String>?
    external fun dexSmali(archive: String, entry: String, index: Int): String
    external fun dexDump(archive: String, entry: String): String
    external fun smaliAssemble(srcDir: String, outDex: String): String
    external fun dexRepair(input: String, output: String): String
    /** Whole DEX (plain file, or [entry] inside [archive]) -> one .smali per class under [outDir]. */
    /** Dex editor session: parse once, then render classes by index. 0 = failed. */
    external fun dexOpen(archive: String, entry: String): Long
    external fun dexClassSmali(handle: Long, index: Int): String
    external fun dexClose(handle: Long)
    /** Dex editor save: rebuild the dexes holding edited classes; returns "rc\tdetail". */
    /** String pool of one dex, in pool order; null when it does not parse. */
    external fun dexStrings(archive: String, entry: String): Array<String>?
    /** Dex editor hammer. Per change: dex entry, class index (>=0 replace, or delete when smali is "";
     *  -1 new class; -2 just rebuild that dex) and smali. renFrom/renTo: descriptor renames, in order. */
    external fun dexSave(
        archive: String, entries: Array<String>, classIdx: IntArray, smali: Array<String>,
        renFrom: Array<String>, renTo: Array<String>, out: String
    ): String
    /** smali_rename over one class text ("La/B;" class or "La/b/" package descriptors). */
    external fun smaliRename(text: String, from: Array<String>, to: Array<String>): String
    /** Search one dex: rows "classIdx\tline\ttext"; null on an invalid query. kind/flags as dex_find.h. */
    external fun dexFind(archive: String, entry: String, kind: Int, query: String, flags: Int, scope: String, limit: Int): Array<String>?
    external fun dexToSmaliDir(archive: String, entry: String?, outDir: String): String
    external fun readBytes(path: String, offset: Long, max: Int): ByteArray?

    // apk
    external fun apkAnalyze(path: String): String
    external fun apkInfo(path: String): Array<String>?
    /** apk_sig_schemes bits: 1 V1, 2 V2, 4 V3, 8 V3.1, 16 source stamp; -1 unreadable. */
    external fun apkSigSchemes(path: String): Int
    external fun signApk(input: String, output: String): String
    external fun apkDecode(apk: String, outDir: String): String
    external fun apkBuild(srcDir: String, outApk: String): String
    external fun apkMerge(input: String, outApk: String): String
    external fun apkRepair(input: String, outApk: String): String
    external fun deobfRename(decodedDir: String): String
    external fun sigKill(input: String, outApk: String): String
}
