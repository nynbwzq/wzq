#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <sstream>
#include <sys/wait.h>
#include <signal.h>
#include <cstdlib>

using namespace std;

// ==================== Fake JNI 环境 ====================
typedef void* JNIEnv; typedef void* jobject; typedef void* jmethodID; typedef int jint; typedef int* jintArray;
void* fake_jni_table[256] = {0};
void* fake_env_ptr = fake_jni_table;
jint Fake_CallIntMethod(JNIEnv* e, jobject o, jmethodID m, ...) { return 1; }
void Fake_CallVoidMethod(JNIEnv* e, jobject o, jmethodID m, ...) {}
jintArray Fake_NewIntArray(JNIEnv* e, jint l) { return new int[l]; }
void Fake_GetIntArrayRegion(JNIEnv* e, jintArray a, jint s, jint l, jint* b) { if(a && b) memcpy(b, a+s, l*sizeof(jint)); }
void setup_fake_jni() { for(int i=0; i<256; i++) fake_jni_table[i] = (void*)Fake_CallVoidMethod; }

// ==================== 原版引擎接口 ====================
typedef jint (*TEFACES_t)(JNIEnv*, jobject, jint, jint, jint, jint, jintArray);
TEFACES_t TEFACES = nullptr;
int chessList[227] = {0};
int step_num = 0;

bool init_engine() {
    setup_fake_jni();
    // 在服务器环境中，直接读取当前目录下的文件
    string so_src = "./libmmai.so";
    string book_src = "./bg.png";

    void* handle = dlopen(so_src.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) { cout << "❌ dlopen失败: " << dlerror() << endl; return false; }
    
    auto LoadLib = (void(*)(char*))dlsym(handle, "_Z7LoadLibPc");
    auto rap_init = (void(*)())dlsym(handle, "_Z8rap_initv");
    TEFACES = (TEFACES_t)dlsym(handle, "Java_com_bf92_ai_AI_TEFACES");
    
    if (!TEFACES) { cout << "❌ 找不到 TEFACES 符号" << endl; return false; }
    if (rap_init) rap_init();
    if (LoadLib) {
        char path_buf[256]; strcpy(path_buf, book_src.c_str()); LoadLib(path_buf);
    }
    return true;
}

