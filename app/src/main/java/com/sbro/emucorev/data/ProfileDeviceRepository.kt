package com.sbro.emucorev.data

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.provider.Settings
import com.google.android.gms.tasks.Task
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.firestore.DocumentSnapshot
import com.google.firebase.firestore.FieldValue
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.SetOptions
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.GpuHardwareProfiles
import com.sbro.emucorev.core.MobileSocNameMapper
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

data class PlayerDevice(
    val deviceId: String,
    val displayName: String,
    val manufacturer: String,
    val model: String,
    val soc: String,
    val gpuFamily: String,
    val ramMb: Long,
    val androidVersion: String,
    val appVersion: String,
    val coreVersion: String,
    val isCurrent: Boolean,
    val isPublic: Boolean,
    val lastSeenAtMs: Long?
)

data class PublicPlayerDevice(
    val displayName: String,
    val soc: String,
    val gpuFamily: String,
    val ramMb: Long,
    val androidVersion: String,
    val appVersion: String,
    val coreVersion: String
)

object ProfileDeviceInfoProvider {
    private const val PREFS = "profile_device"
    private const val DEVICE_ID = "device_id"
    const val CORE_VERSION = "Vita3K"

    fun current(context: Context, isPublic: Boolean = false): PlayerDevice {
        val appContext = context.applicationContext
        val preferences = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val androidId = Settings.Secure.getString(appContext.contentResolver, Settings.Secure.ANDROID_ID)
            .orEmpty()
            .trim()
        val deviceId = if (androidId.isNotEmpty()) {
            stableDeviceId(androidId, appContext.packageName)
        } else {
            preferences.getString(DEVICE_ID, null)?.takeIf { it.isNotBlank() } ?: UUID.randomUUID().toString()
        }
        if (preferences.getString(DEVICE_ID, null) != deviceId) {
            preferences.edit().putString(DEVICE_ID, deviceId).apply()
        }
        val memoryInfo = ActivityManager.MemoryInfo()
        (appContext.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager)
            ?.getMemoryInfo(memoryInfo)
        val ramMb = (memoryInfo.totalMem / (1024L * 1024L)).coerceAtLeast(0L)
        val manufacturer = Build.MANUFACTURER.orEmpty().trim().cleanDeviceLabel()
        val model = Build.MODEL.orEmpty().trim().cleanDeviceLabel()
        val displayName = listOf(manufacturer, model)
            .filter { it.isNotBlank() }
            .distinctBy { it.lowercase(Locale.ROOT) }
            .joinToString(" ")
            .ifBlank { "Android device" }
            .take(120)
        val gpuFamily = when (GpuHardwareProfiles.detectHardwareProfile()) {
            GpuHardwareProfiles.ADRENO -> "Adreno"
            GpuHardwareProfiles.POWERVR -> "PowerVR"
            else -> "Mali"
        }
        return PlayerDevice(
            deviceId = deviceId,
            displayName = displayName,
            manufacturer = manufacturer.take(80),
            model = model.take(120),
            soc = MobileSocNameMapper.currentDeviceName().trim().ifBlank {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL.orEmpty() else Build.HARDWARE.orEmpty()
            }.take(160),
            gpuFamily = gpuFamily,
            ramMb = ramMb,
            androidVersion = "Android ${Build.VERSION.RELEASE.orEmpty()} (API ${Build.VERSION.SDK_INT})".take(40),
            appVersion = BuildConfig.VERSION_NAME.take(40),
            coreVersion = CORE_VERSION,
            isCurrent = true,
            isPublic = isPublic,
            lastSeenAtMs = System.currentTimeMillis()
        )
    }

    private fun String.cleanDeviceLabel(): String =
        replace(Regex("[\\p{Cntrl}]"), "").replace(Regex("\\s+"), " ").trim()

    internal fun stableDeviceId(androidId: String, packageName: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
            .digest("$packageName:$androidId".toByteArray(Charsets.UTF_8))
            .joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
        return "android_${digest.take(40)}"
    }
}

class ProfileDeviceRepository(context: Context) {
    private val appContext = context.applicationContext
    private val auth = FirebaseAuth.getInstance()
    private val firestore = FirebaseFirestore.getInstance()

    fun currentDevice(): PlayerDevice = ProfileDeviceInfoProvider.current(appContext)

