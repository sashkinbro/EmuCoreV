package com.sbro.emucorev.ui.settings

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorev.core.PermissionAskHistory
import com.sbro.emucorev.core.PermissionStatus
import com.sbro.emucorev.core.permissionStatus

/** Live state of one runtime permission plus the two ways to change it from the Settings UI. */
class PeripheralPermissionState(
    val status: PermissionStatus,
    /** Shows the system prompt (CAN_REQUEST) or the app settings page (BLOCKED). */
    val requestOrOpenSettings: () -> Unit
) {
    val granted: Boolean get() = status == PermissionStatus.GRANTED
}

private fun Context.findActivity(): Activity? {
    var current: Context? = this
    while (current is ContextWrapper) {
        if (current is Activity) return current
        current = current.baseContext
    }
    return null
}

/**
 * Tracks [permission] for the Settings screen: refreshed whenever the screen is resumed (after the
 * user came back from the system app settings) and after every system prompt. [onResult] receives the
 * outcome of a prompt started from [PeripheralPermissionState.requestOrOpenSettings].
 */
@Composable
fun rememberPeripheralPermission(permission: String, onResult: (Boolean) -> Unit = {}): PeripheralPermissionState {
    val context = LocalContext.current
    val history = remember(context) { PermissionAskHistory(context) }
    fun compute(): PermissionStatus {
        val granted = ContextCompat.checkSelfPermission(context, permission) == android.content.pm.PackageManager.PERMISSION_GRANTED
        val rationale = context.findActivity()?.let { ActivityCompat.shouldShowRequestPermissionRationale(it, permission) } ?: false
        return permissionStatus(granted, history.wasAsked(permission), rationale)
    }
    var status by remember { mutableStateOf(compute()) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        history.markAsked(permission)
        status = compute()
        onResult(granted)
    }
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) status = compute()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }
    return PeripheralPermissionState(status) {
        when (status) {
            PermissionStatus.GRANTED -> Unit
            PermissionStatus.CAN_REQUEST -> launcher.launch(permission)
            PermissionStatus.BLOCKED -> context.startActivity(
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", context.packageName, null))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }
    }
}
