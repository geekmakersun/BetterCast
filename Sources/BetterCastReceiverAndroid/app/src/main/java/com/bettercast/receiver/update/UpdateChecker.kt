package com.bettercast.receiver.update

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** 一个可下载的新版本，数据来自本仓库的 GitHub Release。 */
data class UpdateInfo(
    val version: String,
    val downloadUrl: String,
    val sizeBytes: Long
)

/**
 * 从 GitHub Release 里找 Android 端最新的中文构建。
 *
 * 不依赖 tag 命名：直接扫描所有 release 的附件，取文件名里版本号最大的那个
 * `BetterCast-Receiver-Android-zh-Hans-<version>.apk`，因此 Windows 和 Android
 * 各自独立发版、甚至发在同一个 release 里都不影响。
 */
object UpdateChecker {
    private const val TAG = "BetterCastUpdate"
    private const val OWNER = "geekmakersun"
    private const val REPO = "BetterCast"

    private val apkPattern =
        Regex("""BetterCast-Receiver-Android-zh-Hans-(\d+(?:\.\d+)*)\.apk""")

    /** 最近一次检查失败的原因；为空表示没有失败（或还没查过）。 */
    var lastError: String? = null
        private set

    suspend fun check(currentVersion: String): UpdateInfo? = withContext(Dispatchers.IO) {
        lastError = null
        runCatching {
            val body = httpGet("https://api.github.com/repos/$OWNER/$REPO/releases?per_page=20")
            val releases = JSONArray(body)

            var best: UpdateInfo? = null
            for (i in 0 until releases.length()) {
                val release = releases.getJSONObject(i)
                if (release.optBoolean("draft")) continue
                val assets = release.optJSONArray("assets") ?: continue
                for (j in 0 until assets.length()) {
                    val asset = assets.getJSONObject(j)
                    val name = asset.optString("name")
                    val version = apkPattern.find(name)?.groupValues?.get(1) ?: continue
                    if (compareVersions(version, currentVersion) <= 0) continue
                    if (best != null && compareVersions(version, best.version) <= 0) continue
                    best = UpdateInfo(
                        version = version,
                        downloadUrl = asset.optString("browser_download_url"),
                        sizeBytes = asset.optLong("size")
                    )
                }
            }
            best
        }.onFailure {
            lastError = it.message ?: it::class.java.simpleName
            Log.w(TAG, "check failed: ${it.message}")
        }.getOrNull()
    }

    /** 按段比较，1.10.0 > 1.9.0；缺少的段按 0 处理。 */
    fun compareVersions(a: String, b: String): Int {
        val pa = a.split(".").map { it.toIntOrNull() ?: 0 }
        val pb = b.split(".").map { it.toIntOrNull() ?: 0 }
        val size = maxOf(pa.size, pb.size)
        for (i in 0 until size) {
            val va = pa.getOrElse(i) { 0 }
            val vb = pb.getOrElse(i) { 0 }
            if (va != vb) return va - vb
        }
        return 0
    }

    private fun httpGet(url: String): String {
        val conn = URL(url).openConnection() as HttpURLConnection
        try {
            conn.requestMethod = "GET"
            conn.connectTimeout = 8000
            conn.readTimeout = 8000
            conn.setRequestProperty("Accept", "application/vnd.github+json")
            conn.setRequestProperty("User-Agent", "BetterCast-Receiver")
            if (conn.responseCode !in 200..299) {
                throw IOException("HTTP ${conn.responseCode}")
            }
            return conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }
}
