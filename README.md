# node-process-watcher

A lightweight real-time monitoring system for all process information.
轻量级的实时监控系统上所有的进程信息。可以获取系统所有的进程信息、进程的子进程信息，杀死进程以及子进程，读取文件夹大小，管理 Windows / macOS 系统代理，以及在 Windows 服务中以当前登录用户身份启动进程。

## 功能一览

1. 实时监控所有进程信息（CPU、内存、用户名、进程名）
2. 获取系统全部进程、指定进程的子进程信息
3. 杀死进程及其所有子进程
4. 统计文件夹下的文件数量与总大小（子线程执行，不阻塞主线程）
5. 读取/设置 Windows、macOS 系统 HTTP 代理
6. 判断当前进程是否以管理员 / root 权限运行
7. 在 Windows Service（LocalSystem）中以当前登录用户身份启动进程 / 执行命令并获取输出
8. 查询系统用户列表、文件所有者，UID 转用户名

## 安装

```bash
npm install node-process-watcher
```

本项目使用 `node-addon-api` 编写原生扩展，安装时会尝试从 GitHub 下载预构建产物，如果网络无法访问 GitHub 则会退化为本地编译（需要 `node-gyp` 与对应平台的编译工具链）。

## 快速开始

```js
const { node_process_watcher } = require("node-process-watcher");
// or import { node_process_watcher } from "node-process-watcher";

// 每秒回调一次系统上所有进程的信息
node_process_watcher.on("screen1", (list) => {
    console.log(list[0]);
    // { id: 4, user_name: 'SYSTEM', cpu: 0, mem: 0, name: 'System' }
    node_process_watcher.close("screen1"); // 关闭该监听
});

// 只监听特定进程（只是过滤作用）
node_process_watcher.on("screen2", (list) => {
    node_process_watcher.pids("screen2", [list[0].id]);
    console.log(list[0]);
    if (list.length === 1) {
        // 所有 on 都关闭后才会真正停止后台监听线程
        node_process_watcher.close("screen2");
    }
});

node_process_watcher.get_all_pid();      // 获取所有进程的 pid / ppid
node_process_watcher.get_all_pid(1234);  // 获取 1234 的直接子进程
node_process_watcher.kill_process(1234, true); // 杀死 1234 及其所有子进程
```

---

## API 说明

### 进程实时监控

#### `on(key, callback, pids?)`

注册一个监听，回调参数为 `process_info[]`。

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `key` | `string` | 监听标识，同一个 `key` 重复调用会被忽略 |
| `callback` | `(list: process_info[]) => any` | 每次采集完成后的回调 |
| `pids` | `number[]` | 可选，只回调这些 pid 的信息（过滤作用，不影响采集） |

`process_info`：

```ts
interface process_info {
    id: number;        // 进程 PID
    name: string;      // 进程名 / 可执行文件名
    user_name: string; // 运行该进程的用户名
    mem: number;       // 内存占用，单位字节
    cpu: number;       // CPU 使用率，百分比数值（如 12.5 表示 12.5%）
}
```

采集间隔由 `set_print_second` 控制，默认 1 秒。

注意：
- 第一次注册 `on` 时才会启动后台采集线程，之后所有 `on` 都 `close` 后线程自动退出，再次 `on` 会重新启动。
- `on` 的回调始终在 JS 主线程执行。

#### `close(key)`

关闭某个 `key` 的监听。只有所有 `key` 都关闭后，程序才会停止实时监控的后台线程。

#### `pids(key, pids)`

修改某个 `key` 的 pid 过滤列表，传空数组 `[]` 表示不过滤（回调所有进程）。

```js
node_process_watcher.pids("screen1", []);           // 取消过滤
node_process_watcher.pids("screen1", [1234, 5678]); // 只看这两个进程
```

#### `set_print_second(second)`

设置采集 / 回调间隔（秒），默认 `1`。虽然注释里叫 `print_second`，实际控制的是实时监控的回调周期。

```js
node_process_watcher.set_print_second(5); // 每 5 秒回调一次
```

---

### 获取进程信息

#### `get_all_pid(ppid?)`

获取进程的 `pid` 与 `ppid` 列表（同步返回）。

- 不传参数：返回系统所有进程
- 传 `ppid`：只返回 `ppid` 等于该值的直接子进程（不会递归获取孙子进程）

```js
const all = node_process_watcher.get_all_pid();
// [{ pid: 4, ppid: 0 }, { pid: 1234, ppid: 4 }, ...]

const children = node_process_watcher.get_all_pid(1234);
// 1234 的直接子进程
```

返回结构：`{ pid: number, ppid: number }[]`

#### `get_all_processes()`

获取所有正在运行的进程的 `pid` 和进程名，比 `get_all_pid` 多带 `name` 字段。

注意：与 `get_all_pid` 不同，这里**会包含 pid 为 `0` 的系统进程**。

```js
const list = node_process_watcher.get_all_processes();
// [{ pid: 0, name: '[System Process]' }, { pid: 4, name: 'System' }, ...]
```

返回结构：`{ pid: number, name: string }[]`

