FROM --platform=linux/arm64 ubuntu:22.04

# 1. 替换为清华源，解决下载慢、网络超时的问题
RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list && \
    sed -i 's@//.*security.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list

# 防止安装时弹出交互界面卡住
ENV DEBIAN_FRONTEND=noninteractive

# 2. 加上 --no-install-recommends，只装最核心的，节省 512MB 内存
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ gcc cmake libelf-dev ca-certificates wget unzip && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# 3. 编译伪造库和 C++ 服务器
RUN gcc -shared -fPIC -o liblog.so fake_android.c && \
    gcc -shared -fPIC -o libandroid.so fake_android.c && \
    g++ -std=c++17 -o server server.cpp -ldl

# 4. 改用 wget 下载处理工具（去掉 git），更轻量
RUN wget -q https://github.com/termux/termux-elf-cleaner/archive/refs/heads/master.zip && \
    unzip -q master.zip && \
    cd termux-elf-cleaner-master && \
    cmake . && make && make install && cd .. && \
    termux-elf-cleaner libmmai.so

EXPOSE 8080
CMD ["./server"]