    fun observeDevices(): Flow<List<PlayerDevice>> = callbackFlow {
        val uid = auth.currentUser?.uid
        if (uid == null) {
            trySend(emptyList())
            close()
            return@callbackFlow
        }
        val registration = firestore.collection(USERS).document(uid).collection(DEVICES)
            .addSnapshotListener { snapshot, error ->
                if (error != null) {
                    close(error)
                    return@addSnapshotListener
                }
                trySend(snapshot?.documents.orEmpty().mapNotNull { it.toPlayerDevice() }
                    .deduplicateDevices()
                    .sortedWith(compareByDescending<PlayerDevice> { it.isCurrent }.thenByDescending { it.lastSeenAtMs ?: 0L }))
            }
        awaitClose { registration.remove() }
    }

    suspend fun registerCurrentDevice(): PlayerDevice? {
        val uid = auth.currentUser?.uid ?: return null
        val ref = firestore.collection(USERS).document(uid).collection(DEVICES)
        val local = currentDevice()
        val existing = ref.document(local.deviceId).get().await()
        val allDevices = ref.get().await().documents
        // Clean up any hardware duplicates (reinstalls) regardless of stable prefix
        val hardwareDuplicates = allDevices.filter { document ->
            document.id != local.deviceId &&
                document.toPlayerDevice()?.sameHardwareProfile(local) == true
        }
        val legacyDuplicates = hardwareDuplicates.filter { !it.id.startsWith(STABLE_ID_PREFIX) }
        val stableDuplicates = hardwareDuplicates.filter { it.id.startsWith(STABLE_ID_PREFIX) }
        // Default to visible: if no device is public yet, make current public (first install and reinstalls)
        // This ensures every user has at least one visible device by default, but still allows manual hide afterwards
        // (hide will be respected until next reinstall/new device, when it will become public again)
        val anyPublicExists = allDevices.any { it.getBoolean(FIELD_PUBLIC) == true }
        val isPublic = when {
            !anyPublicExists -> true
            existing.exists() -> existing.getBoolean(FIELD_PUBLIC) == true
            hardwareDuplicates.any { it.getBoolean(FIELD_PUBLIC) == true } -> true
            else -> false
        }
        val current = local.copy(isPublic = isPublic)
        val data = current.toPrivateMap(uid, includeCreatedAt = !existing.exists())
        val previousCurrent = allDevices.filter { it.getBoolean(FIELD_CURRENT) == true }
        val duplicateIds = hardwareDuplicates.mapTo(mutableSetOf()) { it.id }
        firestore.runBatch { batch ->
            previousCurrent.filter { it.id != current.deviceId && it.id !in duplicateIds }.forEach { document ->
                batch.set(document.reference, mapOf(FIELD_CURRENT to false), SetOptions.merge())
            }
            // Remove reinstall duplicates to keep only unique hardware
            hardwareDuplicates.forEach { document -> batch.delete(document.reference) }
            batch.set(ref.document(current.deviceId), data, SetOptions.merge())
            if (isPublic) {
                batch.set(
                    firestore.collection(PUBLIC_PROFILES).document(uid),
                    mapOf(FIELD_PRIMARY_DEVICE to current.toPublicMap(), FIELD_DEVICE_VISIBLE to true),
                    SetOptions.merge()
                )
            }
        }.await()
        return current
    }

    suspend fun deleteDevice(deviceId: String) {
        val uid = auth.currentUser?.uid ?: return
        // Prevent deleting the current device via manual call - UI should block this,
        // but guard here to avoid accidental removal of active session device
        val local = currentDevice()
        if (deviceId == local.deviceId) return
        firestore.collection(USERS).document(uid).collection(DEVICES).document(deviceId).delete().await()
        // If deleted device was public, clear public profile device visibility
        // (public device will be re-set on next register if needed)
    }

    suspend fun setPublic(deviceId: String, isPublic: Boolean) {
        val uid = auth.currentUser?.uid ?: return
        val deviceRef = firestore.collection(USERS).document(uid).collection(DEVICES).document(deviceId)
        val snapshot = deviceRef.get().await()
        val device = snapshot.toPlayerDevice() ?: return
        val otherPublicDevices = if (isPublic) {
            firestore.collection(USERS).document(uid).collection(DEVICES)
                .whereEqualTo(FIELD_PUBLIC, true).get().await().documents.filter { it.id != deviceId }
        } else {
            emptyList()
        }
        firestore.runBatch { batch ->
            otherPublicDevices.forEach { document ->
                batch.set(document.reference, mapOf(FIELD_PUBLIC to false), SetOptions.merge())
            }
            batch.set(deviceRef, mapOf(FIELD_PUBLIC to isPublic, FIELD_LAST_SEEN to FieldValue.serverTimestamp()), SetOptions.merge())
            val publicPatch = if (isPublic) {
                mapOf(FIELD_PRIMARY_DEVICE to device.copy(isPublic = true).toPublicMap(), FIELD_DEVICE_VISIBLE to true)
            } else {
                mapOf(FIELD_PRIMARY_DEVICE to FieldValue.delete(), FIELD_DEVICE_VISIBLE to false)
            }
            batch.set(firestore.collection(PUBLIC_PROFILES).document(uid), publicPatch, SetOptions.merge())
        }.await()
    }

