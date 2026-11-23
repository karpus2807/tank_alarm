package com.karpus.tankalarm.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.karpus.tankalarm.data.DeviceStatus

@Composable
fun ReadingsPage(tankSide: Boolean, status: DeviceStatus?, error: String?) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = Color.White),
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Text(if (tankSide) "Tank" else "Alarm", fontWeight = FontWeight.Bold, style = MaterialTheme.typography.titleMedium)
                Box(
                    Modifier
                        .clip(RoundedCornerShape(20.dp))
                        .background(if (error == null) Color(0xFFE5F6EA) else Color(0xFFFDECEA))
                        .padding(horizontal = 10.dp, vertical = 4.dp),
                ) {
                    Text(
                        error ?: "live",
                        color = if (error == null) Color(0xFF1B7A3A) else Color(0xFFB71C1C),
                        style = MaterialTheme.typography.labelMedium,
                    )
                }
            }
            if (tankSide) {
                Stat("Distance", cm(status?.cm))
                Stat("Raw echo", cm(status?.rawCm))
                Stat("Level", status?.percent?.let { "${it.toInt()}%" } ?: "--")
                Stat("Level sensor", status?.level?.toString() ?: "--")
                Stat("Board", status?.boardC?.let { "%.1f C".format(it) } ?: "--")
                Stat("Reading", when (status?.stable) {
                    1 -> "steady"
                    0 -> "settling"
                    else -> "--"
                })
                Stat("Spread", status?.spread?.let { "%.1f cm".format(it) } ?: "--")
                Stat("Link", onOff(status?.link))
            } else {
                Stat("Sees", status?.percent?.let { "${it.toInt()}%" } ?: "--")
                Stat("Distance", cm(status?.cm))
                Stat("Sensor", status?.level?.toString() ?: "--")
                Stat("Muted", onOff(status?.muted))
                Stat("Link", onOff(status?.link))
                Stat("Buzzer", hold(status?.buzz))
                Stat("Relay", hold(status?.relay))
                Stat("Relay trigger", when (status?.relayHigh) {
                    1 -> "HIGH"
                    0 -> "LOW"
                    else -> "--"
                })
            }
        }
    }
}

@Composable
private fun Stat(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = Color(0xFF5C717A))
        Text(value, fontWeight = FontWeight.Medium)
    }
}

private fun cm(value: Double?): String = value?.let { "%.1f cm".format(it) } ?: "--"

private fun onOff(value: Int?) = when (value) {
    1 -> "yes"
    0 -> "no"
    else -> "--"
}

private fun hold(value: Int?) = when (value) {
    1 -> "manual on"
    2 -> "manual off"
    0 -> "following alarm"
    else -> "--"
}
