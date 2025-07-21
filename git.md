1.git config --global user.name "bllsll"

2. git config --global user.email "bllsll@163.com"

1. **生成 SSH 密钥**（若没有）：
   打开终端，执行：

   ```bash
   ssh-keygen -t ed25519 -C "你的GitHub邮箱"
   ```

   按提示回车（默认路径和空密码即可）。

2. **将公钥添加到 GitHub**：

   - 查看公钥内容：

     ```bash
     cat ~/.ssh/id_ed25519.pub
     ```

   - 复制输出的全部内容。

   - 登录 GitHub，进入 [Settings → SSH and GPG keys → New SSH key](https://github.com/settings/ssh/new)，粘贴公钥并保存。

1. **修改仓库远程地址为 SSH 格式**：
   进入你的本地仓库目录，执行：

   ```bash
   git remote set-url origin git@github.com:用户名/仓库名.git
   ```

   例如：`git remote set-url origin git@github.com:bllsll/你的仓库名.git`。

2. 之后推送代码时无需输入密码：

   ```bash
   git push
   ```