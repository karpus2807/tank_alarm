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
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.unit.dp
import kotlin.math.sin

@Composable
fun TankGraphic(percent: Float?, sensorOn: Boolean) {
    val animated by animateFloatAsState(
        targetValue = percent?.coerceIn(0f, 100f) ?: 0f,
        animationSpec = tween(900),
        label = "water",
    )
    val motion = rememberInfiniteTransition(label = "water-motion")
    val wave by motion.animateFloat(
        initialValue = 0f,
        targetValue = 6.2832f,
        animationSpec = infiniteRepeatable(tween(2200, easing = LinearEasing), RepeatMode.Restart),
        label = "wave",
    )
    val rise by motion.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(3200, easing = LinearEasing), RepeatMode.Restart),
        label = "bubbles",
    )
    val glow by motion.animateFloat(
        initialValue = 0.45f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(850), RepeatMode.Reverse),
        label = "led",
    )
    Canvas(modifier = Modifier.fillMaxWidth().height(430.dp)) {
        val rx = size.width * 0.27f
        val ry = rx * 0.30f
        val cx = size.width * 0.40f
        val top = size.height * 0.16f
        val bottom = size.height * 0.88f
        val bodyH = bottom - top
        val left = cx - rx
        val right = cx + rx
        val wall = 16f
        val innerLeft = left + wall
        val innerRight = right - wall
        val innerTop = top + 2f
        val innerBottom = bottom - 6f
        val innerH = innerBottom - innerTop

        drawOval(
            color = Color(0x66000000),
            topLeft = Offset(left - 8f, bottom - ry * 0.15f),
            size = Size(rx * 2.35f, ry * 1.7f),
        )
        drawOval(
            brush = Brush.verticalGradient(listOf(Color(0xFF6E8792), Color(0xFF24343C))),
            topLeft = Offset(left, bottom - ry),
            size = Size(rx * 2f, ry * 2f),
        )
        drawRect(
            brush = Brush.horizontalGradient(
                0f to Color(0xFF607680),
                0.16f to Color(0xFFC5D6DC),
                0.34f to Color(0xFFF4FBFD),
                0.62f to Color(0xFF9BB4BE),
                1f to Color(0xFF3E555F),
                startX = left,
                endX = right,
            ),
            topLeft = Offset(left, top),
            size = Size(rx * 2f, bodyH),
        )
        for (rib in 1..4) {
            val y = top + bodyH * rib / 5f
            drawLine(Color(0x33202A30), Offset(left + 2f, y), Offset(right - 2f, y), strokeWidth = 3f)
            drawLine(Color(0x55FFFFFF), Offset(left + 2f, y + 3f), Offset(right - 2f, y + 3f), strokeWidth = 1.5f)
        }

        val spanX = (innerRight - innerLeft).coerceAtLeast(1f)
        val amp = 5.5f
        val level = innerBottom - innerH * (animated / 100f)
        fun along(x: Float) = (x - innerLeft) / spanX
        fun surfaceAt(x: Float): Float {
            val t = along(x)
            return level + sin(wave + t * 10f) * amp + sin(wave * 1.8f + t * 19f) * 2f
        }

        clipRect(innerLeft, innerTop, innerRight, innerBottom) {
            drawRect(
                brush = Brush.horizontalGradient(
                    listOf(Color(0xFF163848), Color(0xFF245066), Color(0xFF142E3C)),
                ),
                topLeft = Offset(innerLeft, innerTop),
                size = Size(innerRight - innerLeft, innerH),
            )
            if (percent != null) {
                val water = Path().apply {
                    moveTo(innerLeft, innerBottom)
                    lineTo(innerLeft, surfaceAt(innerLeft))
                    val steps = 40
                    for (i in 0..steps) {
                        val x = innerLeft + (innerRight - innerLeft) * i / steps
                        lineTo(x, surfaceAt(x))
                    }
                    lineTo(innerRight, innerBottom)
                    close()
                }
                drawPath(
                    water,
                    Brush.verticalGradient(
                        listOf(Color(0xFF9BE7FF), Color(0xFF1E88C7), Color(0xFF0A3E68)),
                        startY = level - 8f,
                        endY = innerBottom,
                    ),
                )
                val span = innerBottom - level
                for (i in 0 until 8) {
                    val t = (rise + i * 0.13f) % 1f
                    val y = innerBottom - t * span
                    if (y < level + 6f) continue
                    val x = innerLeft + (innerRight - innerLeft) * (0.12f + (i * 0.11f) % 0.76f) +
                        sin(wave + i) * 5f
                    drawCircle(Color(0x66FFFFFF), 2.2f + (i % 3), Offset(x, y))
                }
                val steps = 48
                val crest = Path().apply {
                    moveTo(innerLeft, surfaceAt(innerLeft))
                    for (i in 1..steps) {
                        val x = innerLeft + spanX * i / steps
                        lineTo(x, surfaceAt(x))
                    }
                }
                val sheet = Path().apply {
                    moveTo(innerLeft, surfaceAt(innerLeft))
                    for (i in 1..steps) {
                        val x = innerLeft + spanX * i / steps
                        lineTo(x, surfaceAt(x))
                    }
                    for (i in steps downTo 0) {
                        val x = innerLeft + spanX * i / steps
                        val t = i / steps.toFloat()
                        val under = surfaceAt(x) + 4f + sin(-wave * 1.4f + t * 12f) * 1.5f
                        lineTo(x, under)
                    }
                    close()
                }
                drawOval(
                    brush = Brush.verticalGradient(
                        listOf(Color(0xFFD7F6FF), Color(0xFF2E96D0)),
                        startY = level - 6f,
                        endY = level + 4f,
                    ),
                    topLeft = Offset(innerLeft + 4f, level - 3f),
                    size = Size(spanX - 8f, 8f),
                )
                drawPath(sheet, Color(0x883EC4FF))
                drawPath(crest, Color(0xFFF7FDFF), style = Stroke(width = 2.5f, cap = StrokeCap.Round))
                val ripple = Path().apply {
                    moveTo(innerLeft, surfaceAt(innerLeft) + 6f)
                    for (i in 1..steps) {
                        val x = innerLeft + spanX * i / steps
                        lineTo(x, surfaceAt(x) + 6f)
                    }
                }
                drawPath(ripple, Color(0x66FFFFFF), style = Stroke(width = 1.5f, cap = StrokeCap.Round))
            }
            val mark = top + bodyH * 0.16f
            drawLine(
                color = if (sensorOn) Color(0x99FF8A80) else Color(0x66B0BEC5),
                start = Offset(innerLeft, mark),
                end = Offset(innerRight, mark),
                strokeWidth = 2f,
                pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 8f)),
            )
        }

        drawRect(
            brush = Brush.horizontalGradient(
                listOf(Color(0x66FFFFFF), Color(0x00FFFFFF)),
                startX = left + rx * 0.18f,
                endX = left + rx * 0.62f,
            ),
            topLeft = Offset(left, top),
            size = Size(rx * 0.42f, bodyH),
        )
        drawLine(Color(0xAA1B2A32), Offset(right - 1f, top), Offset(right - 1f, bottom), strokeWidth = 3f)
        drawLine(Color(0x88202A30), Offset(left, top), Offset(left, bottom), strokeWidth = 2f)

        drawOval(
            brush = Brush.verticalGradient(listOf(Color(0xFFF7FCFD), Color(0xFF8AA3AD))),
            topLeft = Offset(left - 6f, top - ry),
            size = Size(rx * 2f + 12f, ry * 2f),
        )
        drawOval(
            color = Color(0xFF102833),
            topLeft = Offset(left + wall * 0.7f, top - ry * 0.62f),
            size = Size(rx * 2f - wall * 1.4f, ry * 1.25f),
        )
        if (percent != null && animated > 90f) {
            drawOval(
                color = Color(0xFF7FD4F5),
                topLeft = Offset(left + wall, top - ry * 0.45f),
                size = Size(rx * 2f - wall * 2f, ry * 0.9f),
            )
        }
        drawOval(
            brush = Brush.verticalGradient(listOf(Color(0xFFFFFFFF), Color(0xFFB7C9D0))),
            topLeft = Offset(left + rx * 0.28f, top - ry * 1.35f),
            size = Size(rx * 1.15f, ry * 1.15f),
        )
        val puck = Offset(cx - rx * 0.05f, top - ry * 0.85f)
        drawOval(Color(0xFF263238), Offset(puck.x - 18f, puck.y - 7f), Size(36f, 16f))
        drawOval(Color(0xFF0D1B22), Offset(puck.x - 8f, puck.y - 3f), Size(16f, 8f))
        drawCircle(Color(0xFF90A4AE), 2.2f, Offset(puck.x - 3f, puck.y + 1f))
        drawCircle(Color(0xFF90A4AE), 2.2f, Offset(puck.x + 4f, puck.y + 1f))

        val y = top + bodyH * 0.16f
        drawRoundRect(
            color = Color(0xFF37474F),
            topLeft = Offset(right - 8f, y - 22f),
            size = Size(18f, 44f),
            cornerRadius = CornerRadius(4f, 4f),
        )
        drawCircle(Color(0xFFCFD8DC), 3f, Offset(right - 2f, y - 12f))
        drawCircle(Color(0xFFCFD8DC), 3f, Offset(right - 2f, y + 12f))
        drawLine(
            color = Color(0xFFB0BEC5),
            start = Offset(right - 22f, y),
            end = Offset(right + 10f, y),
            strokeWidth = 4f,
            cap = StrokeCap.Round,
        )
        drawRoundRect(
            color = Color(0xFF455A64),
            topLeft = Offset(right + 6f, y - 7f),
            size = Size(28f, 14f),
            cornerRadius = CornerRadius(3f, 3f),
        )
        val box = Offset(right + 30f, y - 36f)
        val boxSize = Size(78f, 72f)
        if (sensorOn) {
            drawCircle(
                brush = Brush.radialGradient(
                    listOf(Color(0xFFFF1744).copy(alpha = 0.55f * glow), Color.Transparent),
                    center = Offset(box.x + 40f, box.y + 34f),
                    radius = 54f,
                ),
                radius = 54f,
                center = Offset(box.x + 40f, box.y + 34f),
            )
        }
        drawRoundRect(
            brush = Brush.verticalGradient(listOf(Color(0xFF455A64), Color(0xFF102027))),
            topLeft = box,
            size = boxSize,
            cornerRadius = CornerRadius(12f, 12f),
        )
        drawRoundRect(
            color = Color(0xFF1A242B),
            topLeft = Offset(box.x + 10f, box.y + 16f),
            size = Size(58f, 36f),
            cornerRadius = CornerRadius(8f, 8f),
        )
        drawCircle(Color(0xFF90A4AE), 2.4f, Offset(box.x + 16f, box.y + 10f))
        drawCircle(Color(0xFF90A4AE), 2.4f, Offset(box.x + 62f, box.y + 10f))
        val lens = Offset(box.x + 39f, box.y + 34f)
        drawCircle(Color(0xFF212121), 13f, lens)
        if (sensorOn) {
            drawCircle(Color(0xFFFF1744).copy(alpha = glow), 11f, lens)
            drawCircle(Color(0xFFFF8A80), 5f, lens)
            drawCircle(Color(0xFFFFF8E1), 2.2f, lens + Offset(-3f, -3f))
        } else {
            drawCircle(Color(0xFF3E2723), 11f, lens)
            drawCircle(Color(0xFF5D4037), 11f, lens, style = Stroke(width = 2f))
        }
        drawRoundRect(
            color = Color(0xFF263238),
            topLeft = Offset(box.x + 30f, box.y + boxSize.height - 2f),
            size = Size(18f, 8f),
            cornerRadius = CornerRadius(2f, 2f),
        )
        val cable = Path().apply {
            moveTo(box.x + 39f, box.y + boxSize.height + 6f)
            quadraticTo(box.x + 70f, bottom - 10f, box.x + 24f, size.height - 8f)
        }
        drawPath(cable, Color(0xFF10181C), style = Stroke(width = 4f, cap = StrokeCap.Round))
    }
}
