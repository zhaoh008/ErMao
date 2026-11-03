# Agent 接口速查

无头脚本：`ErMao.exe --script <commands.json> --json-out <report.json>`

`--json-out` 报告无法打开或写入时，命令会记录具体路径并以非 0 退出（`--script` / `--validate` 均适用）。

| 命令 | 关键参数 |
|---|---|
| scene.new / scene.save / scene.load / scene.query | path |
| object.create / destroy / set | name, position, rotation, scale, active |
| component.add / set / get | object, component, field, value |
| assets.import / list | kind(mesh|texture), path |
| level.build | kind(survivors3d|survivors|thirdperson|skinning) |
| camera.set | position, yaw, pitch, fov |
| screenshot | path |
| package.game | name, level, target(windows|linux) |
| validate | — |

例子见 `agent/examples/*.json`，完整说明见 `文档/教学文档/AI开发指南.md`。
