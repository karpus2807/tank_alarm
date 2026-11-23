package com.karpus.tankalarm.data

data class BeepFields(
    val pct: String = "",
    val count: String = "",
    val onMs: String = "",
    val gap: String = "",
    val group: String = "",
    val times: String = "",
)

data class PortalForm(
    val tank: Boolean,
    val mode: String,
    val ssid: String,
    val empty: String,
    val full: String,
    val reset: String,
    val low: BeepFields,
    val mid: BeepFields,
    val high: BeepFields,
    val fullAlarm: BeepFields,
) {
    fun saveFields(pass: String, joinTank: Boolean): Map<String, String> {
        val fields = linkedMapOf("mode" to mode, "ssid" to ssid, "pass" to pass)
        if (!tank && joinTank) fields["tankap"] = "1"
        if (!tank) return fields
        fields["empty"] = empty
        fields["full"] = full
        fields["rb"] = reset
        fields.putBeep("l", "low", low)
        fields.putBeep("m", "mid", mid)
        fields.putBeep("h", "high", high)
        fields.putBeep("c", "crit", fullAlarm)
        return fields
    }
}

fun parsePortal(html: String): PortalForm {
    val inputs = Regex("<input\\b([^>]*)>", RegexOption.IGNORE_CASE)
        .findAll(html)
        .map { attrs(it.groupValues[1]) }
        .toList()
    fun value(name: String) = unescape(inputs.firstOrNull { it["name"] == name }?.get("value").orEmpty())
    fun beep(prefix: String, pctName: String) = BeepFields(
        pct = value(pctName),
        count = value("${prefix}n"),
        onMs = value("${prefix}o"),
        gap = value("${prefix}i"),
        group = value("${prefix}g"),
        times = value("${prefix}t"),
    )
    val mode = inputs.firstOrNull { it["name"] == "mode" && it.containsKey("checked") }?.get("value")
        ?: inputs.firstOrNull { it["name"] == "mode" }?.get("value")
        ?: "lan"
    return PortalForm(
        tank = html.contains("<h2>Tank</h2>"),
        mode = mode,
        ssid = value("ssid"),
        empty = value("empty"),
        full = value("full"),
        reset = value("rb"),
        low = beep("l", "low"),
        mid = beep("m", "mid"),
        high = beep("h", "high"),
        fullAlarm = beep("c", "crit"),
    )
}

private fun MutableMap<String, String>.putBeep(prefix: String, pctName: String, beep: BeepFields) {
    this[pctName] = beep.pct
    this["${prefix}n"] = beep.count
    this["${prefix}o"] = beep.onMs
    this["${prefix}i"] = beep.gap
    this["${prefix}g"] = beep.group
    this["${prefix}t"] = beep.times
}

private fun attrs(raw: String): Map<String, String> {
    val map = linkedMapOf<String, String>()
    val re = Regex("""([A-Za-z0-9_-]+)(?:\s*=\s*(?:'([^']*)'|"([^"]*)"|([^\s'"=<>`]+)))?""")
    for (match in re.findAll(raw)) {
        val key = match.groupValues[1].lowercase()
        val value = listOf(match.groupValues[2], match.groupValues[3], match.groupValues[4]).firstOrNull { it.isNotEmpty() }
        map[key] = value ?: "1"
    }
    return map
}

private fun unescape(value: String) = value
    .replace("&quot;", "\"")
    .replace("&lt;", "<")
    .replace("&gt;", ">")
    .replace("&amp;", "&")
