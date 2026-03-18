# AILed DDD Architecture

## Goals
- Decouple business orchestration from framework and driver details.
- Keep current behavior stable while enabling safer feature growth.
- Introduce clear dependency direction: domain <- application <- infrastructure.

## Layer Model
- domain: Pure business policy and rules.
- application: Use-case orchestration and transaction flow.
- infrastructure: ESP-IDF, Feishu, storage, network, device adapters.

## Current Mapping
- Domain
  - agent prompt policy: `domain/agent/system_prompt_policy.*`
  - conversation route policy: `domain/conversation/response_route_policy.*`
  - device indicator policy: `domain/device/indicator_policy.*`
  - ota enable policy: `domain/ota/ota_policy.*` (default disabled)
  - system boot policy: `domain/system/boot_policy.*`
- Application
  - lifecycle use case: `application/services/app_lifecycle_service.*`
  - ports: `domain/ports/app_runtime_ports.h`
  - conversation runtime use case: `application/services/agent_runtime_service.*`
  - conversation payload mapping: `application/services/conversation_payload_mapper.*`
  - conversation ports: `domain/ports/agent_runtime_ports.h`
  - device boot finalization: `application/services/device_boot_service.*`
  - integration boot startup: `application/services/integration_boot_service.*`
  - ota boot gate: `application/services/ota_runtime_service.*`
- Infrastructure
  - default runtime adapter: `infrastructure/runtime/default_runtime_ports.*`
  - default agent runtime adapter: `infrastructure/runtime/default_agent_runtime_ports.*`
  - default ota runtime adapter: `infrastructure/runtime/default_ota_runtime_ports.*`
  - esp ota manager: `infrastructure/ota/esp_ota_manager.*`
  - startup adapter implementation: `infrastructure/startup/system_startup.*`
  - AI adapter: `infrastructure/ai/agent.*`
  - config adapter: `infrastructure/config/app_config.*`
  - device adapter: `infrastructure/device/led_strip_tool.*`
  - integration adapter: `infrastructure/integration/feishu/feishu_bot.*`
  - messaging adapter: `infrastructure/messaging/message_bus.*`
  - network adapters: `infrastructure/network/config_mode.*`, `infrastructure/network/wifi_manager.*`
  - tool adapters: `infrastructure/tools/tool_registry.*`, `infrastructure/tools/tool_builtins.c`

## Migration Status
- Core boot and runtime behavior is orchestrated from application services.
- Legacy outer modules have been physically moved into `infrastructure/*` and removed from build targets.
- Minimal regression checks run at startup via `application/services/ddd_regression_checks.*`.

## Feature Flags
- OTA is currently disabled by domain policy and will log `OTA feature is disabled by domain policy` during boot.

## Boot Use Case
1. Initialize NVS.
2. Initialize LED/tools/message bus stack.
3. Ensure network ready.
4. Start AI runtime.
5. Publish system context (non-blocking per domain policy).
6. Start external channels.
7. Clear status indicator.

## Notes
- OTA is modeled in DDD but remains disabled by policy until explicitly enabled.
- New features should be added by extending `domain`, `application`, and `infrastructure` in dependency order.

## 2026年3月DDD重构与优化总结

### 主要优化点
- 彻底消除跨层依赖：所有 ports 接口已迁移至 domain/ports，infrastructure/application 仅依赖 domain 层接口。
- 去除重复实现：tool_registry、tool_builtins等工具注册与查找逻辑抽象为统一接口，减少重复代码。
- 强化分层与解耦：所有 runtime/service/adapter 只依赖上层接口，未出现反向依赖。
- 业务与基础设施分离：业务规则、用例、适配器职责边界清晰，便于扩展和测试。
- 代码静态检查无误：所有主干代码、接口、适配器、工具均无编译错误。
- 构建环境建议：需用官方 ESP-IDF 终端或正确 PATH 进行构建，避免工具链版本冲突。

### 后续建议
- 新功能开发严格遵循 domain → application → infrastructure 依赖方向。
- 工具注册、消息总线等通用逻辑可进一步抽象为可复用库。
- 持续保持分层边界，避免跨层直接调用。
- 构建/环境问题优先用官方终端或修正PATH。

