@file:Suppress("DEPRECATION")

package com.sbro.emucorev.core.sdl

import android.app.PendingIntent
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log

class HIDDeviceManager private constructor(private val mContext: Context) {
    private val mDevicesById = HashMap<Int, HIDDevice>()
    private val mBluetoothDevices = HashMap<BluetoothDevice, HIDDeviceBLESteamController>()
    private var mNextDeviceId = 0
    private val mSharedPreferences: SharedPreferences = mContext.getSharedPreferences("hidapi", Context.MODE_PRIVATE)
    private val mIsChromebook = mContext.packageManager.hasSystemFeature("org.chromium.arc.device_management")
    private var mUsbManager: UsbManager? = null
    private var mHandler: Handler? = null
    private var mBluetoothManager: BluetoothManager? = null
    private var mLastBluetoothDevices: List<BluetoothDevice> = emptyList()

    private val mUsbBroadcast = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> handleUsbDeviceAttached(intent.parcelableExtra(UsbManager.EXTRA_DEVICE))
                UsbManager.ACTION_USB_DEVICE_DETACHED -> handleUsbDeviceDetached(intent.parcelableExtra(UsbManager.EXTRA_DEVICE))
                ACTION_USB_PERMISSION -> handleUsbDevicePermission(
                    intent.parcelableExtra(UsbManager.EXTRA_DEVICE),
                    intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                )
            }
        }
    }

    private val mBluetoothBroadcast = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BluetoothDevice.ACTION_ACL_CONNECTED -> {
                    val device = intent.parcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                    Log.d(TAG, "Bluetooth device connected: $device")
                    if (isSteamController(device)) {
                        connectBluetoothDevice(device!!)
                    }
                }
                BluetoothDevice.ACTION_ACL_DISCONNECTED -> {
                    val device = intent.parcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                    Log.d(TAG, "Bluetooth device disconnected: $device")
                    if (device != null) {
                        disconnectBluetoothDevice(device)
                    }
                }
            }
        }
    }

    init {
        HIDDeviceRegisterCallback()
        mNextDeviceId = mSharedPreferences.getInt("next_device_id", 0)
    }

    val context: Context
        get() = mContext

    fun getDeviceIDForIdentifier(identifier: String): Int {
        val editor = mSharedPreferences.edit()
        var result = mSharedPreferences.getInt(identifier, 0)
        if (result == 0) {
            result = mNextDeviceId++
            editor.putInt("next_device_id", mNextDeviceId)
        }
        editor.putInt(identifier, result)
        editor.commit()
        return result
    }

    private fun initializeUSB() {
        mUsbManager = mContext.getSystemService(Context.USB_SERVICE) as? UsbManager ?: return

        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_USB_PERMISSION)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            mContext.registerReceiver(mUsbBroadcast, filter, Context.RECEIVER_EXPORTED)
        } else {
            mContext.registerReceiver(mUsbBroadcast, filter)
        }

        mUsbManager?.deviceList?.values?.forEach { handleUsbDeviceAttached(it) }
    }

    fun getUSBManager(): UsbManager? = mUsbManager

    private fun shutdownUSB() {
        try {
            mContext.unregisterReceiver(mUsbBroadcast)
        } catch (_: Exception) {
        }
    }

    private fun isHIDDeviceInterface(usbDevice: UsbDevice, usbInterface: UsbInterface): Boolean {
        return usbInterface.interfaceClass == UsbConstants.USB_CLASS_HID ||
            isXbox360Controller(usbDevice, usbInterface) ||
            isXboxOneController(usbDevice, usbInterface)
    }

    private fun isXbox360Controller(usbDevice: UsbDevice, usbInterface: UsbInterface): Boolean {
        val supportedVendors = intArrayOf(
            0x0079, 0x044f, 0x045e, 0x046d, 0x056e, 0x06a3, 0x0738, 0x07ff, 0x0e6f, 0x0f0d,
            0x1038, 0x11c9, 0x12ab, 0x1430, 0x146b, 0x1532, 0x15e4, 0x162e, 0x1689, 0x1949,
            0x1bad, 0x20d6, 0x24c6, 0x2c22, 0x2dc8, 0x9886
        )
        if (usbInterface.interfaceClass == UsbConstants.USB_CLASS_VENDOR_SPEC &&
            usbInterface.interfaceSubclass == 93 &&
            (usbInterface.interfaceProtocol == 1 || usbInterface.interfaceProtocol == 129)
        ) {
            return supportedVendors.contains(usbDevice.vendorId)
        }
        return false
    }

    private fun isXboxOneController(usbDevice: UsbDevice, usbInterface: UsbInterface): Boolean {
        val supportedVendors = intArrayOf(
            0x03f0, 0x044f, 0x045e, 0x0738, 0x0b05, 0x0e6f, 0x0f0d, 0x10f5,
            0x1532, 0x20d6, 0x24c6, 0x2dc8, 0x2e24, 0x3537
        )
        if (usbInterface.id == 0 &&
            usbInterface.interfaceClass == UsbConstants.USB_CLASS_VENDOR_SPEC &&
            usbInterface.interfaceSubclass == 71 &&
            usbInterface.interfaceProtocol == 208
        ) {
            return supportedVendors.contains(usbDevice.vendorId)
        }
        return false
    }

    private fun handleUsbDeviceAttached(usbDevice: UsbDevice?) {
        if (usbDevice != null) {
            connectHIDDeviceUSB(usbDevice)
        }
    }

    private fun handleUsbDeviceDetached(usbDevice: UsbDevice?) {
        if (usbDevice == null) return
        val devices = arrayListOf<Int>()
        mDevicesById.values.forEach { device ->
            if (usbDevice == device.getDevice()) {
                devices.add(device.getId())
            }
        }
        devices.forEach { id ->
            val device = mDevicesById.remove(id)
            device?.shutdown()
            HIDDeviceDisconnected(id)
        }
    }

    private fun handleUsbDevicePermission(usbDevice: UsbDevice?, permissionGranted: Boolean) {
        if (usbDevice == null) return
        mDevicesById.values.forEach { device ->
            if (usbDevice == device.getDevice()) {
                val opened = if (permissionGranted) device.open() else false
                HIDDeviceOpenResult(device.getId(), opened)
            }
        }
    }

    private fun connectHIDDeviceUSB(usbDevice: UsbDevice) {
        synchronized(this) {
            var interfaceMask = 0
            for (interfaceIndex in 0 until usbDevice.interfaceCount) {
                val usbInterface = usbDevice.getInterface(interfaceIndex)
                if (isHIDDeviceInterface(usbDevice, usbInterface)) {
                    val interfaceId = usbInterface.id
                    if ((interfaceMask and (1 shl interfaceId)) != 0) {
                        continue
                    }
                    interfaceMask = interfaceMask or (1 shl interfaceId)

                    val device = HIDDeviceUSB(this, usbDevice, interfaceIndex)
                    val id = device.getId()
                    mDevicesById[id] = device
                    HIDDeviceConnected(
                        id,
                        device.getIdentifier(),
                        device.getVendorId(),
                        device.getProductId(),
                        device.getSerialNumber(),
                        device.getVersion(),
                        device.getManufacturerName(),
                        device.getProductName(),
                        usbInterface.id,
                        usbInterface.interfaceClass,
                        usbInterface.interfaceSubclass,
                        usbInterface.interfaceProtocol,
                        false
                    )
                }
            }
        }
    }

    private fun initializeBluetooth() {
        Log.d(TAG, "Initializing Bluetooth")

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            mContext.packageManager.checkPermission(android.Manifest.permission.BLUETOOTH_CONNECT, mContext.packageName) != PackageManager.PERMISSION_GRANTED
        ) {
            Log.d(TAG, "Couldn't initialize Bluetooth, missing android.permission.BLUETOOTH_CONNECT")
            return
        }

        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.R &&
            mContext.packageManager.checkPermission(android.Manifest.permission.BLUETOOTH, mContext.packageName) != PackageManager.PERMISSION_GRANTED
        ) {
            Log.d(TAG, "Couldn't initialize Bluetooth, missing android.permission.BLUETOOTH")
            return
        }

        if (!mContext.packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE) ||
            Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN_MR2
        ) {
            Log.d(TAG, "Couldn't initialize Bluetooth, this version of Android does not support Bluetooth LE")
            return
        }

        mBluetoothManager = mContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager ?: return
        val btAdapter = mBluetoothManager?.adapter ?: return

        btAdapter.bondedDevices.forEach { device ->
            Log.d(TAG, "Bluetooth device available: $device")
            if (isSteamController(device)) {
                connectBluetoothDevice(device)
            }
        }

        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_ACL_CONNECTED)
            addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            mContext.registerReceiver(mBluetoothBroadcast, filter, Context.RECEIVER_EXPORTED)
        } else {
            mContext.registerReceiver(mBluetoothBroadcast, filter)
        }

        if (mIsChromebook) {
            mHandler = Handler(Looper.getMainLooper())
            mLastBluetoothDevices = arrayListOf()
        }
    }

    private fun shutdownBluetooth() {
        try {
            mContext.unregisterReceiver(mBluetoothBroadcast)
        } catch (_: Exception) {
        }
    }

    fun chromebookConnectionHandler() {
        if (!mIsChromebook) return

        val disconnected = arrayListOf<BluetoothDevice>()
        val connected = arrayListOf<BluetoothDevice>()
        val currentConnected = mBluetoothManager?.getConnectedDevices(BluetoothProfile.GATT) ?: emptyList()

        currentConnected.forEach { bluetoothDevice ->
            if (!mLastBluetoothDevices.contains(bluetoothDevice)) {
                connected.add(bluetoothDevice)
            }
        }
        mLastBluetoothDevices.forEach { bluetoothDevice ->
            if (!currentConnected.contains(bluetoothDevice)) {
                disconnected.add(bluetoothDevice)
            }
        }
        mLastBluetoothDevices = currentConnected

        disconnected.forEach { disconnectBluetoothDevice(it) }
        connected.forEach { connectBluetoothDevice(it) }

        mHandler?.postDelayed({ chromebookConnectionHandler() }, 10000L)
    }

    fun connectBluetoothDevice(bluetoothDevice: BluetoothDevice): Boolean {
        Log.v(TAG, "connectBluetoothDevice device=$bluetoothDevice")
        synchronized(this) {
            val existing = mBluetoothDevices[bluetoothDevice]
            if (existing != null) {
                Log.v(TAG, "Steam controller with address $bluetoothDevice already exists, attempting reconnect")
                existing.reconnect()
                return false
            }
            val device = HIDDeviceBLESteamController(this, bluetoothDevice)
            val id = device.getId()
            mBluetoothDevices[bluetoothDevice] = device
            mDevicesById[id] = device
        }
        return true
    }

    fun disconnectBluetoothDevice(bluetoothDevice: BluetoothDevice) {
        synchronized(this) {
            val device = mBluetoothDevices.remove(bluetoothDevice) ?: return
            val id = device.getId()
            mDevicesById.remove(id)
            device.shutdown()
            HIDDeviceDisconnected(id)
        }
    }

    fun isSteamController(bluetoothDevice: BluetoothDevice?): Boolean {
        if (bluetoothDevice?.name == null) return false
        return bluetoothDevice.name == "SteamController" &&
            (bluetoothDevice.type and BluetoothDevice.DEVICE_TYPE_LE) != 0
    }

    private fun close() {
        shutdownUSB()
        shutdownBluetooth()
        synchronized(this) {
            mDevicesById.values.forEach { it.shutdown() }
            mDevicesById.clear()
            mBluetoothDevices.clear()
            HIDDeviceReleaseCallback()
        }
    }

    fun setFrozen(frozen: Boolean) {
        synchronized(this) {
            mDevicesById.values.forEach { it.setFrozen(frozen) }
        }
    }

    private fun getDevice(id: Int): HIDDevice? {
        synchronized(this) {
            val result = mDevicesById[id]
            if (result == null) {
                Log.v(TAG, "No device for id: $id")
                Log.v(TAG, "Available devices: ${mDevicesById.keys}")
            }
            return result
        }
    }

    fun initialize(usb: Boolean, bluetooth: Boolean): Boolean {
        Log.v(TAG, "initialize($usb, $bluetooth)")
        if (usb) initializeUSB()
        if (bluetooth) initializeBluetooth()
        return true
    }

    fun openDevice(deviceID: Int): Boolean {
        Log.v(TAG, "openDevice deviceID=$deviceID")
        val device = getDevice(deviceID)
        if (device == null) {
            HIDDeviceDisconnected(deviceID)
            return false
        }

        val usbDevice = device.getDevice()
        if (usbDevice != null && mUsbManager?.hasPermission(usbDevice) == false) {
            HIDDeviceOpenPending(deviceID)
            try {
                val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) FLAG_MUTABLE else 0
                if (Build.VERSION.SDK_INT >= 33) {
                    val intent = Intent(ACTION_USB_PERMISSION).apply { setPackage(mContext.packageName) }
                    mUsbManager?.requestPermission(usbDevice, PendingIntent.getBroadcast(mContext, 0, intent, flags))
                } else {
                    mUsbManager?.requestPermission(
                        usbDevice,
                        PendingIntent.getBroadcast(mContext, 0, Intent(ACTION_USB_PERMISSION), flags)
                    )
                }
            } catch (_: Exception) {
                Log.v(TAG, "Couldn't request permission for USB device $usbDevice")
                HIDDeviceOpenResult(deviceID, false)
            }
            return false
        }

        return try {
            device.open()
        } catch (exception: Exception) {
            Log.e(TAG, "Got exception: ${Log.getStackTraceString(exception)}")
            false
        }
    }

    fun writeReport(deviceID: Int, report: ByteArray, feature: Boolean): Int {
        return try {
            val device = getDevice(deviceID)
            if (device == null) {
                HIDDeviceDisconnected(deviceID)
                -1
            } else {
                device.writeReport(report, feature)
            }
        } catch (exception: Exception) {
            Log.e(TAG, "Got exception: ${Log.getStackTraceString(exception)}")
            -1
        }
    }

    fun readReport(deviceID: Int, report: ByteArray, feature: Boolean): Boolean {
        return try {
            val device = getDevice(deviceID)
            if (device == null) {
                HIDDeviceDisconnected(deviceID)
                false
            } else {
                device.readReport(report, feature)
            }
        } catch (exception: Exception) {
            Log.e(TAG, "Got exception: ${Log.getStackTraceString(exception)}")
            false
        }
    }

    fun closeDevice(deviceID: Int) {
        try {
            Log.v(TAG, "closeDevice deviceID=$deviceID")
            val device = getDevice(deviceID)
            if (device == null) {
                HIDDeviceDisconnected(deviceID)
                return
            }
            device.close()
        } catch (exception: Exception) {
            Log.e(TAG, "Got exception: ${Log.getStackTraceString(exception)}")
        }
    }

    private external fun HIDDeviceRegisterCallback()
    private external fun HIDDeviceReleaseCallback()

    external fun HIDDeviceConnected(
        deviceID: Int,
        identifier: String,
        vendorId: Int,
        productId: Int,
        serialNumber: String,
        releaseNumber: Int,
        manufacturerString: String,
        productString: String,
        interfaceNumber: Int,
        interfaceClass: Int,
        interfaceSubclass: Int,
        interfaceProtocol: Int,
        bluetooth: Boolean
    )

    external fun HIDDeviceOpenPending(deviceID: Int)
    external fun HIDDeviceOpenResult(deviceID: Int, opened: Boolean)
    external fun HIDDeviceDisconnected(deviceID: Int)
    external fun HIDDeviceInputReport(deviceID: Int, report: ByteArray)
    external fun HIDDeviceReportResponse(deviceID: Int, report: ByteArray)

    companion object {
        private const val TAG = "hidapi"
        private const val ACTION_USB_PERMISSION = "com.sbro.emucorev.core.sdl.USB_PERMISSION"
        private const val FLAG_MUTABLE = 0x02000000

        @JvmStatic
        private var sManager: HIDDeviceManager? = null

        @JvmStatic
        private var sManagerRefCount = 0

        @JvmStatic
        fun acquire(context: Context): HIDDeviceManager {
            if (sManagerRefCount == 0) {
                sManager = HIDDeviceManager(context)
            }
            sManagerRefCount++
            return sManager!!
        }

        @JvmStatic
        fun release(manager: HIDDeviceManager) {
            if (manager == sManager) {
                sManagerRefCount--
                if (sManagerRefCount == 0) {
                    sManager?.close()
                    sManager = null
                }
            }
        }
    }
}

private inline fun <reified T> Intent.parcelableExtra(name: String): T? {
    return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        getParcelableExtra(name, T::class.java)
    } else {
        @Suppress("DEPRECATION")
        getParcelableExtra(name)
    }
}
