package com.karpus.tankalarm

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.net.Uri
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import com.karpus.tankalarm.data.DeviceApi
import com.karpus.tankalarm.data.DeviceStatus
import com.karpus.tankalarm.ui.alarmRelay
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class SirenService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private var player: MediaPlayer? = null
    private var wake: PowerManager.WakeLock? = null
    private var playing = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        ensureChannel()
        val note = note(false)
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTE_ID, note, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(NOTE_ID, note)
        }
        if (job == null) job = scope.launch { watch() }
        return START_STICKY
    }

    override fun onDestroy() {
        job?.cancel()
        stopSiren()
        super.onDestroy()
    }

    private suspend fun watch() {
        val api = DeviceApi()
        while (currentCoroutineContext().isActive) {
            val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
            val url = prefs.getString("alarm", "http://192.168.5.1:5005").orEmpty()
            val hold = prefs.getInt(HOLD, -1)
            val status = runCatching { api.status(url) }.getOrNull()
            if (status != null && (hold == 0 || hold == 1) && status.muted == hold) {
                prefs.edit().remove(HOLD).apply()
            }
            val on = phoneSiren(status, prefs.getInt(HOLD, -1))
            withContext(Dispatchers.Main) {
                if (on) startSiren() else stopSiren()
                notify(on)
            }
            delay(2000)
        }
    }

    private fun startSiren() {
        if (playing && player?.isPlaying == true) return
        stopSiren()
        val mp = MediaPlayer()
        mp.setAudioAttributes(
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ALARM)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build(),
        )
        mp.setDataSource(this, Uri.parse("android.resource://$packageName/${R.raw.siren}"))
        mp.isLooping = true
        mp.prepare()
        mp.start()
        player = mp
        playing = true
        val power = getSystemService(PowerManager::class.java)
        val lock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "tankalarm:siren")
        lock.setReferenceCounted(false)
        lock.acquire(15 * 60 * 1000L)
        wake = lock
    }

    private fun stopSiren() {
        playing = false
        player?.runCatching {
            stop()
            release()
        }
        player = null
        if (wake?.isHeld == true) wake?.release()
        wake = null
    }

    private fun ensureChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(CHANNEL, "Tank alarm", NotificationManager.IMPORTANCE_HIGH)
        channel.setSound(null, null)
        channel.description = "Siren while the tank relay is on"
        manager.createNotificationChannel(channel)
    }

    private fun notify(alarming: Boolean) {
        getSystemService(NotificationManager::class.java).notify(NOTE_ID, note(alarming))
    }

    private fun note(alarming: Boolean): Notification {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_warning)
            .setContentTitle(if (alarming) "Tank full" else "Tank alarm")
            .setContentText(if (alarming) "Relay is on. Siren is sounding." else "Watching the alarm.")
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setCategory(if (alarming) Notification.CATEGORY_ALARM else Notification.CATEGORY_SERVICE)
            .build()
    }

    companion object {
        const val PREFS = "links"
        const val HOLD = "mute_hold"
        private const val CHANNEL = "tank-siren"
        private const val NOTE_ID = 41
    }
}

fun phoneSiren(status: DeviceStatus?, hold: Int): Boolean {
    val muted = if (hold == 0 || hold == 1) hold else status?.muted
    if (muted == 1 || status == null) return false
    return when (status.relayOut) {
        1 -> true
        0 -> false
        else -> alarmRelay(status.copy(muted = 0))
    }
}