// ==================== 网页 UI 代码 (内嵌在 C++) ====================
const string HTML_PAGE = R"(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>五子棋终结者 - 原版网页版</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; }
        body { background-color: #121216; color: #fff; font-family: sans-serif; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
        .header { padding: 15px; background: #1a1a24; text-align: center; font-weight: bold; border-bottom: 1px solid #333; color: #64ffda; }
        .game-container { flex: 1; display: flex; flex-direction: column; }
        .canvas-wrapper { flex: 1; display: flex; justify-content: center; align-items: center; padding: 10px; }
        canvas { background-color: #dcb46e; border-radius: 8px; display: block; touch-action: none; box-shadow: 0 5px 15px rgba(0,0,0,0.5); }
        .terminal { height: 120px; background: #000; padding: 10px; font-size: 12px; color: #0f0; overflow-y: auto; border-top: 1px solid #333; font-family: monospace; }
        .controls { padding: 10px; background: #1a1a24; display: flex; gap: 10px; }
        .btn { flex: 1; padding: 12px; text-align: center; background: #333; border-radius: 6px; font-size: 14px; cursor: pointer; color: white; border: none; font-weight: bold; }
        .btn:active { background: #555; }
    </style>
</head>
<body>
    <div class="header">五子棋终结者 (纯正原版引擎)</div>
    <div class="game-container">
        <div class="canvas-wrapper"><canvas id="boardCanvas"></canvas></div>
    </div>
    <div class="terminal" id="logBox">正在连接原版引擎...</div>
    <div class="controls">
        <button class="btn" onclick="resetGame()">新局</button>
        <button class="btn" onclick="swapColor()">换边</button>
    </div>

    <script>
        const N = 15;
        let CELL_SIZE = 30, PADDING = 15;
        let canvas, ctx;
        let board = [];
        let myColor = 1, aiColor = 2;
        let isMyTurn = true, gameOver = false;
        let lastMove = { x: -1, y: -1 };

        function log(msg) {
            const term = document.getElementById('logBox');
            term.innerText = msg;
        }

        window.onload = () => {
            canvas = document.getElementById('boardCanvas');
            ctx = canvas.getContext('2d');
            initCanvas();
            resetGame();
            window.addEventListener('resize', initCanvas);
        };

        function initCanvas() {
            const wrapper = document.querySelector('.canvas-wrapper');
            const size = Math.min(wrapper.clientWidth, wrapper.clientHeight) - 20;
            CELL_SIZE = Math.floor((size - PADDING * 2) / (N - 1));
            const canvasSize = CELL_SIZE * (N - 1) + PADDING * 2;
            canvas.width = canvasSize;
            canvas.height = canvasSize;
            canvas.style.width = canvasSize + 'px';
            canvas.style.height = canvasSize + 'px';
            drawBoard();
        }

        function resetGame() {
            board = Array.from({ length: N }, () => Array(N).fill(0));
            gameOver = false;
            lastMove = { x: -1, y: -1 };
            isMyTurn = (myColor === 1);
            drawBoard();
            log("新局开始！你执" + (myColor === 1 ? "黑" : "白"));
            
            fetch('/new').then(() => {
                if (!isMyTurn) startAI(-1, -1);
            });
        }

        function swapColor() {
            myColor = 3 - myColor;
            aiColor = 3 - myColor;
            resetGame();
        }

        canvas.addEventListener('mousedown', onTouch);
        canvas.addEventListener('touchstart', onTouch, {passive: false});

        function onTouch(e) {
            if (e.cancelable) e.preventDefault();
            if (gameOver || !isMyTurn) return;
            
            const rect = canvas.getBoundingClientRect();
            let clientX = e.clientX || (e.touches && e.touches[0].clientX);
            let clientY = e.clientY || (e.touches && e.touches[0].clientY);
            
            const canvasX = (clientX - rect.left) * (canvas.width / rect.width);
            const canvasY = (clientY - rect.top) * (canvas.height / rect.height);
            let c = Math.round((canvasX - PADDING) / CELL_SIZE);
            let r = Math.round((canvasY - PADDING) / CELL_SIZE);

            if (r >= 0 && r < N && c >= 0 && c < N && board[r][c] === 0) {
                makeMove(r, c, myColor);
                if (!gameOver) {
                    isMyTurn = false;
                    setTimeout(() => startAI(r, c), 50);
                }
            }
        }

        function makeMove(r, c, player) {
            board[r][c] = player;
            lastMove = { x: c, y: r };
            drawBoard();
            log((player === 1 ? '黑' : '白') + "落子: " + String.fromCharCode(65 + c) + (r + 1));
            if (checkWin(r, c, player)) {
                gameOver = true;
                log(player === myColor ? "🎉 你赢了！" : "💀 AI 赢了！");
            }
        }

        function startAI(r, c) {
            log("终结者原版引擎正在思考 (5秒)...");
            fetch(`/move?r=${r}&c=${c}&color=${myColor}&time=5`)
                .then(res => res.json())
                .then(data => {
                    let res = data.move;
                    if (res >= 0 && res < 225) {
                        let ai_r = Math.floor(res / 15);
                        let ai_c = res % 15;
                        if (board[ai_r][ai_c] === 0) {
                            makeMove(ai_r, ai_c, aiColor);
                        }
                    }
                    if (!gameOver) isMyTurn = true;
                }).catch(err => {
                    log("请求后台失败，请确保 C++ 程序正在运行");
                    isMyTurn = true;
                });
        }

        function checkWin(r, c, p) {
            const dx = [0, 1, 1, 1], dy = [1, 0, 1, -1];
            for (let d = 0; d < 4; d++) {
                let cnt = 1;
                for (let k = 1; k < 5; k++) { let rr = r + dx[d]*k, cc = c + dy[d]*k; if (rr<0||rr>=N||cc<0||cc>=N||board[rr][cc]!==p) break; cnt++; }
                for (let k = 1; k < 5; k++) { let rr = r - dx[d]*k, cc = c - dy[d]*k; if (rr<0||rr>=N||cc<0||cc>=N||board[rr][cc]!==p) break; cnt++; }
                if (cnt >= 5) return true;
            }
            return false;
        }

        function drawBoard() {
            ctx.fillStyle = '#dcb46e';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.strokeStyle = '#333';
            ctx.lineWidth = 1;
            for (let i = 0; i < N; i++) {
                let pos = PADDING + i * CELL_SIZE;
                ctx.beginPath(); ctx.moveTo(PADDING, pos); ctx.lineTo(canvas.width - PADDING, pos); ctx.stroke();
                ctx.beginPath(); ctx.moveTo(pos, PADDING); ctx.lineTo(pos, canvas.height - PADDING); ctx.stroke();
            }
            const stars = [[3,3], [11,3], [7,7], [3,11], [11,11]];
            ctx.fillStyle = '#333';
            for (let [r, c] of stars) {
                ctx.beginPath(); ctx.arc(PADDING + c*CELL_SIZE, PADDING + r*CELL_SIZE, 3, 0, 2*Math.PI); ctx.fill();
            }
            for (let r = 0; r < N; r++) {
                for (let c = 0; c < N; c++) {
                    if (board[r][c] === 0) continue;
                    let cx = PADDING + c * CELL_SIZE;
                    let cy = PADDING + r * CELL_SIZE;
                    ctx.beginPath(); ctx.arc(cx, cy, CELL_SIZE / 2 - 2, 0, 2*Math.PI);
                    ctx.fillStyle = board[r][c] === 1 ? '#000' : '#fff';
                    ctx.fill();
                    ctx.strokeStyle = board[r][c] === 1 ? '#555' : '#ccc';
                    ctx.stroke();
                }
            }
            if (lastMove.x >= 0) {
                let cx = PADDING + lastMove.x * CELL_SIZE;
                let cy = PADDING + lastMove.y * CELL_SIZE;
                ctx.beginPath(); ctx.arc(cx, cy, 4, 0, 2*Math.PI); ctx.fillStyle = 'red'; ctx.fill();
            }
        }
    </script>
</body>
</html>
)";

// ==================== HTTP 处理引擎 ====================
void handle_client(int client_sock) {
    char buffer[4096] = {0};
    read(client_sock, buffer, sizeof(buffer) - 1);
    string request(buffer);
    
    string response_body = "";
    string content_type = "text/plain";

    if (request.find("GET / ") != string::npos) {
        response_body = HTML_PAGE;
        content_type = "text/html; charset=utf-8";
    } 
    else if (request.find("GET /new ") != string::npos) {
        step_num = 0;
        memset(chessList, 0, sizeof(chessList));
        TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x2, 1, 0, 0, chessList);
        response_body = "{\"status\":\"ok\"}";
        content_type = "application/json";
    }
    else if (request.find("GET /move") != string::npos) {
        int r = 0, c = 0, color = 1, t_sec = 2;
        size_t pos_r = request.find("r="); if(pos_r != string::npos) r = stoi(request.substr(pos_r+2));
        size_t pos_c = request.find("c="); if(pos_c != string::npos) c = stoi(request.substr(pos_c+2));
        size_t pos_color = request.find("color="); if(pos_color != string::npos) color = stoi(request.substr(pos_color+6));
        size_t pos_time = request.find("time="); if(pos_time != string::npos) t_sec = stoi(request.substr(pos_time+5));

        // 记录玩家落子
        if (r != -1 && c != -1) {
            int pos = r * 15 + c;
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x3, color, pos, step_num, chessList);
            chessList[step_num++] = pos;
        }

        // 🔥 核心防闪退：fork 子进程计算
        int opp = (color == 1) ? 2 : 1;
        pid_t pid = fork();
        if (pid == 0) {
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x7, opp, 0, t_sec, chessList);
            jint res = TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x6, opp, 0, step_num, chessList);
            
            string json = "{\"move\":" + to_string(res) + "}";
            string http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(json.length()) + "\r\n\r\n" + json;
            write(client_sock, http_response.c_str(), http_response.length());
            close(client_sock);
            _exit(0);
        } else {
            waitpid(pid, nullptr, 0);
        }
        close(client_sock);
        return;
    }

    string http_response = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type + "\r\nContent-Length: " + to_string(response_body.length()) + "\r\n\r\n" + response_body;
    write(client_sock, http_response.c_str(), http_response.length());
    close(client_sock);
}

int main() {
    if (!init_engine()) {
        cout << "❌ 引擎加载失败！请检查当前目录下的 libmmai.so 和 bg.png" << endl;
        return 1;
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cout << "❌ 端口 8080 绑定失败！" << endl;
        return 1;
    }
    
    listen(server_fd, 10);
    cout << "========================================" << endl;
    cout << "🚀 原版网页服务器已启动！" << endl;
    cout << "👉 监听端口: 8080" << endl;
    cout << "========================================" << endl;
    
    while (true) {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket >= 0) {
            handle_client(new_socket);
        }
    }
    return 0;
}