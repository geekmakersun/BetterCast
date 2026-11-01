package com.bettercast.receiver.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.bettercast.receiver.R
import com.bettercast.receiver.update.ApkUpdater
import com.bettercast.receiver.update.UpdateInfo
import kotlinx.coroutines.launch
import java.io.File
import kotlin.math.roundToInt

/**
 * 发现新版本时的弹窗：确认下载 → 显示进度 → 调起系统安装器。
 *
 * 下载中不允许取消，避免留下半个 APK；出错则留在弹窗里显示原因，可以再次重试。
 */
@Composable
fun UpdateDialog(info: UpdateInfo, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var progress by remember { mutableStateOf<Float?>(null) }
    var apkFile by remember { mutableStateOf<File?>(null) }
    var error by remember { mutableStateOf<String?>(null) }
    var needsPermission by remember { mutableStateOf(false) }

    fun installNow(file: File) {
        if (ApkUpdater.canInstall(context)) {
            ApkUpdater.install(context, file)
        } else {
            // 系统设置回来后再次点「立即安装」即可
            needsPermission = true
            ApkUpdater.openInstallPermissionSettings(context)
        }
    }

    AlertDialog(
        onDismissRequest = { if (progress == null) onDismiss() },
        title = { Text(stringResource(R.string.update_title, info.version)) },
        text = {
            Column {
                Text(stringResource(R.string.update_body))
                progress?.let { p ->
                    Spacer(Modifier.height(12.dp))
                    LinearProgressIndicator(
                        progress = { p },
                        modifier = Modifier.fillMaxWidth()
                    )
                    Text(stringResource(R.string.update_downloading, (p * 100).roundToInt()))
                }
                needsPermission.takeIf { it }?.let {
                    Spacer(Modifier.height(8.dp))
                    Text(stringResource(R.string.update_permission))
                }
                error?.let {
                    Spacer(Modifier.height(8.dp))
                    Text(it)
                }
            }
        },
        confirmButton = {
            when {
                apkFile != null -> TextButton(onClick = { apkFile?.let { installNow(it) } }) {
                    Text(stringResource(R.string.update_install))
                }
                progress == null -> TextButton(onClick = {
                    error = null
                    progress = 0f
                    scope.launch {
                        runCatching {
                            ApkUpdater.download(context, info) { progress = it }
                        }.onSuccess {
                            apkFile = it
                            progress = 1f
                            installNow(it)
                        }.onFailure {
                            val reason = it.message ?: it.javaClass.simpleName
                            error = context.getString(R.string.update_failed, reason)
                            progress = null
                        }
                    }
                }) {
                    Text(stringResource(R.string.update_action))
                }
                else -> TextButton(onClick = {}, enabled = false) {
                    Text(stringResource(R.string.update_downloading, ((progress ?: 0f) * 100).roundToInt()))
                }
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, enabled = progress == null) {
                Text(stringResource(R.string.update_later))
            }
        }
    )
}
