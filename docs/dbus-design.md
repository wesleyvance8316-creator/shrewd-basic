# sb_dbus 设计文档(数据总线)

> 状态:**一/二/三期全部落地(2026-11-22)**。讨论收敛记录见本文
> 各节"决策"行;API 速查与使用示例见 [dbus-api.md](dbus-api.md)。

## 0. 定位

**带类型、带钩子链的全局命名数据空间**:配置(有状态 KV)与事件
(发布订阅)统一承载,多线程(一期)/ 多进程(二期)共享,模块通过
回调介入读写路径与通知路径。钩子链使总线同时是**驱动抽象**——虚拟
节点(值来自硬件读写)与实值节点在同一命名空间共存。

地基复用:reflect(类型钉死/路径引擎/copy/destroy/dump)、json
(export/import)、text 层(env/args 灌入)、ds(radix/trie/queue)、
bin/msgpack/CBOR(二期线格式)。

## 1. 路径语法

```
path := "/" name ("/" name)* ["." fieldpath]
name      := 资源名(不含 '.')
fieldpath := reflect 字段路径(name | name[i] | 嵌套)
```

| 写法 | 含义 |
|---|---|
| `/config/log` | 资源 `log` 的整体数据 |
| `/config/log.level` | 该资源对象内 **level 字段** |
| `/sensors/imu0.sample[3].x` | 资源内 `sample[3].x` 字段 |

`.` 是资源名与字段选择的分界(资源名禁含点,语法零歧义);字段部分
由 **reflect 路径引擎**解析。**订阅模式**:段级 `*`(单段通配)+
尾缀 `/**`(子树)。

**决策**:斜杠路径(Linux 资源定位习惯);字段选择器区分"整体 vs
单字段"两种访问粒度;字段级订阅免费(路径含字段选择器)。

## 2. API 总览

前缀 `shrewd_basic_dbus_`(简称 sb_dbus);错误码 `-500..-599`
(`SHREWD_BASIC_ERR_DBUS_*`)。动词对称集:

| 对 | 动词 |
|---|---|
| 实例 | `create` / `destroy`(+ `default()` 进程级全局实例) |
| 资源 | `mount` / `unmount`(句柄式) |
| 订阅 | `subscribe` / `unsubscribe`(句柄式) |
| 数据 | `get` / `set`;零拷贝读 `peek`;变更宣告 `touch` |
| 发收 | `publish`(发)→ notify 管道 → `recv`(收,订阅回调) |
| 持久化 | `export` / `import`(json) |
| 观测 | `dump`(log 快照) |

```c
/* 访问族:访问方式由接口选择,注册期不锁死 */
int shrewd_basic_dbus_get(shrewd_basic_dbus_t *bus, const char *path,
                          void *out, size_t size, shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_peek(shrewd_basic_dbus_t *bus, const char *path,
                           const void **out, size_t *size, shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_set(shrewd_basic_dbus_t *bus, const char *path,
                          const void *in, size_t size, shrewd_basic_dbus_stamp_t *stamp);
int shrewd_basic_dbus_touch(shrewd_basic_dbus_t *bus, const char *path,
                            shrewd_basic_dbus_stamp_t *stamp);

typedef struct shrewd_basic_dbus_stamp {
    uint64_t version;   /* set: 期望版本(0=无条件);出参:观测/新版本 */
} shrewd_basic_dbus_stamp_t;
```

- `get` 拷贝出(任意存储);`peek` 借 `const void *`(零拷贝读,含
  字段粒度);`set` 拷贝入;`touch` = 模块直改 LINKED 数据后的
  配套(版本 +1、触发通知)。
- **CAS**:`set/touch` 传 stamp 且 version≠0 时做 compare-and-swap,
  失败 `ERR_DBUS_CONFLICT`。
- `peek` 生命周期契约:**到该资源下一次写为止有效**(读多写少的
  配置场景;调用方用 stamp 版本可自行检测变化)。

## 3. 挂载与存储(生命周期,非访问限制)

```c
typedef struct shrewd_basic_dbus_mount {
    const shrewd_basic_reflect_type_t *type;   /* 节点类型(NULL=纯事件 topic) */
    shrewd_basic_dbus_store_t store;          /* OWNED | LINKED */
    const shrewd_basic_dbus_ops_t *ops;       /* 钩子链(可 NULL) */
    void *ctx;
    uint32_t flags;                           /* EVENT / READONLY / ... */
} shrewd_basic_dbus_mount_t;

typedef enum shrewd_basic_dbus_store {
    SHREWD_BASIC_DBUS_STORE_OWNED = 0,  /* 总线持有:set 拷入、unmount 深释放(reflect copy/destroy) */
    SHREWD_BASIC_DBUS_STORE_LINKED,     /* 模块持有:总线借址直写,touch 通告 */
} shrewd_basic_dbus_store_t;
```

