package com.sbro.emucorev.nativeui

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.Uri
import android.net.wifi.WifiInfo
import android.net.wifi.WifiManager
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.text.format.DateFormat
import java.util.TimeZone

/** System events for the standalone UI. Attach on resume and detach on pause. */
@Suppress("DEPRECATION")
class DeviceStatusSource(context: Context) {
    data class Snapshot(
        val batteryPercent: Int,
        val charging: Boolean,
        val powerSource: Int,
        val wifiEnabled: Int,
        val wifiConnected: Boolean,
        val wifiRssi: Int,
        val bluetoothEnabled: Int,
        val epochMinute: Long,
        val timeZoneId: String,
        val use24HourClock: Boolean,
    )

    private val context = context.applicationContext
    private val handler = Handler(Looper.getMainLooper())
    private val wifi = this.context.getSystemService(WifiManager::class.java)
    private val connectivity = this.context.getSystemService(ConnectivityManager::class.java)
    private val bluetooth = this.context.getSystemService(BluetoothManager::class.java)?.adapter
    private val wifiNetworks = mutableMapOf<Network, NetworkCapabilities>()
    private var listener: ((Snapshot) -> Unit)? = null
    private var previous: Snapshot? = null
    private var running = false
    private var generation = 0
    private var receiver: BroadcastReceiver? = null
    private var networks: ConnectivityManager.NetworkCallback? = null
    private var observer: ContentObserver? = null
    private var batteryPercent = UNKNOWN
    private var charging = false
    private var powerSource = 0
    private var wifiEnabled = UNKNOWN
    private var wifiRssi = Int.MIN_VALUE
    private var bluetoothEnabled = UNKNOWN
    private var timeZoneId = TimeZone.getDefault().id

    private fun requireMainThread() {
        check(Looper.myLooper() === handler.looper) { "Device status lifecycle belongs to the UI thread" }
    }

    private fun active(token: Int) = running && generation == token

    @SuppressLint("MissingPermission") // Denied data remains unknown; this source never asks for access.
    private fun refreshWifi() {
        if (wifi == null) {
            wifiEnabled = UNAVAILABLE
            wifiRssi = Int.MIN_VALUE
            return
        }
        try {
            wifiEnabled = if (wifi.isWifiEnabled) ON else OFF
            wifiRssi = validRssi(wifi.connectionInfo?.rssi ?: Int.MIN_VALUE)
        } catch (_: SecurityException) {
            wifiEnabled = UNKNOWN
            wifiRssi = Int.MIN_VALUE
        }
    }

    @SuppressLint("MissingPermission")
    private fun refreshBluetooth() {
        bluetoothEnabled = if (bluetooth == null) UNAVAILABLE else try {
            if (bluetooth.isEnabled) ON else OFF
        } catch (_: SecurityException) {
            UNKNOWN
        }
    }

