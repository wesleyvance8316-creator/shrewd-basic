# sb_dbus API 总览

速查手册:接口签名与语义要点。设计论证、并发模型与分期见
[dbus-design.md](dbus-design.md)。前缀 `shrewd_basic_dbus_`(简称
sb_dbus);错误码 `-500..-599`(`SHREWD_BASIC_ERR_DBUS_*`)。

**定位**:带类型、带钩子链的全局命名数据空间——配置(KV)与事件
(发布订阅)统一承载;钩子链把驱动映射(虚拟节点)做进读写路径。
多线程 + 多进程(inproc / shm 两种数据面)。

## 0. 路径语法

```
path := "/" name ("/" name)* ["." fieldpath]
```

| 写法 | 含义 |
|---|---|
| `/config/log` | 资源整体数据 |
| `/config/log.level` | 资源内 `level` 字段(reflect 字段路径,支持 `a[2].b`) |
| 订阅 `/config/log` | 该资源**及其字段写** |
| 订阅 `/config/log.level` | 仅该字段 |
| 模式 `*` / `/**` | 单段通配 / 子树 |

`.` 是资源名与字段选择的分界(资源名禁含点);订阅粒度 = 你命名的
层级。`set/publish` 的通知按**完整路径**匹配订阅。

## 1. 实例与选项

```c
int  shrewd_basic_dbus_create(const shrewd_basic_dbus_options_t *opt, shrewd_basic_dbus_t **out);
void shrewd_basic_dbus_destroy(shrewd_basic_dbus_t *bus);
shrewd_basic_dbus_t *shrewd_basic_dbus_default(void);   /* 进程级全局实例 */
int  shrewd_basic_dbus_attach(const char *name, size_t size,
                              const shrewd_basic_dbus_options_t *opt, shrewd_basic_dbus_t **out);
```

| 选项 | 语义 |
|---|---|
| `sync_notify` | 1:通知内联投递(测试确定性);0(默认):分发线程 |

`attach`:打开/创建**共享内存总线**(跨进程)——值在区域(reflect
原样布局,attach 时 ABI 指纹逐项核对,不符 `ERR_DBUS_CONFLICT`)、
robust mutex 崩溃回收、变更环 + 共享 cond 跨进程唤醒;本地钩子与
订阅语义与 inproc 完全一致。**进程契约**:各进程使用资源前须本进程
`mount`(注册本地视图/钩子;值槽按路径跨进程共享),`attach` 不
自动导入他进程的挂载。

## 2. 资源挂载(存储 = 生命周期)

```c
typedef struct shrewd_basic_dbus_mount {
    const shrewd_basic_reflect_type_t *type;   /* NULL + EVENT = 事件 topic */
    shrewd_basic_dbus_store_t store;          /* OWNED | LINKED */
    void *data;                              /* LINKED: 模块持有的字节 */
    const shrewd_basic_dbus_ops_t *ops;      /* 钩子链(可 NULL) */
    void *ctx;
    uint32_t flags;                           /* EVENT / READONLY */
} shrewd_basic_dbus_mount_t;

int shrewd_basic_dbus_mount(shrewd_basic_dbus_t *bus, const char *path,
                            const shrewd_basic_dbus_mount_t *mount,
                            shrewd_basic_dbus_handle_t **out);
int shrewd_basic_dbus_unmount(shrewd_basic_dbus_t *bus, shrewd_basic_dbus_handle_t *handle);
```

| 存储 | 语义 |
|---|---|
| `OWNED`(默认) | 总线分配/深释放(`reflect_destroy`);`set` 原始拷贝(平坦值类型) |
| `LINKED` | 模块持有,总线借址直写;改动后 `touch` 通告 |
| *(自动判定)* | 无数据 + ops 提供 `get`/`set` = **虚拟节点**(驱动映射) |

同路径多次 mount = **拦截链**(纯 ops 挂载);洋葱模型:pre 注册序、
post 逆序。同路径第二个存储挂载 → `KEY_EXISTS`。

## 3. 访问族(方式随接口,注册期不锁死)

```c
typedef struct shrewd_basic_dbus_stamp { uint64_t version; } shrewd_basic_dbus_stamp_t;

int shrewd_basic_dbus_get(bus, path, out, size, stamp);   /* 拷贝出 */
int shrewd_basic_dbus_peek(bus, path, &ptr, &size, stamp);/* const 借址 */
int shrewd_basic_dbus_set(bus, path, in, size, stamp);    /* 拷贝入 */
int shrewd_basic_dbus_touch(bus, path, stamp);            /* 宣告变更 */
int shrewd_basic_dbus_publish(bus, path, payload, size);  /* 事件 */
```

- **stamp**:传入 version≠0 = CAS(不符 `ERR_DBUS_CONFLICT`);出参 =
  观测/新版本;
- **peek**:返回 `const void *`,**到该资源下一次写为止有效**;
  虚拟节点报 `ERR_DBUS_UNADDRESSABLE`;