**决策**:存储形态只声明**归属/生命周期**,与访问方式解耦
(get/peek/set/touch 随接口);**VIRTUAL 自动判定**——无存储 + ops
提供 `get/set` = 计算节点(驱动映射),不再单独声明。
同路径多次 mount = 拦截链(纯 ops 挂载),带存储的主挂载每路径
唯一;拦截链洋葱模型:**pre 按注册序、post 逆序**。

## 4. 钩子九宫格与通知管道

**决策**:动作 × 阶段对称矩阵;pre/post 同形签名;发收对称
(notify ↔ recv)。

| | PRE(准入/改写/否决) | CORE(取值/存值/投递) | POST(收尾/联动) |
|---|---|---|---|
| **GET** | `pre_get` | `get` | `post_get` |
| **SET** | `pre_set` | `set` | `post_set` |
| **NOTIFY** | `pre_notify` | *(总线分发器,内建)* | `post_notify` |

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
} shrewd_basic_dbus_ops_t;   /* CORE 缺位 = 总线默认行为 */

typedef int (*shrewd_basic_dbus_recv_fn)(void *ctx,
                                         const shrewd_basic_dbus_note_t *note);
```

管道:`set/publish → [pre_set…commit…post_set] → 入队 →(分发线程)
[pre_notify → recv×N → post_notify]`。

- **决策(旧值)**:note **只带新值快照**(回调期间有效)。旧值需求
  的出口 = **同步钩子**:`pre_set` 里 `peek` 即旧值(LINKED/touch
  路径物理上无旧值,残缺契约不如不做)。
- **决策(执行模型)**:get/set 钩子**同步**(调用线程,顺序确定);
  通知**异步**(队列 + 单分发线程,保序;版本号供订阅端判丢包)。
  `drain()` 等待队列清空(测试/关停)。高频合并三期。

## 5. 并发与重入(一期 inproc)

- 注册表 + 节点值由总线级 rwlock 保护;**钩子在锁外跑**;
  set 流程 = 解析(引用计数)→ pre_set(锁外)→ 提交(CAS,锁内)
  → post_set(锁外)→ 入队;
- **重入规则**:钩子内可 `get/peek` 任意节点、可 `set` **其他**节点;
  `set/touch` 自身节点 → `ERR_DBUS_REENTRANT`(硬错误);
- `unmount/unsubscribe` 释放保证:在飞钩子/通知放行完毕再回收
  (句柄引用计数)。

## 6. 路由(内部特制,预留升格)

**决策**:router 与业务差异大,按 dbus 特化实现于模块内部
(`library/dbus/` 内部件),**不进公共 API**;匹配语义独立成章,
将来第二消费者(命令分发/权限匹配等)出现再按真实场景升格。

**两段式解析**——路由器只管前半截:

```
/sensors/imu0.sample[3].x
└─ 资源段 ─┘└──── 字段选择 ────┘
   路由层        reflect 路径引擎
