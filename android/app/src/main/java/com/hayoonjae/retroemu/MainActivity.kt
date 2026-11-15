package com.hayoonjae.retroemu

import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File

val Pink = Color(0xFFFF4D9D); val Violet = Color(0xFF9B5CFF); val Sky = Color(0xFF3DA9FF)
val AccentBrush = Brush.horizontalGradient(listOf(Pink, Violet, Sky))
val LightBg = Brush.linearGradient(listOf(Color(0xFFFFEEF7), Color(0xFFEDEBFF), Color(0xFFE6F4FF)))
val GameBg = Brush.verticalGradient(listOf(Color(0xFF2B1B7A), Color(0xFF6A2C9E), Color(0xFFB0307F)))

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { MaterialTheme(colorScheme = lightColorScheme(primary = Violet)) { Root() } }
    }
}

private fun romsDir(ctx: android.content.Context) = File(ctx.filesDir, "roms").apply { mkdirs() }

@Composable
fun Root() {
    val ctx = LocalView.current.context
    var games by remember { mutableStateOf(listGames(ctx)) }
    var playing by remember { mutableStateOf<File?>(null) }
    var error by remember { mutableStateOf<String?>(null) }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        uris.forEach { copyIn(ctx, it) }
        games = listGames(ctx)
    }

    val p = playing
    if (p != null) {
        GameScreen(p, onExit = { playing = null }, onError = { error = it; playing = null })
    } else {
        Library(games, onPlay = { playing = it }, onAdd = { picker.launch(arrayOf("*/*")) },
            onDelete = { f ->
                // .cue 를 지우면 그게 가리키는 .bin 도 같이 지운다
                if (f.extension.equals("cue", true)) GameSystem.cueReferenced(f.parentFile).forEach { File(f.parentFile, it).delete() }
                f.delete(); games = listGames(ctx) })
    }
    error?.let {
        AlertDialog(onDismissRequest = { error = null }, confirmButton = { TextButton(onClick = { error = null }) { Text("확인") } },
            title = { Text("열 수 없어요") }, text = { Text(it) })
    }
}

private fun listGames(ctx: android.content.Context): List<File> =
    romsDir(ctx).listFiles()?.filter { GameSystem.of(it) != null }?.sortedBy { it.name.lowercase() } ?: emptyList()

private fun isRomName(n: String) = GameSystem.fromName(n) != null || n.endsWith(".bin", true)

/** 롬 가져오기. zip 은 풀어서 롬(.cue/.bin 포함)만 넣는다. 이름만 쓰므로 경로 탈출은 없다. */
private fun copyIn(ctx: android.content.Context, uri: Uri) {
    val name = ctx.contentResolver.query(uri, null, null, null, null)?.use { c ->
        if (c.moveToFirst()) c.getString(c.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME)) else null
    } ?: return
    val dir = romsDir(ctx)
    if (name.endsWith(".zip", true)) {
        ctx.contentResolver.openInputStream(uri)?.use { raw ->
            java.util.zip.ZipInputStream(raw).use { z ->
                while (true) {
                    val e = z.nextEntry ?: break
                    val n = e.name.substringAfterLast('/')
                    if (e.isDirectory || !isRomName(n)) continue
                    File(dir, n).outputStream().use { o -> z.copyTo(o) }
                }
            }
        }
        return
    }
    if (!isRomName(name)) return
    ctx.contentResolver.openInputStream(uri)?.use { i -> File(dir, name).outputStream().use { o -> i.copyTo(o) } }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun Library(games: List<File>, onPlay: (File) -> Unit, onAdd: () -> Unit, onDelete: (File) -> Unit) {
    var del by remember { mutableStateOf<File?>(null) }
    Box(Modifier.fillMaxSize().background(LightBg).systemBarsPadding().padding(horizontal = 16.dp)) {
        Column {
            Row(Modifier.fillMaxWidth().padding(top = 18.dp, bottom = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("레트로", fontSize = 30.sp, fontWeight = FontWeight.ExtraBold,
                    style = androidx.compose.ui.text.TextStyle(brush = AccentBrush), modifier = Modifier.weight(1f))
                Box(Modifier.clip(CircleShape).background(AccentBrush).combinedClickable(onClick = onAdd).padding(12.dp)) {
                    Icon(Icons.Filled.Add, "롬 추가", tint = Color.White)
                }
            }
            if (games.isEmpty()) {
                Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("오른쪽 위 + 로 롬 파일을 추가해요", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = Color(0xB3000000))
                    Spacer(Modifier.height(8.dp))
                    Text(GameSystem.entries.joinToString(" · ") { it.title }, fontSize = 12.sp, color = Color(0x80000000))
                }
            } else {
                LazyVerticalGrid(GridCells.Adaptive(160.dp), horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp), contentPadding = PaddingValues(bottom = 24.dp)) {
                    items(games, key = { it.name }) { f ->
                        val sys = GameSystem.of(f)!!
                        Column(
                            Modifier.fillMaxWidth().heightIn(min = 104.dp).clip(RoundedCornerShape(20.dp))
                                .background(Brush.linearGradient(sys.colors))
                                .combinedClickable(onClick = { onPlay(f) }, onLongClick = { del = f }).padding(14.dp),
                            verticalArrangement = Arrangement.Bottom
                        ) {
                            Text(sys.title, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = Color(0xE6FFFFFF))
                            Text(f.nameWithoutExtension, fontSize = 16.sp, fontWeight = FontWeight.ExtraBold, color = Color.White, maxLines = 2)
                        }
                    }
                }
            }
        }
    }
    del?.let { f ->
        AlertDialog(onDismissRequest = { del = null },
            confirmButton = { TextButton(onClick = { onDelete(f); del = null }) { Text("지우기") } },
            dismissButton = { TextButton(onClick = { del = null }) { Text("취소") } },
            title = { Text(f.nameWithoutExtension) }, text = { Text("목록에서 지우고 앱 안의 롬 파일도 삭제해요.") })
    }
}

