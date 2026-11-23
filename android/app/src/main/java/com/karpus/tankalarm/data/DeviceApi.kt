package com.karpus.tankalarm.data

import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.util.concurrent.TimeUnit

class DeviceApi(
    private val client: OkHttpClient = OkHttpClient.Builder()
        .callTimeout(4, TimeUnit.SECONDS)
        .build(),
) {
    fun status(baseUrl: String): DeviceStatus {
        val root = baseUrl.trim().trimEnd('/')
        val request = Request.Builder().url("$root/api/status").build()
        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                error("HTTP ${response.code}")
            }
            val body = response.body?.string().orEmpty()
            return parse(JSONObject(body))
        }
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
        )
    }
}
