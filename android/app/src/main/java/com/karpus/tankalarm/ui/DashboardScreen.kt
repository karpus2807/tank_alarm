package com.karpus.tankalarm.ui

import android.Manifest
import android.content.Context
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.karpus.tankalarm.SirenService
import com.karpus.tankalarm.data.DeviceApi
import com.karpus.tankalarm.data.DeviceFinder
import com.karpus.tankalarm.data.DeviceStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private enum class Page { Home, Readings, Settings }
private enum class Side { Tank, Alarm }

@Composable
fun DashboardScreen(api: DeviceApi = remember { DeviceApi() }) {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences("links", Context.MODE_PRIVATE) }
    var tankUrl by remember { mutableStateOf(prefs.getString("tank", "http://192.168.0.104:5005")!!) }
    var alarmUrl by remember { mutableStateOf(prefs.getString("alarm", "http://192.168.5.1:5005")!!) }
    var tank by remember { mutableStateOf(Readout(null, "waiting")) }
    var alarm by remember { mutableStateOf(Readout(null, "waiting")) }
    var hunt by remember { mutableStateOf("") }
    var searchTick by remember { mutableStateOf(0) }
    var page by remember { mutableStateOf(Page.Home) }
    var side by remember { mutableStateOf(Side.Tank) }
    var menu by remember { mutableStateOf(false) }
    var muteHold by remember { mutableStateOf(prefs.getInt(SirenService.HOLD, -1).takeIf { it == 0 || it == 1 }) }
    var muteBusy by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val askNotify = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    val finder = remember { DeviceFinder(context) }
    val readout = if (side == Side.Tank) tank else alarm

    LaunchedEffect(Unit) {
        if (Build.VERSION.SDK_INT >= 33) askNotify.launch(Manifest.permission.POST_NOTIFICATIONS)
    }
    LaunchedEffect(alarm.status?.muted) {
        val held = prefs.getInt(SirenService.HOLD, -1)
        if ((held == 0 || held == 1) && alarm.status?.muted == held) {
            prefs.edit().remove(SirenService.HOLD).apply()
            muteHold = null
        }
    }
    LaunchedEffect(tankUrl) { prefs.edit().putString("tank", tankUrl).apply() }
    LaunchedEffect(alarmUrl) { prefs.edit().putString("alarm", alarmUrl).apply() }
    LaunchedEffect(searchTick) {
        if (searchTick == 0) return@LaunchedEffect
        hunt = "looking"
        val hits = finder.find()
        hits.forEach { hit ->
            if (hit.role == "tank") tankUrl = hit.url else alarmUrl = hit.url
        }
        hunt = if (hits.isEmpty()) "not found yet" else ""
    }
    LaunchedEffect(Unit) {
        delay(2500)
        if (tank.status == null || alarm.status == null) searchTick += 1
    }
    LaunchedEffect(tankUrl, alarmUrl) {
        while (true) {
            tank = keep(tank, read(api, tankUrl))
            alarm = keep(alarm, read(api, alarmUrl))
            delay(2000)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFFE6EEF1))
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        val bothLive = tank.status != null && tank.error == null && alarm.status != null && alarm.error == null
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box {
                if (page == Page.Home) {
                    TextButton(onClick = { menu = true }) { Text("☰", fontSize = 22.sp, color = Color(0xFF102027)) }
                    DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                        DropdownMenuItem(text = { Text("Tank") }, onClick = {
                            side = Side.Tank
                            menu = false
                        })
                        DropdownMenuItem(text = { Text("Alarm") }, onClick = {
                            side = Side.Alarm
                            menu = false
                        })
                    }
                } else {
                    TextButton(onClick = { page = Page.Home }) { Text("←", fontSize = 22.sp, color = Color(0xFF102027)) }
                }
            }
            Text(
                when {
                    page == Page.Settings && side == Side.Tank -> "Tank settings"
                    page == Page.Settings -> "Alarm settings"
                    page == Page.Readings && side == Side.Tank -> "Tank"
                    page == Page.Readings -> "Alarm"
                    side == Side.Tank -> "Overhead tank"
                    else -> "Alarm"
                },
                modifier = Modifier.weight(1f),
                textAlign = TextAlign.Center,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
            )
            if (page == Page.Settings) {
                Spacer(Modifier.size(48.dp))
            } else {
                TextButton(onClick = { page = Page.Settings }) { Text("⚙", fontSize = 22.sp, color = Color(0xFF102027)) }
            }
        }

        when (page) {
            Page.Home -> {
                if (side == Side.Tank) {
                    Card(
                        onClick = { page = Page.Readings },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(28.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0xFF0C1C27)),
                    ) {
                        Column(Modifier.padding(bottom = 16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                            TankGraphic(percent = tank.status?.percent?.toFloat(), sensorOn = tank.status?.level == 1)
                            Text(
                                tank.status?.percent?.let { "${it.toInt()}%" } ?: "--",
                                style = MaterialTheme.typography.displaySmall,
                                color = Color.White,
                                fontWeight = FontWeight.Bold,
                            )
                        }
                    }
                } else {
                    val shownMuted = muteHold ?: alarm.status?.muted
                    Card(
                        onClick = { page = Page.Readings },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(28.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0xFF0C1C27)),
                    ) {
                        AlarmGraphic(
                            sounding = shownMuted != 1 && alarmSounding(alarm.status),
                            relayOn = shownMuted != 1 && (alarm.status?.relayOut?.let { it == 1 } ?: alarmRelay(alarm.status)),
                        )
                    }
                }
                val shownMuted = muteHold ?: alarm.status?.muted
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterHorizontally),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    if (bothLive) {
                        Box(
                            Modifier
                                .clip(RoundedCornerShape(20.dp))
                                .background(Color(0xFF1B7A3A))
                                .padding(horizontal = 18.dp, vertical = 8.dp),
                        ) {
                            Text("Live", color = Color.White, fontWeight = FontWeight.Bold)
                        }
                    }
                    if (alarm.status != null) {
                        Button(
                            onClick = {
                                val next = if (shownMuted == 1) 0 else 1
                                muteHold = next
                                prefs.edit().putInt(SirenService.HOLD, next).apply()
                                muteBusy = true
                                scope.launch {
                                    val ok = withContext(Dispatchers.IO) {
                                        runCatching { api.mute(alarmUrl, next) }.isSuccess
                                    }
                                    muteBusy = false
                                    if (!ok) {
                                        prefs.edit().remove(SirenService.HOLD).apply()
                                        muteHold = null
                                    }
                                }
                            },
                            enabled = !muteBusy,
                        ) { Text(if (shownMuted == 1) "Unmute" else "Mute") }
                    }
                    if (!bothLive) {
                        Button(onClick = { searchTick += 1 }) { Text("Find both devices") }
                    }
                }
                if (!bothLive && hunt.isNotBlank()) Text(hunt, color = Color(0xFF37515C))
                OutlinedTextField(tankUrl, { tankUrl = it }, label = { Text("Tank address") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
                OutlinedTextField(alarmUrl, { alarmUrl = it }, label = { Text("Alarm address") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            }
            Page.Readings -> ReadingsPage(side == Side.Tank, readout.status, readout.error)
            Page.Settings -> SettingsPage(api, side == Side.Tank, if (side == Side.Tank) tankUrl else alarmUrl)
        }
    }
}

private data class Readout(val status: DeviceStatus?, val error: String?)

private fun keep(previous: Readout, next: Readout): Readout {
    if (next.status != null) return next
    val dropped = next.error?.contains("end of stream", ignoreCase = true) == true
    return if (dropped && previous.status != null) previous else next
}

private suspend fun read(api: DeviceApi, url: String): Readout = withContext(Dispatchers.IO) {
    runCatching { Readout(api.status(url), null) }.getOrElse { Readout(null, it.message ?: "no reply") }
}