    private fun PlayerDevice.toPrivateMap(uid: String, includeCreatedAt: Boolean): Map<String, Any> = buildMap {
        putAll(mapOf(
        "uid" to uid,
        "deviceId" to deviceId,
        "displayName" to displayName,
        "manufacturer" to manufacturer,
        "model" to model,
        "soc" to soc,
        "gpuFamily" to gpuFamily,
        "ramMb" to ramMb,
        "androidVersion" to androidVersion,
        "appVersion" to appVersion,
        "coreVersion" to coreVersion,
        FIELD_CURRENT to isCurrent,
        FIELD_PUBLIC to isPublic,
        FIELD_LAST_SEEN to FieldValue.serverTimestamp()
        ))
        if (includeCreatedAt) put("createdAt", FieldValue.serverTimestamp())
    }

    private fun PlayerDevice.toPublicMap(): Map<String, Any> = mapOf(
        "displayName" to displayName,
        "soc" to soc,
        "gpuFamily" to gpuFamily,
        "ramMb" to ramMb,
        "androidVersion" to androidVersion,
        "appVersion" to appVersion,
        "coreVersion" to coreVersion
    )

    private fun DocumentSnapshot.toPlayerDevice(): PlayerDevice? {
        if (!exists()) return null
        return PlayerDevice(
            deviceId = getString("deviceId") ?: id,
            displayName = getString("displayName").orEmpty(),
            manufacturer = getString("manufacturer").orEmpty(),
            model = getString("model").orEmpty(),
            soc = getString("soc").orEmpty(),
            gpuFamily = getString("gpuFamily").orEmpty(),
            ramMb = getLong("ramMb") ?: 0L,
            androidVersion = getString("androidVersion").orEmpty(),
            appVersion = getString("appVersion").orEmpty(),
            coreVersion = getString("coreVersion").orEmpty(),
            isCurrent = getBoolean(FIELD_CURRENT) == true,
            isPublic = getBoolean(FIELD_PUBLIC) == true,
            lastSeenAtMs = getTimestamp(FIELD_LAST_SEEN)?.toDate()?.time
        )
    }

    private fun List<PlayerDevice>.deduplicateDevices(): List<PlayerDevice> {
        // Hide legacy random IDs when a stable android_ device with same hardware exists
        val stableProfiles = filter { it.deviceId.startsWith(STABLE_ID_PREFIX) }
        val afterMigration = filter { device ->
            device.deviceId.startsWith(STABLE_ID_PREFIX) || stableProfiles.none { it.sameHardwareProfile(device) }
        }
        // Deduplicate reinstalls: same hardware (manufacturer+model+soc+gpu+ram) -> keep most recent
        val sorted = afterMigration.sortedWith(compareByDescending<PlayerDevice> { it.isCurrent }.thenByDescending { it.lastSeenAtMs ?: 0L })
        val seenHardware = mutableSetOf<String>()
        return sorted.filter { device ->
            val key = "${device.manufacturer.lowercase()}|${device.model.lowercase()}|${device.soc.lowercase()}|${device.gpuFamily.lowercase()}|${device.ramMb}"
            if (key in seenHardware) false else { seenHardware.add(key); true }
        }.distinctBy(PlayerDevice::deviceId)
    }

    private fun PlayerDevice.sameHardwareProfile(other: PlayerDevice): Boolean =
        manufacturer.equals(other.manufacturer, ignoreCase = true) &&
            model.equals(other.model, ignoreCase = true) &&
            soc.equals(other.soc, ignoreCase = true) &&
            gpuFamily.equals(other.gpuFamily, ignoreCase = true) &&
            ramMb == other.ramMb

    private suspend fun <T> Task<T>.await(): T = suspendCancellableCoroutine { continuation ->
        addOnSuccessListener { continuation.resume(it) }
        addOnFailureListener { continuation.resumeWithException(it) }
        addOnCanceledListener { continuation.cancel() }
    }

    private companion object {
        const val USERS = "users"
        const val DEVICES = "devices"
        const val PUBLIC_PROFILES = "publicProfiles"
        const val FIELD_CURRENT = "isCurrent"
        const val FIELD_PUBLIC = "isPublic"
        const val FIELD_LAST_SEEN = "lastSeenAt"
        const val FIELD_PRIMARY_DEVICE = "primaryDevice"
        const val FIELD_DEVICE_VISIBLE = "deviceVisibility"
        const val STABLE_ID_PREFIX = "android_"
    }
}