- **touch**:模块直改 LINKED 后的配套(版本 +1、通知);
- **publish**:纯扇出到匹配订阅者(无需挂载)。

## 4. 钩子九宫格(动作 × 阶段)

```c
typedef struct shrewd_basic_dbus_ops {
    int  (*pre_get)(void *ctx, shrewd_basic_dbus_req_t *req);
    int  (*get)(void *ctx, const shrewd_basic_dbus_req_t *req, void *out, size_t *size);
    void (*post_get)(void *ctx, shrewd_basic_dbus_req_t *req);
    int  (*pre_set)(void *ctx, shrewd_basic_dbus_req_t *req);
    int  (*set)(void *ctx, const shrewd_basic_dbus_req_t *req, const void *in, size_t size);
    void (*post_set)(void *ctx, shrewd_basic_dbus_req_t *req);
    int  (*pre_notify)(void *ctx, shrewd_basic_dbus_req_t *req);
    void (*post_notify)(void *ctx, shrewd_basic_dbus_req_t *req);
} shrewd_basic_dbus_ops_t;
```

| | PRE(准入/改写/否决) | CORE(取值/存值) | POST(收尾/联动) |
|---|---|---|---|
| GET | `pre_get` | `get`(虚拟供应商) | `post_get` |
| SET | `pre_set` | `set`(虚拟供应商) | `post_set` |
| NOTIFY | `pre_notify` | *(总线分发器)* | `post_notify` |

- pre/post **同步**(调用线程,pre 注册序/post 逆序);通知**异步**
  (队列 + 分发线程;`sync_notify` 内联);`drain()` 排空;
- req 访问器:`req_path/req_field/req_type/req_version`、
  `req_deny(req, err)`(否决)、`req_rewrite(req, in, size)`(pre_set
  改写值);
- **重入**:钩子内可 get/peek 任意节点、可 set/touch **其他**节点;
  set/touch 自身节点 → `ERR_DBUS_REENTRANT`;
- **旧值**需求的出口 = `pre_set` 里 `peek`(note 只带新值快照)。

## 5. 订阅与通知

```c
typedef int (*shrewd_basic_dbus_recv_fn)(void *ctx, const shrewd_basic_dbus_note_t *note);

int shrewd_basic_dbus_subscribe(bus, pattern, recv, ctx, &handle);
int shrewd_basic_dbus_unsubscribe(bus, handle);
int shrewd_basic_dbus_coalesce(bus, int on);   /* 未投递同 path 折叠为最新 */
int shrewd_basic_dbus_drain(bus);
```

`note = {path, value(快照,回调期间有效), size, version, kind(CHANGE|EVENT)}`。
订阅模式含字段选择器 = 字段级订阅;不含 = 资源及其字段写。

## 6. 批量/事务/守卫/策略(三期)

```c
int shrewd_basic_dbus_set_many(bus, count, paths[], ins[], sizes[], stamps[]);
int shrewd_basic_dbus_get_many(bus, count, paths[], outs[], sizes[], stamps[]);
int shrewd_basic_dbus_make_guard(uint32_t allow_mask, shrewd_basic_dbus_ops_t *out);
int shrewd_basic_dbus_make_policy_ops(const shrewd_basic_json_info_t *info,
                                      shrewd_basic_dbus_ops_t *out);
```

- `set_many`:**全有或全无**(先核对全部 stamp/尺寸再写入),任一
  失败零提交零通知;仅存储型节点(ops->set 节点拒);
- `make_guard`:掩码(`ALLOW_READ`/`ALLOW_WRITE`)→ pre_get/pre_set
  守卫,挂拦截链即权限门;
- `make_policy_ops`:json 策略行校验(RANGE 越界拒 + **HIDDEN** 字段
  双向拒)——**CORE get/set 虚拟包装**(pre_set 无值读取器的 API
  缺口,偏差说明见 `dbus_ops.c` 文件头);ctx = `{void *target;
  const shrewd_basic_json_info_t *info;}`;
- **事务**:`txn_begin/get/set/commit/abort`——get 记录读集版本,
  set 暂存写集,commit 全有或全无(读集 CAS,冲突零提交);
  单次句柄;读/写集各限 32 项;
- `shrewd_basic_dbus_save/load(bus, pattern, file)`:json 快照落盘/
  回灌(import 语义:绕过钩子与通知);
- `note.folded`:coalesce 折叠掉的同路径变更次数;`options.ring_size`
  定 shm 变更环容量(默认 64)。

**HIDDEN 边界(现行语义)**:字段选择器命中才拒;`get("/res")` 整体
拷贝仍携带隐藏字段字节——收紧策略(整体读清零/拒读)待决策。

## 7. 持久化与观测

```c
int shrewd_basic_dbus_export(bus, pattern, &out_json);   /* json 快照 */
int shrewd_basic_dbus_import(bus, pattern, json, len);   /* 回灌:绕过钩子与通知 */
int shrewd_basic_dbus_dump(bus, pattern);                /* log 快照 */
```

