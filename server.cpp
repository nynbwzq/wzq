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

// ==================== Fake JNI ====================
typedef void* JNIEnv; typedef void* jobject; typedef void* jmethodID; typedef int jint; typedef int* jintArray;
void* fake_jni_table[256] = {0};
void* fake_env_ptr = fake_jni_table;
jint Fake_CallIntMethod(JNIEnv* e, jobject o, jmethodID m, ...) { return 1; }
void Fake_CallVoidMethod(JNIEnv* e, jobject o, jmethodID m, ...) {}
jintArray Fake_NewIntArray(JNIEnv* e, jint l) { return new int[l]; }
void Fake_GetIntArrayRegion(JNIEnv* e, jintArray a, jint s, jint l, jint* b) { if(a && b) memcpy(b, a+s, l*sizeof(jint)); }
void setup_fake_jni() { for(int i=0; i<256; i++) fake_jni_table[i] = (void*)Fake_CallVoidMethod; }

// ==================== 原版引擎 ====================
typedef jint (*TEFACES_t)(JNIEnv*, jobject, jint, jint, jint, jint, jintArray);
TEFACES_t TEFACES = nullptr;
int chessList[227] = {0};
int step_num = 0;

bool init_engine() {
    setup_fake_jni();
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

// ==================== HTTP 处理 ====================
void handle_client(int client_sock) {
    char buffer[4096] = {0};
    read(client_sock, buffer, sizeof(buffer) - 1);
    string request(buffer);
    
    string response_body = "Server is running.";
    string content_type = "text/plain";

    // 放行 OPTIONS 预检
    if (request.find("OPTIONS ") == 0) {
        string http_response = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n\r\n";
        write(client_sock, http_response.c_str(), http_response.length());
        close(client_sock);
        return;
    }

    if (request.find("GET /new ") != string::npos) {
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

        if (r != -1 && c != -1) {
            int pos = r * 15 + c;
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x3, color, pos, step_num, chessList);
            chessList[step_num++] = pos;
        }

        int opp = (color == 1) ? 2 : 1;
        pid_t pid = fork();
        if (pid == 0) {
            TEFACES((JNIEnv*)&fake_env_ptr, nullptr, 0x7, opp, 0, t_sec / 1000, chessList);
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

    string http_response = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type + "\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + to_string(response_body.length()) + "\r\n\r\n" + response_body;
    write(client_sock, http_response.c_str(), http_response.length());
    close(client_sock);
}

int main() {
    if (!init_engine()) return 1;
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    
    listen(server_fd, 10);
    
    while (true) {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket >= 0) {
            handle_client(new_socket);
        }
    }
    return 0;
}
