


```bash
# 卸载旧版本
sudo yum remove protobuf-compiler

# 下载并编译 proto3（以 3.20.3 为例，可替换为更高版本）
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.20.3/protobuf-all-3.20.3.tar.gz
tar -zxvf protobuf-all-3.20.3.tar.gz
cd protobuf-3.20.3
./configure
make
sudo make install
sudo ldconfig  # 刷新动态链接库

# 验证版本
protoc --version  # 应显示 3.20.3 或更高
```