    private fun battery(intent: Intent) {
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
        batteryPercent = if (intent.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true) && level >= 0 && scale > 0) {
            kotlin.math.round(level * 100f / scale).toInt().coerceIn(0, 100)
        } else UNKNOWN
        powerSource = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0)
        val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_UNKNOWN)
        charging = status == BatteryManager.BATTERY_STATUS_CHARGING ||
            (status == BatteryManager.BATTERY_STATUS_FULL && powerSource != 0)
    }

    private fun publish() {
        if (!running) return
        val connected = wifiNetworks.isNotEmpty()
        val next = Snapshot(
            batteryPercent, charging, powerSource, wifiEnabled, connected,
            if (connected) wifiRssi else Int.MIN_VALUE, bluetoothEnabled,
            System.currentTimeMillis() / 60000, timeZoneId, DateFormat.is24HourFormat(context),
        )
        if (next != previous) {
            previous = next
            listener?.invoke(next)
        }
    }

    @SuppressLint("MissingPermission")
    fun start(onChanged: (Snapshot) -> Unit) {
        requireMainThread()
        listener = onChanged
        if (running) {
            previous = null
            publish()
            return
        }
        running = true
        val token = ++generation
        previous = null
        batteryPercent = UNKNOWN
        powerSource = 0
        charging = false
        timeZoneId = TimeZone.getDefault().id
        wifiNetworks.clear()
        refreshWifi()
        refreshBluetooth()
        try {
            val nextReceiver = object : BroadcastReceiver() {
                override fun onReceive(context: Context, intent: Intent) {
                    if (!active(token)) return
                    when (intent.action) {
                        Intent.ACTION_BATTERY_CHANGED -> battery(intent)
                        Intent.ACTION_TIMEZONE_CHANGED -> timeZoneId =
                            intent.getStringExtra("time-zone") ?: TimeZone.getDefault().id
                        WifiManager.RSSI_CHANGED_ACTION -> wifiRssi =
                            validRssi(intent.getIntExtra(WifiManager.EXTRA_NEW_RSSI, Int.MIN_VALUE))
                        WifiManager.WIFI_STATE_CHANGED_ACTION -> refreshWifi()
                        BluetoothAdapter.ACTION_STATE_CHANGED -> refreshBluetooth()
                    }
                    publish()
                }
            }
            val filter = IntentFilter().apply {
                addAction(Intent.ACTION_BATTERY_CHANGED)
                addAction(Intent.ACTION_TIME_TICK)
                addAction(Intent.ACTION_TIME_CHANGED)
                addAction(Intent.ACTION_TIMEZONE_CHANGED)
                addAction(WifiManager.RSSI_CHANGED_ACTION)
                addAction(WifiManager.WIFI_STATE_CHANGED_ACTION)
                addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            }
            // Protected system actions; Bluetooth may send from a privileged
            // UID distinct from the main system UID, requiring EXPORTED.
            val sticky = if (Build.VERSION.SDK_INT >= 33) {
                context.registerReceiver(nextReceiver, filter, Context.RECEIVER_EXPORTED)
            } else context.registerReceiver(nextReceiver, filter)
            receiver = nextReceiver
            if (sticky?.action == Intent.ACTION_BATTERY_CHANGED) battery(sticky)
            if (connectivity != null) {
                val activeNetwork = connectivity.activeNetwork
                val capabilities = activeNetwork?.let(connectivity::getNetworkCapabilities)
                if (activeNetwork != null && capabilities?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true) {
                    wifiNetworks[activeNetwork] = capabilities
                }
                val nextNetworks = object : ConnectivityManager.NetworkCallback() {
                    override fun onCapabilitiesChanged(network: Network, capabilities: NetworkCapabilities) {
                        if (!active(token)) return
                        wifiNetworks[network] = capabilities
                        val info = capabilities.transportInfo as? WifiInfo
                        val rssi = validRssi(info?.rssi ?: capabilities.signalStrength)
                        if (rssi != Int.MIN_VALUE) wifiRssi = rssi
                        publish()
                    }
                    override fun onLost(network: Network) {
                        if (!active(token)) return
                        wifiNetworks.remove(network)
                        refreshWifi()
                        publish()
                    }
                }
                connectivity.registerNetworkCallback(
                    NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build(),
                    nextNetworks, handler,
                )
                networks = nextNetworks
            }
            val nextObserver = object : ContentObserver(handler) {
                override fun onChange(selfChange: Boolean, uri: Uri?) {
                    if (!active(token)) return
                    if (uri == Settings.Global.getUriFor(Settings.Global.BLUETOOTH_ON)) refreshBluetooth()
                    publish()
                }
            }
            context.contentResolver.registerContentObserver(
                Settings.System.getUriFor(Settings.System.TIME_12_24), false, nextObserver,
            )
            observer = nextObserver
            context.contentResolver.registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.BLUETOOTH_ON), false, nextObserver,
            )
            publish()
        } catch (failure: RuntimeException) {
            stop()
            throw failure
        }
    }

    fun stop() {
        requireMainThread()
        running = false
        ++generation // Late callbacks from an earlier registration cannot change the new session.
        listener = null
        previous = null
        receiver?.let(context::unregisterReceiver)
        receiver = null
        networks?.let { connectivity?.unregisterNetworkCallback(it) }
        networks = null
        observer?.let(context.contentResolver::unregisterContentObserver)
        observer = null
        wifiNetworks.clear()
    }

    companion object {
        const val UNAVAILABLE = -2
        const val UNKNOWN = -1
        const val OFF = 0
        const val ON = 1
        private fun validRssi(value: Int) = if (value > -127 && value <= 0) value else Int.MIN_VALUE
    }
}
