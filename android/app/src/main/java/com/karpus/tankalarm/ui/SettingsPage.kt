package com.karpus.tankalarm.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.karpus.tankalarm.data.BeepFields
import com.karpus.tankalarm.data.DeviceApi
import com.karpus.tankalarm.data.PortalForm
import com.karpus.tankalarm.data.parsePortal
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

@Composable
fun SettingsPage(api: DeviceApi, tankSide: Boolean, url: String) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var form by remember(tankSide, url) { mutableStateOf<PortalForm?>(null) }
    var pass by remember(tankSide, url) { mutableStateOf("") }
    var joinTank by remember(tankSide, url) { mutableStateOf(false) }
    var note by remember(tankSide, url) { mutableStateOf("reading settings") }
    var busy by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }
    var networks by remember { mutableStateOf<List<Pair<String, Int>>>(emptyList()) }
    var nowLevel by remember(tankSide, url) { mutableStateOf("") }
    val pickFirmware = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) scope.launch { flash(context, api, url, uri) { note = it; busy = it == "flashing" } }
    }

    LaunchedEffect(tankSide, url) {
        note = "reading settings"
        form = runCatching { withContext(Dispatchers.IO) { parsePortal(api.page(url)) } }
            .onFailure { note = it.message ?: "no settings" }
            .getOrNull()
        if (form != null) note = ""
    }

    val current = form
    if (current == null) {
        Text(note.ifBlank { "reading settings" })
        return
    }

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(if (current.tank) "Tank settings" else "Alarm settings", fontWeight = FontWeight.Bold)
        if (note.isNotBlank()) Text(note)
        Text("Link")
        val modes = if (current.tank) listOf("wifi" to "Home Wi-Fi", "lan" to "LAN", "ap" to "Hotspot") else listOf("lan" to "LAN", "wifi" to "Wi-Fi")
        modes.forEach { (value, label) ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                RadioButton(selected = current.mode == value, onClick = { form = current.copy(mode = value) })
                Text(label)
            }
        }
        if (!current.tank) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = joinTank, onCheckedChange = { joinTank = it })
                Text("Join tank hotspot")
            }
        }
        OutlinedTextField(current.ssid, { form = current.copy(ssid = it) }, label = { Text("Wi-Fi name") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
        OutlinedTextField(
            pass,
            { pass = it },
            label = { Text("Password") },
            placeholder = { Text("blank keeps old") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
        )
        Button(onClick = {
            scope.launch {
                busy = true
                note = "scanning"
                networks = scan(api, url)
                note = if (networks.isEmpty()) "no network found" else ""
                busy = false
            }
        }, enabled = !busy) { Text("Show Wi-Fi networks") }
        networks.forEach { (ssid, rssi) ->
            TextButton(onClick = {
                form = current.copy(ssid = ssid)
                if (!current.tank && ssid == "tank") joinTank = true
            }) { Text("$ssid   $rssi dBm") }
        }
        if (current.tank) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumberField("Empty cm", current.empty, Modifier.weight(1f)) { form = current.copy(empty = it) }
                NumberField("Full cm", current.full, Modifier.weight(1f)) { form = current.copy(full = it) }
            }
            NumberField("Auto reset below %", current.reset, Modifier.fillMaxWidth()) { form = current.copy(reset = it) }
            Text("Beeps", fontWeight = FontWeight.Bold)
            Text("Times 0 keeps going until mute.")
            BeepEditor("Low", current.low) { form = current.copy(low = it) }
            BeepEditor("Mid", current.mid) { form = current.copy(mid = it) }
            BeepEditor("High", current.high) { form = current.copy(high = it) }
            BeepEditor("Full", current.fullAlarm) { form = current.copy(fullAlarm = it) }
            Text("Ultrasonic calibration", fontWeight = FontWeight.Bold)
            NumberField("Current water %", nowLevel, Modifier.fillMaxWidth()) { nowLevel = it }
            Button(onClick = {
                scope.launch {
                    busy = true
                    note = runAction { api.calibrate(url, mapOf("cempty" to current.empty, "cfull" to current.full, "cnow" to nowLevel)) }
                    busy = false
                }
            }, enabled = !busy) { Text("Calibrate") }
        } else {
            Text("Output test", fontWeight = FontWeight.Bold)
            Text("A 2 second touch still mutes the buzzer and the relay.")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "buzzon") } } }, enabled = !busy) { Text("Buzzer on") }
                OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "buzzoff") } } }, enabled = !busy) { Text("Buzzer off") }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "relayon") } } }, enabled = !busy) { Text("Relay on") }
                OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "relayoff") } } }, enabled = !busy) { Text("Relay off") }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "rlyhigh") } } }, enabled = !busy) { Text("Active HIGH") }
                OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "rlylow") } } }, enabled = !busy) { Text("Active LOW") }
            }
            OutlinedButton(onClick = { scope.launch { note = runAction { api.test(url, "auto") } } }, enabled = !busy) { Text("Follow alarm") }
        }
        Button(onClick = {
            scope.launch {
                busy = true
                note = runAction { api.save(url, current.saveFields(pass, joinTank)) }
                busy = false
            }
        }, enabled = !busy) { Text("Save and reboot") }
        Text("Firmware update", fontWeight = FontWeight.Bold)
        Button(onClick = { pickFirmware.launch("*/*") }, enabled = !busy) { Text("Choose .bin and flash") }
        OutlinedButton(onClick = { confirmReset = true }, enabled = !busy) { Text("Factory reset") }
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Factory reset") },
            text = { Text("This clears saved settings and reboots to the hotspot.") },
            confirmButton = {
                TextButton(onClick = {
                    confirmReset = false
                    scope.launch {
                        busy = true
                        note = runAction { api.reset(url) }
                        busy = false
                    }
                }) { Text("Reset") }
            },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun NumberField(label: String, value: String, modifier: Modifier, onChange: (String) -> Unit) {
    OutlinedTextField(
        value,
        onChange,
        label = { Text(label) },
        modifier = modifier,
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
    )
}

