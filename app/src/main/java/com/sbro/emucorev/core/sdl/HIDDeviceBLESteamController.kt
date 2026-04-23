@file:Suppress("DEPRECATION")

package com.sbro.emucorev.core.sdl

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.hardware.usb.UsbDevice
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.Arrays
import java.util.LinkedList
import java.util.UUID

internal class HIDDeviceBLESteamController(
    private var mManager: HIDDeviceManager?,
    private val mDevice: BluetoothDevice
) : BluetoothGattCallback(), HIDDevice {
    private var mDeviceId: Int = mManager!!.getDeviceIDForIdentifier(getIdentifier())
    private var mGatt: BluetoothGatt? = null
    private var mIsRegistered = false
    private var mIsConnected = false
    private var mIsChromebook = mManager!!.context.packageManager.hasSystemFeature("org.chromium.arc.device_management")
    private var mIsReconnecting = false
    private var mFrozen = false
    private val mOperations = LinkedList<GattOperation>()
    private var mCurrentOperation: GattOperation? = null
    private val mHandler = Handler(Looper.getMainLooper())

    init {
        mGatt = connectGatt()
    }

    fun getIdentifier(): String = "SteamController.${mDevice.address}"

    fun getGatt(): BluetoothGatt? = mGatt

    private fun connectGatt(managed: Boolean): BluetoothGatt {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                mDevice.connectGatt(mManager!!.context, managed, this, TRANSPORT_LE)
            } catch (_: Exception) {
                mDevice.connectGatt(mManager!!.context, managed, this)
            }
        } else {
            mDevice.connectGatt(mManager!!.context, managed, this)
        }
    }

    private fun connectGatt(): BluetoothGatt = connectGatt(false)

    private fun getConnectionState(): Int {
        val context = mManager?.context ?: return BluetoothProfile.STATE_DISCONNECTED
        val btManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
            ?: return BluetoothProfile.STATE_DISCONNECTED
        return btManager.getConnectionState(mDevice, BluetoothProfile.GATT)
    }

    fun reconnect() {
        if (getConnectionState() != BluetoothProfile.STATE_CONNECTED) {
            mGatt?.disconnect()
            mGatt = connectGatt()
        }
    }

    fun checkConnectionForChromebookIssue() {
        if (!mIsChromebook) {
            return
        }

        when (val connectionState = getConnectionState()) {
            BluetoothProfile.STATE_CONNECTED -> {
                if (!mIsConnected) {
                    Log.v(TAG, "Chromebook: forcing reconnect because controller looks connected but callback never arrived.")
                    mIsReconnecting = true
                    mGatt?.disconnect()
                    mGatt = connectGatt(false)
                } else if (!isRegistered()) {
                    val gatt = mGatt
                    if (gatt != null && gatt.services.isNotEmpty()) {
                        Log.v(TAG, "Chromebook: connected but not registered, trying to recover.")
                        probeService()
                    } else {
                        Log.v(TAG, "Chromebook: connected but services not discovered, trying to recover.")
                        mIsReconnecting = true
                        mGatt?.disconnect()
                        mGatt = connectGatt(false)
                    }
                } else {
                    Log.v(TAG, "Chromebook: connected and registered.")
                    return
                }
            }
            BluetoothProfile.STATE_DISCONNECTED -> {
                Log.v(TAG, "Chromebook: disconnected or BtGatt.ContextMap bug hit, reconnecting.")
                mIsReconnecting = true
                mGatt?.disconnect()
                mGatt = connectGatt(false)
            }
            BluetoothProfile.STATE_CONNECTING -> {
                Log.v(TAG, "Chromebook: still connecting.")
            }
            else -> {
                Log.v(TAG, "Chromebook: unhandled connection state $connectionState")
            }
        }

        mHandler.postDelayed({ checkConnectionForChromebookIssue() }, CHROMEBOOK_CONNECTION_CHECK_INTERVAL.toLong())
    }

    private fun isRegistered(): Boolean = mIsRegistered

    private fun setRegistered() {
        mIsRegistered = true
    }

    private fun probeService(): Boolean {
        if (isRegistered()) return true
        if (!mIsConnected) return false

        val gatt = mGatt ?: return false
        Log.v(TAG, "probeService controller=$this")

        gatt.services.forEach { service ->
            if (service.uuid == steamControllerService) {
                Log.v(TAG, "Found Valve steam controller service ${service.uuid}")
                service.characteristics.forEach { chr ->
                    if (chr.uuid == inputCharacteristic) {
                        Log.v(TAG, "Found input characteristic")
                        val cccd = chr.getDescriptor(CLIENT_DESCRIPTOR_UUID)
                        if (cccd != null) {
                            enableNotification(chr.uuid)
                        }
                    }
                }
                return true
            }
        }

        if (gatt.services.isEmpty() && mIsChromebook && !mIsReconnecting) {
            Log.e(TAG, "Chromebook: discovered services are empty, reconnecting.")
            mIsConnected = false
            mIsReconnecting = true
            gatt.disconnect()
            mGatt = connectGatt(false)
        }
        return false
    }

    private fun finishCurrentGattOperation() {
        val op = synchronized(mOperations) {
            val current = mCurrentOperation
            mCurrentOperation = null
            current
        }
        if (op != null) {
            val result = op.finish()
            if (!result) {
                synchronized(mOperations) {
                    mOperations.addFirst(op)
                }
            }
        }
        executeNextGattOperation()
    }

    private fun executeNextGattOperation() {
        synchronized(mOperations) {
            if (mCurrentOperation != null || mOperations.isEmpty()) {
                return
            }
            mCurrentOperation = mOperations.removeFirst()
        }

        mHandler.post {
            synchronized(mOperations) {
                val current = mCurrentOperation
                if (current == null) {
                    Log.e(TAG, "Current operation null in executor?")
                    return@post
                }
                current.run()
            }
        }
    }

    private fun queueGattOperation(op: GattOperation) {
        synchronized(mOperations) {
            mOperations.add(op)
        }
        executeNextGattOperation()
    }

    private fun enableNotification(chrUuid: UUID) {
        val gatt = mGatt ?: return
        queueGattOperation(GattOperation.enableNotification(gatt, chrUuid))
    }

    private fun writeCharacteristic(uuid: UUID, value: ByteArray) {
        val gatt = mGatt ?: return
        queueGattOperation(GattOperation.writeCharacteristic(gatt, uuid, value))
    }

    private fun readCharacteristic(uuid: UUID) {
        val gatt = mGatt ?: return
        queueGattOperation(GattOperation.readCharacteristic(gatt, uuid))
    }

    override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
        mIsReconnecting = false
        if (newState == BluetoothProfile.STATE_CONNECTED) {
            mIsConnected = true
            if (!isRegistered()) {
                mHandler.post { mGatt?.discoverServices() }
            }
        } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
            mIsConnected = false
        }
    }

    override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
        if (status == 0) {
            if (gatt.services.isEmpty()) {
                Log.v(TAG, "onServicesDiscovered returned zero services; reconnecting.")
                mIsReconnecting = true
                mIsConnected = false
                gatt.disconnect()
                mGatt = connectGatt(false)
            } else {
                probeService()
            }
        }
    }

    override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
        if (characteristic.uuid == reportCharacteristic && !mFrozen) {
            mManager?.HIDDeviceReportResponse(getId(), characteristic.value)
        }
        finishCurrentGattOperation()
    }

    override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
        if (characteristic.uuid == reportCharacteristic && !isRegistered()) {
            Log.v(TAG, "Registering Steam Controller with ID: ${getId()}")
            mManager?.HIDDeviceConnected(
                getId(),
                getIdentifier(),
                getVendorId(),
                getProductId(),
                getSerialNumber(),
                getVersion(),
                getManufacturerName(),
                getProductName(),
                0,
                0,
                0,
                0,
                true
            )
            setRegistered()
        }
        finishCurrentGattOperation()
    }

    override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        if (characteristic.uuid == inputCharacteristic && !mFrozen) {
            mManager?.HIDDeviceInputReport(getId(), characteristic.value)
        }
    }

    override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
        val chr = descriptor.characteristic
        if (chr.uuid == inputCharacteristic) {
            val reportChr = chr.service?.getCharacteristic(reportCharacteristic)
            if (reportChr != null) {
                Log.v(TAG, "Writing report characteristic to enter valve mode")
                reportChr.value = enterValveMode
                gatt.writeCharacteristic(reportChr)
            }
        }
        finishCurrentGattOperation()
    }

    override fun getId(): Int = mDeviceId
    override fun getVendorId(): Int = 0x28DE
    override fun getProductId(): Int = 0x1106
    override fun getSerialNumber(): String = "12345"
    override fun getVersion(): Int = 0
    override fun getManufacturerName(): String = "Valve Corporation"
    override fun getProductName(): String = "Steam Controller"
    override fun getDevice(): UsbDevice? = null
    override fun open(): Boolean = true

    override fun writeReport(report: ByteArray, feature: Boolean): Int {
        if (!isRegistered()) {
            Log.e(TAG, "Attempted writeReport before Steam Controller is registered!")
            if (mIsConnected) {
                probeService()
            }
            return -1
        }

        if (feature) {
            val actualReport = Arrays.copyOfRange(report, 1, report.size - 1)
            writeCharacteristic(reportCharacteristic, actualReport)
            return report.size
        }

        writeCharacteristic(reportCharacteristic, report)
        return report.size
    }

    override fun readReport(report: ByteArray, feature: Boolean): Boolean {
        if (!isRegistered()) {
            Log.e(TAG, "Attempted readReport before Steam Controller is registered!")
            if (mIsConnected) {
                probeService()
            }
            return false
        }

        return if (feature) {
            readCharacteristic(reportCharacteristic)
            true
        } else {
            false
        }
    }

    override fun close() {
    }

    override fun setFrozen(frozen: Boolean) {
        mFrozen = frozen
    }

    override fun shutdown() {
        close()
        mGatt?.let { gatt ->
            gatt.disconnect()
            gatt.close()
        }
        mGatt = null
        mManager = null
        mIsRegistered = false
        mIsConnected = false
        synchronized(mOperations) {
            mOperations.clear()
            mCurrentOperation = null
        }
    }

    private class GattOperation(
        private val mGatt: BluetoothGatt,
        private val mOp: Operation,
        private val mUuid: UUID,
        private val mValue: ByteArray? = null
    ) {
        private var mResult = true

        fun run() {
            val chr = getCharacteristic(mUuid)
            when (mOp) {
                Operation.CHR_READ -> {
                    if (chr == null || !mGatt.readCharacteristic(chr)) {
                        Log.e(TAG, "Unable to read characteristic ${mUuid}")
                        mResult = false
                    }
                }
                Operation.CHR_WRITE -> {
                    if (chr == null) {
                        Log.e(TAG, "Unable to write characteristic ${mUuid}")
                        mResult = false
                        return
                    }
                    chr.value = mValue
                    if (!mGatt.writeCharacteristic(chr)) {
                        Log.e(TAG, "Unable to write characteristic ${mUuid}")
                        mResult = false
                    }
                }
                Operation.ENABLE_NOTIFICATION -> {
                    if (chr == null) {
                        mResult = false
                        return
                    }
                    val cccd = chr.getDescriptor(CLIENT_DESCRIPTOR_UUID)
                    if (cccd == null) {
                        mResult = false
                        return
                    }
                    val properties = chr.properties
                    val value = when {
                        (properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY) == BluetoothGattCharacteristic.PROPERTY_NOTIFY ->
                            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        (properties and BluetoothGattCharacteristic.PROPERTY_INDICATE) == BluetoothGattCharacteristic.PROPERTY_INDICATE ->
                            BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
                        else -> {
                            Log.e(TAG, "Unable to start notifications on input characteristic")
                            mResult = false
                            return
                        }
                    }

                    mGatt.setCharacteristicNotification(chr, true)
                    cccd.value = value
                    if (!mGatt.writeDescriptor(cccd)) {
                        Log.e(TAG, "Unable to write descriptor ${mUuid}")
                        mResult = false
                    }
                }
            }
        }

        fun finish(): Boolean = mResult

        private fun getCharacteristic(uuid: UUID): BluetoothGattCharacteristic? {
            val valveService = mGatt.getService(steamControllerService) ?: return null
            return valveService.getCharacteristic(uuid)
        }

        private enum class Operation {
            CHR_READ,
            CHR_WRITE,
            ENABLE_NOTIFICATION
        }

        companion object {
            fun readCharacteristic(gatt: BluetoothGatt, uuid: UUID): GattOperation {
                return GattOperation(gatt, Operation.CHR_READ, uuid)
            }

            fun writeCharacteristic(gatt: BluetoothGatt, uuid: UUID, value: ByteArray): GattOperation {
                return GattOperation(gatt, Operation.CHR_WRITE, uuid, value)
            }

            fun enableNotification(gatt: BluetoothGatt, uuid: UUID): GattOperation {
                return GattOperation(gatt, Operation.ENABLE_NOTIFICATION, uuid)
            }
        }
    }

    private companion object {
        private const val TAG = "hidapi"
        private const val TRANSPORT_LE = 2
        private const val CHROMEBOOK_CONNECTION_CHECK_INTERVAL = 10000
        val steamControllerService: UUID = UUID.fromString("100F6C32-1735-4313-B402-38567131E5F3")
        val inputCharacteristic: UUID = UUID.fromString("100F6C33-1735-4313-B402-38567131E5F3")
        val reportCharacteristic: UUID = UUID.fromString("100F6C34-1735-4313-B402-38567131E5F3")
        val CLIENT_DESCRIPTOR_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        val enterValveMode: ByteArray = byteArrayOf(0xC0.toByte(), 0x87.toByte(), 0x03, 0x08, 0x07, 0x00)
    }
}
