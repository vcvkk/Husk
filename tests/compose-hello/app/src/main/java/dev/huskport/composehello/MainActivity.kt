package dev.huskport.composehello

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.runtime.mutableStateOf
import androidx.compose.material3.TextButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.Canvas
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(Modifier.fillMaxSize()) { Box(Modifier.fillMaxSize()) {
                    // a tiled wallpaper, as DepCalc's: one small bitmap drawn scaled again and again, half transparent
                    val tile = remember {
                        val b = android.graphics.Bitmap.createBitmap(64, 64, android.graphics.Bitmap.Config.ARGB_8888)
                        val c = android.graphics.Canvas(b)
                        val paint = android.graphics.Paint().apply { isAntiAlias = true; color = 0x5533AA55; strokeWidth = 6f }
                        c.drawCircle(32f, 32f, 20f, paint)
                        paint.color = 0x88228833.toInt(); c.drawLine(8f, 40f, 28f, 56f, paint); c.drawLine(28f, 56f, 58f, 10f, paint)
                        b.asImageBitmap()
                    }
                    Canvas(Modifier.fillMaxSize()) {
                        val step = 150f
                        var y = 0f
                        while (y < size.height) {
                            var x = 0f
                            while (x < size.width) {
                                // every other one tinted, as icons are (ColorFilter.tint)
                                val tint = if (((x + y) / step).toInt() % 2 == 1) ColorFilter.tint(Color(0xFF3355CC)) else null
                                drawImage(tile, dstOffset = IntOffset(x.toInt(), y.toInt()), dstSize = IntSize(step.toInt(), step.toInt()), colorFilter = tint)
                                x += step
                            }
                            y += step
                        }
                    }
                    // text measured and laid out at heavy weights and large sizes, as DepCalc's titles; above, where it moves nothing
                    Column(Modifier.align(Alignment.TopStart).padding(top = 48.dp)) {
                        Text("Ещё", Modifier.padding(24.dp, 6.dp), style = MaterialTheme.typography.displayMedium, fontWeight = FontWeight.Black, letterSpacing = (-2).sp)
                        Text("99", Modifier.padding(horizontal = 24.dp), fontSize = 96.sp, fontWeight = FontWeight.Bold, letterSpacing = (-4).sp)
                        // a text field: CI taps it and the host types into it, as the iOS keyboard does
                        var typed by remember { mutableStateOf("") }
                        OutlinedTextField(typed, { typed = it }, Modifier.padding(horizontal = 24.dp), label = { Text("Type here") })
                        // segmented buttons: their outlines are path differences (DepCalc's theme switch drew as solid grey)
                        var mode by remember { mutableIntStateOf(1) }
                        SingleChoiceSegmentedButtonRow(Modifier.padding(24.dp)) {
                            listOf("System", "Light", "Dark").forEachIndexed { i, label ->
                                SegmentedButton(selected = mode == i, onClick = { mode = i }, shape = SegmentedButtonDefaults.itemShape(i, 3)) { Text(label) }
                            }
                        }
                    }
                    var taps by remember { mutableIntStateOf(0) }
                    Column(
                        Modifier.fillMaxSize(),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Text("Hello from Compose on husk-port", style = MaterialTheme.typography.headlineSmall)
                        Text("taps: $taps", style = MaterialTheme.typography.bodyLarge)
                        // always moving: every display refresh draws a frame (HUSK_FRAME_STATS measures them in CI)
                        CircularProgressIndicator()
                        Button(onClick = { taps++ }) { Text("Tap") }
                    }
                    // a tap opens an alert dialog with a long message, as DepCalc's update offer (Compose's Dialog window, whose
                    // content sits in a LinearLayout's weighted frame)
                    if (taps > 0) AlertDialog(
                        onDismissRequest = {},
                        title = { Text("Update available v1.9.1") },
                        text = { Text((1..12).joinToString("\n") { "line $it of the changes" }) },
                        confirmButton = { TextButton(onClick = {}) { Text("Update") } },
                        dismissButton = { TextButton(onClick = {}) { Text("Later") } },
                    )
                } }
            }
        }
    }
}