@Composable
private fun BeepEditor(title: String, beep: BeepFields, onChange: (BeepFields) -> Unit) {
    Text(title, fontWeight = FontWeight.Medium)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        NumberField("%", beep.pct, Modifier.weight(1f)) { onChange(beep.copy(pct = it)) }
        NumberField("beeps", beep.count, Modifier.weight(1f)) { onChange(beep.copy(count = it)) }
        NumberField("on ms", beep.onMs, Modifier.weight(1f)) { onChange(beep.copy(onMs = it)) }
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        NumberField("gap", beep.gap, Modifier.weight(1f)) { onChange(beep.copy(gap = it)) }
        NumberField("group", beep.group, Modifier.weight(1f)) { onChange(beep.copy(group = it)) }
        NumberField("times", beep.times, Modifier.weight(1f)) { onChange(beep.copy(times = it)) }
    }
}

private suspend fun scan(api: DeviceApi, url: String): List<Pair<String, Int>> = withContext(Dispatchers.IO) {
    runCatching { api.wifi(url, start = true) }
    var found: List<Pair<String, Int>>? = null
    for (attempt in 1..12) {
        if (found != null) break
        kotlinx.coroutines.delay(1000)
        val body = runCatching { api.wifi(url, start = false) }.getOrNull() ?: continue
        val json = runCatching { JSONObject(body) }.getOrNull() ?: continue
        if (json.optInt("scanning") == 1) continue
        val list = json.optJSONArray("networks")
        found = if (list == null) {
            emptyList()
        } else {
            buildList {
                for (i in 0 until list.length()) {
                    val item = list.optJSONObject(i) ?: continue
                    add(item.optString("ssid") to item.optInt("rssi"))
                }
            }
        }
    }
    found ?: emptyList()
}

private suspend fun runAction(block: () -> String): String = withContext(Dispatchers.IO) {
    runCatching { block() }.getOrElse { it.message ?: "failed" }
}

private suspend fun flash(
    context: android.content.Context,
    api: DeviceApi,
    url: String,
    uri: Uri,
    report: (String) -> Unit,
) {
    report("flashing")
    val result = withContext(Dispatchers.IO) {
        runCatching {
            val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: error("file empty")
            api.flash(url, "firmware.bin", bytes)
        }.getOrElse { it.message ?: "flash failed" }
    }
    report(result)
}
