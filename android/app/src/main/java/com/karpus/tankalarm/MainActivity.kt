package com.karpus.tankalarm

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.karpus.tankalarm.data.DeviceApi
import com.karpus.tankalarm.data.DeviceStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    PlatformScreen()
                }
            }
        }
    }
}

@Composable
private fun PlatformScreen(api: DeviceApi = remember { DeviceApi() }) {
    var tankUrl by remember { mutableStateOf("http://192.168.4.1:5005") }
    var alarmUrl by remember { mutableStateOf("http://192.168.5.1:5005") }
    var tankText by remember { mutableStateOf("not checked") }
    var alarmText by remember { mutableStateOf("not checked") }
    val scope = rememberCoroutineScope()

    Column(
        modifier = Modifier.padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Tank Alarm", style = MaterialTheme.typography.headlineMedium)
        Text("Platform is ready. The live tank picture comes next.")
        OutlinedTextField(
            value = tankUrl,
            onValueChange = { tankUrl = it },
            label = { Text("Tank") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        OutlinedTextField(
            value = alarmUrl,
            onValueChange = { alarmUrl = it },
            label = { Text("Alarm") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )
        Button(onClick = {
            scope.launch {
                tankText = "checking"
                alarmText = "checking"
                tankText = readStatus(api, tankUrl)
                alarmText = readStatus(api, alarmUrl)
            }
        }) {
            Text("Check both devices")
        }
        Text("Tank: $tankText")
        Text("Alarm: $alarmText")
        Spacer(Modifier.height(8.dp))
    }
}

private suspend fun readStatus(api: DeviceApi, url: String): String = withContext(Dispatchers.IO) {
    runCatching { api.status(url).summary() }
        .getOrElse { it.message ?: "no reply" }
}

private fun DeviceStatus.summary(): String {
    val fill = percent?.let { "${it.toInt()}%" } ?: "--"
    val sensor = level?.toString() ?: "--"
    return "$role $mode $ip  level $fill  sensor $sensor"
}
