package com.hayoonjae.retroemu

import android.annotation.SuppressLint
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
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.Refresh
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
import kotlinx.coroutines.launch

val Pink = Color(0xFFFF4D9D); val Violet = Color(0xFF9B5CFF); val Sky = Color(0xFF3DA9FF)
val AccentBrush = Brush.horizontalGradient(listOf(Pink, Violet, Sky))
val LightBg = Brush.linearGradient(listOf(Color(0xFFFFEEF7), Color(0xFFEDEBFF), Color(0xFFE6F4FF)))
val GameBg = Brush.verticalGradient(listOf(Color(0xFF2B1B7A), Color(0xFF6A2C9E), Color(0xFFB0307F)))

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // 점검용: adb shell am start -n ... --es sync_code X --es sync_server http://127.0.0.1:8270/retro-sync
        intent?.getStringExtra("sync_code")?.let { Sync.setCode(this, it) }
        intent?.getStringExtra("sync_server")?.let { Sync.setServer(this, it.takeIf { s -> s != "default" }) }
        setContent { MaterialTheme(colorScheme = lightColorScheme(primary = Violet)) { Root() } }
    }

    // 홈·잠금으로 뒤로 가면 멈추고 자동 저장(뒤에서 꺼져도 진행이 남게)
    override fun onStop() { super.onStop(); Gamepad.engine?.setBackground(true) }
    override fun onStart() { super.onStart(); Gamepad.engine?.setBackground(false) }

    // 게임 중이면 패드 입력을 먼저 가로챈다(메뉴 화면 등에선 평소대로)
    override fun dispatchKeyEvent(e: android.view.KeyEvent): Boolean =
        (Gamepad.isPad(e.device) || android.view.KeyEvent.isGamepadButton(e.keyCode)) && Gamepad.onKey(e) || super.dispatchKeyEvent(e)
    override fun dispatchGenericMotionEvent(e: android.view.MotionEvent): Boolean =
        Gamepad.onMotion(e) || super.dispatchGenericMotionEvent(e)
}

private fun romsDir(ctx: android.content.Context) = File(ctx.filesDir, "roms").apply { mkdirs() }

@Composable
fun Root() {
    val ctx = LocalView.current.context
    var games by remember { mutableStateOf(listGames(ctx)) }
    var playing by remember { mutableStateOf<File?>(null) }
    var error by remember { mutableStateOf<String?>(null) }

    // 가져오기는 몇 GB 짜리도 있어서 백그라운드에서 하고 진행만 보여 준다
    var importing by remember { mutableStateOf<String?>(null) }
    val importScope = rememberCoroutineScope()
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult
        importScope.launch {
            uris.forEachIndexed { i, u ->
                importing = "가져오는 중 ${i + 1}/${uris.size}"
                kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { runCatching { copyIn(ctx, u) } }
                games = listGames(ctx)
            }
            importing = null
        }
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
    importing?.let { msg ->
        AlertDialog(onDismissRequest = {}, confirmButton = {},
            title = { Text(msg) },
            text = { Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 3.dp, color = Violet)
                Spacer(Modifier.width(12.dp)); Text("큰 게임은 몇 분 걸려요")
            } })
    }
    error?.let {
        AlertDialog(onDismissRequest = { error = null }, confirmButton = { TextButton(onClick = { error = null }) { Text("확인") } },
            title = { Text("열 수 없어요") }, text = { Text(it) })
    }
}

private fun listGames(ctx: android.content.Context): List<File> =
    romsDir(ctx).listFiles()?.filter { GameSystem.of(it) != null }?.sortedBy { it.name.lowercase() } ?: emptyList()

