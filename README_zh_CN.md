# PocketJS-IDF

PocketJS-IDF 是面向 ESP32-P4 的 PocketJS 运行时与 RGB565 逻辑
framebuffer 管理组件。

> [!IMPORTANT]
> `0.1.0` 是首个稳定组件版本。组件、干净归档消费者和 Tab5 三套固件
> 都已独立完成构建。显示路径此前已通过 rc.1 的 960 帧与人工画面门禁；
> 维护者批准发布稳定版，但不将其表述为一次新的设备运行。发布记录参阅
> [发布清单](docs/releasing.md)。

组件负责单运行时、单 `.pocket` 应用、单逻辑显示器、脏区历史、damage
halo、U8.4 相位对齐、分块、绘制 buffer 复用、flush transaction、PPA
加速和 QuickJS 扩展。面板、触摸、传感器、旋转缩放、物理 framebuffer
及 swap 策略仍由板级工程负责。

## 依赖

- ESP32-P4；
- ESP-IDF 5.4 及以上；
- Cargo 与 `riscv32imafc-unknown-none-elf` Rust target；
- QuickJS-NG 0.14.0 由 Component Manager 自动解析；
- 只有源码生成 `.pocket` 时才需要 Bun。

在消费者的 `idf_component.yml` 中添加：

```yaml
dependencies:
  halfsweet/pocketjs-idf:
    version: "0.1.0"
```

本地开发可临时增加：

```yaml
    override_path: "/absolute/path/to/pocketjs-idf"
```

## 两种应用接入方式

嵌入预编译包，不需要 Bun：

```cmake
pocketjs_embed_package(
    TARGET ${COMPONENT_LIB}
    NAME dashboard
    PACKAGE "app/dashboard.pocket"
)
```

在 ESP-IDF build 目录中从源码生成：

```cmake
pocketjs_compile_app(
    TARGET ${COMPONENT_LIB}
    NAME dashboard
    MANIFEST "app/pocket.json"
)
```

两者都会生成 `pocketjs_app_dashboard.h` 和
`pocketjs_app_dashboard` 描述符，不会修改组件或应用源码目录。

## 运行与显示

固定流程为：

1. `pocketjs_create(app, config, &runtime)`；
2. 可选 `pocketjs_attach_display(runtime, display)`；
3. 每帧调用 `pocketjs_run_frame(runtime, input, &stats)`；
4. 可用 `pocketjs_detach_display()` 切回 headless；
5. `pocketjs_destroy()` 释放运行时。

`pocketjs_display_t` 表示逻辑 RGB565 显示器。绘制 buffer 由调用方
提供并保持所有权；必选的 `flush` 回调负责提交像素，可通过
`pocketjs_display_flush_ready()` 或 ISR 专用版本报告异步完成。渲染模式
支持 `PARTIAL`、`DIRECT`、`FULL`。除
`pocketjs_display_flush_ready_from_isr()` 外，所有 API 都必须由同一个
owner task/core 非重入调用。

完整用法、回调生命周期和限制请参阅：

- [英文 README](README.md)
- [API 说明](API.md)
- [架构边界](docs/architecture.md)
- [发布与硬件门禁](docs/releasing.md)
