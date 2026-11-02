# Agent 接口速查

无头脚本：`ErMao.exe --script <commands.json> --json-out <report.json>`

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

例子见 `agent/examples/*.json`，完整说明见 `docs/教学/AI开发指南.md`。
