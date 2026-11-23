package com.karpus.tankalarm.data

import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

class DeviceApi {
    fun status(baseUrl: String): DeviceStatus {
        var last: Exception? = null
        repeat(2) {
            try {
                val (code, body) = exchange("GET", baseUrl, "/api/status", null)
                val start = body.indexOf('{')
                val end = body.lastIndexOf('}')
                if (code == 200 && start >= 0 && end > start) {
                    return parse(JSONObject(body.substring(start, end + 1)))
                }
                error(if (code == 0) "no reply" else "HTTP $code")
            } catch (e: Exception) {
                last = e
            }
        }
        throw last ?: IllegalStateException("no reply")
    }

    private fun parse(json: JSONObject): DeviceStatus {
        fun num(key: String): Double? = if (json.has(key) && !json.isNull(key)) json.optDouble(key) else null
        fun whole(key: String): Int? = if (json.has(key) && !json.isNull(key)) json.optInt(key) else null
        return DeviceStatus(
            role = json.optString("role"),
            mode = json.optString("mode"),
            ip = json.optString("ip"),
            hotspot = json.optString("ap"),
            cm = num("cm")?.takeUnless { it < 0 },
            rawCm = num("raw")?.takeUnless { it < 0 },
            percent = num("pct")?.takeUnless { it < 0 },
            level = whole("lvl"),
            stable = whole("stable"),
            muted = whole("muted"),
            link = whole("link"),
            boardC = num("temp")?.takeUnless { it < 0 },
            spread = num("spread"),
            buzz = whole("buzz"),
            relay = whole("relay"),
            relayHigh = whole("rlyh"),
            relayOut = whole("out"),
        )
    }

    fun mute(baseUrl: String, muted: Int): String = post(baseUrl, "/mute", mapOf("muted" to muted.toString()))

    fun page(baseUrl: String): String = get(baseUrl, "/")

    fun save(baseUrl: String, fields: Map<String, String>): String = post(baseUrl, "/save", fields)

    fun calibrate(baseUrl: String, fields: Map<String, String>): String = post(baseUrl, "/calibrate", fields)

    fun test(baseUrl: String, act: String): String = post(baseUrl, "/test", mapOf("act" to act))

    fun reset(baseUrl: String): String = post(baseUrl, "/reset", mapOf("go" to "1"))

    fun wifi(baseUrl: String, start: Boolean): String = get(baseUrl, if (start) "/api/wifi?start=1" else "/api/wifi")

    fun flash(baseUrl: String, name: String, bytes: ByteArray): String {
        val body = MultipartBody.Builder()
            .setType(MultipartBody.FORM)
            .addFormDataPart("firmware", name, bytes.toRequestBody("application/octet-stream".toMediaType()))
            .build()
        val request = Request.Builder().url(root(baseUrl) + "/update").post(body).build()
        flashClient.newCall(request).execute().use { response ->
            val text = response.body?.string().orEmpty().trim()
            if (!response.isSuccessful) error(text.ifBlank { "HTTP ${response.code}" })
            return text.ifBlank { "ok" }
        }
    }

    private fun get(baseUrl: String, path: String): String {
        val (code, body) = exchange("GET", baseUrl, path, null)
        if (body.isBlank()) error(if (code == 0) "no reply" else "HTTP $code")
        if (code >= 400) error(messageOf(body).ifBlank { "HTTP $code" })
        return body
    }

    private fun post(baseUrl: String, path: String, fields: Map<String, String>): String {
        val encoded = fields.entries.joinToString("&") { (key, value) ->
            URLEncoder.encode(key, "UTF-8") + "=" + URLEncoder.encode(value, "UTF-8")
        }
        val (code, body) = exchange("POST", baseUrl, path, encoded)
        if (code >= 400) error(messageOf(body).ifBlank { "HTTP $code" })
        if (code == 0 && body.isBlank()) error("no reply")
        return messageOf(body).ifBlank { "ok" }
    }

    private fun exchange(method: String, baseUrl: String, path: String, form: String?): Pair<Int, String> {
        val root = baseUrl.trim().trimEnd('/').removePrefix("http://").removePrefix("https://")
        val hostPort = root.substringBefore('/')
        val host = hostPort.substringBefore(':')
        val port = hostPort.substringAfter(':', "80").toInt()
        val target = if (path.startsWith("/")) path else "/$path"
        Socket().use { socket ->
            socket.connect(InetSocketAddress(host, port), 4000)
            socket.soTimeout = 5000
            val payload = form?.toByteArray(Charsets.UTF_8)
            val head = buildString {
                append(method).append(' ').append(target).append(" HTTP/1.0\r\n")
                append("Host: ").append(host).append(':').append(port).append("\r\n")
                append("Connection: close\r\n")
                if (payload != null) {
                    append("Content-Type: application/x-www-form-urlencoded\r\n")
                    append("Content-Length: ").append(payload.size).append("\r\n")
                }
                append("\r\n")
            }
            val out = socket.getOutputStream()
            out.write(head.toByteArray(Charsets.UTF_8))
            if (payload != null) out.write(payload)
            out.flush()
            val data = java.io.ByteArrayOutputStream()
            val buf = ByteArray(1024)
            while (true) {
                val n = try {
                    socket.getInputStream().read(buf)
                } catch (_: SocketTimeoutException) {
                    break
                }
                if (n <= 0) break
                data.write(buf, 0, n)
            }
            val raw = data.toString(Charsets.UTF_8)
            val split = raw.indexOf("\r\n\r\n")
            if (split < 0) return 0 to raw
            val code = raw.substringBefore(' ').let { status ->
                raw.substring(status.length).trimStart().substringBefore(' ').toIntOrNull() ?: 0
            }
            return code to raw.substring(split + 4)
        }
    }

    private fun root(baseUrl: String) = baseUrl.trim().trimEnd('/')

    private val flashClient = OkHttpClient.Builder().callTimeout(3, TimeUnit.MINUTES).build()
}

private fun messageOf(html: String): String {
    val plain = html.replace(Regex("<[^>]+>"), " ")
        .replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace(Regex("\\s+"), " ")
        .trim()
    val direct = Regex("Saved[^.]*\\.|Reset[^.]*\\.|Calibrat[^.]*\\.").find(plain)?.value
    if (direct != null) return direct.trim()
    val bold = Regex("<b>(.*?)</b>", RegexOption.IGNORE_CASE).findAll(html)
        .map { it.groupValues[1] }
        .firstOrNull { it.isNotBlank() && !it.contains("http", ignoreCase = true) }
    return bold ?: plain.take(180)
}
