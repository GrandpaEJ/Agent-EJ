package org.agentej.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun ViewerScreen(title: String, body: String, onBack: () -> Unit) {
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
        Surface(color = MaterialTheme.colorScheme.surface, tonalElevation = 2.dp) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp)) {
                TextButton(onClick = onBack) { Text("\u2190", fontSize = 16.sp) }
                Text(
                    title,
                    fontSize = 12.sp,
                    modifier = Modifier.padding(start = 4.dp, top = 12.dp),
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 1
                )
            }
        }
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp)) {
            Text(
                text = body.ifBlank { "(empty)" },
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp,
                lineHeight = 15.sp,
                color = MaterialTheme.colorScheme.onBackground
            )
        }
    }
}
