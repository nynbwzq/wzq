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

typedef void* JNIEnv; typedef void* jobject; typedef void* jmethodID; typedef int jint; typedef int* jintArray;
void* fake_jni_table[256] = {0};
void* fake_env_ptr = fake_jni_table;
jint Fake_CallIntMethod(JNIEnv* e, jobject o, jmethodID m, ...) { return 1; }
void Fake_CallVoidMethod(JNIEnv* e, jobject o, jmethodID m, ...) {}
jintArray Fake_NewIntArray(JNIEnv* e, jint l) { return new int[l]; }
void Fake_GetIntArrayRegion(JNIEnv* e, jintArray a, jint s, jint l, jint* b) { if(a && b) memcpy(b, a+s, l*sizeof(jint)); }
void setup_fake_jni() { for(int i=0; i<256; i++) fake_jni_table[i] = (void*)Fake_CallVoidMethod; }

typedef jint (*TEFACES_t)(JNIEnv*, jobject, jint, jint, jint, jint, jintArray);
TEFACES_t TEFACES = nullptr;
int chessList[227] = {0};
int step_num = 0;

bool init_engine() {
    setup_fake_jni();
    void* handle = dlopen("./libmmai.so", RTLD_LAZY | RTLD_LOCAL);
    if (!handle) { cout << "❌ dlopen失败: " << dlerror() << endl; return false; }
    auto LoadLib = (void(*)(char*))dlsym(handle, "_Z7LoadLibPc");
    auto rap_init = (void(*)())dlsym(handle, "_Z8rap_initv");
    TEFACES = (TEFACES_t)dlsym(handle, "Java_com_bf92_ai_AI_TEFACES");
    if (!TEFACES) { cout << "❌ 找不到 TEFACES 符号" << endl; return false; }
    if (rap_init) rap_init();
    if (LoadLib) { char path_buf[256]; strcpy(path_buf, "./bg.png"); LoadLib(path_buf); }
    return true;
}

void handle_client(int client_sock) {
    char buffer[4096] = {0};
    read(client_sock, buffer, sizeof(buffer) - 1);
    string request(buffer);
    string response_body = "Server is running.";
    string content_type = "text/plain";

    // 1. 放行 CORS 预检
    if (request.find("OPTIONS ") == 0) {
        string http_response = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n\r\n";
        write(client_sock, http_response.c_str(), http_response.length());
        close(client_sock); return;
    }

    // 2. 新局
    if (request.find("GET /new ") != string::npos) {
        step_num = 0; memset(chessList, 0, sizeof(chessList));
        TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x2, 1, 0, 0, chessList);
        response_body = "{\"status\":\"ok\"}";
        content_type = "application/json";
    }
    // 3. 悔棋
    else if (request.find("GET /undo ") != string::npos) {
        if (step_num >= 2) {
            step_num -= 2;
            chessList[step_num] = 0;
            chessList[step_num + 1] = 0;
            response_body = "{\"status\":\"ok\"}";
        } else {
            step_num = 0;
            response_body = "{\"status\":\"ok\"}";
        }
        content_type = "application/json";
    }
    // 4. 落子与 AI 计算
    else if (request.find("GET /move") != string::npos) {
        int r = 0, c = 0, color = 1;
        int t_ms = 5000; // 默认5秒
        
        size_t pos_r = request.find("r="); if(pos_r != string::npos) r = stoi(request.substr(pos_r+2));
        size_t pos_c = request.find("c="); if(pos_c != string::npos) c = stoi(request.substr(pos_c+2));
        size_t pos_color = request.find("color="); if(pos_color != string::npos) color = stoi(request.substr(pos_color+6));
        size_t pos_time = request.find("time="); if(pos_time != string::npos) t_ms = stoi(request.substr(pos_time+5));

        // 恢复前端传过来的历史落子
        if (r != -1 && c != -1) {
            int pos = r * 15 + c;
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x3, color, pos, step_num, chessList);
            chessList[step_num++] = pos;
        }

        int opp = (color == 1) ? 2 : 1;
        int time_sec = t_ms / 1000;
        
        // 🔥 核心移植：完美复刻 pn.cpp 的极速秒下逻辑
        int actual_time = (time_sec == 0) ? 1 : time_sec; 

        pid_t pid = fork();
        if (pid == 0) {
            // 子进程：调用原版 so 进行计算
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x7, opp, 0, actual_time, chessList);
            jint res = TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x6, opp, 0, step_num, chessList);
            string json = "{\"move\":" + to_string(res) + "}";
            string http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(json.length()) + "\r\n\r\n" + json;
            write(client_sock, http_response.c_str(), http_response.length());
            close(client_sock);
            _exit(0);
        } else if (pid > 0) {
            // 父进程：非阻塞等待，防死锁（彻底解决不下棋卡死的问题）
            int max_wait_ms = actual_time * 1000 + 8000; 
            int waited = 0;
            bool finished = false;
            while (waited < max_wait_ms) {
                if (waitpid(pid, nullptr, WNOHANG) == pid) {
                    finished = true; break;
                }
                usleep(10000); // 每次睡 10ms
                waited += 10;
            }
            if (!finished) {
                // 引擎超时，直接强杀防卡死
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                string json = "{\"move\":-1}";
                string http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(json.length()) + "\r\n\r\n" + json;
                write(client_sock, http_response.c_str(), http_response.length());
            }
            close(client_sock);
            return;
        }
    }
    string http_response = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type + "\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(response_body.length()) + "\r\n\r\n" + response_body;
    write(client_sock, http_response.c_str(), http_response.length());
    close(client_sock);
}

int main() {
    if (!init_engine()) return 1;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in address;
    address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(8080);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    listen(server_fd, 10);
    while (true) {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket >= 0) handle_client(new_socket);
    }
    return 0;
}