`export` 文本释放走 `shrewd_basic_json_text_free`;pattern 为资源
粒度(NULL = 全量)。

## 8. 错误码(-500 号段)

| 码 | 语义 |
|---|---|
| `ERR_DBUS_CONFLICT` | CAS 版本不符 / shm ABI 指纹不符 |
| `ERR_DBUS_REENTRANT` | 钩子内 set/touch 自身节点 |
| `ERR_DBUS_UNADDRESSABLE` | peek 虚拟节点 |

其余复用:`KEY_NOTFOUND`(无路由/守卫拒)、`ARG_INVALID`、
`REFLECT_MISMATCH`(尺寸/范围)、`REFLECT_TEXT`(策略越界)、
`REFLECT_NOT_SUPPORTED`(只读/形状)、`JSON_*`。

## 9. 使用示例

### 9.1 配置资源 + 守卫 + 订阅(inproc)

```c
typedef struct { int32_t level; double ratio; } cfg_t;
SHREWD_BASIC_REFLECT_TYPE_DEFINE(cfg_reflect, cfg_t, ...);

shrewd_basic_dbus_t *bus = shrewd_basic_dbus_default();
shrewd_basic_dbus_handle_t *h = NULL;
shrewd_basic_dbus_mount_t mount = {.type = &cfg_reflect,
                                   .store = SHREWD_BASIC_DBUS_STORE_OWNED};
shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h);

shrewd_basic_dbus_ops_t guard;
shrewd_basic_dbus_make_guard(SHREWD_BASIC_DBUS_ALLOW_READ, &guard);  /* 只读子树 */
shrewd_basic_dbus_mount_t gate = {.ops = &guard};
shrewd_basic_dbus_handle_t *g = NULL;
shrewd_basic_dbus_mount(bus, "/config/log", &gate, &g);   /* 拦截链 */

shrewd_basic_dbus_subscribe(bus, "/config/**", on_change, ctx, &sub);

int32_t level = 5;
shrewd_basic_dbus_set(bus, "/config/log.level", &level, sizeof(level), NULL);
/* on_change 收到 {path:"/config/log.level", value:&5, size:4, kind:CHANGE} */
```

### 9.2 驱动映射(虚拟节点)

```c
static int adc_get(void *ctx, const shrewd_basic_dbus_req_t *req, void *out, size_t *size) {
    *(int32_t *)out = hal_adc_read(*(int *)ctx);   /* 硬件读 */
    *size = sizeof(int32_t);
    return SHREWD_BASIC_ERR_OK;
}
static int adc_set(void *ctx, const shrewd_basic_dbus_req_t *req, const void *in, size_t size) {
    return hal_dac_write(*(int *)ctx, *(const int32_t *)in);   /* 硬件写 */
}
static const shrewd_basic_dbus_ops_t adc_ops = {.get = adc_get, .set = adc_set};

shrewd_basic_dbus_mount_t hw = {.type = &i32_reflect, .ops = &adc_ops, .ctx = &channel};
shrewd_basic_dbus_mount(bus, "/hw/adc0", &hw, &h);   /* 无数据 = 虚拟节点 */
```

### 9.3 LINKED + touch(零拷贝写)

```c
static sensor_state_t g_state;                      /* 模块自己的内存 */
mount.store = SHREWD_BASIC_DBUS_STORE_LINKED;
mount.data  = &g_state;
shrewd_basic_dbus_mount(bus, "/sensors/imu0", &mount, &h);

g_state.sample[3].x = 12;                           /* 直接改 */
shrewd_basic_dbus_touch(bus, "/sensors/imu0", NULL);/* 版本+1、通知 */
```

### 9.4 跨进程(shm)

```c
/* 进程 A(数据面拥有者) */
shrewd_basic_dbus_attach("/demo_bus", 1 << 20, NULL, &bus);
shrewd_basic_dbus_mount(bus, "/config/log", &mount, &h);

/* 进程 B */
shrewd_basic_dbus_attach("/demo_bus", 1 << 20, NULL, &bus);
shrewd_basic_dbus_get(bus, "/config/log.level", &level, sizeof(level), NULL);
shrewd_basic_dbus_subscribe(bus, "/config/**", on_change, ctx, &sub);
```

ABI 指纹在 attach 时核对(类型布局不符直接 `ERR_DBUS_CONFLICT`);
测试 `tests/test_dbus_shm.c` 有完整的两进程 fork 冒烟可抄。

## 10. 已知限制(v1)

- `OWNED` 的 `set` 是原始拷贝(平坦值类型;深指针图用 LINKED 或
  export/import);
- shm 崩溃恢复:robust 锁自动回收,但死者最后一次未完提交丢失;
- 变更环 64 项:滞后进程可能跳过中间变更(终值必达,seq 可判);
- `set_many` 仅存储型节点原子;`get_many` 顺序读非全局原子。
