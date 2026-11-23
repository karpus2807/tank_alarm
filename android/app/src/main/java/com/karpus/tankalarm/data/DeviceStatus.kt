package com.karpus.tankalarm.data

data class DeviceStatus(
    val role: String,
    val mode: String,
    val ip: String,
    val hotspot: String,
    val cm: Double?,
    val rawCm: Double?,
    val percent: Double?,
    val level: Int?,
    val stable: Int?,
    val muted: Int?,
    val link: Int?,
    val boardC: Double?,
    val spread: Double? = null,
    val buzz: Int? = null,
    val relay: Int? = null,
    val relayHigh: Int? = null,
    val relayOut: Int? = null,
)