@Composable
fun GameScreen(rom: File, onExit: () -> Unit, onError: (String) -> Unit) {
    val ctx = LocalView.current.context
    val view = LocalView.current
    val engine = remember { Engine(ctx) }
    val sys = remember { GameSystem.of(rom)!! }
    var started by remember { mutableStateOf(false) }
    var menu by remember { mutableStateOf(false) }
    var slotsOpen by remember { mutableStateOf(false) }

    DisposableEffect(Unit) {
        view.keepScreenOn = true
        val err = engine.start(rom, sys)
        if (err != null) onError(err) else {
            started = true
            // 지난번에 나갈 때 자동 저장한 게 있으면 저장 목록부터 연다
            if (engine.slots().first().time != null) { menu = true; slotsOpen = true }
        }
        onDispose { view.keepScreenOn = false; engine.stop() }
    }
    LaunchedEffect(menu) { engine.paused = menu; if (menu) engine.releaseAll() }
    BackHandler { if (slotsOpen) slotsOpen = false else menu = !menu }
    LaunchedEffect(engine.toast) { if (engine.toast != null) { kotlinx.coroutines.delay(1800); engine.toast = null } }
    if (!started) return

    val landscape = LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp
    Box(Modifier.fillMaxSize().background(GameBg)) {
        if (landscape) {
            Row(Modifier.fillMaxSize().systemBarsPadding(), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.width(190.dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Shoulders(engine, sys, true); Spacer(Modifier.height(10.dp)); DPad(engine)
                    }
                }
                Box(Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.Center) { Screen(engine) }
                Box(Modifier.width(190.dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Shoulders(engine, sys, false); Spacer(Modifier.height(10.dp)); Face(engine, sys)
                        Spacer(Modifier.height(10.dp)); StartSelect(engine)
                    }
                }
            }
        } else {
            Column(Modifier.fillMaxSize().systemBarsPadding(), horizontalAlignment = Alignment.CenterHorizontally) {
                Box(Modifier.fillMaxWidth().padding(top = 8.dp), contentAlignment = Alignment.Center) { Screen(engine) }
                Spacer(Modifier.weight(1f))
                Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    Shoulders(engine, sys, true); Shoulders(engine, sys, false)
                }
                Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    DPad(engine); Face(engine, sys)
                }
                StartSelect(engine)
                Spacer(Modifier.height(28.dp))
            }
        }
        Box(Modifier.align(Alignment.TopEnd).systemBarsPadding().padding(10.dp).size(40.dp).clip(CircleShape)
            .background(Color(0x40FFFFFF)).combinedClickable(onClick = { menu = true }), contentAlignment = Alignment.Center) {
            Icon(Icons.Filled.Menu, "메뉴", tint = Color.White)
        }
        engine.toast?.let {
            Text(it, color = Color.White, fontWeight = FontWeight.Bold, fontSize = 14.sp,
                modifier = Modifier.align(Alignment.TopCenter).systemBarsPadding().padding(top = 14.dp)
                    .clip(CircleShape).background(AccentBrush).padding(horizontal = 16.dp, vertical = 8.dp))
        }
        if (menu) {
            Box(Modifier.fillMaxSize().background(Color(0x99000000)).pointerInput(Unit) { awaitEachGesture { awaitFirstDown(); } },
                contentAlignment = Alignment.Center) {
                Column(Modifier.clip(RoundedCornerShape(24.dp)).background(Color.White).padding(20.dp).widthIn(min = 240.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    MenuItem("이어하기") { menu = false }
                    MenuItem("저장 / 불러오기") { slotsOpen = true }
                    MenuItem("나가기") { onExit() }
                }
            }
        }
        if (menu && slotsOpen) {
            SlotSheet(engine, onClose = { slotsOpen = false }, onLoaded = { slotsOpen = false; menu = false })
        }
    }
}