private fun isRomName(n: String) = GameSystem.fromName(n) != null || n.endsWith(".bin", true) || n.endsWith(".iso", true)

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
    // 다 받은 뒤에 이름을 붙여서, 도중에 꺼져도 반쪽짜리 게임이 목록에 안 뜨게 한다
    val tmp = File(dir, "$name.part")
    ctx.contentResolver.openInputStream(uri)?.use { i -> tmp.outputStream().use { o -> i.copyTo(o, 1 shl 20) } }
    if (!tmp.renameTo(File(dir, name))) tmp.delete()
}

/** 잠금을 풀 때 경고(맥·윈도우·iOS 와 같은 문구) */
const val LOCK_WARNING = "예전엔 게임이 비싸서 하나를 끝까지 파고들었죠. 잠금을 풀면 그 맛이 흐려져요. 지금 하는 게임을 충분히 즐긴 뒤에 푸는 걸 추천해요."

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun Library(games: List<File>, onPlay: (File) -> Unit, onAdd: () -> Unit, onDelete: (File) -> Unit) {
    var del by remember { mutableStateOf<File?>(null) }
    var syncOpen by remember { mutableStateOf(false) }
    val ctx = LocalView.current.context
    // 잠긴 게임(롬 파일 이름). 잠긴 건 눌러도 안 켜지고, 풀 때 경고를 띄운다.
    val lockPrefs = remember { ctx.getSharedPreferences("locks", android.content.Context.MODE_PRIVATE) }
    var locked by remember { mutableStateOf(lockPrefs.getStringSet("names", emptySet())!!.toSet()) }
    fun setLocked(f: File, on: Boolean) {
        locked = if (on) locked + f.name else locked - f.name
        lockPrefs.edit().putStringSet("names", locked).apply()
    }
    var actionFor by remember { mutableStateOf<File?>(null) }
    var unlockFor by remember { mutableStateOf<File?>(null) }
    Box(Modifier.fillMaxSize().background(LightBg).systemBarsPadding().padding(horizontal = 16.dp)) {
        Column {
            Row(Modifier.fillMaxWidth().padding(top = 18.dp, bottom = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("레트로", fontSize = 30.sp, fontWeight = FontWeight.ExtraBold,
                    style = androidx.compose.ui.text.TextStyle(brush = AccentBrush), modifier = Modifier.weight(1f))
                Row(Modifier.clip(CircleShape).background(Color(0xD9FFFFFF)).combinedClickable(onClick = { syncOpen = true })
                    .padding(horizontal = 14.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Refresh, null, tint = Violet, modifier = Modifier.size(20.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(if (Sync.enabled(ctx)) "동기화 켜짐" else "동기화", fontSize = 14.sp, fontWeight = FontWeight.Bold, color = Color(0xFF5C33BF))
                }
                Spacer(Modifier.width(10.dp))
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
                        Box(
                            Modifier.fillMaxWidth().height(150.dp).clip(RoundedCornerShape(20.dp))
                                .background(Brush.linearGradient(sys.colors))
                                .combinedClickable(onClick = {
                                    if (f.name in locked) android.widget.Toast.makeText(ctx, "잠긴 게임이에요 · 길게 눌러 잠금을 풀어요", android.widget.Toast.LENGTH_SHORT).show()
                                    else onPlay(f)
                                }, onLongClick = { actionFor = f })
                        ) {
                            // 게임기 사진: 오른쪽 위, 글자와 안 겹치게
                            androidx.compose.foundation.Image(
                                androidx.compose.ui.res.painterResource(sys.photo), sys.title,
                                contentScale = androidx.compose.ui.layout.ContentScale.Fit,
                                alignment = Alignment.TopEnd,
                                modifier = Modifier.align(Alignment.TopEnd).padding(top = 10.dp, end = 10.dp)
                                    .fillMaxWidth(if (sys.tallPhoto) 0.42f else 0.72f).height(if (sys.tallPhoto) 82.dp else 70.dp)
                            )
                            if (f.name in locked) {
                                Box(Modifier.matchParentSize().background(Color(0x77969AAA)))
                                Row(Modifier.align(Alignment.TopStart).padding(10.dp).clip(CircleShape).background(Color(0x5C000000))
                                    .padding(horizontal = 10.dp, vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
                                    Icon(Icons.Filled.Lock, null, tint = Color.White, modifier = Modifier.size(14.dp))
                                    Spacer(Modifier.width(4.dp))
                                    Text("잠김", fontSize = 12.sp, fontWeight = FontWeight.ExtraBold, color = Color.White)
                                }
                            }
                            Column(Modifier.align(Alignment.BottomStart).padding(14.dp)) {
                                Text(sys.title, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = Color(0xE6FFFFFF))
                                Text(f.nameWithoutExtension.removeSuffix(".nkit"), fontSize = 16.sp, fontWeight = FontWeight.ExtraBold, color = Color.White,
                                    maxLines = 2, lineHeight = 19.sp)
                            }
                        }
                    }
                }
            }
        }
    }
    if (syncOpen) SyncDialog(games) { syncOpen = false }
    actionFor?.let { f ->
        val isLocked = f.name in locked
        AlertDialog(onDismissRequest = { actionFor = null },
            title = { Text(f.nameWithoutExtension.removeSuffix(".nkit")) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Box(Modifier.fillMaxWidth().clip(CircleShape).background(AccentBrush)
                        .combinedClickable(onClick = { actionFor = null; if (isLocked) unlockFor = f else setLocked(f, true) })
                        .padding(vertical = 13.dp), contentAlignment = Alignment.Center) {
                        Text(if (isLocked) "잠금 풀기" else "잠그기", fontWeight = FontWeight.Bold, color = Color.White)
                    }
                    Box(Modifier.fillMaxWidth().clip(CircleShape).background(Color(0xFFEDE8FF))
                        .combinedClickable(onClick = { actionFor = null; del = f })
                        .padding(vertical = 13.dp), contentAlignment = Alignment.Center) {
                        Text("목록에서 지우기", fontWeight = FontWeight.Bold, color = Color(0xFF5C33BF))
                    }
                }
            },
            confirmButton = { TextButton(onClick = { actionFor = null }) { Text("닫기") } })
    }
    unlockFor?.let { f ->
        AlertDialog(onDismissRequest = { unlockFor = null },
            title = { Text("${f.nameWithoutExtension.removeSuffix(".nkit")} 잠금을 풀까요?") },
            text = { Text(LOCK_WARNING) },
            confirmButton = { TextButton(onClick = { setLocked(f, false); unlockFor = null }) { Text("잠금 풀기", color = Color(0xFFD62D6E)) } },
            dismissButton = { TextButton(onClick = { unlockFor = null }) { Text("취소") } })
    }
    del?.let { f ->
        AlertDialog(onDismissRequest = { del = null },
            confirmButton = { TextButton(onClick = { onDelete(f); del = null }) { Text("지우기") } },
            dismissButton = { TextButton(onClick = { del = null }) { Text("취소") } },
            title = { Text(f.nameWithoutExtension.removeSuffix(".nkit")) }, text = { Text("목록에서 지우고 앱 안의 롬 파일도 삭제해요.") })
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
    val prefs = remember { ControlPrefs(ctx) }
    val k = if (prefs.big) BIG_SCALE else 1f

    // 동기화를 켰으면 다른 기기 저장부터 받고 시작한다
    var ready by remember { mutableStateOf(!Sync.enabled(ctx)) }
    LaunchedEffect(Unit) {
        if (!ready) {
            val r = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { runCatching { Sync.syncGame(ctx, rom.name, true) }.getOrNull() }
            if (r == null) engine.toast = "동기화 서버에 못 닿아서 이 기기 저장으로 시작해요"
            ready = true
        }
    }
    DisposableEffect(ready) {
        if (!ready) return@DisposableEffect onDispose {}
        view.keepScreenOn = true
        val err = engine.start(rom, sys)
        if (err != null) onError(err) else {
            started = true
            // 지난번에 나갈 때 자동 저장한 게 있으면 저장 목록부터 연다
            if (engine.slots().first().time != null) { menu = true; slotsOpen = true }
        }
        onDispose { view.keepScreenOn = false; engine.stop() }
    }
    // 게임패드: 붙어 있으면 터치 버튼을 숨기고 화면을 크게 쓴다
    var padName by remember { mutableStateOf(Gamepad.connected(ctx)) }
    DisposableEffect(Unit) {
        Gamepad.engine = engine
        Gamepad.analog = sys.hasAnalog
        Gamepad.onMenu = { if (slotsOpen) slotsOpen = false else menu = !menu }
        val im = ctx.getSystemService(android.hardware.input.InputManager::class.java)
        val l = object : android.hardware.input.InputManager.InputDeviceListener {
            override fun onInputDeviceAdded(id: Int) { refresh() }
            override fun onInputDeviceRemoved(id: Int) { refresh() }
            override fun onInputDeviceChanged(id: Int) { refresh() }
            fun refresh() {
                val n = Gamepad.connected(ctx)
                if (n != padName) {
                    engine.toast = if (n != null) "패드 연결됨 · 셀렉트+스타트 = 메뉴" else "패드 끊김 · 터치 버튼으로 바꿈"
                    if (n == null) Gamepad.reset()
                    padName = n
                }
            }
        }
        im.registerInputDeviceListener(l, android.os.Handler(android.os.Looper.getMainLooper()))
        if (padName != null) engine.toast = "패드 연결됨 · 셀렉트+스타트 = 메뉴"
        onDispose { im.unregisterInputDeviceListener(l); Gamepad.engine = null; Gamepad.onMenu = null; Gamepad.reset() }
    }
    LaunchedEffect(menu) { engine.paused = menu; if (menu) engine.releaseAll() }
    BackHandler { if (slotsOpen) slotsOpen = false else menu = !menu }
    LaunchedEffect(engine.toast) { if (engine.toast != null) { kotlinx.coroutines.delay(1800); engine.toast = null } }
    if (!started) {
        if (!ready) Box(Modifier.fillMaxSize().background(GameBg), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                CircularProgressIndicator(color = Color.White)
                Spacer(Modifier.height(12.dp))
                Text("다른 기기 저장을 받는 중", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 15.sp)
            }
        }
        return
    }

    val landscape = LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp
    Box(Modifier.fillMaxSize().background(GameBg)) {
        if (padName != null) {
            Box(Modifier.fillMaxSize().systemBarsPadding().padding(8.dp), contentAlignment = Alignment.Center) { Screen(engine, sys) }
        } else if (landscape) {
            Row(Modifier.fillMaxSize().systemBarsPadding(), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.width((190 * k).dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Shoulders(engine, sys, true, k); Spacer(Modifier.height(10.dp)); DirPad(engine, sys, k, prefs.stick)
                    }
                }
                Box(Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.Center) { Screen(engine, sys) }
                Box(Modifier.width((190 * k).dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Shoulders(engine, sys, false, k); Spacer(Modifier.height(10.dp)); Face(engine, sys, k)
                        Spacer(Modifier.height(10.dp)); StartSelect(engine, k)
                    }
                }
            }
        } else {
            Column(Modifier.fillMaxSize().systemBarsPadding(), horizontalAlignment = Alignment.CenterHorizontally) {
                Box(Modifier.fillMaxWidth().padding(top = 8.dp), contentAlignment = Alignment.Center) { Screen(engine, sys) }
                Spacer(Modifier.weight(1f))
                Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    Shoulders(engine, sys, true, k); Shoulders(engine, sys, false, k)
                }
                Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 8.dp), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    DirPad(engine, sys, k, prefs.stick); Face(engine, sys, k)
                }
                StartSelect(engine, k)
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
                    MenuItem("버튼 크기: " + if (prefs.big) "크게" else "기존") { prefs.big = !prefs.big }
                    MenuItem("방향키: " + if (prefs.stick) "조이스틱" else "십자키") { prefs.stick = !prefs.stick }
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
                            if (!s.isAuto) SlotPill("저장", false, engine.hasRun, Modifier.weight(1f)) {
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

@SuppressLint("ClickableViewAccessibility")
@Composable
fun Screen(engine: Engine, sys: GameSystem) {
    // 에뮬 스레드가 이 서피스에 바로 그린다. 컴포즈는 자리만 잡아 준다.
    androidx.compose.ui.viewinterop.AndroidView(
        factory = { c ->
            android.view.SurfaceView(c).apply {
                // DS 아래 화면 터치: 버퍼는 합성기가 뷰 전체로 늘리고, 뷰는 게임 비율이라 뷰 기준으로 계산한다
                if (sys.hasTouch) setOnTouchListener { v, e ->
                    if (v.width > 0 && v.height > 0) {
                        val x = e.x / v.width; val y = e.y / v.height
                        val down = e.actionMasked != android.view.MotionEvent.ACTION_UP && e.actionMasked != android.view.MotionEvent.ACTION_CANCEL
                        engine.setPointer(x, y, down && x in 0f..1f && y in 0f..1f)
                    }
                    true
                }
                holder.addCallback(object : android.view.SurfaceHolder.Callback {
                    override fun surfaceCreated(h: android.view.SurfaceHolder) {}
                    override fun surfaceChanged(h: android.view.SurfaceHolder, f: Int, w: Int, hh: Int) = engine.setSurface(h.surface, w, hh)
                    override fun surfaceDestroyed(h: android.view.SurfaceHolder) = engine.setSurface(null, 0, 0)
                })
            }
        },
        modifier = Modifier.aspectRatio(engine.aspect.toFloat().coerceIn(0.5f, 3f)).fillMaxWidth().heightIn(max = 4000.dp).background(Color.Black)
    )
}

// ---- 터치 컨트롤 ----

const val BIG_SCALE = 1.23f

/** 버튼 크기·방향키 종류. 기기에 저장된다. */
class ControlPrefs(ctx: android.content.Context) {
    private val sp = ctx.getSharedPreferences("controls", android.content.Context.MODE_PRIVATE)
    private var _big by mutableStateOf(sp.getBoolean("big", false))
    private var _stick by mutableStateOf(sp.getBoolean("stick", false))
    var big: Boolean get() = _big; set(v) { _big = v; sp.edit().putBoolean("big", v).apply() }
    var stick: Boolean get() = _stick; set(v) { _stick = v; sp.edit().putBoolean("stick", v).apply() }
}

private fun dp(v: Float) = v.dp

@Composable
fun HoldButton(engine: Engine, bit: Int, label: String, size: Float = 56f, shape: androidx.compose.ui.graphics.Shape = CircleShape,
               w: Float = size, h: Float = size) {
    var down by remember { mutableStateOf(false) }
    val haptic = LocalHapticFeedback.current
    Box(Modifier.size(dp(w), dp(h)).clip(shape)
        .background(if (down) Brush.linearGradient(listOf(Pink, Violet)) else Brush.linearGradient(listOf(Color(0x66FFFFFF), Color(0x33FFFFFF))))
        .pointerInput(bit) {
            awaitEachGesture {
                awaitFirstDown(); down = true; engine.press(bit, true); haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                do { val e = awaitPointerEvent() } while (e.changes.any { it.pressed })
                down = false; engine.press(bit, false)
            }
        }, contentAlignment = Alignment.Center) {
        Text(label, color = Color.White, fontWeight = FontWeight.ExtraBold, fontSize = (size / 3.2f).sp)
    }
}

/** 손가락 위치(가운데 기준) -> 방향 비트(1=왼 2=오 4=위 8=아래). 대각선은 둘 다. */
private fun dirsOf(d: Offset, dead: Float): Int {
    val ax = kotlin.math.abs(d.x); val ay = kotlin.math.abs(d.y)
    val l = d.x < -dead && ax > ay * 0.45f
    val r = d.x > dead && ax > ay * 0.45f
    val u = d.y < -dead && ay > ax * 0.45f
    val dn = d.y > dead && ay > ax * 0.45f
    return (if (l) 1 else 0) or (if (r) 2 else 0) or (if (u) 4 else 0) or (if (dn) 8 else 0)
}

private fun Engine.setDirs(m: Int) {
    press(Pad.LEFT, m and 1 != 0); press(Pad.RIGHT, m and 2 != 0); press(Pad.UP, m and 4 != 0); press(Pad.DOWN, m and 8 != 0)
}

@Composable
fun DirPad(engine: Engine, sys: GameSystem, k: Float, stick: Boolean) =
    if (sys.hasAnalog) Joystick(engine, k, analog = true) else if (stick) Joystick(engine, k) else DPad(engine, k)

@Composable
fun DPad(engine: Engine, k: Float) {
    var dirs by remember { mutableStateOf(0) }
    val haptic = LocalHapticFeedback.current
    Box(Modifier.size(dp(150 * k)).clip(CircleShape).background(Brush.radialGradient(listOf(Color(0x40FFFFFF), Color(0x20FFFFFF))))
        .pointerInput(Unit) {
            awaitEachGesture {
                val first = awaitFirstDown()
                fun apply(p: Offset) {
                    val m = dirsOf(p - Offset(size.width / 2f, size.height / 2f), size.width * 0.12f)
                    if (m != dirs) {
                        if (m != 0) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        dirs = m; engine.setDirs(m)
                    }
                }
                apply(first.position)
                do {
                    val e = awaitPointerEvent(PointerEventPass.Main)
                    e.changes.firstOrNull { it.id == first.id }?.let { apply(it.position) }
                } while (e.changes.any { it.pressed && it.id == first.id })
                dirs = 0; engine.setDirs(0)
            }
        }, contentAlignment = Alignment.Center) {
        fun arm(on: Boolean) = if (on) Brush.linearGradient(listOf(Pink, Violet)) else Brush.linearGradient(listOf(Color(0x66FFFFFF), Color(0x44FFFFFF)))
        val a = dp(44 * k); val b = dp(50 * k); val pad = dp(6 * k); val r = RoundedCornerShape(dp(12 * k))
        Box(Modifier.align(Alignment.TopCenter).padding(top = pad).size(a, b).clip(r).background(arm(dirs and 4 != 0)))
        Box(Modifier.align(Alignment.BottomCenter).padding(bottom = pad).size(a, b).clip(r).background(arm(dirs and 8 != 0)))
        Box(Modifier.align(Alignment.CenterStart).padding(start = pad).size(b, a).clip(r).background(arm(dirs and 1 != 0)))
        Box(Modifier.align(Alignment.CenterEnd).padding(end = pad).size(b, a).clip(r).background(arm(dirs and 2 != 0)))
    }
}

/** 조이스틱: 손가락을 따라가는 손잡이. 고전 게임기는 8방향 디지털로, analog 면(N64·게임큐브·PSP·PS2) 아날로그 스틱으로 들어간다. */
@Composable
fun Joystick(engine: Engine, k: Float, analog: Boolean = false) {
    var knob by remember { mutableStateOf(Offset.Zero) }
    var dirs by remember { mutableStateOf(0) }
    val haptic = LocalHapticFeedback.current
    val sz = 150 * k
    Box(Modifier.size(dp(sz)).clip(CircleShape)
        .background(Brush.radialGradient(listOf(Color(0x33FFFFFF), Color(0x55FFFFFF))))
        .pointerInput(Unit) {
            awaitEachGesture {
                val first = awaitFirstDown()
                val c = Offset(size.width / 2f, size.height / 2f)
                val maxR = size.width * 0.32f
                fun apply(p: Offset) {
                    var d = p - c
                    val len = d.getDistance()
                    if (len > maxR) d = d * (maxR / len)
                    knob = d
                    if (analog) { engine.setAnalog(0, d.x / maxR, d.y / maxR); dirs = if (len > maxR * 0.2f) 1 else 0; return }
                    val m = dirsOf(d, maxR * 0.35f)
                    if (m != dirs) {
                        if (m != 0) haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        dirs = m; engine.setDirs(m)
                    }
                }
                apply(first.position)
                do {
                    val e = awaitPointerEvent(PointerEventPass.Main)
                    e.changes.firstOrNull { it.id == first.id }?.let { apply(it.position) }
                } while (e.changes.any { it.pressed && it.id == first.id })
                knob = Offset.Zero; dirs = 0
                if (analog) engine.setAnalog(0, 0f, 0f) else engine.setDirs(0)
            }
        }, contentAlignment = Alignment.Center) {
        Box(Modifier.offset { IntOffset(knob.x.toInt(), knob.y.toInt()) }.size(dp(sz * 0.42f)).clip(CircleShape)
            .background(if (dirs != 0) Brush.linearGradient(listOf(Pink, Violet)) else Brush.linearGradient(listOf(Color(0xAAFFFFFF), Color(0x77FFFFFF)))))
    }
}

@Composable
fun Face(engine: Engine, sys: GameSystem, k: Float) {
    Box(Modifier.size(dp(150 * k))) {
        if (sys.hasXY) {
            val b = 52 * k
            Box(Modifier.align(Alignment.TopCenter)) { HoldButton(engine, Pad.X, sys.label(Pad.X) ?: "X", b) }
            Box(Modifier.align(Alignment.CenterStart)) { HoldButton(engine, Pad.Y, sys.label(Pad.Y) ?: "Y", b) }
            Box(Modifier.align(Alignment.CenterEnd)) { HoldButton(engine, Pad.A, sys.label(Pad.A) ?: "A", b) }
            Box(Modifier.align(Alignment.BottomCenter)) { HoldButton(engine, Pad.B, sys.label(Pad.B) ?: "B", b) }
        } else {
            Box(Modifier.align(Alignment.CenterStart).offset(y = dp(20 * k))) { HoldButton(engine, Pad.B, "B", 62 * k) }
            Box(Modifier.align(Alignment.CenterEnd).offset(y = dp(-20 * k))) { HoldButton(engine, Pad.A, "A", 62 * k) }
        }
    }
}

@Composable
fun Shoulders(engine: Engine, sys: GameSystem, left: Boolean, k: Float) {
    if (!sys.hasShoulders) { Spacer(Modifier.height(1.dp)); return }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        val r = RoundedCornerShape(dp(14 * k)); val f = 40 * k; val w = 64 * k; val h = 40 * k
        if (left) {
            if (sys.hasTriggers) HoldButton(engine, Pad.L2, "L2", f, r, w, h)
            HoldButton(engine, Pad.L, "L", f, r, w, h)
        } else {
            HoldButton(engine, Pad.R, "R", f, r, w, h)
            if (sys.hasTriggers) HoldButton(engine, Pad.R2, "R2", f, r, w, h)
        }
    }
}

@Composable
fun StartSelect(engine: Engine, k: Float) {
    Row(horizontalArrangement = Arrangement.spacedBy(14.dp)) {
        val h = 34 * k; val r = RoundedCornerShape(dp(h / 2)); val w = 84 * k
        HoldButton(engine, Pad.SELECT, "셀렉트", h, r, w, h)
        HoldButton(engine, Pad.START, "스타트", h, r, w, h)
    }
}

/** 동기화 설정. 같은 코드를 다른 기기에도 넣으면 저장을 주고받는다. */
@Composable
fun SyncDialog(games: List<File>, onClose: () -> Unit) {
    val ctx = LocalView.current.context
    var code by remember { mutableStateOf(Sync.code(ctx)) }
    var server by remember { mutableStateOf(Sync.server(ctx)) }
    var status by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var on by remember { mutableStateOf(Sync.enabled(ctx)) }
    val scope = rememberCoroutineScope()
    fun lastText(): String {
        val t = Sync.lastSync(ctx)
        return if (t == 0L) "아직 동기화한 적 없어요"
        else "마지막 동기화: " + java.text.SimpleDateFormat("M월 d일 a h:mm", java.util.Locale.KOREA).format(java.util.Date(t))
    }
    fun syncNow() {
        busy = true; status = "서버와 맞추는 중"
        scope.launch {
            val (ok, n) = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                var ok = Sync.check(ctx); var n = 0
                if (ok) for (g in games) { val c = runCatching { Sync.syncGame(ctx, g.name, true) }.getOrNull(); if (c == null) ok = false else n += c }
                ok to n
            }
            busy = false
            status = if (!ok) "동기화 서버에 못 닿았어요" else if (n == 0) "이미 맞춰져 있어요" else "파일 ${n}개를 주고받았어요"
        }
    }
    AlertDialog(onDismissRequest = onClose,
        confirmButton = { TextButton(onClick = onClose) { Text("닫기") } },
        title = { Text("기기 간 저장 동기화", fontWeight = FontWeight.ExtraBold) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("같은 코드를 넣은 기기끼리 슬롯 저장, 자동 저장, 게임 안 세이브를 주고받아요. 게임을 켤 때 받고, 저장하거나 나갈 때 올려요. 롬 파일 이름이 기기마다 같아야 해요.",
                    fontSize = 13.sp, color = Color(0xA6000000))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(code, { code = it }, label = { Text("동기화 코드") }, singleLine = true,
                        placeholder = { Text("예: K7PM-2QXA") }, modifier = Modifier.weight(1f),
                        textStyle = androidx.compose.ui.text.TextStyle(fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 17.sp))
                    Spacer(Modifier.width(8.dp))
                    TextButton(onClick = { code = Sync.newCode() }) { Text("새 코드") }
                }
                Text("처음 쓰는 기기에서 새 코드를 만들고, 다른 기기엔 그 코드를 그대로 넣어요. 코드를 아는 사람은 저장을 볼 수 있으니 남한테 알려주지 마세요.",
                    fontSize = 11.sp, color = Color(0x80000000))
                OutlinedTextField(server, { server = it }, label = { Text("서버 주소") }, singleLine = true,
                    placeholder = { Text("192.168.0.10:8270") }, modifier = Modifier.fillMaxWidth(),
                    textStyle = androidx.compose.ui.text.TextStyle(fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace, fontSize = 13.sp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { server = Sync.DEFAULT_SERVER }) { Text("파이(테일스케일)") }
                }
                Text("같은 와이파이면 서버를 깐 맥의 주소(맥 앱 동기화 창에 나와요)를, 테일스케일을 쓰면 파이 주소를 넣어요.",
                    fontSize = 11.sp, color = Color(0x80000000))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { Sync.setCode(ctx, code); code = Sync.code(ctx); Sync.setServer(ctx, server); server = Sync.server(ctx); on = Sync.enabled(ctx); syncNow() },
                        enabled = code.trim().length >= 4 && !busy) { Text("저장") }
                    if (on) OutlinedButton(onClick = { syncNow() }, enabled = !busy) { Text("지금 맞추기") }
                }
                if (on) TextButton(onClick = { Sync.setCode(ctx, ""); code = ""; on = false; status = "동기화를 껐어요" }) { Text("동기화 끄기") }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (busy) { CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp); Spacer(Modifier.width(8.dp)) }
                    Text(status.ifEmpty { lastText() }, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = Color(0x8C000000))
                }
            }
        })
}
