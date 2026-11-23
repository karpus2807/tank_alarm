package com.karpus.tankalarm.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.karpus.tankalarm.data.DeviceStatus
import kotlin.math.abs
import kotlin.math.sin

@Composable
fun AlarmGraphic(sounding: Boolean, relayOn: Boolean) {
    val pulse by rememberInfiniteTransition(label = "alarm").animateFloat(
        initialValue = 0f,
        targetValue = 6.2832f,
        animationSpec = infiniteRepeatable(tween(700, easing = LinearEasing), RepeatMode.Restart),
        label = "pulse",
    )
    val gap by animateFloatAsState(if (relayOn) 0f else 18f, tween(220), label = "relay")
    Canvas(Modifier.fillMaxWidth().height(360.dp)) {
        val speaker = Offset(size.width * 0.32f, size.height * 0.48f)
        val body = 78f
        if (sounding) {
            val beat = (sin(pulse) + 1f) / 2f
            for (i in 0 until 3) {
                val radius = body + 18f + i * 22f + beat * 10f
                drawCircle(
                    color = Color(0xFFFFC107).copy(alpha = 0.35f - i * 0.08f),
                    radius = radius,
                    center = speaker,
                    style = Stroke(width = 4f),
                )
            }
        }
        drawCircle(
            brush = Brush.radialGradient(
                listOf(Color(0xFF455A64), Color(0xFF102027)),
                center = speaker,
                radius = body,
            ),
            radius = body,
            center = speaker,
        )
        drawCircle(Color(0xFF263238), body * 0.72f, speaker)
        val cone = Path().apply {
            moveTo(speaker.x - 16f, speaker.y - 28f)
            lineTo(speaker.x + 22f, speaker.y)
            lineTo(speaker.x - 16f, speaker.y + 28f)
            close()
        }
        drawPath(cone, if (sounding) Color(0xFFFFC107) else Color(0xFF546E7A))
        drawCircle(if (sounding) Color(0xFFFFE082) else Color(0xFF37474F), 8f, speaker + Offset(10f, 0f))

        val box = Offset(size.width * 0.62f, size.height * 0.30f)
        val boxSize = Size(size.width * 0.28f, size.height * 0.42f)
        if (relayOn) {
            drawCircle(
                Color(0x664CAF50),
                radius = 36f,
                center = Offset(box.x + boxSize.width * 0.72f, box.y + 28f),
            )
        }
        drawRoundRect(
            brush = Brush.verticalGradient(listOf(Color(0xFF37474F), Color(0xFF10181C))),
            topLeft = box,
            size = boxSize,
            cornerRadius = CornerRadius(16f, 16f),
        )
        val led = Offset(box.x + boxSize.width * 0.72f, box.y + 28f)
        drawCircle(Color(0xFF212121), 12f, led)
        drawCircle(if (relayOn) Color(0xFF69F0AE) else Color(0xFF1B3A2A), 8f, led)
        val coil = Offset(box.x + boxSize.width * 0.34f, box.y + boxSize.height * 0.58f)
        drawCircle(Color(0xFF8D6E63), 22f, coil, style = Stroke(width = 7f))
        drawCircle(Color(0xFF5D4037), 10f, coil)
        val midY = coil.y + 46f
        val leftTip = coil.x - 28f
        val rightTip = coil.x + 28f + gap
        drawLine(Color(0xFFB0BEC5), Offset(leftTip - 16f, midY), Offset(leftTip, midY), 5f, StrokeCap.Round)
        drawLine(Color(0xFFECEFF1), Offset(leftTip, midY - 10f), Offset(leftTip, midY + 10f), 4f, StrokeCap.Round)
        drawLine(Color(0xFFB0BEC5), Offset(rightTip, midY), Offset(rightTip + 16f, midY), 5f, StrokeCap.Round)
        drawLine(Color(0xFFECEFF1), Offset(rightTip, midY - 10f), Offset(rightTip, midY + 10f), 4f, StrokeCap.Round)
        if (relayOn) {
            drawCircle(Color(0xFFFFEE58), 4f, Offset((leftTip + rightTip) / 2f, midY))
        }
    }
}

fun alarmSounding(status: DeviceStatus?): Boolean {
    val state = status ?: return false
    if (state.buzz == 1) return true
    if (state.buzz == 2 || state.muted == 1 || state.link != 1) return false
    val pct = state.percent
    return state.level == 1 || (pct != null && (pct >= 80.0 || abs(pct - 10.0) <= 1.0))
}

fun alarmRelay(status: DeviceStatus?): Boolean {
    val state = status ?: return false
    if (state.relay == 1) return true
    if (state.relay == 2 || state.muted == 1 || state.link != 1) return false
    val pct = state.percent
    return state.level == 1 || (pct != null && pct >= 95.0)
}