---

### 终止进程

#### `kill_process(pid, kill_all_children?)`

| 参数 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `pid` | `number` | - | 目标进程 PID，不能为 `0` |
| `kill_all_children` | `boolean` | `false` | 是否连同**所有子孙进程**一起终止 |

行为说明：

- `kill_all_children = true` 时，先收集目标进程的完整子孙列表（一次性快照 + 去重，父进程 id 异常也不会死循环），再逐个终止，最后终止目标进程自身。
- Linux / macOS 使用 `SIGTERM`；Windows 使用 `TerminateProcess`，并最多等待 3 秒确保进程退出。
- 如果目标进程一个都没能终止（pid 不存在、权限不足等），**会抛出异常**，错误信息里带系统错误码，例如 `kill process failed, error code: 87`。
- `pid` 传 `0` 会直接抛错。Windows 上大量顶层进程的父进程 id 就是 0，若允许按 0 处理会误杀整台机器的进程。

```js
try {
    node_process_watcher.kill_process(1234, true);
} catch (e) {
    console.error(e.message); // kill process failed, error code: 5
}
```

---

### 文件夹大小统计

#### `on_folder_size(folder_name, on)`

在**子线程**中遍历目录，统计文件数量与总大小。每遍历完一层目录回调一次。

```js
node_process_watcher.on_folder_size("D:/data", (file_num, total_size) => {
    console.log("已统计文件数:", file_num, "总大小(字节):", total_size);
});
```

| 参数 | 说明 |
| --- | --- |
| `folder_name` | 要统计的目录路径 |
| `on` | `(file_num: number, total_size: number) => void` 进度回调 |

说明：
- 回调是**增量进度**，`file_num` / `total_size` 是当前累计值，最后一次回调即为最终结果。
- 路径不存在或不是目录时不会回调。
- 遍历过程中目录内的文件数量变化会导致结果有偏差。

#### `stop_folder_size(folder_name)`

停止对某个目录的统计。传入的路径必须和 `on_folder_size` 时完全一致。

```js
node_process_watcher.on_folder_size("D:/data", cb);
node_process_watcher.stop_folder_size("D:/data"); // 停止
```

---

### 系统代理

#### Windows

##### `get_system_proxy_for_windows()`

读取当前用户的系统代理设置（来自注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings`）。

```js
const proxy = node_process_watcher.get_system_proxy_for_windows();
// { enabled: true, ip: '127.0.0.1', port: 7890, bypass: 'localhost', useForLocal: false }
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `enabled` | `boolean` | 代理是否启用 |
| `ip` | `string` | 代理地址 |
| `port` | `number` | 代理端口 |
| `bypass` | `string` | 不走代理的地址列表（对应 `ProxyOverride`，已剔除 `<local>`） |
| `useForLocal` | `boolean` | 是否跳过本地地址（对应 `ProxyOverride` 中的 `<local>`） |

##### `set_system_proxy_for_windows(data)`

设置系统代理，返回是否成功。

```js
node_process_watcher.set_system_proxy_for_windows({
    enabled: true,
    ip: "127.0.0.1",
    port: 7890,
    bypass: "localhost;127.0.0.1",
    useForLocal: true,
});
```

- `enabled = true` 且 `ip` 非空、`port` 非 0 时写入代理并启用。
- 其它情况（含 `enabled = false`）会关闭代理。
- 修改后会调用 `InternetSetOption` 通知系统刷新（`INTERNET_OPTION_SETTINGS_CHANGED` / `INTERNET_OPTION_REFRESH`），不需要重启浏览器即可生效（部分已建立的连接仍需重连）。

#### macOS

##### `get_system_proxy_for_mac()`

获取 macOS 所有网络服务的代理配置，返回数组，每项对应一个网络服务（如 Wi-Fi、Ethernet）。

