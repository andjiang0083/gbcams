## What / 做了什么

<!-- 一两句。关联 issue 请写 fixes #N -->

## How it was verified / 怎么验证的

- [ ] 在真机上用**屏幕**验收（本项目不用串口当验收手段）/ verified on screen, not via serial
- [ ] 改的是 UI/版式 → 先在 PC 上重渲染核对（行列不越界、元素不相撞），**再**刷机
- [ ] 拍照/相册相关 → 换过 SD 卡验证过（或说明为何没验）
- [ ] 三端里改了哪端就在哪端验过：`cardputer` / `cams3/espnow` / `cams3/wifi`

## Checklist / 自检

- [ ] 版本号按小步走（点号最后一位 +1；不要把多个改动合成大版本跳跃）
- [ ] 帮助页 / README / CHANGELOG 里过时的键位与描述一并更新
- [ ] 没有把二进制、`.pio/`、日志提交进仓库
- [ ] 新增的字符串**同时**给了中文与英文（本项目界面与文档默认双语）
