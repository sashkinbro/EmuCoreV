package com.sbro.emucorev.core.sdl

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.os.Build
import android.util.Log
import java.util.Arrays

internal class HIDDeviceUSB(
    manager: HIDDeviceManager,
    private val device: UsbDevice,
    private val interfaceIndex: Int
) : HIDDevice {
    private var manager: HIDDeviceManager? = manager
    private val usbInterfaceId = device.getInterface(interfaceIndex).id
    private val deviceIdentifier = String.format(
        "%s/%x/%x/%d",
        device.deviceName,
        device.vendorId,
        device.productId,
        interfaceIndex
    )
    private val deviceId = manager.getDeviceIDForIdentifier(deviceIdentifier)
    private var connection: UsbDeviceConnection? = null
    private var inputEndpoint: UsbEndpoint? = null
    private var outputEndpoint: UsbEndpoint? = null
    private var inputThread: InputThread? = null
    private var running = false
    private var frozen = false

    override fun getId(): Int = deviceId

    override fun getVendorId(): Int = device.vendorId

    override fun getProductId(): Int = device.productId

    override fun getSerialNumber(): String {
        val result = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            try {
                device.serialNumber
            } catch (_: SecurityException) {
                null
            }
        } else {
            null
        }
        return result ?: ""
    }

    override fun getVersion(): Int = 0

    override fun getManufacturerName(): String {
        val result = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            device.manufacturerName
        } else {
            null
        }
        return result ?: String.format("%x", getVendorId())
    }

    override fun getProductName(): String {
        val result = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            device.productName
        } else {
            null
        }
        return result ?: String.format("%x", getProductId())
    }

    override fun getDevice(): UsbDevice = device

    fun getIdentifier(): String = deviceIdentifier

    fun getDeviceName(): String {
        return buildString {
            append(getManufacturerName())
            append(' ')
            append(getProductName())
            append("(0x")
            append(String.format("%x", getVendorId()))
            append("/0x")
            append(String.format("%x", getProductId()))
            append(')')
        }
    }

    override fun open(): Boolean {
        val usbManager = manager?.getUSBManager()
        connection = usbManager?.openDevice(device)
        val currentConnection = connection
        if (currentConnection == null) {
            Log.w(TAG, "Unable to open USB device ${getDeviceName()}")
            return false
        }

        val iface = device.getInterface(interfaceIndex)
        if (!currentConnection.claimInterface(iface, true)) {
            Log.w(TAG, "Failed to claim interfaces on USB device ${getDeviceName()}")
            close()
            return false
        }

        inputEndpoint = null
        outputEndpoint = null
        repeat(iface.endpointCount) { index ->
            val endpoint = iface.getEndpoint(index)
            when (endpoint.direction) {
                UsbConstants.USB_DIR_IN -> if (inputEndpoint == null) {
                    inputEndpoint = endpoint
                }
                UsbConstants.USB_DIR_OUT -> if (outputEndpoint == null) {
                    outputEndpoint = endpoint
                }
            }
        }

        if (inputEndpoint == null || outputEndpoint == null) {
            Log.w(TAG, "Missing required endpoint on USB device ${getDeviceName()}")
            close()
            return false
        }

        running = true
        inputThread = InputThread().also { it.start() }
        return true
    }

    override fun writeReport(report: ByteArray, feature: Boolean): Int {
        val currentConnection = connection
        if (currentConnection == null) {
            Log.w(TAG, "writeReport() called with no device connection")
            return -1
        }

        if (feature) {
            var offset = 0
            var length = report.size
            var skippedReportId = false
            val reportNumber = report[0]

            if (reportNumber.toInt() == 0x0) {
                offset++
                length--
                skippedReportId = true
            }

            val result = currentConnection.controlTransfer(
                UsbConstants.USB_TYPE_CLASS or 0x01 or UsbConstants.USB_DIR_OUT,
                0x09,
                (3 shl 8) or reportNumber.toInt(),
                usbInterfaceId,
                report,
                offset,
                length,
                1000
            )

            if (result < 0) {
                Log.w(TAG, "writeFeatureReport() returned $result on device ${getDeviceName()}")
                return -1
            }

            if (skippedReportId) {
                length++
            }
            return length
        }

        val result = currentConnection.bulkTransfer(outputEndpoint, report, report.size, 1000)
        if (result != report.size) {
            Log.w(TAG, "writeOutputReport() returned $result on device ${getDeviceName()}")
        }
        return result
    }

    override fun readReport(report: ByteArray, feature: Boolean): Boolean {
        val currentConnection = connection
        if (currentConnection == null) {
            Log.w(TAG, "readReport() called with no device connection")
            return false
        }

        var offset = 0
        var length = report.size
        var skippedReportId = false
        val reportNumber = report[0]

        if (reportNumber.toInt() == 0x0) {
            offset++
            length--
            skippedReportId = true
        }

        var result = currentConnection.controlTransfer(
            UsbConstants.USB_TYPE_CLASS or 0x01 or UsbConstants.USB_DIR_IN,
            0x01,
            ((if (feature) 3 else 1) shl 8) or reportNumber.toInt(),
            usbInterfaceId,
            report,
            offset,
            length,
            1000
        )

        if (result < 0) {
            Log.w(TAG, "getFeatureReport() returned $result on device ${getDeviceName()}")
            return false
        }

        if (skippedReportId) {
            result++
            length++
        }

        val data = if (result == length) {
            report
        } else {
            Arrays.copyOfRange(report, 0, result)
        }
        manager?.HIDDeviceReportResponse(deviceId, data)
        return true
    }

    override fun close() {
        running = false
        inputThread?.let { thread ->
            while (thread.isAlive) {
                thread.interrupt()
                try {
                    thread.join()
                } catch (_: InterruptedException) {
                }
            }
        }
        inputThread = null

        connection?.let { currentConnection ->
            val iface: UsbInterface = device.getInterface(interfaceIndex)
            currentConnection.releaseInterface(iface)
            currentConnection.close()
        }
        connection = null
    }

    override fun shutdown() {
        close()
        manager = null
    }

    override fun setFrozen(frozen: Boolean) {
        this.frozen = frozen
    }

    private inner class InputThread : Thread() {
        override fun run() {
            val currentInputEndpoint = inputEndpoint ?: return
            val packetSize = currentInputEndpoint.maxPacketSize
            val packet = ByteArray(packetSize)
            while (running) {
                val result = try {
                    connection?.bulkTransfer(currentInputEndpoint, packet, packetSize, 1000) ?: break
                } catch (exception: Exception) {
                    Log.v(TAG, "Exception in UsbDeviceConnection bulktransfer: $exception")
                    break
                }

                if (result > 0) {
                    val data = if (result == packetSize) {
                        packet
                    } else {
                        Arrays.copyOfRange(packet, 0, result)
                    }

                    if (!frozen) {
                        manager?.HIDDeviceInputReport(deviceId, data)
                    }
                }
            }
        }
    }

    private companion object {
        private const val TAG = "hidapi"
    }
}
