package dev.huskport.composehello

import android.os.Bundle
import android.view.ContextThemeWrapper
import androidx.compose.runtime.LaunchedEffect
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
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

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(Modifier.fillMaxSize()) { Box(Modifier.fillMaxSize()) {
                    // text measured and laid out at heavy weights and large sizes, as DepCalc's titles; above, where it moves nothing
                    Column(Modifier.align(Alignment.TopStart).padding(top = 48.dp)) {
                        Text("Ещё", Modifier.padding(24.dp, 6.dp), style = MaterialTheme.typography.displayMedium, fontWeight = FontWeight.Black, letterSpacing = (-2).sp)
                        Text("99", Modifier.padding(horizontal = 24.dp), fontSize = 96.sp, fontWeight = FontWeight.Bold, letterSpacing = (-4).sp)
                    }
                    var taps by remember { mutableIntStateOf(0) }
                    Column(
                        Modifier.fillMaxSize(),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Text("Hello from Compose on husk-port", style = MaterialTheme.typography.headlineSmall)
                        Text("taps: $taps", style = MaterialTheme.typography.bodyLarge)
                        Button(onClick = { taps++ }) { Text("Tap") }
                    }
                    // a tap opens a classic (View) alert dialog with a long message, as DepCalc's update offer that showed cut
                    if (taps == 1) LaunchedEffect(Unit) {
                        MaterialAlertDialogBuilder(ContextThemeWrapper(this@MainActivity, com.google.android.material.R.style.Theme_Material3_DayNight_NoActionBar))
                            .setTitle("Update available v1.9.1")
                            .setMessage((1..30).joinToString("\n") { "line $it of the changes" })
                            .setPositiveButton("Update", null)
                            .setNegativeButton("Later", null)
                            .show()
                    }
                } }
            }
        }
    }
}
