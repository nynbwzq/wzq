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
#include <vector>
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
    char buffer[8192] = {0};
    read(client_sock, buffer, sizeof(buffer) - 1);
    string request(buffer);
    string response_body = "Server is running.";
    string content_type = "text/plain";

    // 放行 CORS 预检
    if (request.find("OPTIONS ") == 0) {
        string http_response = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n\r\n";
        write(client_sock, http_response.c_str(), http_response.length());
        close(client_sock); return;
    }

    // 修复 /new 和 /new?wakeup=xxx 的匹配
    if (request.find("GET /new") != string::npos) {
        step_num = 0; memset(chessList, 0, sizeof(chessList));
        TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x2, 1, 0, 0, chessList);
        response_body = "{\"status\":\"ok\"}";
        content_type = "application/json";
    }
    else if (request.find("GET /move") != string::npos) {
        int color = 1, t_ms = 5000;
        string list_str = "";
        
        size_t pos_color = request.find("color="); if(pos_color != string::npos) color = stoi(request.substr(pos_color+6));
        size_t pos_time = request.find("time="); if(pos_time != string::npos) t_ms = stoi(request.substr(pos_time+5));
        size_t pos_list = request.find("list="); 
        if(pos_list != string::npos) {
            size_t end = request.find("&", pos_list);
            list_str = request.substr(pos_list + 5, end == string::npos ? string::npos : end - (pos_list + 5));
        }

        // 每次请求先清空引擎，重演历史记录
        step_num = 0;
        memset(chessList, 0, sizeof(chessList));
        TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x2, 1, 0, 0, chessList);

        if (!list_str.empty()) {
            stringstream ss(list_str);
            string item;
            vector<int> tokens;
            while (getline(ss, item, ',')) {
                if (!item.empty()) tokens.push_back(stoi(item));
            }
            for (size_t i = 0; i + 2 < tokens.size(); i += 3) {
                int r = tokens[i], c = tokens[i+1], p = tokens[i+2];
                int pos = r * 15 + c;
                TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x3, p, pos, step_num, chessList);
                chessList[step_num++] = pos;
            }
            cout << "✅ 重演完毕，当前步数: " << step_num << endl;
        }

        int time_sec = t_ms / 1000;
        int actual_time = (time_sec == 0) ? 1 : time_sec; 
        // 为了防止 502，把最大思考时间强行限制在 10 秒，防止网关超时
        if (actual_time > 10) actual_time = 10;

        int pipefd[2];
        if (pipe(pipefd) == -1) { close(client_sock); return; }
        pid_t pid = fork();
        if (pid == 0) {
            close(pipefd[0]); 
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x7, color, 0, actual_time, chessList);
            jint res = TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x6, color, 0, step_num, chessList);
            write(pipefd[1], &res, sizeof(res));
            close(pipefd[1]);
            _exit(0);
        } else if (pid > 0) {
            close(pipefd[1]); 
            int res = -1;
            int status;
            // 这里直接阻塞等待子进程结束，不搞花里胡哨的定时器，防止出现底层 Bug
            waitpid(pid, &status, 0); 
            read(pipefd[0], &res, sizeof(res));
            close(pipefd[0]);
            
            cout << "🤖 AI 计算完成，落子: " << res << endl;
            string json = "{\"move\":" + to_string(res) + "}";
            string http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(json.length()) + "\r\n\r\n" + json;
            write(client_sock, http_response.c_str(), http_response.length());
            close(client_sock); return;
        }
    }
    string http_response = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type + "\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(response_body.length()) + "\r\n\r\n" + response_body;
    write(client_sock, http_response.c_str(), http_response.length());
    close(client_sock);
}

int main() {
    // 删除了 signal(SIGCHLD, SIG_IGN); 防止 AWS 底层冲突
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
        if (new_socket >= 0) {
            pid_t pid = fork();
            if (pid == 0) {
                close(server_fd);
                handle_client(new_socket);
                _exit(0);
            } else {
                close(new_socket);
                waitpid(pid, nullptr, WNOHANG); // 父进程顺手回收一下僵尸进程
            }
        }
    }
    return 0;
}
