package dev.huskport.composehello

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
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

@OptIn(ExperimentalMaterial3Api::class)
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
                    // a tap opens a bottom sheet in its own window, taller than half the screen, as DepCalc's update sheet
                    if (taps > 0) ModalBottomSheet(onDismissRequest = {}) {
                        Column(Modifier.padding(24.dp)) {
                            Text("Bottom sheet", style = MaterialTheme.typography.headlineSmall)
                            for (i in 1..24) Text("line $i of the sheet", style = MaterialTheme.typography.bodyLarge)
                        }
                    }
                } }
            }
        }
    }
}
