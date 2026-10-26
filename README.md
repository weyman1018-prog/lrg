# lrg

`lrg` 是一个用 C 编写的简易文本搜索工具，按行输出包含指定字符串的内容，支持忽略 ASCII 大小写、行号、匹配行计数、多文件和标准输入。

文件内容仅通过 `open`、`read`、`close` 读取，自行管理文件描述符、缓冲区和错误处理，不使用 `fopen`、`fgets`、`getline` 读取文件内容。

我的调试过程没有构建makefile，而是如题解中写的直接在bash中编译并运行。

## 编译

环境：Linux、GCC。在项目根目录执行：

```bash
gcc -std=gnu11 -Wall -Wextra -g src/lrg.c -o lrg
```

生成的可执行文件为 `./lrg`。

## 用法

```text
lrg [-nic] PATTERN [FILE...]
```

| 参数 | 含义 |
| --- | --- |
| `PATTERN` | 待查找的普通字符串 |
| `FILE...` | 零个或多个文件路径；省略时读取标准输入 |
| `-n` | 显示匹配行的行号，从 1 开始 |
| `-i` | 忽略 ASCII 英文字母大小写，输出保留原文 |
| `-c` | 只输出每个输入的匹配行数 |

选项放在模式前，可以合并，例如 `-ni` 等价于 `-n -i`。未知选项或缺少模式时返回用法错误。

### 匹配与输出规则

- 默认区分大小写，使用普通子串匹配，不支持正则表达式。
- 空模式 `""` 匹配所有实际存在的行，包括空行；空输入没有行。
- 同一行中关键词出现多次，仍只输出或计数一次。
- 同时指定 `-n` 和 `-c` 时，`-c` 优先，只输出计数。
- 多文件时添加文件名前缀；行号和计数均对每个文件独立计算。
- 单文件和省略文件参数的标准输入不显示文件名前缀。
- 末行没有换行符也会参与匹配，输出时补上换行符。
- 支持超过一次读取缓冲区大小的长行，实际长度受可用内存限制。

## 示例

准备示例文件：

```bash
printf 'boot ok\nERROR: retry\nerror: stop\n' > sample.log
printf 'all good\nError: wait' > notes.txt
```

### 普通匹配

```bash
./lrg error sample.log
```

输出：

```text
error: stop
```

### 忽略大小写、显示行号

```bash
./lrg -ni error sample.log
```

输出：

```text
2:ERROR: retry
3:error: stop
```

### 统计匹配行数

```bash
./lrg -ci error sample.log
```

输出：

```text
2
```

### 多文件匹配

```bash
./lrg -ni error sample.log notes.txt
```

输出：

```text
sample.log:2:ERROR: retry
sample.log:3:error: stop
notes.txt:2:Error: wait
```

使用 `./lrg -ci error sample.log notes.txt` 时，输出：

```text
sample.log:2
notes.txt:1
```

### 标准输入

```bash
printf 'hello\nERROR: retry\nerror: stop' | ./lrg -ni error
```

输出：

```text
2:ERROR: retry
3:error: stop
```

也可以执行 `./lrg error` 后从键盘逐行输入待搜索内容。在空行开头按 `Ctrl+D` 结束输入。命令中的 `error` 是搜索模式，程序运行后输入的文字是被搜索的数据。

## 退出码与错误处理

| 退出码 | 含义 |
| --- | --- |
| `0` | 无错误，且至少有一行匹配 |
| `1` | 无错误，但所有输入均无匹配 |
| `2` | 参数错误，或打开、读取、关闭、内存分配、输出等处理失败 |

匹配结果写入标准输出，错误说明写入标准错误。多个文件中某个文件打开失败时，仍继续处理后续文件；只要发生错误，最终返回 `2`，即使其他文件存在匹配。

检查无匹配时的退出码：

```bash
./lrg zzz sample.log
echo "exit=$?"
```

输出：

```text
exit=1
```

`$?` 表示 Shell 中上一条命令的退出码，应在运行待检查命令后立即读取。

## 实现结构

源代码位于 `src/lrg.c`。

| 函数 | 职责 |
| --- | --- |
| `main` | 解析参数，选择输入，打开和关闭文件，汇总错误与匹配状态，刷新标准输出并确定退出码 |
| `process_fd` | 循环读取字节、拼接完整行、动态扩容、处理末行并统计匹配行数 |
| `handle_line` | 处理一条完整行，判断匹配并根据选项输出 |
| `line_matches` | 普通子串匹配或忽略 ASCII 大小写的匹配 |
| `ascii_lower` | 将 ASCII 大写英文字母转换为小写，其他字节保持不变 |

读取缓冲区每次最多接收 4096 字节。动态行缓冲区从 128 字节开始，跨多次读取保留当前行；空间不足时使用 `realloc` 倍增，并检查容量溢出和分配失败。

遇到换行符时处理当前行并复用行缓冲区。输入结束后处理尚未输出的末行，再释放内存。内存占用随最长行长度增长，无需一次加载整个文件。

`read` 因 `EINTR` 中断时重试；其他读取错误报告后返回。正常结果输出检查 `printf` 的返回值，最后通过 `fflush(stdout)` 检查缓冲数据写出时的错误。

## 测试

| 场景 | 命令 | 预期 |
| --- | --- | --- |
| 空输入 | `printf '' \| ./lrg -c ""` | 输出 `0`，退出码 `1` |
| 空行与末行无换行 | `printf 'hello\n\nERROR\nerror' \| ./lrg -ni error` | 输出 `3:ERROR`、`4:error`，退出码 `0` |
| 空模式匹配空行 | `printf '\n' \| ./lrg -c ""` | 输出 `1`，退出码 `0` |
| 非法选项 | `./lrg -x error sample.log` | 用法错误，退出码 `2` |
| 缺少模式 | `./lrg -n` | 用法错误，退出码 `2` |

每次执行待测命令后，立即运行 `echo $?` 检查退出码。

### 超长行与内存检查

编译检查版本：

```bash
gcc -std=gnu11 -Wall -Wextra -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    src/lrg.c -o lrg-check
```

运行：

```bash
printf 'hello\n\nERROR\nerror' | ./lrg-check -ni error
python3 -c 'print("a" * 1048576 + "error")' | ./lrg-check -ci error
```

预期第一条输出 `3:ERROR` 和 `4:error`，第二条输出 `1`。第二条使用 Python 3 生成含 1048576 个 `a` 和一个 `error` 子串的长行，验证跨多次读取和扩容后的匹配。

Sanitizer 用于发现运行中触发的部分内存错误和未定义行为；无报告不代表所有输入都已覆盖。Python 3 仅用于生成测试数据，运行 `lrg` 本身不依赖 Python。

### 输出失败

Linux 的 `/dev/full` 会使写入失败，可用于验证输出错误及退出码：

```bash
printf 'error\n' | ./lrg error > /dev/full
echo $?
```

预期向标准错误报告输出失败，并返回 `2`。

## 范围与限制

- 面向文本输入，使用以 `\0` 结尾的 C 字符串，不支持含内嵌 NUL 字节的二进制匹配。
- `-i` 仅处理 ASCII 英文字母，不实现 Unicode 大小写折叠。
- 不实现完整 GNU grep 的选项集合和正则表达式语法。

## 版本控制

提交源代码、README 和实际编写的测试文件；可执行文件及其他构建产物不加入版本控制。
`.gitignore` 中包含：

```gitignore
/lrg
/lrg-check
/a.out
/build/
*.o
```

