package com.karpus.tankalarm.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.wifi.WifiManager
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.SocketTimeoutException
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext

/**
 * Current firmware already names both MACs in the UDP packet on port 5005.
 * The HTTP status page does not include a MAC, so a LAN sweep can only match role.
 */
class DeviceFinder(context: Context) {
    private val app = context.applicationContext
    private val probe = OkHttpClient.Builder()
        .connectTimeout(250, TimeUnit.MILLISECONDS)
        .readTimeout(400, TimeUnit.MILLISECONDS)
        .callTimeout(700, TimeUnit.MILLISECONDS)
        .build()

    data class Hit(val role: String, val url: String, val macs: String, val confirmed: Boolean)

    suspend fun find(): List<Hit> = withContext(Dispatchers.IO) {
        val found = linkedMapOf<String, Hit>()
        coroutineScope {
            val udp = async { listenUdp() }
            val http = async { sweepLan() }
            (udp.await() + http.await()).forEach { hit ->
                val current = found[hit.role]
                if (current == null || (hit.confirmed && !current.confirmed)) found[hit.role] = hit
            }
        }
        found.values.toList()
    }

    private fun listenUdp(): List<Hit> {
        val wifi = app.getSystemService(WifiManager::class.java)
        val lock = wifi?.createMulticastLock("tank-alarm")
        lock?.setReferenceCounted(false)
        lock?.acquire()
        val hits = linkedMapOf<String, Hit>()
        val socket = DatagramSocket(null)
        try {
            socket.reuseAddress = true
            socket.broadcast = true
            socket.soTimeout = 800
            socket.bind(InetSocketAddress(5005))
            val deadline = System.currentTimeMillis() + 3500
            val buf = ByteArray(1200)
            while (System.currentTimeMillis() < deadline) {
                val packet = DatagramPacket(buf, buf.size)
                try {
                    socket.receive(packet)
                } catch (_: SocketTimeoutException) {
                    continue
                }
                val text = String(packet.data, 0, packet.length, Charsets.UTF_8)
                val ip = packet.address?.hostAddress ?: continue
                classify(text, ip, fromUdp = true)?.let { hits.putIfAbsent(it.role, it) }
            }
        } catch (_: Exception) {
        } finally {
            socket.close()
            if (lock?.isHeld == true) lock.release()
        }
        return hits.values.toList()
    }

    private suspend fun sweepLan(): List<Hit> = coroutineScope {
        val prefix = lanPrefix() ?: return@coroutineScope emptyList()
        val hits = mutableListOf<Hit>()
        (1..254).chunked(8).forEach { hosts ->
            hits += hosts.map { host ->
                async(Dispatchers.IO) {
                    val ip = "$prefix.$host"
                    val body = get("http://$ip:5005/api/status") ?: return@async null
                    classify(body, ip, fromUdp = false)
                }
            }.awaitAll().filterNotNull()
            if (hits.any { it.role == "tank" } && hits.any { it.role == "alarm" }) return@coroutineScope hits
        }
        hits
    }

    private fun get(url: String): String? = try {
        val request = Request.Builder().url(url).build()
        probe.newCall(request).execute().use { response ->
            if (!response.isSuccessful) null else response.body?.string()
        }
    } catch (_: Exception) {
        null
    }

    private fun classify(json: String, ip: String, fromUdp: Boolean): Hit? {
        val obj = try {
            JSONObject(json)
        } catch (_: Exception) {
            return null
        }
        val seen = listOf(obj.optString("mac"), obj.optString("lan"), obj.optString("wifi"))
            .map { it.trim().uppercase() }
            .filter { it.length >= 12 }
            .map { it.take(12) }
        val role = when {
            seen.any { it in TANK } -> "tank"
            seen.any { it in ALARM } -> "alarm"
            obj.optString("role") == "tank" -> "tank"
            obj.optString("role") == "alarm" -> "alarm"
            else -> return null
        }
        val confirmed = seen.any { it in if (role == "tank") TANK else ALARM }
        if (fromUdp && !confirmed) return null
        val macs = if (confirmed) seen.filter { it in TANK || it in ALARM }.distinct().joinToString(" ") else "not in HTTP status"
        return Hit(role, "http://$ip:5005", macs, confirmed)
    }

    private fun lanPrefix(): String? {
        val cm = app.getSystemService(ConnectivityManager::class.java) ?: return null
        val props = cm.getLinkProperties(cm.activeNetwork) ?: return null
        val ip = props.linkAddresses.firstOrNull { it.address is Inet4Address && !it.address.isLoopbackAddress }
            ?.address?.hostAddress ?: return null
        val parts = ip.split(".")
        if (parts.size != 4) return null
        return parts.take(3).joinToString(".")
    }

    private companion object {
        val TANK = setOf("0254414E4B01", "14C19FC8E058")
        val ALARM = setOf("02414C524D01", "007007E1BCD4")
    }
}