```

- **精确表**:`shrewd_basic_radix`(整串路径 → 节点,压缩前缀树,
  查找 O(键长),gin httprouter 同款思想);
- **通配表**:段级匹配(`*` 单段、`/**` 子树),订阅注册时**编译**
  (注册期编译、查找期 O(段数));分发沿路径收集精确 + 通配 + 子树
  命中并去重;
- 不做 gin 式参数提取(总线无参数化路由);字段级匹配 = 路径串
  精确比较(字段选择器是路径的一部分)。

## 7. 与地基接线

| 地基 | 用途 |
|---|---|
| reflect | 类型钉死、字段路径解析、`check_type`/ABI 指纹(二期)、copy/destroy、dump |
| json | `export/import` 持久化(按 pattern 过滤);策略行(RANGE 等)并入 pre_set 校验(三期) |
| text 层 | env/args 启动灌配置(`/config/**` ← `APP_*`,上层助手) |
| ds | radix 精确表、trie 段表、queue 通知队列、dlist 钩子链 |
| bin/msgpack/CBOR | 二期 daemon transport 线格式(任选) |

## 8. 错误码(-500 号段)

| 码 | 语义 |
|---|---|
| `ERR_DBUS_CONFLICT` | CAS 版本不符 |
| `ERR_DBUS_REENTRANT` | 钩子内 set/touch 自身节点 |
| 其余 | 复用 `KEY_NOTFOUND`(无路由)、`ARG_INVALID`、`REFLECT_*`(字段/文本层)、`JSON_*` |

## 9. 分期

1. **一期 ✅(2026-11-22)**:§2–§8 全量 inproc——访问族、九宫格
   钩子、路由(`*`/`/**`)、异步分发 + `drain`、export/import/dump;
2. **二期 ✅(2026-11-22)**:shm transport(robust mutex、ABI 指纹、
   变更环 + 进程共享 cond 唤醒——原 eventfd 方案证伪:fd 进程私有,
   跨进程写不成立)+ 两进程冒烟;
3. **三期 ✅(2026-11-22)**:set_many(全有或全无)/get_many、
   coalesce、make_guard、make_policy_ops(RANGE 校验落 CORE get/set
   虚拟包装——pre_set 无值读取器的 API 缺口,偏差见文件注释)。

## 10. 二期:shm transport(契约,**已落地**)

**选型依据**:钩子是**进程内语义**(驱动映射必须在本进程跑)——
shm 数据面让各进程挂自己的钩子链、读写共享值;daemon 模型会把钩子
困在服务进程(排除)。transport 以插件形态挂在 core 之下(inproc
之外的第二种实现)。

```c
int shrewd_basic_dbus_attach(const char *name, size_t size,
                             const shrewd_basic_dbus_options_t *opt,
                             shrewd_basic_dbus_t **out);   /* 打开/创建 shm 总线 */
```

- **区域布局**:头(magic、格式版本、类型表:名字 + sb_refl_abi +
  sizeof)+ 节点表(路径、类型索引、值偏移、seq 版本)+ 值池;
- **ABI 指纹**:attach 时按类型表逐项核对本进程描述符
  (`sb_refl_abi` + `sizeof` + `check_type`),不符
  `ERR_DBUS_CONFLICT`,绝不静默共存;
- **互斥**:POSIX robust mutex(进程崩溃自动回收,EOWNERDEAD
  走恢复);seq 版本为无锁读优化留位;
- **通知**:每进程注册 eventfd 进区域表;写方 commit 后对注册
  fd 广播;本进程订阅匹配照旧(纯本地);
- **值语义**:区域内为 reflect 原样布局;`set` = 锁内 memcpy;
  `peek` 直接返回区域地址(零拷贝);OWNED/LINKED 在 shm 下统一为
  "区域持有"(attach 即共享);
- **进程契约**:每个进程**使用资源前须本进程 `mount`**——挂载注册
  本地视图与钩子链(钩子进程内语义由此成立),值槽按路径跨进程
  共享;`attach` 不自动导入他进程的挂载;
- 进程内钩子链、订阅、CAS、批量全部复用 core(transport 只换
  "值从哪来、往哪写、怎么叫醒")。

## 11. 三期:增强(契约,**已落地**)

```c
/* 批量/事务:全有或全无(CAS 逐个核对,整体提交;失败零提交) */
int shrewd_basic_dbus_set_many(shrewd_basic_dbus_t *bus, size_t count,
                               const char *const *paths, const void *const *ins,
                               const size_t *sizes, shrewd_basic_dbus_stamp_t *stamps);
int shrewd_basic_dbus_get_many(shrewd_basic_dbus_t *bus, size_t count,
                               const char *const *paths, void *const *outs,
                               const size_t *sizes, shrewd_basic_dbus_stamp_t *stamps);

/* 通知合并:options.coalesce(默认 0)——同路径未投递变更折叠为最新快照 */
int shrewd_basic_dbus_coalesce(shrewd_basic_dbus_t *bus, int on);

/* 权限钩子:按掩码生成 pre_get/pre_set 守卫(挂拦截链即生效) */
int shrewd_basic_dbus_make_guard(uint32_t allow_mask,
                                 shrewd_basic_dbus_ops_t *out);
#define SHREWD_BASIC_DBUS_ALLOW_READ  ((uint32_t)1u)
#define SHREWD_BASIC_DBUS_ALLOW_WRITE ((uint32_t)2u)

/* json 策略行并入 pre_set:RANGE 校验钩子 */
int shrewd_basic_dbus_make_policy_ops(const shrewd_basic_json_info_t *info,
                                      shrewd_basic_dbus_ops_t *out);
```

- `set_many`:持全局锁跨全部提交(天然原子);任一 CAS/尺寸/守卫
  失败 → 整体拒绝、零提交;成功才逐个 post_set + 入队;
- 合并仅作用于**尚未投递**的队列项(已交给 recv 的不撤回)。