```js
const list = node_process_watcher.get_system_proxy_for_mac();
// [{
//     name: 'Wi-Fi',
//     bypass: '',
//     proxies: [
//         { enabled: true, ip: '127.0.0.1', port: 7890, type: 1 }
//     ]
// }]
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | `string` | 网络服务名 |
| `bypass` | `string` | 绕过的地址列表 |
| `proxies` | `array` | 该服务下各协议代理配置 |
| `proxies[].type` | `number` | `1` = HTTP，`2` = HTTPS |
| `proxies[].enabled` | `boolean` | 是否启用 |
| `proxies[].ip` | `string` | 代理地址 |
| `proxies[].port` | `number` | 代理端口 |

##### `set_system_proxy_for_mac(data)`

按网络服务设置代理，参数结构与 `get_system_proxy_for_mac` 的返回值一致，必须是数组，返回是否成功。

```js
node_process_watcher.set_system_proxy_for_mac([
    {
        name: "Wi-Fi",
        bypass: "localhost",
        proxies: [{ type: 1, enabled: true, ip: "127.0.0.1", port: 7890 }],
    },
]);
```

> 系统代理相关函数在非对应平台上调用不会报错，但会返回空值 / `false`。

---

### 权限判断

#### `is_admin()`

判断当前进程是否以管理员（Windows）/ root（Linux、macOS）权限运行，返回 `boolean`。

```js
if (!node_process_watcher.is_admin()) {
    console.log("请以管理员身份运行");
}
```

---

### Windows 服务中启动进程

以下函数**仅 Windows 生效**，且要求调用方以 `LocalSystem` 运行（通常是 Windows 服务），通过 `WTSQueryUserToken` 拿到当前登录用户令牌后用 `CreateProcessAsUser` 启动，进程会运行在当前登录用户的桌面上。

#### `launch_process_as_user_for_win_service(exe_path, args?, cwd?)`

以当前登录用户身份启动程序，返回新进程的 PID；失败返回 `-1`。

```js
const pid = node_process_watcher.launch_process_as_user_for_win_service(
    "C:/Windows/System32/notepad.exe",
    "",
    "C:/"
);
if (pid === -1) console.error("启动失败");
```

| 参数 | 说明 |
| --- | --- |
| `exe_path` | 可执行文件完整路径 |
| `args` | 命令行参数字符串，可省略 |
| `cwd` | 工作目录，可省略 |

#### `launch_process_as_user_for_win_service_console(exe_path, args, cwd, on_data, on_done, on_pid)`

以当前登录用户身份启动程序，并捕获其标准输出 / 标准错误。内部使用异步任务，不阻塞事件循环。

```js
node_process_watcher.launch_process_as_user_for_win_service_console(
    "C:/Windows/System32/cmd.exe",
    "/c dir C:\\",
    "C:/",
    (data) => console.log("输出:", data),  // 每次读到管道数据回调（可能分段）
    () => console.log("进程结束"),          // 进程退出后回调
    (pid) => console.log("PID:", pid)       // 进程创建成功后回调
);
```

| 参数 | 说明 |
| --- | --- |
| `on_data` | `(data: string) => void` 接收输出，数据是分段到达的，不做行切分 |
| `on_done` | `() => void` 进程结束回调；创建进程失败时会在 `on_done` 前通过错误形式抛出 |
| `on_pid` | `(pid: number) => void` 进程创建成功后回调 |

---

### 用户与文件所有者

#### `get_username_by_uid(uid)`

- Linux / macOS：根据 UID 返回用户名，找不到返回 `null`。
- Windows：没有 UID 概念，**始终返回 `null`**。

```js
const name = node_process_watcher.get_username_by_uid(0); // Linux: 'root'
```

#### `get_all_users()`

获取系统中的用户列表，返回结构因平台而异。

Linux / macOS：

```ts
{ uid: number, username: string }[]
```

Windows（额外包含所属本地组、域、SID）：

```ts
{
    username: string,
    domain: string,
    sid: string,
    groups: string[]
}[]
```

```js
const users = node_process_watcher.get_all_users();
```

> Windows 下返回的是本地普通账户（`NetUserEnum` + `FILTER_NORMAL_ACCOUNT`），不包含域账户。

#### `get_file_owner(file_path)`（仅 Windows）

获取文件 / 目录的所有者信息。

```js
const owner = node_process_watcher.get_file_owner("C:/Windows/System32/notepad.exe");
// { username: 'Administrators', domain: 'BUILTIN', sid: 'S-1-5-32-544', type: 'alias' }
```

| 字段 | 说明 |
| --- | --- |
| `username` | 所有者名称 |
| `domain` | 所属域 / 计算机名 |
| `sid` | 所有者 SID 字符串 |
| `type` | SID 类型：`user`、`group`、`well-known-group`、`alias`、`deleted-account`、`invalid`、`unknown`、`computer`、`other` |

获取失败（路径不存在、权限不足）时会抛出异常。

---

## 平台支持

| 平台 | 进程监控 | 杀死进程 | 文件夹统计 | 系统代理 | 用户查询 |
| --- | --- | --- | --- | --- | --- |
| Linux | ✅ | ✅ | ✅ | ⛔ 未实现（返回空 / false） | ✅ |
| Windows | ✅ | ✅ | ✅ | ✅ | ✅ |
| macOS | ✅ | ✅ | ✅ | ✅ | ✅ |


## 说明

- 进程的内存信息：Windows 下采集的是工作集（Working Set, wss）大小，即一段时间内进程所需的内存页集合大小；Linux 下采集的是 RSS 物理常驻集大小，包含共享内存。
- `cpu` 字段是两次采样之间的 CPU 时间差占系统总 CPU 时间的百分比，代表采样区间内的平均使用率，不是瞬时值。Windows 下超过 100% 代表进程占用了多个逻辑核心。
- 监控采集在独立的原生线程中完成，回调通过 `ThreadSafeFunction` 回到 JS 主线程，不会阻塞事件循环。
- 编译产物位于 `build/Release/node-process-watcher.node`，`lib/index.js` 为 TS 编译后的入口。

## 开发

```bash
npm run cc-configure   # 配置 node-gyp
npm run install_build  # 编译 release 版本
npm run build-ts       # 编译 TypeScript 到 lib/
```