private val slotTimeFmt = java.text.SimpleDateFormat("M월 d일 a h:mm", java.util.Locale.KOREAN)

/** 저장 칸 목록. 저장은 게임이 멈춘 채로 하고, 불러오면 바로 게임으로 돌아간다. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SlotSheet(engine: Engine, onClose: () -> Unit, onLoaded: () -> Unit) {
    val slots = remember(engine.slotsVersion) { engine.slots() }
    var overwrite by remember { mutableStateOf<String?>(null) }
    var del by remember { mutableStateOf<String?>(null) }
    Box(Modifier.fillMaxSize().background(Color(0x99000000)).pointerInput(Unit) { awaitEachGesture { awaitFirstDown() } }
        .systemBarsPadding().padding(12.dp), contentAlignment = Alignment.Center) {
        Column(Modifier.widthIn(max = 720.dp).fillMaxWidth().clip(RoundedCornerShape(24.dp)).background(LightBg).padding(16.dp)) {
            Row(Modifier.fillMaxWidth().padding(bottom = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("저장 / 불러오기", fontSize = 20.sp, fontWeight = FontWeight.ExtraBold,
                    style = androidx.compose.ui.text.TextStyle(brush = AccentBrush), modifier = Modifier.weight(1f))
                Box(Modifier.clip(CircleShape).background(Color(0xFFEDE8FF)).combinedClickable(onClick = onClose)
                    .padding(horizontal = 14.dp, vertical = 8.dp)) {
                    Text("닫기", fontSize = 14.sp, fontWeight = FontWeight.Bold, color = Color(0xFF5B33BF))
                }
            }
            LazyVerticalGrid(GridCells.Adaptive(150.dp), horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)) {
                items(slots, key = { it.id }) { s ->
                    Column(Modifier.clip(RoundedCornerShape(16.dp)).background(Color(0xD9FFFFFF))
                        .combinedClickable(onClick = {}, onLongClick = { if (s.time != null) del = s.id }).padding(8.dp)) {
                        Box(Modifier.fillMaxWidth().aspectRatio(4f / 3f).clip(RoundedCornerShape(10.dp)).background(Color.Black),
                            contentAlignment = Alignment.Center) {
                            val t = s.thumb
                            if (t != null) Canvas(Modifier.fillMaxSize()) {
                                val sc = minOf(size.width / t.width, size.height / t.height)
                                val w = (t.width * sc).toInt(); val h = (t.height * sc).toInt()
                                drawImage(t.asImageBitmap(), dstOffset = IntOffset(((size.width - w) / 2).toInt(), ((size.height - h) / 2).toInt()),
                                    dstSize = IntSize(w, h), filterQuality = FilterQuality.None)
                            } else Text(if (s.time == null) "비어 있음" else "화면 없음", fontSize = 12.sp, color = Color(0x80FFFFFF))
                        }
                        Text(SaveSlot.name(s.id), fontSize = 14.sp, fontWeight = FontWeight.ExtraBold, color = Color(0xCC000000),
                            modifier = Modifier.padding(top = 6.dp))
                        Text(s.time?.let { slotTimeFmt.format(java.util.Date(it)) } ?: "", fontSize = 11.sp, color = Color(0x80000000), maxLines = 1)
                        Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            if (!s.isAuto) SlotPill("저장", false, true, Modifier.weight(1f)) {
                                if (s.time != null) overwrite = s.id else engine.save(s.id)
                            }
                            SlotPill("불러오기", true, s.time != null, Modifier.weight(1f)) { engine.load(s.id); onLoaded() }
                        }
                    }
                }
            }
        }
    }
    overwrite?.let { id ->
        AlertDialog(onDismissRequest = { overwrite = null },
            confirmButton = { TextButton(onClick = { engine.save(id); overwrite = null }) { Text("덮어쓰기") } },
            dismissButton = { TextButton(onClick = { overwrite = null }) { Text("취소") } },
            title = { Text("덮어쓸까요?") }, text = { Text("${SaveSlot.name(id)}에 있던 저장이 지워져요.") })
    }
    del?.let { id ->
        AlertDialog(onDismissRequest = { del = null },
            confirmButton = { TextButton(onClick = { engine.delete(id); del = null }) { Text("지우기") } },
            dismissButton = { TextButton(onClick = { del = null }) { Text("취소") } },
            title = { Text("${SaveSlot.name(id)} 지우기") }, text = { Text("이 저장을 지워요. 되돌릴 수 없어요.") })
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SlotPill(text: String, filled: Boolean, enabled: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val bg = if (filled) AccentBrush else Brush.horizontalGradient(listOf(Color(0xFFEDE8FF), Color(0xFFEDE8FF)))
    Box(modifier.alpha(if (enabled) 1f else 0.35f).clip(CircleShape).background(bg)
        .then(if (enabled) Modifier.combinedClickable(onClick = onClick) else Modifier).padding(vertical = 9.dp),
        contentAlignment = Alignment.Center) {
        Text(text, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = if (filled) Color.White else Color(0xFF5B33BF))
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MenuItem(text: String, onClick: () -> Unit) {
    Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(Brush.horizontalGradient(listOf(Color(0xFFFFE3F1), Color(0xFFE3E8FF))))
        .combinedClickable(onClick = onClick).padding(vertical = 14.dp), contentAlignment = Alignment.Center) {
        Text(text, fontSize = 16.sp, fontWeight = FontWeight.Bold, color = Color(0xFF3B2A8C))
    }
}

@Composable
fun Screen(engine: Engine) {
    engine.frameTick
    val bmp = engine.bitmap
    Canvas(Modifier.aspectRatio(engine.aspect.toFloat().coerceIn(0.5f, 3f)).fillMaxWidth().heightIn(max = 4000.dp).background(Color.Black)) {
        if (bmp != null) drawImage(bmp.asImageBitmap(), dstOffset = IntOffset.Zero, dstSize = IntSize(size.width.toInt(), size.height.toInt()),
            filterQuality = FilterQuality.None)
    }
}

// ---- 터치 컨트롤 ----

@Composable
fun HoldButton(engine: Engine, bit: Int, label: String, size: Int = 56, shape: androidx.compose.ui.graphics.Shape = CircleShape,
               w: Int = size, h: Int = size) {
    var down by remember { mutableStateOf(false) }
    val haptic = LocalHapticFeedback.current
    Box(Modifier.size(w.dp, h.dp).clip(shape)
        .background(if (down) Brush.linearGradient(listOf(Pink, Violet)) else Brush.linearGradient(listOf(Color(0x66FFFFFF), Color(0x33FFFFFF))))
        .pointerInput(bit) {
            awaitEachGesture {
                awaitFirstDown(); down = true; engine.press(bit, true); haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                do { val e = awaitPointerEvent() } while (e.changes.any { it.pressed })
                down = false; engine.press(bit, false)
            }
        }, contentAlignment = Alignment.Center) {
        Text(label, color = Color.White, fontWeight = FontWeight.ExtraBold, fontSize = (size / 3.2).sp)
    }
}

@Composable
fun DPad(engine: Engine) {
    val sz = 150
    var dirs by remember { mutableStateOf(0) }
    val haptic = LocalHapticFeedback.current
    Box(Modifier.size(sz.dp).clip(CircleShape).background(Brush.radialGradient(listOf(Color(0x40FFFFFF), Color(0x20FFFFFF))))
        .pointerInput(Unit) {
            awaitEachGesture {
                val first = awaitFirstDown()
                fun apply(p: Offset) {
                    val c = Offset(size.width / 2f, size.height / 2f); val d = p - c
                    val dead = size.width * 0.12f
                    val l = d.x < -dead && kotlin.math.abs(d.x) > kotlin.math.abs(d.y) * 0.45f
                    val r = d.x > dead && kotlin.math.abs(d.x) > kotlin.math.abs(d.y) * 0.45f
                    val u = d.y < -dead && kotlin.math.abs(d.y) > kotlin.math.abs(d.x) * 0.45f
                    val dn = d.y > dead && kotlin.math.abs(d.y) > kotlin.math.abs(d.x) * 0.45f
                    val m = (if (l) 1 else 0) or (if (r) 2 else 0) or (if (u) 4 else 0) or (if (dn) 8 else 0)
                    if (m != dirs) {
                        if (m != 0) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        dirs = m
                        engine.press(Pad.LEFT, l); engine.press(Pad.RIGHT, r); engine.press(Pad.UP, u); engine.press(Pad.DOWN, dn)
                    }
                }
                apply(first.position)
                do {
                    val e = awaitPointerEvent(PointerEventPass.Main)
                    e.changes.firstOrNull { it.id == first.id }?.let { apply(it.position) }
                } while (e.changes.any { it.pressed && it.id == first.id })
                dirs = 0
                engine.press(Pad.LEFT, false); engine.press(Pad.RIGHT, false); engine.press(Pad.UP, false); engine.press(Pad.DOWN, false)
            }
        }, contentAlignment = Alignment.Center) {
        fun arm(on: Boolean) = if (on) Brush.linearGradient(listOf(Pink, Violet)) else Brush.linearGradient(listOf(Color(0x66FFFFFF), Color(0x44FFFFFF)))
        Box(Modifier.align(Alignment.TopCenter).padding(top = 6.dp).size(44.dp, 50.dp).clip(RoundedCornerShape(12.dp)).background(arm(dirs and 4 != 0)))
        Box(Modifier.align(Alignment.BottomCenter).padding(bottom = 6.dp).size(44.dp, 50.dp).clip(RoundedCornerShape(12.dp)).background(arm(dirs and 8 != 0)))
        Box(Modifier.align(Alignment.CenterStart).padding(start = 6.dp).size(50.dp, 44.dp).clip(RoundedCornerShape(12.dp)).background(arm(dirs and 1 != 0)))
        Box(Modifier.align(Alignment.CenterEnd).padding(end = 6.dp).size(50.dp, 44.dp).clip(RoundedCornerShape(12.dp)).background(arm(dirs and 2 != 0)))
    }
}

@Composable
fun Face(engine: Engine, sys: GameSystem) {
    if (sys.hasXY) {
        Box(Modifier.size(150.dp)) {
            Box(Modifier.align(Alignment.TopCenter)) { HoldButton(engine, Pad.X, if (sys == GameSystem.PS1) "삼각" else "X", 52) }
            Box(Modifier.align(Alignment.CenterStart)) { HoldButton(engine, Pad.Y, if (sys == GameSystem.PS1) "사각" else "Y", 52) }
            Box(Modifier.align(Alignment.CenterEnd)) { HoldButton(engine, Pad.A, if (sys == GameSystem.PS1) "동그" else "A", 52) }
            Box(Modifier.align(Alignment.BottomCenter)) { HoldButton(engine, Pad.B, if (sys == GameSystem.PS1) "엑스" else "B", 52) }
        }
    } else {
        Box(Modifier.size(150.dp)) {
            Box(Modifier.align(Alignment.CenterStart).offset(y = 20.dp)) { HoldButton(engine, Pad.B, "B", 62) }
            Box(Modifier.align(Alignment.CenterEnd).offset(y = (-20).dp)) { HoldButton(engine, Pad.A, "A", 62) }
        }
    }
}

@Composable
fun Shoulders(engine: Engine, sys: GameSystem, left: Boolean) {
    if (!sys.hasShoulders) { Spacer(Modifier.height(1.dp)); return }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        val r = RoundedCornerShape(14.dp)
        if (left) {
            if (sys.hasTriggers) HoldButton(engine, Pad.L2, "L2", 40, r, 64, 40)
            HoldButton(engine, Pad.L, "L", 40, r, 64, 40)
        } else {
            HoldButton(engine, Pad.R, "R", 40, r, 64, 40)
            if (sys.hasTriggers) HoldButton(engine, Pad.R2, "R2", 40, r, 64, 40)
        }
    }
}

@Composable
fun StartSelect(engine: Engine) {
    Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
        HoldButton(engine, Pad.SELECT, "셀렉트", 34, RoundedCornerShape(17.dp), 84, 34)
        HoldButton(engine, Pad.START, "스타트", 34, RoundedCornerShape(17.dp), 84, 34)
    }
}
