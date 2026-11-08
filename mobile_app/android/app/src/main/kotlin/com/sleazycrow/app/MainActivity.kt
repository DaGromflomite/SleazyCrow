package com.sleazycrow.app

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

/**
 * Adds classic Bluetooth (BR/EDR) inquiry on top of Flutter. flutter_blue_plus
 * only does BLE, but gas-pump card skimmers and similar gear use classic BT, so
 * we expose BluetoothAdapter.startDiscovery() to Dart:
 *   - MethodChannel  sleazycrow/classic_bt         -> isSupported / start / stop
 *   - EventChannel   sleazycrow/classic_bt/events  -> one map per found device
 */
class MainActivity : FlutterActivity() {
    private val methodChannelName = "sleazycrow/classic_bt"
    private val eventChannelName = "sleazycrow/classic_bt/events"

    private var eventSink: EventChannel.EventSink? = null
    private var receiver: BroadcastReceiver? = null
    private var adapter: BluetoothAdapter? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)

        val btManager = getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        adapter = btManager?.adapter

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, methodChannelName)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "isSupported" -> result.success(adapter != null)
                    "startDiscovery" -> result.success(startDiscovery())
                    "stopDiscovery" -> {
                        stopDiscovery()
                        result.success(true)
                    }
                    else -> result.notImplemented()
                }
            }

        EventChannel(flutterEngine.dartExecutor.binaryMessenger, eventChannelName)
            .setStreamHandler(object : EventChannel.StreamHandler {
                override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                    eventSink = events
                }

                override fun onCancel(arguments: Any?) {
                    eventSink = null
                }
            })
    }

    private fun startDiscovery(): Boolean {
        val a = adapter ?: return false
        registerReceiverIfNeeded()
        return try {
            if (a.isDiscovering) a.cancelDiscovery()
            a.startDiscovery()
        } catch (e: SecurityException) {
            false
        }
    }

    private fun stopDiscovery() {
        try {
            adapter?.cancelDiscovery()
        } catch (_: SecurityException) {
        }
    }

    private fun registerReceiverIfNeeded() {
        if (receiver != null) return
        receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                when (intent?.action) {
                    BluetoothDevice.ACTION_FOUND -> {
                        @Suppress("DEPRECATION")
                        val device: BluetoothDevice? =
                            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                        val rssi = intent
                            .getShortExtra(BluetoothDevice.EXTRA_RSSI, Short.MIN_VALUE)
                            .toInt()
                        if (device != null) {
                            val name = try {
                                device.name
                            } catch (e: SecurityException) {
                                null
                            }
                            // Needs BLUETOOTH_CONNECT on Android 12+; never crash the
                            // receiver if it was revoked.
                            val btClass = try {
                                device.bluetoothClass
                            } catch (e: SecurityException) {
                                null
                            }
                            eventSink?.success(
                                hashMapOf<String, Any?>(
                                    "address" to device.address,
                                    "name" to name,
                                    "rssi" to rssi,
                                    "deviceClass" to (btClass?.deviceClass ?: 0),
                                    "majorClass" to (btClass?.majorDeviceClass ?: 0)
                                )
                            )
                        }
                    }
                    BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> {
                        eventSink?.success(hashMapOf<String, Any?>("event" to "finished"))
                    }
                }
            }
        }
        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
        }
        registerReceiver(receiver, filter)
    }

    override fun onDestroy() {
        try {
            receiver?.let { unregisterReceiver(it) }
        } catch (_: Exception) {
        }
        receiver = null
        stopDiscovery()
        super.onDestroy()
    }
}
